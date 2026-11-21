#include "ServerPage.h"
#include "ActressPhotos.h"
#include "BrowserWindow.h"
#include "SceneTools.h"
#include "Widgets.h"
#include "core/Gender.h"
#include "core/I18n.h"
#include "core/Release.h"
#include "Frameless.h"

#include <QCheckBox>
#include <functional>
#include <QComboBox>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSysInfo>
#include <QTableWidget>
#include <QUrlQuery>
#include <QUuid>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineView>

// ======================= IAFD =======================

QString IafdBio::searchUrl(const QString &name)
{
    const QString base = qEnvironmentVariableIsSet("ARISTOCRAT_TEST_IAFD") ? qEnvironmentVariable("ARISTOCRAT_TEST_IAFD")
                                                                           : QStringLiteral("https://www.iafd.com");
    return base + QStringLiteral("/results.asp?searchtype=comprehensive&searchstring=")
           + QString::fromLatin1(QUrl::toPercentEncoding(name));
}

IafdBio::IafdBio(QObject *parent)
    : QObject(parent)
{
    m_view = new QWebEngineView;
    m_view->setAttribute(Qt::WA_DontShowOnScreen);
    m_view->resize(1300, 1000);
    m_view->setPage(new QWebEnginePage(BrowserWindow::profile(), m_view));
    m_view->show();
    connect(m_view, &QWebEngineView::loadFinished, this, [this] {
        const int serial = m_serial;
        QTimer::singleShot(1500, this, [this, serial] { if (serial == m_serial) step(m_stage); });
    });
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] { nextSource(); });   // источник не ответил — к следующему
}

IafdBio::~IafdBio()
{
    delete m_view;
}

// Бесплатные источники по порядку: IAFD, Babepedia, FreeOnes, Boobpedia, Википедия.
// Из каждого берётся то, чего ещё нет; псевдонимы собираются со всех.
void IafdBio::fetch(const QString &name, Callback cb)
{
    m_cb = std::move(cb);
    m_name = name;
    m_merged = QJsonObject();
    m_found.clear();
    const QString under = QString(name).replace(' ', '_');
    const QString dash = name.toLower().replace(' ', '-');
    m_sources = {{QStringLiteral("IAFD"), searchUrl(name)},
                 {QStringLiteral("Babepedia"), QStringLiteral("https://www.babepedia.com/babe/") + under},
                 {QStringLiteral("FreeOnes"), QStringLiteral("https://www.freeones.com/") + dash + QStringLiteral("/bio")},
                 {QStringLiteral("Boobpedia"), QStringLiteral("https://www.boobpedia.com/boobs/") + under},
                 {QStringLiteral("Wikipedia"), QStringLiteral("https://en.wikipedia.org/wiki/") + under}};
    if (qEnvironmentVariableIsSet("ARISTOCRAT_TEST_IAFD_ONLY"))
        m_sources = m_sources.mid(0, 1);
    m_index = -1;
    if (qEnvironmentVariableIsSet("ARISTOCRAT_TEST_SKIP_IAFD"))   // автопроверка остальных источников
        m_index = 0;
    nextSource();
}

void IafdBio::nextSource()
{
    ++m_serial;
    ++m_index;
    m_attempt = 0;
    if (m_index >= m_sources.size()) {
        done(m_merged);
        return;
    }
    m_stage = m_index == 0 ? 0 : 1;   // у IAFD сначала поиск, у остальных сразу страница
    if (m_index == 0)
        m_stage = 0;
    m_timeout.start(25000);
    m_view->load(QUrl(m_sources[m_index].second));
}

void IafdBio::done(const QJsonObject &oIn)
{
    m_timeout.stop();
    ++m_serial;
    QJsonObject o = oIn;
    o.insert("sources", QJsonArray::fromStringList(m_found));
    QStringList aka;
    for (const QString &a : m_aka)
        if (!aka.contains(a, Qt::CaseInsensitive))
            aka << a;
    o.insert("akaList", QJsonArray::fromStringList(aka));
    m_aka.clear();
    auto cb = m_cb;
    m_cb = nullptr;
    if (cb)
        cb(o);
}

void IafdBio::merge(const QJsonObject &in)
{
    static const QRegularExpression paren(QStringLiteral("\\s*\\([^)]*\\)"));
    bool any = false;
    for (const QString &k : {QStringLiteral("birthday"), QStringLiteral("birthplace"), QStringLiteral("years"), QStringLiteral("height"),
                             QStringLiteral("nat"), QStringLiteral("eth"), QStringLiteral("hair")}) {
        QString v = in.value(k).toString().trimmed();
        if (v.compare(QLatin1String("No data"), Qt::CaseInsensitive) == 0 || v == QLatin1String("-") || v.compare(QLatin1String("Unknown"), Qt::CaseInsensitive) == 0)
            v.clear();
        v.remove(paren);
        v = v.simplified();
        if (!v.isEmpty() && m_merged.value(k).toString().isEmpty()) {
            m_merged.insert(k, v);
            any = true;
        }
    }
    if (in.value("movies").toInt() > m_merged.value("movies").toInt())
        m_merged.insert("movies", in.value("movies").toInt());
    for (QString a : in.value("aka").toString().split(QRegularExpression(QStringLiteral("[\\n,;/]|\\s+-\\s+")), Qt::SkipEmptyParts)) {
        a.remove(paren);
        a = a.simplified();
        if (!a.isEmpty() && a.size() < 40 && a.compare(m_name, Qt::CaseInsensitive) != 0 && !a.contains(QLatin1String("No known"), Qt::CaseInsensitive)) {
            m_aka << a;
            any = true;
        }
    }
    if (any)
        m_found << m_sources[m_index].first;
}

void IafdBio::step(int stage)
{
    const QString nameJs = QString::fromUtf8(QJsonDocument(QJsonArray{m_name}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
    const int serial = m_serial;
    if (stage == 0) {
        // Результаты поиска IAFD: ссылка на актрису с точно таким именем (из таблицы актрис, если она есть)
        static const char *js = R"JS((function(){var n=%1.toLowerCase().replace(/\s+/g,' ').trim();var best='',fem='';
[].slice.call(document.querySelectorAll('a[href*="person.rme"]')).forEach(function(a){
 var t=(a.textContent||'').toLowerCase().replace(/\s+/g,' ').trim();if(t!==n)return;if(!best)best=a.href;
 for(var e=a;e&&e!==document.body;e=e.parentElement){var id=(e.id||'')+' '+(typeof e.className=='string'?e.className:'');if(/fem/i.test(id)){if(!fem)fem=a.href;break;}}});
return fem||best;})())JS";
        m_view->page()->runJavaScript(QString::fromUtf8(js).arg(nameJs), [this, serial](const QVariant &v) {
            if (serial != m_serial)
                return;
            const QString href = v.toString();
            if (href.isEmpty()) {
                if (++m_attempt < 3) {   // страница может ещё догружаться
                    QTimer::singleShot(2000, this, [this, serial] { if (serial == m_serial) step(0); });
                    return;
                }
                nextSource();
                return;
            }
            m_stage = 1;
            m_attempt = 0;
            ++m_serial;
            m_view->load(QUrl(href));
        });
        return;
    }
    // Страница с анкетой: подписи («Birthday», «Born:», «Date of Birth»…) и значения рядом с ними
    static const char *js = R"JS((function(){function t(e){return (e&&(e.innerText||e.textContent)||'').trim();}
var L={birthday:/^(birthday|date of birth|born|birth date|birthdate)$/i,birthplace:/^(birthplace|place of birth|birth place|born in)$/i,
 years:/^(years active|career|active|career start|years)$/i,height:/^height$/i,aka:/^(performer aka|aka|also known as|aliases|alias|other names)$/i,
 nat:/^(nationality|country)$/i,eth:/^ethnicity$/i,hair:/^hair( colou?rs?)?$/i};
var body=document.body?document.body.innerText:'';var r={};r.adult=/pornograph|adult film|adult model|porn ?star|adult entertain/i.test(body);
var els=document.querySelectorAll('p,dt,th,td,div,span,b,strong,h3,h4,h5,label,li');
for(var i=0;i<els.length;i++){var e=els[i];if(e.children.length>1)continue;var s=t(e).replace(/:\s*$/,'');if(!s||s.length>30)continue;
 for(var k in L){if(r[k]!==undefined||!L[k].test(s))continue;var v=e.nextElementSibling;var val=v?t(v):'';
  if(!val&&e.parentElement){var pt=t(e.parentElement);if(pt.length>s.length+2&&pt.length<300)val=pt.replace(t(e),'').replace(/^[:\s]+/,'');}
  if(!val&&e.parentElement&&e.parentElement.nextElementSibling)val=t(e.parentElement.nextElementSibling);
  if(val&&val.length<400)r[k]=val;}}
// Википедия: точные поля в карточке
var bd=document.querySelector('.infobox .bday');if(bd)r.birthday=bd.textContent;
var bp=document.querySelector('.infobox .birthplace');if(bp)r.birthplace=t(bp);
var rows=0;[].forEach.call(document.querySelectorAll('table'),function(tb){var h=t(tb.querySelector('thead')||tb.rows[0]);
 if(/title/i.test(h)&&/year/i.test(h)){var n=tb.querySelectorAll('tbody tr').length||Math.max(0,tb.rows.length-1);if(n>rows)rows=n;}});
r.movies=location.hostname.indexOf('iafd')>=0?rows:0;return JSON.stringify(r);})())JS";
    m_view->page()->runJavaScript(QString::fromUtf8(js), [this, serial](const QVariant &v) {
        if (serial != m_serial)
            return;
        const QJsonObject o = QJsonDocument::fromJson(v.toString().toUtf8()).object();
        const bool empty = o.value("birthday").toString().isEmpty() && o.value("years").toString().isEmpty() && o.value("aka").toString().isEmpty();
        if (empty && ++m_attempt < 2) {
            QTimer::singleShot(2000, this, [this, serial] { if (serial == m_serial) step(1); });
            return;
        }
        qInfo("source %s: %s", qPrintable(m_sources[m_index].first), QJsonDocument(o).toJson(QJsonDocument::Compact).left(500).constData());
        // Википедия — только если статья действительно об актрисе фильмов для взрослых (тёзки бывают)
        if (m_sources[m_index].first != QLatin1String("Wikipedia") || o.value("adult").toBool())
            merge(o);
        nextSource();
    });
}

// ======================= Шаблон анкеты =======================

namespace Bio {

QString monthDate(const QDate &d)
{
    static const char *months[] = {"января", "февраля", "марта", "апреля", "мая", "июня",
                                   "июля", "августа", "сентября", "октября", "ноября", "декабря"};
    return QStringLiteral("%1 %2 %3").arg(d.day()).arg(QString::fromUtf8(months[d.month() - 1])).arg(d.year());
}

static QString nationalityRu(const QString &nat, const QString &place)
{
    // Прилагательное для «… актриса»: по национальности, иначе по стране рождения
    static const QList<QPair<QString, QString>> map = {
        {"american", "американская"}, {"united states", "американская"}, {"usa", "американская"},
        {"russia", "российская"}, {"ukrain", "украинская"}, {"belarus", "белорусская"}, {"kazakh", "казахстанская"},
        {"czech", "чешская"}, {"slovak", "словацкая"}, {"hungar", "венгерская"}, {"polish", "польская"}, {"poland", "польская"},
        {"roman", "румынская"}, {"bulgar", "болгарская"}, {"serbia", "сербская"}, {"croat", "хорватская"},
        {"latvi", "латвийская"}, {"lithuan", "литовская"}, {"eston", "эстонская"}, {"french", "французская"}, {"france", "французская"},
        {"italian", "итальянская"}, {"italy", "итальянская"}, {"spanish", "испанская"}, {"spain", "испанская"},
        {"portug", "португальская"}, {"german", "немецкая"}, {"germany", "немецкая"}, {"dutch", "нидерландская"},
        {"netherlands", "нидерландская"}, {"british", "британская"}, {"english", "британская"}, {"england", "британская"},
        {"united kingdom", "британская"}, {"scot", "шотландская"}, {"irish", "ирландская"}, {"ireland", "ирландская"},
        {"swed", "шведская"}, {"norw", "норвежская"}, {"danish", "датская"}, {"denmark", "датская"}, {"finn", "финская"},
        {"canad", "канадская"}, {"brazil", "бразильская"}, {"colombia", "колумбийская"}, {"venezuel", "венесуэльская"},
        {"argentin", "аргентинская"}, {"mexic", "мексиканская"}, {"cuba", "кубинская"}, {"chile", "чилийская"},
        {"peru", "перуанская"}, {"puerto ric", "пуэрториканская"}, {"dominican", "доминиканская"}, {"japan", "японская"},
        {"chin", "китайская"}, {"korea", "корейская"}, {"thai", "тайская"}, {"filipin", "филиппинская"}, {"philippin", "филиппинская"},
        {"vietnam", "вьетнамская"}, {"indian", "индийская"}, {"australia", "австралийская"}, {"new zealand", "новозеландская"},
        {"south africa", "южноафриканская"}, {"israel", "израильская"}, {"turk", "турецкая"}, {"greek", "греческая"}, {"greece", "греческая"},
        {"austria", "австрийская"}, {"swiss", "швейцарская"}, {"switzerland", "швейцарская"}, {"belgi", "бельгийская"},
        {"moldov", "молдавская"}, {"georgia", "грузинская"}, {"armenia", "армянская"}, {"uzbek", "узбекская"}};
    for (const QString &src : {nat.toLower(), place.toLower().section(',', -1).trimmed()}) {
        if (src.isEmpty())
            continue;
        // Для США в месте рождения пишут штат: «Los Angeles, CA»
        if (src == place.toLower().section(',', -1).trimmed() && QRegularExpression(QStringLiteral("^[a-z]{2}$")).match(src).hasMatch())
            return QStringLiteral("американская");
        for (const auto &p : map)
            if (src.contains(p.first))
                return p.second;
    }
    return {};
}

QString build(const QJsonObject &b, const QString &ruName, const QString &bornIn)
{
    const QString latin = b.value("name").toString();
    const QString first = ruName.section(' ', 0, 0);
    QStringList aka;
    for (const QJsonValue &v : b.value("akaList").toArray())
        aka << v.toString();
    aka = aka.mid(0, 3);   // как в образце: три псевдонима

    // 1. Кто и где родилась
    QString p1 = ruName + QStringLiteral(" (") + latin + QLatin1Char(')');
    if (!aka.isEmpty()) {
        const QString last = aka.takeLast();
        p1 += QStringLiteral(", также известная как ") + (aka.isEmpty() ? last : aka.join(QStringLiteral(", ")) + QStringLiteral(" и ") + last);
    }
    const QString nat = nationalityRu(b.value("nat").toString(), b.value("birthplace").toString());
    p1 += QStringLiteral(" — ") + (nat.isEmpty() ? QString() : nat + QLatin1Char(' ')) + QStringLiteral("актриса фильмов для взрослых.");
    const QDate born = Release::parseDate(b.value("birthday").toString());
    if (born.isValid())
        p1 += QStringLiteral(" Родилась ") + monthDate(born) + QStringLiteral(" года") + (bornIn.isEmpty() ? QString() : QLatin1Char(' ') + bornIn) + QLatin1Char('.');
    else if (!bornIn.isEmpty())
        p1 += QStringLiteral(" Родилась ") + bornIn + QLatin1Char('.');

    // 2. Карьера
    // «2013-2022», «2023 - present», «2016»
    static const QRegularExpression yrs(QStringLiteral("((?:19|20)\\d{2})(?:\\s*[-–]\\s*((?:19|20)\\d{2}))?"));
    const auto ym = yrs.match(b.value("years").toString());
    const int start = ym.hasMatch() ? ym.captured(1).toInt() : 0;
    const int end = ym.hasMatch() ? ym.captured(2).toInt() : 0;
    QString p2;
    if (start > 0)
        p2 = QStringLiteral("Карьеру в кино для взрослых %1 начала в %2 году.").arg(first).arg(start);
    const int movies = b.value("movies").toInt();
    if (movies >= 5) {
        const int shown = movies >= 100 ? movies / 50 * 50 : movies >= 20 ? movies / 10 * 10 : movies;
        p2 += (p2.isEmpty() ? QString() : QStringLiteral(" "))
              + (movies > shown ? QStringLiteral("По данным IAFD, она снялась более чем в %1 фильмах.").arg(shown)
                                : QStringLiteral("По данным IAFD, она снялась в %1 фильмах.").arg(shown));
    }

    // 3. Снимается или завершила карьеру
    QString p3;
    if (end > 0 && end < QDate::currentDate().year() - 1)
        p3 = QStringLiteral("%1 снималась до %2 года и затем завершила карьеру.").arg(ruName).arg(end);
    else if (start > 0)
        p3 = QStringLiteral("%1 продолжает сниматься.").arg(ruName);

    QStringList parts{p1};
    if (!p2.isEmpty()) parts << p2;
    if (!p3.isEmpty()) parts << p3;
    return parts.join(QStringLiteral("\n\n"));
}

QString transliterate(const QString &latin)
{
    // Запасной вариант, если перевод недоступен: простая практическая транскрипция
    static const QList<QPair<QString, QString>> rules = {
        {"sch", "ш"}, {"tch", "ч"}, {"sh", "ш"}, {"ch", "ч"}, {"zh", "ж"}, {"kh", "х"}, {"th", "т"}, {"ph", "ф"}, {"ck", "к"},
        {"ee", "и"}, {"oo", "у"}, {"ea", "и"}, {"ia", "ия"}, {"ya", "я"}, {"yu", "ю"}, {"ye", "е"}, {"ai", "ай"}, {"ay", "эй"},
        {"ey", "ей"}, {"ie", "и"}, {"qu", "кв"}, {"x", "кс"}, {"j", "дж"}, {"w", "в"}, {"q", "к"},
        {"a", "а"}, {"b", "б"}, {"c", "к"}, {"d", "д"}, {"e", "е"}, {"f", "ф"}, {"g", "г"}, {"h", "х"}, {"i", "и"}, {"k", "к"},
        {"l", "л"}, {"m", "м"}, {"n", "н"}, {"o", "о"}, {"p", "п"}, {"r", "р"}, {"s", "с"}, {"t", "т"}, {"u", "у"}, {"v", "в"},
        {"y", "и"}, {"z", "з"}};
    QStringList out;
    for (const QString &word : latin.simplified().split(' ', Qt::SkipEmptyParts)) {
        QString w = word.toLower(), r;
        int i = 0;
        while (i < w.size()) {
            bool hit = false;
            for (const auto &p : rules) {
                if (w.midRef(i).startsWith(p.first)) {
                    QString t = p.second;
                    if (p.first == QLatin1String("c") && i + 1 < w.size() && QStringLiteral("eiy").contains(w[i + 1]))
                        t = QStringLiteral("с");
                    if (p.first == QLatin1String("e") && i == 0)
                        t = QStringLiteral("э");
                    r += t;
                    i += p.first.size();
                    hit = true;
                    break;
                }
            }
            if (!hit)
                r += w[i++];
        }
        if (!r.isEmpty())
            r[0] = r[0].toUpper();
        out << r;
    }
    return out.join(' ');
}

} // namespace Bio

// ======================= Нейросеть (Claude / Qwen) =======================

namespace Ai {
enum Provider { Off = 0, Claude = 1, Qwen = 2, Custom = 3 };
struct Config {
    int provider = Off;
    QString key, model, baseUrl;
    bool search = false;
};
static Config load()
{
    QSettings st;
    Config c;
    c.provider = st.value(QStringLiteral("ai/provider"), int(Off)).toInt();
    c.key = st.value(QStringLiteral("ai/key")).toString();
    c.model = st.value(QStringLiteral("ai/model")).toString();
    c.baseUrl = st.value(QStringLiteral("ai/baseUrl")).toString();
    c.search = st.value(QStringLiteral("ai/search"), true).toBool();
    if (c.model.isEmpty())
        c.model = c.provider == Claude ? QStringLiteral("claude-sonnet-5-5") : QStringLiteral("qwen-plus");
    if (c.baseUrl.isEmpty())
        c.baseUrl = c.provider == Claude ? QStringLiteral("https://api.anthropic.com")
                                         : QStringLiteral("https://dashscope-intl.aliyuncs.com/compatible-mode/v1");
    return c;
}
static bool enabled()
{
    const Config c = load();
    return c.provider != Off && !c.key.isEmpty();
}

// Задание для нейросети: ваш образец анкеты, правила и собранные сведения
static QString prompt(const QString &name, const QJsonObject &iafd, const QString &wiki)
{
    QString p = QStringLiteral(
        "Напиши биографию актрисы фильмов для взрослых %1 для карточки исполнителя в медиатеке Jellyfin.\n\n"
        "Требования:\n"
        "- Русский язык, три коротких абзаца, нейтральный энциклопедический стиль, без откровенных подробностей.\n"
        "- Первая фраза: «Русское имя (Латинское имя), также известная как до трёх псевдонимов — национальность актриса фильмов для взрослых "
        "[и эротическая модель, если это так]. Родилась дата в месте.»\n"
        "- Второй абзац: начало карьеры, студии, число сцен или фильмов, образ, другие псевдонимы.\n"
        "- Третий абзац: снимается ли сейчас или когда завершила карьеру и чем занялась; награды или «Сведений о наградах и номинациях нет».\n"
        "- Только проверяемые факты из сведений ниже или из надёжных источников. Если факт не подтверждён — не пиши его. Ничего не выдумывай.\n"
        "- Не добавляй разделов «Краткая справка», списков и пояснений — только текст биографии.\n\n"
        "Пример нужного стиля:\n"
        "Таисия Шанти (Taissia Shanti), также известная как Eva Shanti, Yanika Shanti и Taisia — российская актриса фильмов для взрослых и "
        "эротическая модель. Родилась 9 ноября 1992 года в России.\n\n"
        "Карьеру в кино для взрослых Таисия начала в 2013 году и снималась в основном у европейских студий. За почти десять лет работы она "
        "снялась более чем в 400 сценах и стала одной из самых плодовитых российских актрис своего поколения. Работала под многими "
        "псевдонимами, включая Cindy March, Diva, Elizavetta и Magda.\n\n"
        "Таисия Шанти снималась до 2022 года и затем завершила карьеру. Сведений о наградах и номинациях нет.\n\n").arg(name);
    p += QStringLiteral("Сведения с IAFD (JSON): ") + QString::fromUtf8(QJsonDocument(iafd).toJson(QJsonDocument::Compact)) + QStringLiteral("\n");
    if (!wiki.isEmpty())
        p += QStringLiteral("Сведения из Википедии: ") + wiki.left(4000) + QStringLiteral("\n");
    p += QStringLiteral(
        "\nОтветь ТОЛЬКО одним JSON-объектом без пояснений:\n"
        "{\"ru_name\": \"имя по-русски\", \"bio\": \"три абзаца, разделённые \\n\\n\", \"birth_date\": \"ГГГГ-ММ-ДД или пусто\", "
        "\"birth_place\": \"город, страна по-русски или пусто\", \"aliases\": [\"все известные псевдонимы латиницей\"]}");
    return p;
}

static void ask(QNetworkAccessManager *net, QObject *ctx, const QString &text, std::function<void(const QString &, const QString &)> cb)
{
    const Config c = load();
    QNetworkRequest req;
    QJsonObject body;
    if (c.provider == Claude) {
        req.setUrl(QUrl(c.baseUrl + QStringLiteral("/v1/messages")));
        req.setRawHeader("x-api-key", c.key.toUtf8());
        req.setRawHeader("anthropic-version", "2023-06-01");
        body = {{"model", c.model}, {"max_tokens", 3000},
                {"messages", QJsonArray{QJsonObject{{"role", "user"}, {"content", text}}}}};
        if (c.search)   // поиск в интернете силами Claude
            body.insert("tools", QJsonArray{QJsonObject{{"type", "web_search_20250305"}, {"name", "web_search"}, {"max_uses", 5}}});
    } else {
        req.setUrl(QUrl(c.baseUrl + QStringLiteral("/chat/completions")));
        req.setRawHeader("Authorization", "Bearer " + c.key.toUtf8());
        body = {{"model", c.model}, {"messages", QJsonArray{QJsonObject{{"role", "user"}, {"content", text}}}}};
        if (c.search && c.provider == Qwen)   // поиск в интернете силами Qwen
            body.insert("enable_search", true);
    }
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(180000);
    QNetworkReply *r = net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QObject::connect(r, &QNetworkReply::finished, ctx, [r, cb, c] {
        r->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        QString out;
        if (c.provider == Claude) {
            for (const QJsonValue &b : o.value("content").toArray())
                if (b.toObject().value("type").toString() == QLatin1String("text"))
                    out += b.toObject().value("text").toString();
        } else {
            out = o.value("choices").toArray().at(0).toObject().value("message").toObject().value("content").toString();
        }
        QString err;
        if (out.isEmpty())
            err = o.value("error").toObject().value("message").toString();
        if (out.isEmpty() && err.isEmpty())
            err = r->errorString();
        cb(out, err);
    });
}

// JSON из ответа (нейросеть иногда оборачивает его в ```)
static QJsonObject parse(const QString &text)
{
    const int a = text.indexOf('{');
    const int b = text.lastIndexOf('}');
    if (a < 0 || b <= a)
        return {};
    return QJsonDocument::fromJson(text.mid(a, b - a + 1).toUtf8()).object();
}
} // namespace Ai

// ======================= Вкладка «Сервер» =======================

ServerPage::ServerPage(QWidget *parent)
    : QWidget(parent)
{
    m_net = new QNetworkAccessManager(this);
    QSettings st;
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 14, 20, 14);
    root->setSpacing(8);

    auto *g = new QGridLayout;
    g->setHorizontalSpacing(8);
    m_url = new QLineEdit(this);
    m_url->setPlaceholderText(QStringLiteral("http://203.0.113.10:8096"));
    m_url->setText(st.value(QStringLiteral("server/url")).toString());
    m_user = new QLineEdit(this);
    m_user->setText(st.value(QStringLiteral("server/user")).toString());
    m_pass = new QLineEdit(this);
    m_pass->setEchoMode(QLineEdit::Password);
    m_pass->setPlaceholderText(T("пароль не сохраняется"));
    m_login = new QPushButton(this);
    m_loginState = new QLabel(this);
    m_loginState->setObjectName(QStringLiteral("Muted"));
    g->addWidget(new QLabel(T("Сервер Jellyfin:"), this), 0, 0);
    g->addWidget(m_url, 0, 1, 1, 3);
    g->addWidget(new QLabel(T("Логин:"), this), 1, 0);
    g->addWidget(m_user, 1, 1);
    g->addWidget(new QLabel(T("Пароль:"), this), 1, 2);
    g->addWidget(m_pass, 1, 3);
    g->addWidget(m_login, 0, 4, 2, 1);
    g->setColumnStretch(1, 1);
    g->setColumnStretch(3, 1);
    root->addLayout(g);
    root->addWidget(m_loginState);

    auto *r2 = new QHBoxLayout;
    m_source = new QComboBox(this);
    m_source->addItem(T("Актрисы релиза"));
    m_source->addItem(T("Все анкеты на сервере без биографии"));
    m_release = new QLineEdit(this);
    m_release->setPlaceholderText(T("Папка релиза"));
    auto *pick = new QPushButton(T("Выбрать…"), this);
    m_load = new QPushButton(T("Найти анкеты"), this);
    r2->addWidget(m_source);
    r2->addWidget(m_release, 1);
    r2->addWidget(pick);
    r2->addWidget(m_load);
    root->addLayout(r2);

    m_table = new QTableWidget(0, ColCount, this);
    m_table->setHorizontalHeaderLabels({QString(), T("Актриса"), T("Состояние"), T("Имя по-русски"), T("Дата рождения"),
                                        T("Место рождения"), T("Псевдонимы (теги)"), T("Биография (двойной щелчок — изменить)")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColBio, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setWordWrap(false);
    Ui::themedStyle(m_table, QStringLiteral(
        "QTableWidget{background:#171113;gridline-color:#33282C;border:1px solid #33282C;}"
        "QHeaderView::section{background:#261E21;color:#CFC3A8;border:none;border-bottom:1px solid #33282C;padding:5px;}"));
    root->addWidget(m_table, 1);

    m_state = new QLabel(this);
    m_state->setObjectName(QStringLiteral("StatusLabel"));
    m_state->setWordWrap(true);
    root->addWidget(m_state);

    auto *btns = new QHBoxLayout;
    m_photos = new QCheckBox(T("Загрузить фото актрисы, если на сервере его нет"), this);
    m_photos->setObjectName(QStringLiteral("PlainCheck"));
    m_photos->setChecked(st.value(QStringLiteral("server/photos"), true).toBool());
    m_write = Ui::primaryButton(T("Записать на сервер"), this);
    m_write->setMinimumWidth(200);
    btns->addWidget(m_photos);
    btns->addStretch();
    m_aiState = new QLabel(this);
    m_aiState->setObjectName(QStringLiteral("Muted"));
    auto *aiBtn = new QPushButton(T("Нейросеть…"), this);
    btns->addWidget(m_aiState);
    btns->addWidget(aiBtn);
    connect(aiBtn, &QPushButton::clicked, this, &ServerPage::aiSettings);
    btns->addWidget(m_write);
    root->addLayout(btns);

    connect(m_login, &QPushButton::clicked, this, [this] { loggedIn() ? logout() : login(); });
    connect(m_pass, &QLineEdit::returnPressed, this, &ServerPage::login);
    connect(pick, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, T("Папка релиза"), m_release->text());
        if (!d.isEmpty())
            setRelease(d);
    });
    connect(m_source, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) { m_release->setEnabled(i == 0); });
    connect(m_load, &QPushButton::clicked, this, &ServerPage::loadList);
    connect(m_write, &QPushButton::clicked, this, &ServerPage::writeAll);
    connect(m_photos, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue(QStringLiteral("server/photos"), on); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        if (col == ColBio)
            editBio(row);
    });

    setRelease(st.value(QStringLiteral("ae/lastRelease")).toString());
    updateLogin();
    const Ai::Config ai = Ai::load();
    m_aiState->setText(!Ai::enabled() ? T("анкеты по шаблону")
                                      : (ai.provider == Ai::Claude ? T("анкеты пишет Claude") : T("анкеты пишет %1").arg(ai.model)));

    // Автопроверка сборки: вход на тестовый сервер, поиск анкет и запись
    if (qEnvironmentVariableIsSet("ARISTOCRAT_TEST_SERVER")) {
        m_url->setText(qEnvironmentVariable("ARISTOCRAT_TEST_SERVER"));
        m_user->setText(QStringLiteral("test"));
        m_pass->setText(QStringLiteral("test"));
        m_auto = true;
        if (qEnvironmentVariableIsSet("ARISTOCRAT_TEST_AI")) {   // заменитель нейросети на тестовом сервере
            QSettings ai;
            ai.setValue(QStringLiteral("ai/provider"), 3);
            ai.setValue(QStringLiteral("ai/key"), QStringLiteral("test-key"));
            ai.setValue(QStringLiteral("ai/model"), QStringLiteral("test-model"));
            ai.setValue(QStringLiteral("ai/baseUrl"), qEnvironmentVariable("ARISTOCRAT_TEST_AI"));
        }
        QTimer::singleShot(500, this, &ServerPage::login);
    }
}

void ServerPage::setRelease(const QString &dir)
{
    if (!dir.isEmpty())
        m_release->setText(QDir::toNativeSeparators(dir));
}

void ServerPage::setBusy(bool busy, const QString &text)
{
    m_load->setEnabled(!busy);
    m_write->setEnabled(!busy);
    m_login->setEnabled(!busy);
    if (!text.isEmpty())
        m_state->setText(text);
}

bool ServerPage::loggedIn() const
{
    return !QSettings().value(QStringLiteral("server/token")).toString().isEmpty();
}

void ServerPage::updateLogin()
{
    const bool in = loggedIn();
    m_login->setText(in ? T("Выйти") : T("Войти"));
    m_pass->setEnabled(!in);
    m_loginState->setText(in ? T("Подключено как %1.").arg(QSettings().value(QStringLiteral("server/user")).toString())
                             : T("Введите адрес сервера (например, http://ваш-IP:8096), логин и пароль. Пароль не сохраняется — "
                                 "программа хранит только ключ доступа."));
}

QByteArray ServerPage::authHeader() const
{
    QSettings st;
    QString dev = st.value(QStringLiteral("server/deviceId")).toString();
    if (dev.isEmpty()) {
        dev = QUuid::createUuid().toString(QUuid::WithoutBraces);
        st.setValue(QStringLiteral("server/deviceId"), dev);
    }
    QString h = QStringLiteral("MediaBrowser Client=\"Aristocrat Videokonverter\", Device=\"%1\", DeviceId=\"%2\", Version=\"1.0\"")
                    .arg(QSysInfo::machineHostName().isEmpty() ? QStringLiteral("PC") : QSysInfo::machineHostName(), dev);
    const QString token = st.value(QStringLiteral("server/token")).toString();
    if (!token.isEmpty())
        h += QStringLiteral(", Token=\"%1\"").arg(token);
    return h.toUtf8();
}

QNetworkReply *ServerPage::api(const QString &method, const QString &path, const QByteArray &body, const QByteArray &type)
{
    QString base = QSettings().value(QStringLiteral("server/url")).toString().trimmed();
    while (base.endsWith('/'))
        base.chop(1);
    QNetworkRequest req(QUrl(base + path));
    req.setRawHeader("Authorization", authHeader());
    req.setRawHeader("X-Emby-Authorization", authHeader());
    const QString token = QSettings().value(QStringLiteral("server/token")).toString();
    if (!token.isEmpty())
        req.setRawHeader("X-Emby-Token", token.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QString::fromLatin1(type));
    req.setTransferTimeout(30000);
    if (method == QLatin1String("GET"))
        return m_net->get(req);
    return m_net->sendCustomRequest(req, method.toLatin1(), body);
}

void ServerPage::login()
{
    QString url = m_url->text().trimmed();
    if (!url.isEmpty() && !url.contains(QLatin1String("://")))
        url.prepend(QStringLiteral("http://"));
    if (url.isEmpty() || m_user->text().trimmed().isEmpty()) {
        m_state->setText(T("Введите адрес сервера и логин."));
        return;
    }
    m_url->setText(url);
    QSettings st;
    st.setValue(QStringLiteral("server/url"), url);
    st.setValue(QStringLiteral("server/user"), m_user->text().trimmed());
    st.remove(QStringLiteral("server/token"));
    setBusy(true, T("Вход на сервер…"));
    const QByteArray body = QJsonDocument(QJsonObject{{"Username", m_user->text().trimmed()}, {"Pw", m_pass->text()}}).toJson();
    QNetworkReply *r = api(QStringLiteral("POST"), QStringLiteral("/Users/AuthenticateByName"), body);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        setBusy(false);
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        const QString token = o.value("AccessToken").toString();
        if (r->error() != QNetworkReply::NoError || token.isEmpty()) {
            m_state->setText(r->error() == QNetworkReply::AuthenticationRequiredError
                                 ? T("Неверный логин или пароль.")
                                 : T("Сервер не ответил: %1").arg(r->errorString()));
            return;
        }
        QSettings st;
        st.setValue(QStringLiteral("server/token"), token);
        st.setValue(QStringLiteral("server/userId"), o.value("User").toObject().value("Id").toString());
        m_pass->clear();
        updateLogin();
        m_state->setText(T("Вход выполнен. Нажмите «Найти анкеты»."));
        if (m_auto)
            loadList();
    });
}

void ServerPage::logout()
{
    QSettings().remove(QStringLiteral("server/token"));
    updateLogin();
}

static QStringList releaseActresses(const QString &dir)
{
    // Актрисы из NFO релиза (мужчин там уже нет)
    QStringList names;
    for (const QString &f : QDir(dir).entryList({QStringLiteral("*.nfo")}, QDir::Files)) {
        QFile nfo(QDir(dir).filePath(f));
        if (!nfo.open(QIODevice::ReadOnly))
            continue;
        const QString x = QString::fromUtf8(nfo.readAll());
        static const QRegularExpression re(QStringLiteral("<actor>\\s*<name>([^<]+)</name>"));
        auto it = re.globalMatch(x);
        while (it.hasNext()) {
            QString n = it.next().captured(1).trimmed();
            n.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
            if (!names.contains(n))
                names << n;
        }
    }
    return names;
}

void ServerPage::loadList()
{
    if (!loggedIn()) {
        m_state->setText(T("Сначала войдите на сервер."));
        return;
    }
    m_table->setRowCount(0);
    m_queue.clear();
    auto addRow = [this](const QString &name, const QJsonObject &person) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto *chk = new QTableWidgetItem;
        chk->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        chk->setCheckState(Qt::Unchecked);
        m_table->setItem(row, ColCheck, chk);
        auto *n = new QTableWidgetItem(name);
        n->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        n->setData(Qt::UserRole, person.value("Id").toString());
        n->setData(Qt::UserRole + 1, !person.value("Overview").toString().trimmed().isEmpty());
        n->setData(Qt::UserRole + 2, person.value("ImageTags").toObject().contains("Primary"));
        m_table->setItem(row, ColName, n);
        for (int c = ColStatus; c < ColCount; ++c)
            m_table->setItem(row, c, new QTableWidgetItem);
        m_table->item(row, ColStatus)->setFlags(Qt::ItemIsEnabled);
        m_table->item(row, ColBio)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_queue << row;
    };

    if (m_source->currentIndex() == 0) {
        const QString dir = QDir::fromNativeSeparators(m_release->text().trimmed());
        const QStringList names = releaseActresses(dir);
        if (names.isEmpty()) {
            m_state->setText(T("В папке релиза нет NFO с актрисами."));
            return;
        }
        for (const QString &n : names)
            addRow(n, QJsonObject());
        setBusy(true, T("Ищу анкеты на сервере…"));
        lookupNext();
        return;
    }

    // Все исполнители без биографии
    setBusy(true, T("Загружаю список исполнителей с сервера…"));
    QNetworkReply *r = api(QStringLiteral("GET"), QStringLiteral("/Persons?fields=Overview&enableImages=true&limit=5000"));
    connect(r, &QNetworkReply::finished, this, [this, r, addRow] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            setBusy(false, T("Сервер не ответил: %1").arg(r->errorString()));
            return;
        }
        int men = 0;
        for (const QJsonValue &v : QJsonDocument::fromJson(r->readAll()).object().value("Items").toArray()) {
            const QJsonObject p = v.toObject();
            const QString name = p.value("Name").toString();
            if (name.isEmpty() || !p.value("Overview").toString().trimmed().isEmpty())
                continue;
            Gender::Source src;
            if (Gender::known(name, &src) == Gender::Male || (src == Gender::None && Gender::byFirstName(name) == Gender::Male)) {
                ++men;
                continue;
            }
            addRow(name, p);
        }
        m_state->setText(T("Без биографии: %1 (мужчины пропущены: %2). Собираю данные…").arg(m_table->rowCount()).arg(men));
        lookupNext();
    });
}

void ServerPage::lookupNext()
{
    if (m_queue.isEmpty()) {
        int ready = 0;
        for (int r = 0; r < m_table->rowCount(); ++r)
            ready += m_table->item(r, ColCheck)->checkState() == Qt::Checked;
        setBusy(false, T("Готово: можно записать %1 анкет. Проверьте тексты — двойной щелчок по биографии открывает её целиком.").arg(ready));
        if (m_auto) {
            m_auto = false;
            QTimer::singleShot(500, this, &ServerPage::writeAll);
        }
        return;
    }
    const int row = m_queue.takeFirst();
    QTableWidgetItem *n = m_table->item(row, ColName);
    const QString name = n->text();

    auto afterPerson = [this, row, n, name] {
        if (n->data(Qt::UserRole).toString().isEmpty()) {
            m_table->item(row, ColStatus)->setText(T("нет на сервере"));
            lookupNext();
            return;
        }
        if (n->data(Qt::UserRole + 1).toBool()) {
            m_table->item(row, ColStatus)->setText(T("анкета уже есть — пропуск"));
            lookupNext();
            return;
        }
        m_table->item(row, ColStatus)->setText(T("ищу в базах (IAFD, Babepedia, FreeOnes, Boobpedia, Википедия)…"));
        m_state->setText(T("Собираю данные: %1").arg(name));
        if (!m_iafd)
            m_iafd = new IafdBio(this);
        m_iafd->fetch(name, [this, row](const QJsonObject &bio) { prepareRow(row, bio); });
    };

    if (!n->data(Qt::UserRole).toString().isEmpty()) {
        afterPerson();
        return;
    }
    // Поиск исполнителя на сервере по имени
    QNetworkReply *r = api(QStringLiteral("GET"), QStringLiteral("/Persons?fields=Overview&enableImages=true&searchTerm=")
                                                      + QString::fromLatin1(QUrl::toPercentEncoding(name)));
    connect(r, &QNetworkReply::finished, this, [r, n, name, afterPerson] {
        r->deleteLater();
        for (const QJsonValue &v : QJsonDocument::fromJson(r->readAll()).object().value("Items").toArray()) {
            const QJsonObject p = v.toObject();
            if (p.value("Name").toString().compare(name, Qt::CaseInsensitive) == 0) {
                n->setData(Qt::UserRole, p.value("Id").toString());
                n->setData(Qt::UserRole + 1, !p.value("Overview").toString().trimmed().isEmpty());
                n->setData(Qt::UserRole + 2, p.value("ImageTags").toObject().contains("Primary"));
                break;
            }
        }
        afterPerson();
    });
}

void ServerPage::prepareRow(int row, const QJsonObject &bioIn)
{
    QJsonObject bio = bioIn;
    const QString name = m_table->item(row, ColName)->text();
    if (bio.isEmpty() || (bio.value("birthday").toString().isEmpty() && bio.value("years").toString().isEmpty())) {
        // Бесплатные базы ничего не дали — крайний случай: нейросеть, если указан ключ
        if (Ai::enabled()) {
            aiRow(row, bio);
            return;
        }
        m_table->item(row, ColStatus)->setText(T("нет данных в базах — заполните вручную"));
        lookupNext();
        return;
    }
    bio.insert("name", name);
    // Перевод: русское написание имени и место рождения (в двух формах: «в Уфе, Россия» и «Уфа, Россия»)
    const QString place = bio.value("birthplace").toString();
    QStringList src = {QStringLiteral("The actress %1 smiled.").arg(name)};
    if (!place.isEmpty())
        src << QStringLiteral("She was born in %1.").arg(place) << place;
    auto *tr = new Translator(this);
    auto finish = [this, row, bio, name, place](const QStringList &out) {
        QString ru;
        if (!out.isEmpty()) {
            static const QRegularExpression re(QStringLiteral("[Аа]ктриса\\s+(.+?)\\s+(улыбнулась|улыбалась|улыбается)"));
            const auto m = re.match(out[0]);
            if (m.hasMatch() && !m.captured(1).contains(QRegularExpression(QStringLiteral("[A-Za-z]"))))
                ru = m.captured(1).trimmed();
        }
        if (ru.isEmpty())
            ru = Bio::transliterate(name);
        QString bornIn, placeRu;
        if (out.size() >= 3) {
            static const QRegularExpression re(QStringLiteral("родилась\\s+((?:в|во)\\s+.+?)\\.?$"), QRegularExpression::CaseInsensitiveOption);
            bornIn = re.match(out[1]).captured(1).trimmed();
            placeRu = out[2].trimmed();
            placeRu.remove(QRegularExpression(QStringLiteral("\\.$")));
        }
        const QDate born = Release::parseDate(bio.value("birthday").toString());
        m_table->item(row, ColRuName)->setText(ru);
        m_table->item(row, ColBorn)->setText(born.isValid() ? born.toString(Qt::ISODate) : QString());
        m_table->item(row, ColPlace)->setText(placeRu.isEmpty() ? place : placeRu);
        // Все псевдонимы — в теги (в тексте биографии только первые три)
        QStringList aka;
        for (const QJsonValue &v : bio.value("akaList").toArray())
            aka << v.toString();
        m_table->item(row, ColTags)->setText(aka.join(QStringLiteral(", ")));
        const QString text = Bio::build(bio, ru, bornIn);
        m_table->item(row, ColBio)->setText(text.section('\n', 0, 0));
        m_table->item(row, ColBio)->setData(Qt::UserRole, text);
        m_table->item(row, ColBio)->setToolTip(text);
        QStringList srcs;
        for (const QJsonValue &v : bio.value("sources").toArray())
            srcs << v.toString();
        m_table->item(row, ColStatus)->setText(T("готово (%1)").arg(srcs.join(QStringLiteral(", "))));
        qInfo("bio %s: %s", qPrintable(name), QJsonDocument(bio).toJson(QJsonDocument::Compact).constData());
        m_table->item(row, ColCheck)->setCheckState(Qt::Checked);
        lookupNext();
    };
    connect(tr, &Translator::finished, this, [tr, finish](const QStringList &out) { tr->deleteLater(); finish(out); });
    connect(tr, &Translator::failed, this, [tr, finish](const QString &) { tr->deleteLater(); finish({}); });
    tr->translate(src);
}

void ServerPage::fillRow(int row, const QString &ruName, const QString &born, const QString &place,
                         const QStringList &aliases, const QString &text, const QString &status)
{
    m_table->item(row, ColRuName)->setText(ruName);
    m_table->item(row, ColBorn)->setText(born);
    m_table->item(row, ColPlace)->setText(place);
    m_table->item(row, ColTags)->setText(aliases.join(QStringLiteral(", ")));
    m_table->item(row, ColBio)->setText(text.section('\n', 0, 0));
    m_table->item(row, ColBio)->setData(Qt::UserRole, text);
    m_table->item(row, ColBio)->setToolTip(text);
    m_table->item(row, ColStatus)->setText(status);
    m_table->item(row, ColCheck)->setCheckState(text.isEmpty() ? Qt::Unchecked : Qt::Checked);
}

void ServerPage::aiRow(int row, const QJsonObject &iafdIn)
{
    const QString name = m_table->item(row, ColName)->text();
    QJsonObject iafd = iafdIn;
    iafd.remove("page");
    iafd.remove("h1");
    m_table->item(row, ColStatus)->setText(T("пишет нейросеть…"));
    m_state->setText(T("Нейросеть пишет анкету: %1").arg(name));
    // Википедия (краткая статья), если есть
    QNetworkRequest wreq(QUrl(QStringLiteral("https://en.wikipedia.org/api/rest_v1/page/summary/")
                              + QString::fromLatin1(QUrl::toPercentEncoding(QString(name).replace(' ', '_')))));
    wreq.setTransferTimeout(15000);
    QNetworkReply *w = m_net->get(wreq);
    connect(w, &QNetworkReply::finished, this, [this, w, row, name, iafd] {
        w->deleteLater();
        const QJsonObject wo = QJsonDocument::fromJson(w->readAll()).object();
        QString wiki;
        if (w->error() == QNetworkReply::NoError && wo.value("type").toString() == QLatin1String("standard"))
            wiki = wo.value("extract").toString();
        Ai::ask(m_net, this, Ai::prompt(name, iafd, wiki), [this, row, name, iafd](const QString &text, const QString &err) {
            const QJsonObject a = Ai::parse(text);
            const QString bio = a.value("bio").toString().trimmed();
            if (bio.isEmpty()) {
                // Нейросеть не ответила — анкета по шаблону, если есть данные IAFD
                m_table->item(row, ColStatus)->setText(T("нейросеть: %1").arg(err.isEmpty() ? T("пустой ответ") : err.left(120)));
                if (!iafd.value("birthday").toString().isEmpty() || !iafd.value("years").toString().isEmpty()) {
                    QJsonObject b = iafd;
                    b.insert("name", name);
                    // шаблон без нейросети
                    const QString ru = Bio::transliterate(name);
                    QStringList aka;
                    for (const QJsonValue &v : iafd.value("akaList").toArray())
                        aka << v.toString();
                    const QDate born = Release::parseDate(iafd.value("birthday").toString());
                    fillRow(row, ru, born.isValid() ? born.toString(Qt::ISODate) : QString(), iafd.value("birthplace").toString(), aka,
                            Bio::build(b, ru, QString()), T("по шаблону (нейросеть не ответила)"));
                }
                lookupNext();
                return;
            }
            QStringList aliases;
            for (const QJsonValue &v : a.value("aliases").toArray())
                if (!v.toString().trimmed().isEmpty() && v.toString().compare(name, Qt::CaseInsensitive) != 0)
                    aliases << v.toString().trimmed();
            if (aliases.isEmpty())
                for (const QJsonValue &v : iafd.value("akaList").toArray())
                    aliases << v.toString();
            QString born = a.value("birth_date").toString().trimmed();
            if (!QDate::fromString(born, Qt::ISODate).isValid()) {
                const QDate d = Release::parseDate(iafd.value("birthday").toString());
                born = d.isValid() ? d.toString(Qt::ISODate) : QString();
            }
            fillRow(row, a.value("ru_name").toString().trimmed(), born, a.value("birth_place").toString().trimmed(), aliases,
                    bio, T("готово (нейросеть) — проверьте"));
            lookupNext();
        });
    });
}

void ServerPage::aiSettings()
{
    FramedDialog d(this, true);
    d.setWindowTitle(T("Нейросеть для анкет"));
    d.resize(620, 400);
    auto *root = d.contentLayout();
    root->setContentsMargins(20, 14, 20, 14);
    auto *info = new QLabel(T("Нейросеть пишет анкету по вашему образцу из данных IAFD и Википедии и может искать в интернете. "
                              "Нужен ключ API — он платный (у Qwen есть бесплатный пробный объём для новых аккаунтов). "
                              "Без ключа анкеты собираются по шаблону."), &d);
    info->setWordWrap(true);
    root->addWidget(info);
    Ai::Config c = Ai::load();
    auto *form = new QGridLayout;
    auto *prov = new QComboBox(&d);
    prov->addItems({T("Выключено — по шаблону"), QStringLiteral("Claude (Anthropic)"), QStringLiteral("Qwen (Alibaba Model Studio)"),
                    T("Другая (совместимая с OpenAI)")});
    prov->setCurrentIndex(c.provider);
    auto *key = new QLineEdit(c.key, &d);
    key->setEchoMode(QLineEdit::Password);
    auto *model = new QLineEdit(QSettings().value(QStringLiteral("ai/model")).toString(), &d);
    auto *base = new QLineEdit(QSettings().value(QStringLiteral("ai/baseUrl")).toString(), &d);
    auto *search = new QCheckBox(T("Разрешить искать в интернете (точнее, но дороже)"), &d);
    search->setObjectName(QStringLiteral("PlainCheck"));
    search->setChecked(c.search);
    auto hints = [prov, model, base] {
        const int p = prov->currentIndex();
        model->setPlaceholderText(p == Ai::Claude ? QStringLiteral("claude-sonnet-5-5") : QStringLiteral("qwen-plus"));
        base->setPlaceholderText(p == Ai::Claude ? QStringLiteral("https://api.anthropic.com")
                                                 : QStringLiteral("https://dashscope-intl.aliyuncs.com/compatible-mode/v1"));
    };
    hints();
    connect(prov, QOverload<int>::of(&QComboBox::currentIndexChanged), &d, hints);
    form->addWidget(new QLabel(T("Нейросеть:"), &d), 0, 0);
    form->addWidget(prov, 0, 1);
    form->addWidget(new QLabel(T("Ключ API:"), &d), 1, 0);
    form->addWidget(key, 1, 1);
    form->addWidget(new QLabel(T("Модель:"), &d), 2, 0);
    form->addWidget(model, 2, 1);
    form->addWidget(new QLabel(T("Адрес API:"), &d), 3, 0);
    form->addWidget(base, 3, 1);
    form->addWidget(search, 4, 1);
    form->setColumnStretch(1, 1);
    root->addLayout(form);
    root->addStretch();
    auto *btns = new QHBoxLayout;
    auto *cancel = new QPushButton(T("Отмена"), &d);
    auto *ok = Ui::primaryButton(T("Сохранить"), &d);
    btns->addStretch();
    btns->addWidget(cancel);
    btns->addWidget(ok);
    root->addLayout(btns);
    connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
    connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
    if (d.exec() != QDialog::Accepted)
        return;
    QSettings st;
    st.setValue(QStringLiteral("ai/provider"), prov->currentIndex());
    st.setValue(QStringLiteral("ai/key"), key->text().trimmed());
    st.setValue(QStringLiteral("ai/model"), model->text().trimmed());
    st.setValue(QStringLiteral("ai/baseUrl"), base->text().trimmed());
    st.setValue(QStringLiteral("ai/search"), search->isChecked());
    const Ai::Config n = Ai::load();
    m_aiState->setText(!Ai::enabled() ? T("анкеты по шаблону")
                                      : (n.provider == Ai::Claude ? T("анкеты пишет Claude") : T("анкеты пишет %1").arg(n.model)));
}

void ServerPage::editBio(int row)
{
    FramedDialog d(this, true);
    d.setWindowTitle(T("Биография: %1").arg(m_table->item(row, ColName)->text()));
    d.resize(720, 460);
    auto *edit = new QPlainTextEdit(&d);
    edit->setPlainText(m_table->item(row, ColBio)->data(Qt::UserRole).toString());
    d.contentLayout()->setContentsMargins(16, 12, 16, 12);
    d.contentLayout()->addWidget(edit, 1);
    auto *btns = new QHBoxLayout;
    auto *cancel = new QPushButton(T("Отмена"), &d);
    auto *ok = Ui::primaryButton(T("Сохранить"), &d);
    btns->addStretch();
    btns->addWidget(cancel);
    btns->addWidget(ok);
    d.contentLayout()->addLayout(btns);
    connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
    connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
    if (d.exec() != QDialog::Accepted)
        return;
    const QString text = edit->toPlainText().trimmed();
    m_table->item(row, ColBio)->setData(Qt::UserRole, text);
    m_table->item(row, ColBio)->setText(text.section('\n', 0, 0));
    m_table->item(row, ColBio)->setToolTip(text);
    if (!text.isEmpty() && !m_table->item(row, ColName)->data(Qt::UserRole).toString().isEmpty())
        m_table->item(row, ColCheck)->setCheckState(Qt::Checked);
}

void ServerPage::writeAll()
{
    m_queue.clear();
    for (int r = 0; r < m_table->rowCount(); ++r)
        if (m_table->item(r, ColCheck)->checkState() == Qt::Checked && !m_table->item(r, ColName)->data(Qt::UserRole).toString().isEmpty())
            m_queue << r;
    if (m_queue.isEmpty()) {
        m_state->setText(T("Отметьте анкеты для записи."));
        return;
    }
    setBusy(true, T("Записываю анкеты на сервер…"));
    writeNext();
}

void ServerPage::writeNext()
{
    if (m_queue.isEmpty()) {
        setBusy(false, T("Анкеты записаны на сервер."));
        return;
    }
    const int row = m_queue.takeFirst();
    QTableWidgetItem *n = m_table->item(row, ColName);
    const QString id = n->data(Qt::UserRole).toString();
    const QString uid = QSettings().value(QStringLiteral("server/userId")).toString();
    // Jellyfin требует полную карточку: берём, меняем нужные поля, отправляем обратно
    QNetworkReply *r = api(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items/%2").arg(uid, id));
    connect(r, &QNetworkReply::finished, this, [this, r, row, n, id] {
        r->deleteLater();
        QJsonObject item = QJsonDocument::fromJson(r->readAll()).object();
        if (r->error() != QNetworkReply::NoError || item.isEmpty()) {
            m_table->item(row, ColStatus)->setText(T("ошибка: %1").arg(r->errorString()));
            writeNext();
            return;
        }
        item.insert("Overview", m_table->item(row, ColBio)->data(Qt::UserRole).toString());
        const QDate born = QDate::fromString(m_table->item(row, ColBorn)->text().trimmed(), Qt::ISODate);
        if (born.isValid())
            item.insert("PremiereDate", born.toString(Qt::ISODate) + QStringLiteral("T00:00:00.0000000Z"));
        const QString place = m_table->item(row, ColPlace)->text().trimmed();
        if (!place.isEmpty())
            item.insert("ProductionLocations", QJsonArray{place});
        // Псевдонимы → теги (уже стоящие теги сохраняются)
        QJsonArray tags = item.value("Tags").toArray();
        for (const QString &t : m_table->item(row, ColTags)->text().split(',', Qt::SkipEmptyParts)) {
            const QString a = t.trimmed();
            bool have = false;
            for (const QJsonValue &v : tags)
                have |= v.toString().compare(a, Qt::CaseInsensitive) == 0;
            if (!a.isEmpty() && !have)
                tags.append(a);
        }
        item.insert("Tags", tags);
        QNetworkReply *u = api(QStringLiteral("POST"), QStringLiteral("/Items/") + id, QJsonDocument(item).toJson(QJsonDocument::Compact));
        connect(u, &QNetworkReply::finished, this, [this, u, row, n, id] {
            u->deleteLater();
            if (u->error() != QNetworkReply::NoError) {
                m_table->item(row, ColStatus)->setText(T("ошибка записи: %1").arg(u->errorString()));
                writeNext();
                return;
            }
            m_table->item(row, ColStatus)->setText(T("записано"));
            m_table->item(row, ColCheck)->setCheckState(Qt::Unchecked);
            n->setData(Qt::UserRole + 1, true);
            // Фото: только если на сервере его нет
            QString photo;
            if (m_photos->isChecked() && !n->data(Qt::UserRole + 2).toBool()) {
                const QString name = n->text();
                const QString rel = QDir::fromNativeSeparators(m_release->text().trimmed());
                const QString inRelease = QDir(rel).filePath(QStringLiteral(".actors/") + Release::actorFile(name) + QStringLiteral(".jpg"));
                for (const QString &f : {ActressPhotos::cacheFile(name), inRelease})
                    if (photo.isEmpty() && QFileInfo(f).size() > 20000)
                        photo = f;
            }
            QFile pf(photo);
            if (photo.isEmpty() || !pf.open(QIODevice::ReadOnly)) {
                writeNext();
                return;
            }
            QNetworkReply *p = api(QStringLiteral("POST"), QStringLiteral("/Items/%1/Images/Primary").arg(id), pf.readAll().toBase64(), "image/jpeg");
            connect(p, &QNetworkReply::finished, this, [this, p, row, n] {
                p->deleteLater();
                if (p->error() == QNetworkReply::NoError) {
                    m_table->item(row, ColStatus)->setText(T("записано, фото загружено"));
                    n->setData(Qt::UserRole + 2, true);
                }
                writeNext();
            });
        });
    });
}
