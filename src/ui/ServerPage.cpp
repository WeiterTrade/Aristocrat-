#include "ServerPage.h"
#include "ActressPhotos.h"
#include "BrowserWindow.h"
#include "SceneTools.h"
#include "ServerScan.h"
#include "Widgets.h"
#include "core/Gender.h"
#include "core/I18n.h"
#include "core/Release.h"
#include "Frameless.h"

#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QPixmap>
#include <QSignalBlocker>
#include <functional>
#include <memory>
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
    const bool complete = !m_merged.value("birthday").toString().isEmpty() && !m_merged.value("years").toString().isEmpty()
                          && !m_merged.value("birthplace").toString().isEmpty() && !m_aka.isEmpty();
    if (m_index >= m_sources.size() || complete) {   // всё главное уже есть — остальные базы не нужны
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


// ---- Анкета, вставленная вручную (например, из чата) ----
// Понимает строки-поля («Дата рождения: …», «Место рождения: …», «Псевдонимы: …», «Имя: …», «Дата смерти: …») —
// они в текст биографии не попадают, — и обычный текст по образцу:
// «Таисия Шанти (Taissia Shanti), также известная как A, B и C — … Родилась 2 мая 1997 года в Уфе, Россия.»
namespace Pasted {

struct Card {
    QString ruName, place, overview;
    QString realName;      // «Оригинальное имя» в Jellyfin: настоящее имя или «Не Раскрывается»
    QDate born, died;
    QStringList aliases;
    bool hasDied = false;
};

static QDate ruDate(const QString &textIn)
{
    const QString text = textIn.trimmed();
    static const QStringList months = {"январ", "феврал", "март", "апрел", "ма", "июн", "июл", "август", "сентябр", "октябр", "ноябр", "декабр"};
    static const QRegularExpression ru(QStringLiteral("(\\d{1,2})\\s+([а-яё]+)\\s+((?:19|20)\\d{2})"), QRegularExpression::CaseInsensitiveOption);
    const auto m = ru.match(text);
    if (m.hasMatch()) {
        const QString w = m.captured(2).toLower();
        int month = 0;
        for (int i = 0; i < months.size() && !month; ++i)
            if (i == 4 ? (w == QStringLiteral("мая") || w == QStringLiteral("май")) : w.startsWith(months[i]))
                month = i + 1;
        const QDate d(m.captured(3).toInt(), month, m.captured(1).toInt());
        if (d.isValid())
            return d;
    }
    static const QRegularExpression dots(QStringLiteral("(\\d{1,2})[./](\\d{1,2})[./]((?:19|20)\\d{2})"));
    const auto d = dots.match(text);
    if (d.hasMatch()) {
        const QDate x(d.captured(3).toInt(), d.captured(2).toInt(), d.captured(1).toInt());
        if (x.isValid())
            return x;
    }
    const QDate iso = QDate::fromString(text.left(10), Qt::ISODate);
    if (iso.isValid())
        return iso;
    return Release::parseDate(text);
}

// «Уфе, Россия» → «Уфа, Россия», «Лос-Анджелесе, Калифорнии» → «Лос-Анджелес, Калифорния» (частые случаи; поле можно поправить)
static QString nominative(const QString &place)
{
    static const QStringList femA = {"Москв", "Праг", "Вен", "Риг", "Уф", "Самар", "Варшав", "Анкар", "Гаван", "Женев", "Флорид", "Аризон",
                                     "Невад", "Небраск", "Алабам", "Оклахом", "Луизиан", "Миннесот", "Монтан", "Индиан", "Айов", "Канад",
                                     "Кубан", "Венгри", "Казан", "Пенз", "Твер", "Кир", "Ялт", "Одесс", "Полтав", "Гаага", "Сицили", "Барселон",
                                     "Флоренци", "Верон", "Генуя", "Пермь", "Тул", "Калуг", "Анап", "Чит", "Ухт", "Вологд", "Костром", "Рязан"};
    QStringList out;
    for (const QString &partIn : place.split(',')) {
        QString w = partIn.trimmed();
        w.remove(QRegularExpression(QStringLiteral("^(?:городе|город|г\\.|посёлке|поселке|деревне|штате|штат|провинции|области)\\s+")));
        if (w.endsWith(QStringLiteral("ии")))
            w = w.left(w.size() - 1) + QStringLiteral("я");            // России → Россия, Калифорнии → Калифорния
        else if (w.endsWith(QLatin1Char('е')) && !w.contains(' ')) {
            const QString stem = w.left(w.size() - 1);
            bool fem = false;
            for (const QString &f : femA)
                fem |= stem == f;
            w = fem ? stem + QStringLiteral("а") : stem;                  // Уфе → Уфа, Минске → Минск
        }
        if (!w.isEmpty())
            out << w;
    }
    return out.join(QStringLiteral(", "));
}

static QStringList splitAliases(const QString &s)
{
    QStringList out;
    QString t = s;
    t.replace(QRegularExpression(QStringLiteral("\\s+и\\s+|\\s+and\\s+|;")), QStringLiteral(","));
    for (const QString &a : t.split(',', Qt::SkipEmptyParts)) {
        const QString x = a.trimmed().remove(QRegularExpression(QStringLiteral("^[«\"]|[»\".]$")));
        if (!x.isEmpty() && x.size() < 60 && !out.contains(x, Qt::CaseInsensitive))
            out << x;
    }
    return out;
}

static Card parse(const QString &raw)
{
    Card c;
    QStringList body;
    static const QRegularExpression field(QStringLiteral("^\\s*[*•-]?\\s*\\**\\s*(Имя(?: на русском)?|Русское имя|Настоящее имя|Имя при рождении|Оригинальное имя|Дата рождения|Родилась|Место рождения|"
                                                         "Псевдонимы|Также известна как|Теги|Дата смерти|Умерла|Биография)\\s*\\**\\s*:\\s*\\**\\s*(.*)$"),
                                          QRegularExpression::CaseInsensitiveOption);
    // Разделы ответа из чата: «Биография» (текст), «Краткая справка» (только поля), «Что проверено» (не нужно)
    static const QRegularExpression bioHead(QStringLiteral("^\\s*#*\\s*\\**\\s*Биография\\s*\\**\\s*:?\\s*$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression infoHead(QStringLiteral("^\\s*#*\\s*\\**\\s*(Краткая справка|Справка|Основные данные|Данные для полей|Метаданные)\\b"),
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression skipHead(QStringLiteral("^\\s*#*\\s*\\**\\s*(Что проверено|Проверено|Источники|Примечани[ея]|Sources)\\b"),
                                             QRegularExpression::CaseInsensitiveOption);
    enum { Text, Info, Skip } part = Text;
    for (const QString &line : raw.split('\n')) {
        if (skipHead.match(line).hasMatch()) {
            part = Skip;
            continue;
        }
        if (part == Skip)
            continue;
        if (infoHead.match(line).hasMatch()) {
            part = Info;
            continue;
        }
        if (bioHead.match(line).hasMatch()) {
            part = Text;
            continue;
        }
        const auto m = field.match(line);
        if (!m.hasMatch()) {
            if (part == Text)
                body << line;
            continue;
        }
        const QString key = m.captured(1).toLower(), val = m.captured(2).trimmed();
        if (key.startsWith(QStringLiteral("настоящее")) || key.startsWith(QStringLiteral("имя при")) || key.startsWith(QStringLiteral("оригинальное")))
            c.realName = val.contains(QRegularExpression(QStringLiteral("не\\s+раскрыва|неизвестн|не\\s+разглаш"), QRegularExpression::CaseInsensitiveOption))
                             ? QStringLiteral("Не Раскрывается") : QString(val).remove(QRegularExpression(QStringLiteral("[.;]+$")));
        else if (key.startsWith(QStringLiteral("имя")) || key.startsWith(QStringLiteral("русское")))
            c.ruName = val;
        else if (key.startsWith(QStringLiteral("дата рожд")) || key == QStringLiteral("родилась"))
            c.born = ruDate(val);
        else if (key.startsWith(QStringLiteral("место")))
            c.place = val;
        else if (key.startsWith(QStringLiteral("псевдоним")) || key.startsWith(QStringLiteral("также")) || key == QStringLiteral("теги"))
            c.aliases = splitAliases(val);
        else if (key.startsWith(QStringLiteral("дата смерти")) || key == QStringLiteral("умерла")) {
            c.hasDied = true;
            c.died = ruDate(val);
        } else if (key == QStringLiteral("биография") && !val.isEmpty())
            body << val;
    }
    c.overview = body.join('\n').trimmed();
    // Фраза о настоящем имени уходит из биографии в поле «Оригинальное имя»
    static const QRegularExpression realRe(QStringLiteral("\\s*(?:[Ее]ё\\s+)?[Нн]астоящее\\s+имя[^.\\n]*\\.?"));
    const auto rm = realRe.match(c.overview);
    if (rm.hasMatch()) {
        const QString phrase = rm.captured(0).trimmed();
        if (c.realName.isEmpty()) {
            if (phrase.contains(QRegularExpression(QStringLiteral("не\\s+раскрыва|неизвестн|не\\s+разглаш"), QRegularExpression::CaseInsensitiveOption)))
                c.realName = QStringLiteral("Не Раскрывается");
            else {
                static const QRegularExpression val(QStringLiteral("имя(?:\\s+актрисы)?\\s*(?:—|–|-|:)\\s*([^.\\n]+)"));
                c.realName = val.match(phrase).captured(1).trimmed();
            }
        }
        c.overview.remove(rm.capturedStart(0), rm.capturedLength(0));
        c.overview.replace(QRegularExpression(QStringLiteral("[ \\t]{2,}")), QStringLiteral(" "));
        c.overview = c.overview.trimmed();
    }
    c.overview.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));
    const QString t = c.overview;
    // Имя: «Таисия Шанти (Taissia Shanti), …»
    if (c.ruName.isEmpty()) {
        static const QRegularExpression nm(QStringLiteral("^\\s*([А-ЯЁ][А-Яа-яЁё\\-]+(?:\\s+[А-ЯЁ][А-Яа-яЁё\\-]+){0,2})\\s*\\("));
        c.ruName = nm.match(t).captured(1).trimmed();
    }
    // Дата и место рождения: «Родилась 2 мая 1997 года в Уфе, Россия.»
    static const QRegularExpression bornRe(QStringLiteral("[Рр]одилась\\s+(\\d{1,2}\\s+[а-яё]+\\s+(?:19|20)\\d{2}|\\d{1,2}[./]\\d{1,2}[./](?:19|20)\\d{2})"
                                                          "(?:\\s+года)?(?:\\s*(?:в|во)\\s+([^.\\n]+))?"));
    const auto b = bornRe.match(t);
    if (b.hasMatch()) {
        if (!c.born.isValid())
            c.born = ruDate(b.captured(1));
        if (c.place.isEmpty())
            c.place = nominative(b.captured(2).trimmed());
    }
    if (c.aliases.isEmpty()) {
        static const QRegularExpression aka(QStringLiteral("также\\s+известн\\w*\\s+(?:как|под\\s+именами?)\\s+([^—–\\n]+?)\\s*(?:—|–|\\s-\\s|\\.|$)"));
        c.aliases = splitAliases(aka.match(t).captured(1));
    }
    return c;
}

} // namespace Pasted

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

// ======================= Сборка анкеты (общая для вкладки и пробежки) =======================

void composeBio(QNetworkAccessManager *net, QObject *ctx, const QString &name, const QJsonObject &dataIn,
                std::function<void(const QJsonObject &)> cb)
{
    QJsonObject data = dataIn;
    data.insert("name", name);
    QStringList aka;
    for (const QJsonValue &v : data.value("akaList").toArray())
        aka << v.toString();
    const bool enough = !data.value("birthday").toString().isEmpty() || !data.value("years").toString().isEmpty();
    if (!enough) {
        if (!Ai::enabled()) {
            cb({});
            return;
        }
        // Крайний случай: нейросеть (Википедия в помощь)
        QNetworkRequest wreq(QUrl(QStringLiteral("https://en.wikipedia.org/api/rest_v1/page/summary/")
                                  + QString::fromLatin1(QUrl::toPercentEncoding(QString(name).replace(' ', '_')))));
        wreq.setTransferTimeout(15000);
        QNetworkReply *w = net->get(wreq);
        QObject::connect(w, &QNetworkReply::finished, ctx, [w, net, ctx, name, data, aka, cb] {
            w->deleteLater();
            const QJsonObject wo = QJsonDocument::fromJson(w->readAll()).object();
            const QString wiki = wo.value("type").toString() == QLatin1String("standard") ? wo.value("extract").toString() : QString();
            QJsonObject clean = data;
            clean.remove("page");
            Ai::ask(net, ctx, Ai::prompt(name, clean, wiki), [name, aka, cb](const QString &text, const QString &) {
                const QJsonObject a = Ai::parse(text);
                if (a.value("bio").toString().trimmed().isEmpty()) {
                    cb({});
                    return;
                }
                QJsonArray al = a.value("aliases").toArray();
                if (al.isEmpty())
                    al = QJsonArray::fromStringList(aka);
                const QString born = a.value("birth_date").toString().trimmed();
                cb(QJsonObject{{"ruName", a.value("ru_name").toString().trimmed()},
                               {"born", QDate::fromString(born, Qt::ISODate).isValid() ? born : QString()},
                               {"place", a.value("birth_place").toString().trimmed()},
                               {"aliases", al},
                               {"text", a.value("bio").toString().trimmed()},
                               {"via", QStringLiteral("AI")}});
            });
        });
        return;
    }
    // Шаблон: русское имя и место рождения через перевод
    const QString place = data.value("birthplace").toString();
    QStringList src = {QStringLiteral("The actress %1 smiled.").arg(name)};
    if (!place.isEmpty())
        src << QStringLiteral("She was born in %1.").arg(place) << place;
    auto *tr = new Translator(ctx);
    auto finish = [data, name, place, aka, cb](const QStringList &out) {
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
        const QDate born = Release::parseDate(data.value("birthday").toString());
        QStringList srcs;
        for (const QJsonValue &v : data.value("sources").toArray())
            srcs << v.toString();
        cb(QJsonObject{{"ruName", ru},
                       {"born", born.isValid() ? born.toString(Qt::ISODate) : QString()},
                       {"place", placeRu.isEmpty() ? place : placeRu},
                       {"aliases", QJsonArray::fromStringList(aka)},
                       {"text", Bio::build(data, ru, bornIn)},
                       {"via", srcs.join(QStringLiteral(", "))}});
    };
    QObject::connect(tr, &Translator::finished, ctx, [tr, finish](const QStringList &out) { tr->deleteLater(); finish(out); });
    QObject::connect(tr, &Translator::failed, ctx, [tr, finish](const QString &) { tr->deleteLater(); finish({}); });
    tr->translate(src);
}

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
    m_source->addItem(T("Все актрисы на сервере"));
    m_release = new QLineEdit(this);
    m_release->setPlaceholderText(T("Папка релиза"));
    auto *pick = new QPushButton(T("Выбрать…"), this);
    m_load = new QPushButton(T("Найти анкеты"), this);
    auto *scan = new QPushButton(T("Пробежаться по серверу…"), this);
    scan->setToolTip(T("Найти на сервере актрис без биографии или фото и фильмы с неполными данными и дописать недостающее"));
    r2->addWidget(m_source);
    r2->addWidget(m_release, 1);
    r2->addWidget(pick);
    r2->addWidget(m_load);
    r2->addWidget(scan);
    auto *imgs = new QPushButton(T("Изображения релиза → сервер"), this);
    imgs->setToolTip(T("Задняя обложка, постер, фан-арт и фото актрис из папки релиза — Jellyfin сам их не подхватывает"));
    r2->addWidget(imgs);
    connect(imgs, &QPushButton::clicked, this, [this] {
        if (!loggedIn()) {
            m_state->setText(T("Сначала войдите на сервер."));
            return;
        }
        pushReleaseImages(0);
    });
    connect(scan, &QPushButton::clicked, this, [this] {
        if (!loggedIn()) {
            m_state->setText(T("Сначала войдите на сервер."));
            return;
        }
        ServerScanDialog d(window());
        d.exec();
    });
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
    m_autoBio = new QCheckBox(T("Собирать анкеты из баз автоматически"), this);
    m_autoBio->setObjectName(QStringLiteral("PlainCheck"));
    m_autoBio->setChecked(st.value(QStringLiteral("server/autoBio"), false).toBool());
    connect(m_autoBio, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue(QStringLiteral("server/autoBio"), on); });
    btns->addWidget(m_autoBio);
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
        if (col != ColCheck)
            editActress(row);
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

void ServerPage::updateLogin()
{
    const bool in = loggedIn();
    m_login->setText(in ? T("Выйти") : T("Войти"));
    m_pass->setEnabled(!in);
    m_loginState->setText(in ? T("Подключено как %1.").arg(QSettings().value(QStringLiteral("server/user")).toString())
                             : T("Введите адрес сервера (например, http://ваш-IP:8096), логин и пароль. Пароль не сохраняется — "
                                 "программа хранит только ключ доступа."));
}

namespace Jellyfin {

static QByteArray authHeader()
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

bool loggedIn()
{
    return !QSettings().value(QStringLiteral("server/token")).toString().isEmpty();
}

QNetworkReply *request(QNetworkAccessManager *net, const QString &method, const QString &path, const QByteArray &body, const QByteArray &type)
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
    // Jellyfin во время сканирования библиотеки отвечает медленно: ждём до 2 минут тишины
    req.setTransferTimeout(120000);
    if (method == QLatin1String("GET"))
        return net->get(req);
    return net->sendCustomRequest(req, method.toLatin1(), body);
}

void findPerson(QNetworkAccessManager *net, QObject *ctx, const QString &name, std::function<void(const QJsonObject &)> cb)
{
    if (!loggedIn()) {
        cb({});
        return;
    }
    QNetworkReply *r = request(net, QStringLiteral("GET"),
                               QStringLiteral("/Persons?fields=Overview,PremiereDate,ProductionLocations,EndDate&enableImages=true&searchTerm=")
                                   + QString::fromLatin1(QUrl::toPercentEncoding(name)));
    QObject::connect(r, &QNetworkReply::finished, ctx, [r, name, cb] {
        r->deleteLater();
        for (const QJsonValue &v : QJsonDocument::fromJson(r->readAll()).object().value("Items").toArray())
            if (v.toObject().value("Name").toString().compare(name, Qt::CaseInsensitive) == 0) {
                cb(v.toObject());
                return;
            }
        cb({});
    });
}

QString itemQuery(const QString &id)
{
    const QString uid = QSettings().value(QStringLiteral("server/userId")).toString();
    return QStringLiteral("/Items?ids=%1&userId=%2&fields=Overview,Genres,Tags,Studios,People,ProductionLocations,ProviderIds,"
                          "Taglines,OriginalTitle,SortName,DateCreated,ExternalUrls,RemoteTrailers,CustomRating")
        .arg(id, uid);
}

QJsonObject itemFrom(const QByteArray &json)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    if (o.contains(QStringLiteral("Items")))
    {
        const QJsonArray a = o.value(QStringLiteral("Items")).toArray();
        return a.isEmpty() ? QJsonObject() : a.at(0).toObject();
    }
    return o;
}

void lockItem(QNetworkAccessManager *net, QObject *ctx, const QString &id, std::function<void()> done)
{
    const QString uid = QSettings().value(QStringLiteral("server/userId")).toString();
    Q_UNUSED(uid)
    QNetworkReply *g = request(net, QStringLiteral("GET"), itemQuery(id));
    QObject::connect(g, &QNetworkReply::finished, ctx, [net, ctx, g, id, done] {
        g->deleteLater();
        QJsonObject item = itemFrom(g->readAll());
        if (item.isEmpty() || item.value("LockData").toBool()) {
            done();
            return;
        }
        item.insert("LockData", true);
        QNetworkReply *u = request(net, QStringLiteral("POST"), QStringLiteral("/Items/%1").arg(id), QJsonDocument(item).toJson(QJsonDocument::Compact));
        QObject::connect(u, &QNetworkReply::finished, ctx, [u, done] {
            u->deleteLater();
            done();
        });
    });
}

void directorPhoto(QNetworkAccessManager *net, QObject *ctx, const QString &name, std::function<void(int)> cb)
{
    findPerson(net, ctx, name, [net, ctx, name, cb](const QJsonObject &p) {
        const QString id = p.value("Id").toString();
        if (id.isEmpty() || p.value("ImageTags").toObject().contains("Primary")) {
            cb(0);
            return;
        }
        // Снимается как актриса — у неё своё фото
        QNetworkReply *a = request(net, QStringLiteral("GET"), QStringLiteral("/Persons?personTypes=Actor&searchTerm=")
                                                                   + QString::fromLatin1(QUrl::toPercentEncoding(name)));
        QObject::connect(a, &QNetworkReply::finished, ctx, [net, ctx, a, name, id, cb] {
            a->deleteLater();
            for (const QJsonValue &v : QJsonDocument::fromJson(a->readAll()).object().value("Items").toArray())
                if (v.toObject().value("Name").toString().compare(name, Qt::CaseInsensitive) == 0) {
                    cb(0);
                    return;
                }
            QFile f(QStringLiteral(":/director.jpg"));
            f.open(QIODevice::ReadOnly);
            QNetworkReply *u = request(net, QStringLiteral("POST"), QStringLiteral("/Items/%1/Images/Primary").arg(id),
                                       f.readAll().toBase64(), "image/jpeg");
            QObject::connect(u, &QNetworkReply::finished, ctx, [u, cb] {
                u->deleteLater();
                cb(u->error() == QNetworkReply::NoError ? 1 : -1);
            });
        });
    });
}

void photo(QNetworkAccessManager *net, QObject *ctx, const QString &id, std::function<void(const QByteArray &)> cb)
{
    // Оригинал без пережатия — как скачивает Adult Soft
    QNetworkReply *r = request(net, QStringLiteral("GET"), QStringLiteral("/Items/%1/Images/Primary?quality=100").arg(id));
    QObject::connect(r, &QNetworkReply::finished, ctx, [r, cb] {
        r->deleteLater();
        cb(r->error() == QNetworkReply::NoError ? r->readAll() : QByteArray());
    });
}

} // namespace Jellyfin

QNetworkReply *ServerPage::api(const QString &method, const QString &path, const QByteArray &body, const QByteArray &type)
{
    return Jellyfin::request(m_net, method, path, body, type);
}

bool ServerPage::loggedIn() const
{
    return Jellyfin::loggedIn();
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
        n->setData(Qt::UserRole + 3, person);
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
    QNetworkReply *r = api(QStringLiteral("GET"), QStringLiteral("/Persons?fields=Overview&enableImages=true&personTypes=Actor&limit=10000"));
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
            if (name.isEmpty() || (m_source->currentIndex() == 1 && !p.value("Overview").toString().trimmed().isEmpty()))
                continue;
            Gender::Source src;
            if (Gender::known(name, &src) == Gender::Male || (src == Gender::None && Gender::byFirstName(name) == Gender::Male)) {
                ++men;
                continue;
            }
            addRow(name, p);
        }
        m_state->setText(T("Актрис в списке: %1 (мужчины пропущены: %2).").arg(m_table->rowCount()).arg(men));
        lookupNext();
    });
}

void ServerPage::lookupNext()
{
    if (m_queue.isEmpty()) {
        int ready = 0;
        for (int r = 0; r < m_table->rowCount(); ++r)
            ready += m_table->item(r, ColCheck)->checkState() == Qt::Checked;
        setBusy(false, T("Двойной щелчок по актрисе — окно анкеты: вставьте биографию и фото и запишите на сервер. Отмечено к записи: %1.").arg(ready));
        if (m_auto) {
            m_auto = false;
            QTimer::singleShot(500, this, &ServerPage::writeAll);
            if (qEnvironmentVariableIsSet("ARISTOCRAT_TEST_SCAN"))   // автопроверка пробежки
                QTimer::singleShot(6000, this, [this] { auto *d = new ServerScanDialog(window()); d->setAttribute(Qt::WA_DeleteOnClose); d->show(); });
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
            // Анкета уже есть: фото с сервера — в .actors релиза
            keepOffline(row, true);
            return;
        }
        if (!m_autoBio->isChecked() && !m_auto) {
            m_table->item(row, ColStatus)->setText(T("нет биографии — двойной щелчок, чтобы заполнить"));
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
                n->setData(Qt::UserRole + 3, p);
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

void ServerPage::keepOffline(int row, bool thenNext)   // фото актрисы с сервера → .actors релиза
{
    QTableWidgetItem *n = m_table->item(row, ColName);
    const QString name = n->text();
    const QJsonObject person = n->data(Qt::UserRole + 3).toJsonObject();
    const QString rel = QDir::fromNativeSeparators(m_release->text().trimmed());
    const QString file = QDir(rel).filePath(QStringLiteral(".actors/") + Release::actorFile(name) + QStringLiteral(".jpg"));
    auto finish = [this, row, thenNext](bool photo) {
        m_table->item(row, ColStatus)->setText(photo ? T("анкета уже есть — фото скопировано в .actors") : T("анкета уже есть — пропуск"));
        if (thenNext)
            lookupNext();
    };
    if (rel.isEmpty() || !n->data(Qt::UserRole + 2).toBool() || person.value("Id").toString().isEmpty()) {
        finish(false);
        return;
    }
    Jellyfin::photo(m_net, this, person.value("Id").toString(), [file, finish](const QByteArray &data) {
        if (data.size() < 1000) {
            finish(false);
            return;
        }
        QDir().mkpath(QFileInfo(file).absolutePath());
        QFile f(file);
        const bool ok = f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
        finish(ok);
    });
}

void ServerPage::pushReleaseImages(int attempt)
{
    const QString dir = QDir::fromNativeSeparators(m_release->text().trimmed());
    const QStringList nfos = QDir(dir).entryList({QStringLiteral("*.nfo")}, QDir::Files);
    if (dir.isEmpty() || nfos.isEmpty()) {
        m_state->setText(T("В папке релиза нет NFO."));
        return;
    }
    QFile nf(QDir(dir).filePath(nfos.first()));
    nf.open(QIODevice::ReadOnly);
    const QString x = QString::fromUtf8(nf.readAll());
    auto tag = [&x](const QString &t) {
        const auto m = QRegularExpression(QStringLiteral("<%1>([^<]*)</%1>").arg(t)).match(x);
        return m.captured(1).trimmed().replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    };
    const QString title = tag(QStringLiteral("title")), orig = tag(QStringLiteral("originaltitle"));
    const QString base = QFileInfo(nfos.first()).completeBaseName();
    const QStringList actresses = releaseActresses(dir);
    QStringList directors;
    {
        static const QRegularExpression dre(QStringLiteral("<director>([^<]+)</director>"));
        auto it = dre.globalMatch(x);
        while (it.hasNext()) {
            const QString d = it.next().captured(1).trimmed().replace(QStringLiteral("&amp;"), QStringLiteral("&"));
            if (!d.isEmpty() && !directors.contains(d) && !actresses.contains(d, Qt::CaseInsensitive))
                directors << d;
        }
    }
    setBusy(true, T("Ищу фильм на сервере: %1…").arg(title));

    const QString uid = QSettings().value(QStringLiteral("server/userId")).toString();
    QNetworkReply *r = api(QStringLiteral("GET"),
        QStringLiteral("/Users/%1/Items?IncludeItemTypes=Movie&Recursive=true&Fields=OriginalTitle&EnableImageTypes=Primary,Backdrop,BoxRear&searchTerm=%2")
            .arg(uid, QString::fromLatin1(QUrl::toPercentEncoding(title.left(60)))));
    connect(r, &QNetworkReply::finished, this, [this, r, dir, base, title, orig, actresses, directors, attempt] {
        r->deleteLater();
        QJsonObject movie;
        for (const QJsonValue &v : QJsonDocument::fromJson(r->readAll()).object().value("Items").toArray()) {
            const QJsonObject m = v.toObject();
            if (m.value("Name").toString().compare(title, Qt::CaseInsensitive) == 0
                || (!orig.isEmpty() && m.value("OriginalTitle").toString().compare(orig, Qt::CaseInsensitive) == 0)) {
                movie = m;
                break;
            }
        }
        if (movie.isEmpty()) {
            if (attempt >= 5) {
                setBusy(false, T("Фильм «%1» так и не появился на сервере. Проверьте, что релиз выгружен в папку библиотеки, и нажмите «Изображения релиза → сервер» позже.").arg(title));
                return;
            }
            // Сервер ещё не видит новый релиз — просим пересканировать библиотеку и пробуем снова
            if (attempt == 0) {
                QNetworkReply *ref = api(QStringLiteral("POST"), QStringLiteral("/Library/Refresh"));
                connect(ref, &QNetworkReply::finished, ref, &QObject::deleteLater);
            }
            m_state->setText(T("Фильма «%1» пока нет на сервере — обновляю библиотеку, попытка %2 из 5 через минуту…").arg(title).arg(attempt + 1));
            const int delay = qEnvironmentVariableIsSet("ARISTOCRAT_TEST_SERVER") ? 1000 : 60000;
            QTimer::singleShot(delay, this, [this, attempt] { pushReleaseImages(attempt + 1); });
            return;
        }
        // Что загрузить: только того, чего на сервере нет
        const QString id = movie.value("Id").toString();
        const QJsonObject tags = movie.value("ImageTags").toObject();
        struct Up { QString path, type, label; };
        auto ups = std::make_shared<QList<Up>>();
        const QString back = QDir(dir).filePath(base + QStringLiteral("-back.jpg"));
        const QString poster = QDir(dir).filePath(base + QStringLiteral("-poster.jpg"));
        const QString fanart = QDir(dir).filePath(base + QStringLiteral("-fanart.jpg"));
        if (!tags.contains("BoxRear") && QFileInfo::exists(back))
            ups->append({back, QStringLiteral("/Items/%1/Images/BoxRear").arg(id), T("задняя обложка")});
        if (!tags.contains("Primary") && QFileInfo::exists(poster))
            ups->append({poster, QStringLiteral("/Items/%1/Images/Primary").arg(id), T("постер")});
        if (movie.value("BackdropImageTags").toArray().isEmpty() && QFileInfo::exists(fanart))
            ups->append({fanart, QStringLiteral("/Items/%1/Images/Backdrop").arg(id), T("фан-арт")});
        auto done = std::make_shared<QStringList>();
        auto people = std::make_shared<QStringList>(actresses);
        auto dirs = std::make_shared<QStringList>(directors);
        auto step = std::make_shared<std::function<void()>>();
        auto locked = std::make_shared<bool>(false);
        *step = [this, ups, done, people, dirs, step, dir, id, locked] {
            if (!ups->isEmpty()) {
                const Up u = ups->takeFirst();
                QFile f(u.path);
                if (!f.open(QIODevice::ReadOnly)) {
                    (*step)();
                    return;
                }
                QNetworkReply *p = api(QStringLiteral("POST"), u.path.isEmpty() ? QString() : u.type, f.readAll().toBase64(), "image/jpeg");
                connect(p, &QNetworkReply::finished, this, [p, done, step, u] {
                    p->deleteLater();
                    if (p->error() == QNetworkReply::NoError)
                        *done << u.label;
                    (*step)();
                });
                return;
            }
            if (!people->isEmpty()) {
                // Фото актрисы — только если на сервере его нет
                const QString name = people->takeFirst();
                const QString photo = QDir(dir).filePath(QStringLiteral(".actors/") + Release::actorFile(name) + QStringLiteral(".jpg"));
                if (QFileInfo(photo).size() < 20000) {   // нет фото или серое «пустое»
                    (*step)();
                    return;
                }
                Jellyfin::findPerson(m_net, this, name, [this, name, photo, done, step](const QJsonObject &p) {
                    if (p.isEmpty() || p.value("ImageTags").toObject().contains("Primary")) {
                        (*step)();
                        return;
                    }
                    QFile f(photo);
                    f.open(QIODevice::ReadOnly);
                    QNetworkReply *u = api(QStringLiteral("POST"), QStringLiteral("/Items/%1/Images/Primary").arg(p.value("Id").toString()),
                                           f.readAll().toBase64(), "image/jpeg");
                    connect(u, &QNetworkReply::finished, this, [u, name, done, step] {
                        u->deleteLater();
                        if (u->error() == QNetworkReply::NoError)
                            *done << name;
                        (*step)();
                    });
                });
                return;
            }
            if (!*locked) {
                // Закрепить фильм: сканирование библиотеки не будет пересобирать его данные и картинки
                // (иначе загруженная задняя обложка со временем пропадает)
                *locked = true;
                const QString uid = QSettings().value(QStringLiteral("server/userId")).toString();
                QNetworkReply *g = api(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items/%2").arg(uid, id));
                connect(g, &QNetworkReply::finished, this, [this, g, id, step] {
                    g->deleteLater();
                    QJsonObject item = QJsonDocument::fromJson(g->readAll()).object();
                    if (item.isEmpty() || item.value("LockData").toBool()) {
                        (*step)();
                        return;
                    }
                    item.insert("LockData", true);
                    QNetworkReply *u = api(QStringLiteral("POST"), QStringLiteral("/Items/%1").arg(id),
                                           QJsonDocument(item).toJson(QJsonDocument::Compact));
                    connect(u, &QNetworkReply::finished, this, [u, step] {
                        u->deleteLater();
                        (*step)();
                    });
                });
                return;
            }
            if (!dirs->isEmpty()) {
                const QString name = dirs->takeFirst();
                Jellyfin::directorPhoto(m_net, this, name, [name, done, step](int res) {
                    if (res == 1)
                        *done << T("режиссёр %1").arg(name);
                    (*step)();
                });
                return;
            }
            setBusy(false, done->isEmpty() ? T("На сервере уже есть все изображения релиза.")
                                           : T("Загружено на сервер: %1.").arg(done->join(QStringLiteral(", "))));
        };
        m_state->setText(T("Загружаю изображения релиза на сервер…"));
        (*step)();
    });
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

void ServerPage::editActress(int row)
{
    QTableWidgetItem *n = m_table->item(row, ColName);
    const QString name = n->text();
    const QString id = n->data(Qt::UserRole).toString();
    FramedDialog d(this, true);
    d.setWindowTitle(T("Анкета: %1").arg(name));
    d.resize(980, 640);
    auto *body = new QHBoxLayout;
    d.contentLayout()->setContentsMargins(16, 12, 16, 12);
    d.contentLayout()->addLayout(body, 1);

    // Слева: фото
    auto *left = new QVBoxLayout;
    auto *photo = new QLabel(T("нет фото"), &d);
    photo->setFixedSize(200, 300);
    photo->setAlignment(Qt::AlignCenter);
    photo->setStyleSheet(QStringLiteral("border:1px solid rgba(128,128,128,0.4);"));
    left->addWidget(photo);
    auto *pasteImg = new QPushButton(T("Вставить фото"), &d);
    pasteImg->setToolTip(T("Картинка или ссылка на картинку из буфера обмена"));
    auto *fileImg = new QPushButton(T("Из файла…"), &d);
    auto *findImg = new QPushButton(T("Найти в интернете…"), &d);
    auto *photoState = new QLabel(&d);
    photoState->setWordWrap(true);
    photoState->setFixedWidth(200);
    for (QWidget *w : {static_cast<QWidget *>(pasteImg), static_cast<QWidget *>(fileImg), static_cast<QWidget *>(findImg)})
        left->addWidget(w);
    left->addWidget(photoState);
    left->addStretch();
    body->addLayout(left);

    // Справа: поля и биография
    auto *right = new QVBoxLayout;
    auto *hint = new QLabel(T("Вставьте готовую анкету — дата и место рождения, псевдонимы и русское имя возьмутся из текста "
                              "(можно и строками «Дата рождения: …», «Место рождения: …», «Псевдонимы: …»). Проверьте поля и нажмите «Записать на сервер»."), &d);
    hint->setWordWrap(true);
    right->addWidget(hint);
    auto *g = new QGridLayout;
    auto *ru = new QLineEdit(m_table->item(row, ColRuName)->text(), &d);
    auto *born = new QLineEdit(m_table->item(row, ColBorn)->text(), &d);
    born->setPlaceholderText(T("ГГГГ-ММ-ДД или «2 мая 1997»"));
    auto *place = new QLineEdit(m_table->item(row, ColPlace)->text(), &d);
    place->setPlaceholderText(T("Уфа, Россия"));
    auto *died = new QLineEdit(m_table->item(row, ColBorn)->data(Qt::UserRole).toString(), &d);
    died->setPlaceholderText(T("пусто — дата смерти будет удалена с сервера"));
    auto *tags = new QLineEdit(m_table->item(row, ColTags)->text(), &d);
    tags->setPlaceholderText(T("псевдонимы через запятую"));
    auto *real = new QLineEdit(m_table->item(row, ColRuName)->data(Qt::UserRole).toString(), &d);
    real->setPlaceholderText(QStringLiteral("Не Раскрывается"));
    real->setToolTip(T("Записывается в поле Jellyfin «Оригинальное имя». Пусто — «Не Раскрывается»."));
    int r = 0;
    for (auto pr : {qMakePair(T("Имя по-русски:"), ru), qMakePair(T("Настоящее имя:"), real), qMakePair(T("Дата рождения:"), born), qMakePair(T("Место рождения:"), place),
                    qMakePair(T("Дата смерти:"), died), qMakePair(T("Псевдонимы (теги):"), tags)}) {
        g->addWidget(new QLabel(pr.first, &d), r, 0);
        g->addWidget(pr.second, r++, 1);
    }
    right->addLayout(g);
    auto *bioRow = new QHBoxLayout;
    bioRow->addWidget(new QLabel(T("Биография:"), &d));
    bioRow->addStretch();
    auto *pasteText = new QPushButton(T("Вставить из буфера"), &d);
    bioRow->addWidget(pasteText);
    right->addLayout(bioRow);
    auto *edit = new QPlainTextEdit(&d);
    edit->setPlainText(m_table->item(row, ColBio)->data(Qt::UserRole).toString());
    right->addWidget(edit, 1);
    body->addLayout(right, 1);

    auto *btns = new QHBoxLayout;
    auto *state = new QLabel(&d);
    state->setWordWrap(true);
    btns->addWidget(state, 1);
    auto *cancel = new QPushButton(T("Отмена"), &d);
    auto *save = new QPushButton(T("Только сохранить"), &d);
    auto *write = Ui::primaryButton(T("Записать на сервер"), &d);
    write->setEnabled(loggedIn());
    if (id.isEmpty())
        state->setText(T("Когда список загружался, актрисы на сервере не было. При записи программа поищет её ещё раз — "
                         "если фильм уже в библиотеке Jellyfin, всё запишется."));
    btns->addWidget(cancel);
    btns->addWidget(save);
    btns->addWidget(write);
    d.contentLayout()->addLayout(btns);

    // Фото: выбранное раньше → с сервера
    auto chosen = std::make_shared<QString>(n->data(Qt::UserRole + 4).toString());
    auto show = [photo](const QImage &img) {
        if (img.isNull())
            return;
        photo->setPixmap(QPixmap::fromImage(img.scaled(photo->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    };
    if (!chosen->isEmpty() && QFileInfo::exists(*chosen)) {
        show(QImage(*chosen));
        photoState->setText(T("Выбрано — загрузится на сервер."));
    } else if (!id.isEmpty() && n->data(Qt::UserRole + 2).toBool()) {
        photoState->setText(T("Фото на сервере уже есть."));
        Jellyfin::photo(m_net, &d, id, [show](const QByteArray &data) { show(QImage::fromData(data)); });
    } else if (QFileInfo::exists(ActressPhotos::cacheFile(name))) {
        show(QImage(ActressPhotos::cacheFile(name)));
        *chosen = ActressPhotos::cacheFile(name);
        photoState->setText(T("Найдено раньше — загрузится на сервер."));
    }
    auto takePhoto = [name, chosen, show, photoState](const QByteArray &data) {
        if (!ActressPhotos::saveChosen(name, data)) {
            photoState->setText(T("Картинка не подошла (нужна вертикальная, от 200×300)."));
            return;
        }
        *chosen = ActressPhotos::cacheFile(name);
        show(QImage(*chosen));
        photoState->setText(T("Фото выбрано (1000×1500) — загрузится на сервер."));
    };
    connect(pasteImg, &QPushButton::clicked, &d, [&d, takePhoto, photoState] {
        const QMimeData *md = QGuiApplication::clipboard()->mimeData();
        if (md && md->hasImage()) {
            QByteArray buf;
            QBuffer b(&buf);
            b.open(QIODevice::WriteOnly);
            qvariant_cast<QImage>(md->imageData()).save(&b, "PNG");
            takePhoto(buf);
            return;
        }
        const QString text = md ? md->text().trimmed() : QString();
        if (QFileInfo::exists(text)) {
            QFile f(text);
            if (f.open(QIODevice::ReadOnly))
                takePhoto(f.readAll());
            return;
        }
        const QUrl url(text);
        if (!url.isValid() || !url.scheme().startsWith(QLatin1String("http"))) {
            photoState->setText(T("В буфере нет картинки или ссылки на неё."));
            return;
        }
        photoState->setText(T("загрузка…"));
        WebFetch::get(url, &d, [takePhoto, photoState](const QByteArray &data) {
            if (data.isEmpty())
                photoState->setText(T("Картинка по ссылке не загрузилась."));
            else
                takePhoto(data);
        });
    });
    connect(fileImg, &QPushButton::clicked, &d, [&d, takePhoto] {
        const QString f = QFileDialog::getOpenFileName(&d, T("Фото актрисы"), QString(), T("Изображения (*.jpg *.jpeg *.png *.webp *.bmp)"));
        QFile file(f);
        if (!f.isEmpty() && file.open(QIODevice::ReadOnly))
            takePhoto(file.readAll());
    });
    connect(findImg, &QPushButton::clicked, &d, [this, &d, name, chosen, show, photoState] {
        const QString before = QFileInfo(ActressPhotos::cacheFile(name)).lastModified().toString(Qt::ISODate);
        ActressPhotoDialog pd({name}, QDir::fromNativeSeparators(QSettings().value(QStringLiteral("ae/baseDir")).toString()), &d);
        pd.exec();
        const QString f = ActressPhotos::cacheFile(name);
        if (QFileInfo::exists(f) && QFileInfo(f).lastModified().toString(Qt::ISODate) != before) {
            *chosen = f;
            show(QImage(f));
            photoState->setText(T("Фото выбрано (1000×1500) — загрузится на сервер."));
        }
    });

    // Текст анкеты → поля
    auto apply = [=](const QString &raw) {
        const Pasted::Card c = Pasted::parse(raw);
        edit->setPlainText(c.overview);
        if (!c.ruName.isEmpty()) ru->setText(c.ruName);
        if (c.born.isValid()) born->setText(c.born.toString(Qt::ISODate));
        if (!c.place.isEmpty()) place->setText(c.place);
        if (!c.aliases.isEmpty()) tags->setText(c.aliases.join(QStringLiteral(", ")));
        if (!c.realName.isEmpty()) real->setText(c.realName);
        if (c.hasDied) died->setText(c.died.isValid() ? c.died.toString(Qt::ISODate) : QString());
        QStringList got;
        if (c.born.isValid()) got << T("дата рождения");
        if (!c.place.isEmpty()) got << T("место рождения");
        if (!c.aliases.isEmpty()) got << T("псевдонимы: %1").arg(c.aliases.size());
        state->setText(got.isEmpty() ? T("Из текста ничего не взято — заполните поля вручную.")
                                     : T("Из текста взято: %1. Проверьте поля.").arg(got.join(QStringLiteral(", "))));
    };
    connect(pasteText, &QPushButton::clicked, &d, [apply] { apply(QGuiApplication::clipboard()->text()); });
    // Вставка Ctrl+V в пустое поле — сразу разбор
    connect(edit, &QPlainTextEdit::textChanged, &d, [edit, apply] {
        static bool busy = false;
        if (busy)
            return;
        const QString t = edit->toPlainText();
        if (edit->property("wasEmpty").toBool() && t.size() > 80) {
            busy = true;
            apply(t);
            busy = false;
        }
        edit->setProperty("wasEmpty", t.trimmed().isEmpty());
    });
    edit->setProperty("wasEmpty", edit->toPlainText().trimmed().isEmpty());

    // Что сейчас на сервере (если в таблице пусто)
    if (!id.isEmpty() && loggedIn()) {
        const QString uid = QSettings().value(QStringLiteral("server/userId")).toString();
        Q_UNUSED(uid)
        QNetworkReply *rep = api(QStringLiteral("GET"), Jellyfin::itemQuery(id));
        connect(rep, &QNetworkReply::finished, &d, [=] {
            rep->deleteLater();
            const QJsonObject it = Jellyfin::itemFrom(rep->readAll());
            if (it.isEmpty())
                return;
            const QSignalBlocker block(edit);
            if (edit->toPlainText().trimmed().isEmpty()) {
                edit->setPlainText(it.value("Overview").toString());
                edit->setProperty("wasEmpty", edit->toPlainText().trimmed().isEmpty());
            }
            const QDate pb = QDate::fromString(it.value("PremiereDate").toString().left(10), Qt::ISODate);
            const QDate pd = QDate::fromString(it.value("EndDate").toString().left(10), Qt::ISODate);
            if (born->text().isEmpty() && pb.isValid())
                born->setText(pb.toString(Qt::ISODate));
            const QJsonArray locs = it.value("ProductionLocations").toArray();
            if (place->text().isEmpty() && !locs.isEmpty())
                place->setText(locs.first().toString());
            if (real->text().isEmpty())
                real->setText(it.value("OriginalTitle").toString());
            if (tags->text().isEmpty()) {
                QStringList t;
                for (const QJsonValue &v : it.value("Tags").toArray())
                    t << v.toString();
                tags->setText(t.join(QStringLiteral(", ")));
            }
            if (pd.isValid())
                state->setText(T("На сервере стоит дата смерти %1 и рождения %2 — скорее всего, Jellyfin взял их у однофамильца (TMDb). "
                                 "При записи дата смерти будет удалена, если поле пустое.").arg(pd.toString(Qt::ISODate), pb.toString(Qt::ISODate)));
        });
    }

    auto store = [=]() -> bool {
        const QString b = born->text().trimmed();
        const QDate bd = b.isEmpty() ? QDate() : Pasted::ruDate(b);
        if (!b.isEmpty() && !bd.isValid()) {
            state->setText(T("Не понял дату рождения — напишите как 1997-05-02."));
            return false;
        }
        const QString dd = died->text().trimmed();
        const QDate dt = dd.isEmpty() ? QDate() : Pasted::ruDate(dd);
        m_table->item(row, ColRuName)->setText(ru->text().trimmed());
        QString realName = real->text().trimmed();
        if (realName.contains(QRegularExpression(QStringLiteral("не\\s+раскрыва|неизвестн|не\\s+разглаш"), QRegularExpression::CaseInsensitiveOption)))
            realName = QStringLiteral("Не Раскрывается");
        m_table->item(row, ColRuName)->setData(Qt::UserRole, realName);
        m_table->item(row, ColBorn)->setText(bd.isValid() ? bd.toString(Qt::ISODate) : QString());
        m_table->item(row, ColBorn)->setData(Qt::UserRole, dt.isValid() ? dt.toString(Qt::ISODate) : QString());
        m_table->item(row, ColPlace)->setText(place->text().trimmed());
        m_table->item(row, ColTags)->setText(tags->text().trimmed());
        const QString text = edit->toPlainText().trimmed();
        m_table->item(row, ColBio)->setData(Qt::UserRole, text);
        m_table->item(row, ColBio)->setText(text.section('\n', 0, 0));
        m_table->item(row, ColBio)->setToolTip(text);
        n->setData(Qt::UserRole + 4, *chosen);
        m_table->item(row, ColStatus)->setText(T("заполнено вручную"));
        if (!text.isEmpty())
            m_table->item(row, ColCheck)->setCheckState(Qt::Checked);
        return true;
    };
    connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
    connect(save, &QPushButton::clicked, &d, [&d, store] { if (store()) d.accept(); });
    bool writeNow = false;
    connect(write, &QPushButton::clicked, &d, [&d, store, &writeNow] { if (store()) { writeNow = true; d.accept(); } });
    if (d.exec() != QDialog::Accepted || !writeNow)
        return;
    m_queue = {row};
    m_single = true;
    setBusy(true, T("Записываю анкету %1…").arg(name));
    writeNext();
}

void ServerPage::searchPhoto(int row)
{
    QTableWidgetItem *n = m_table->item(row, ColName);
    const QString name = n->text(), id = n->data(Qt::UserRole).toString();
    m_table->item(row, ColStatus)->setText(T("записано, ищу фото…"));
    if (!m_img)
        m_img = new ImageSearch(this);
    m_img->search(ActressPhotos::query(name), [this, row, n, name, id](const QList<ActressPhotos::Candidate> &list) {
        QList<ActressPhotos::Candidate> good;
        for (const ActressPhotos::Candidate &c : list)
            if (ActressPhotos::score(c) > 0)
                good << c;
        std::stable_sort(good.begin(), good.end(), [](const ActressPhotos::Candidate &a, const ActressPhotos::Candidate &b) {
            return ActressPhotos::score(a) > ActressPhotos::score(b);
        });
        if (good.isEmpty())
            qInfo("photo search %s: %d results, none suitable", qPrintable(name), int(list.size()));
        auto tryOne = std::make_shared<std::function<void(int)>>();
        *tryOne = [this, row, n, name, id, good, tryOne](int i) {
            if (i >= good.size() || i >= 10) {
                m_table->item(row, ColStatus)->setText(T("записано, фото не найдено — выберите в окне анкеты"));
                writeNext();
                return;
            }
            m_img->fetchBest(good[i], [this, row, n, name, id, tryOne, i](const QByteArray &data) {
                if (!ActressPhotos::saveChosen(name, data)) {
                    (*tryOne)(i + 1);
                    return;
                }
                QFile f(ActressPhotos::cacheFile(name));
                f.open(QIODevice::ReadOnly);
                QNetworkReply *p = api(QStringLiteral("POST"), QStringLiteral("/Items/%1/Images/Primary").arg(id), f.readAll().toBase64(), "image/jpeg");
                connect(p, &QNetworkReply::finished, this, [this, p, row, n] {
                    p->deleteLater();
                    if (p->error() == QNetworkReply::NoError) {
                        m_table->item(row, ColStatus)->setText(T("записано, фото найдено и загружено"));
                        n->setData(Qt::UserRole + 2, true);
                    } else {
                        m_table->item(row, ColStatus)->setText(T("записано, фото не загрузилось: %1").arg(p->errorString()));
                    }
                    writeNext();
                });
            });
        };
        (*tryOne)(0);
    });
}

void ServerPage::writeAll()
{
    m_queue.clear();
    for (int r = 0; r < m_table->rowCount(); ++r)
        if (m_table->item(r, ColCheck)->checkState() == Qt::Checked)   // без номера на сервере — найдём при записи
            m_queue << r;
    if (m_queue.isEmpty()) {
        m_state->setText(T("Отметьте анкеты для записи."));
        return;
    }
    setBusy(true, T("Записываю анкеты на сервер…"));
    writeNext();
}

// Сервер не ответил или оборвал связь — до 3 попыток с паузой (Jellyfin бывает занят сканированием)
bool ServerPage::retryRow(int row, QNetworkReply *r)
{
    const auto e = r->error();
    const bool transient = e == QNetworkReply::OperationCanceledError || e == QNetworkReply::TimeoutError
                           || e == QNetworkReply::RemoteHostClosedError || e == QNetworkReply::TemporaryNetworkFailureError
                           || e == QNetworkReply::NetworkSessionFailedError || e == QNetworkReply::ServiceUnavailableError
                           || e == QNetworkReply::UnknownNetworkError || e == QNetworkReply::ProxyTimeoutError
                           || e == QNetworkReply::InternalServerError;
    QTableWidgetItem *n = m_table->item(row, ColName);
    const int tries = n->data(Qt::UserRole + 5).toInt();
    if (!transient || tries >= 2) {
        n->setData(Qt::UserRole + 5, 0);
        return false;
    }
    n->setData(Qt::UserRole + 5, tries + 1);
    m_table->item(row, ColStatus)->setText(T("сервер долго не отвечает — повтор %1 из 2 через 10 с…").arg(tries + 1));
    QTimer::singleShot(10000, this, [this, row] {
        m_queue.prepend(row);
        writeNext();
    });
    return true;
}

QString ServerPage::serverError(QNetworkReply *r)
{
    if (r->error() == QNetworkReply::OperationCanceledError || r->error() == QNetworkReply::TimeoutError)
        return T("сервер не ответил за 2 минуты (возможно, занят сканированием библиотеки) — попробуйте позже");
    return r->errorString();
}

void ServerPage::writeNext()
{
    if (m_queue.isEmpty()) {
        const int errors = m_writeErrors;
        m_writeErrors = 0;
        if (m_single) {
            m_single = false;
            setBusy(false, errors ? T("Анкета не записана — причина в столбце «Состояние».") : T("Анкета записана на сервер."));
            return;
        }
        setBusy(false, errors ? T("Записано с ошибками: %1 — причина в столбце «Состояние».").arg(errors) : T("Анкеты записаны на сервер."));
        if (m_source->currentIndex() == 0)   // релиз: заодно изображения, которые Jellyfin сам не берёт
            QTimer::singleShot(300, this, [this] { pushReleaseImages(0); });
        return;
    }
    const int row = m_queue.takeFirst();
    QTableWidgetItem *n = m_table->item(row, ColName);
    const QString id = n->data(Qt::UserRole).toString();
    if (id.isEmpty()) {
        // Раньше актрисы на сервере не было (фильм ещё не добавлен) — ищем заново, заполненное сохраняется
        m_table->item(row, ColStatus)->setText(T("ищу на сервере…"));
        Jellyfin::findPerson(m_net, this, n->text(), [this, row, n](const QJsonObject &p) {
            const QString found = p.value("Id").toString();
            if (found.isEmpty()) {
                m_table->item(row, ColStatus)->setText(T("всё ещё нет на сервере — Jellyfin пока не добавил фильм; попробуйте через пару минут"));
                writeNext();
                return;
            }
            n->setData(Qt::UserRole, found);
            n->setData(Qt::UserRole + 2, p.value("ImageTags").toObject().contains("Primary"));
            n->setData(Qt::UserRole + 3, p);
            m_queue.prepend(row);
            writeNext();
        });
        return;
    }
    const QString uid = QSettings().value(QStringLiteral("server/userId")).toString();
    // Jellyfin требует полную карточку: берём, меняем нужные поля, отправляем обратно
    Q_UNUSED(uid)
    QNetworkReply *r = api(QStringLiteral("GET"), Jellyfin::itemQuery(id));
    connect(r, &QNetworkReply::finished, this, [this, r, row, n, id] {
        r->deleteLater();
        QJsonObject item = Jellyfin::itemFrom(r->readAll());
        if (r->error() != QNetworkReply::NoError || item.isEmpty()) {
            if (retryRow(row, r))
                return;
            m_table->item(row, ColStatus)->setText(T("ошибка: %1").arg(serverError(r)));
            ++m_writeErrors;
            writeNext();
            return;
        }
        item.insert("Overview", m_table->item(row, ColBio)->data(Qt::UserRole).toString());
        const QDate born = QDate::fromString(m_table->item(row, ColBorn)->text().trimmed(), Qt::ISODate);
        if (born.isValid()) {
            item.insert("PremiereDate", born.toString(Qt::ISODate) + QStringLiteral("T00:00:00.0000000Z"));
        }
        // Дата смерти: только если указана. Иначе убираем — Jellyfin (TMDb) часто ставит даты однофамильца
        const QDate died = QDate::fromString(m_table->item(row, ColBorn)->data(Qt::UserRole).toString(), Qt::ISODate);
        if (died.isValid())
            item.insert("EndDate", died.toString(Qt::ISODate) + QStringLiteral("T00:00:00.0000000Z"));
        else
            item.remove("EndDate");
        // «Оригинальное имя» — настоящее имя; неизвестно — «Не раскрывается» (если на сервере пусто)
        const QString realName = m_table->item(row, ColRuName)->data(Qt::UserRole).toString().trimmed();
        if (!realName.isEmpty())
            item.insert("OriginalTitle", realName);
        else if (item.value("OriginalTitle").toString().trimmed().isEmpty())
            item.insert("OriginalTitle", QStringLiteral("Не Раскрывается"));
        // Закрепить карточку, чтобы обновление метаданных её не переписало
        item.insert("LockData", true);
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
                if (retryRow(row, u))
                    return;
                m_table->item(row, ColStatus)->setText(T("ошибка записи: %1").arg(serverError(u)));
                ++m_writeErrors;
                writeNext();
                return;
            }
            n->setData(Qt::UserRole + 5, 0);
            m_table->item(row, ColStatus)->setText(T("записано"));
            m_table->item(row, ColCheck)->setCheckState(Qt::Unchecked);
            n->setData(Qt::UserRole + 1, true);
            // Фото: только если на сервере его нет
            QString photo = n->data(Qt::UserRole + 4).toString();   // выбранное в окне анкеты — всегда
            if (!QFileInfo::exists(photo))
                photo.clear();
            if (photo.isEmpty() && m_photos->isChecked() && !n->data(Qt::UserRole + 2).toBool()) {
                const QString name = n->text();
                const QString rel = QDir::fromNativeSeparators(m_release->text().trimmed());
                const QString inRelease = QDir(rel).filePath(QStringLiteral(".actors/") + Release::actorFile(name) + QStringLiteral(".jpg"));
                for (const QString &f : {ActressPhotos::cacheFile(name), inRelease,
                                         ActressPhotos::findInLibrary(name, QDir::fromNativeSeparators(QSettings().value(QStringLiteral("ae/baseDir")).toString()))})
                    if (photo.isEmpty() && !f.isEmpty() && QFileInfo(f).size() > 20000)
                        photo = f;
                if (photo.isEmpty()) {   // нигде нет — поиск в интернете (1000×1500)
                    searchPhoto(row);
                    return;
                }
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
                    n->setData(Qt::UserRole + 4, QString());
                } else {
                    m_table->item(row, ColStatus)->setText(T("записано, фото не загрузилось: %1").arg(p->errorString()));
                }
                writeNext();
            });
        });
    });
}
