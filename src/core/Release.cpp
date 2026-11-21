#include "Release.h"

#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QUrl>
#include "Tools.h"

namespace Release {

static QString esc(QString s)
{
    s.replace('&', QStringLiteral("&amp;"));
    s.replace('<', QStringLiteral("&lt;"));
    s.replace('>', QStringLiteral("&gt;"));
    s.replace('"', QStringLiteral("&quot;"));
    return s;
}

static QString slug(const QString &s)
{
    QString t = s.toLower();
    t.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    t.remove(QRegularExpression(QStringLiteral("^-|-$")));
    return t;
}

QString actorFile(const QString &name)
{
    // Как в Adult Soft и Kodi: «Phoenix Marie» → «Phoenix_Marie» (запрещённые в именах файлов символы → «_»)
    QString f = name.simplified();
    f.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1F]")), QStringLiteral("_"));
    f = f.trimmed();
    while (f.endsWith('.') || f.endsWith(' '))
        f.chop(1);
    if (f.isEmpty())
        f = QStringLiteral("_");
    return f.replace(' ', '_');
}

QString safeName(const QString &s)
{
    QString t = s;
    t.remove(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")));
    t = t.simplified();
    t.remove(QRegularExpression(QStringLiteral("[. ]+$")));
    return t;
}

QString baseName(const ReleaseInfo &r)
{
    const QString t = safeName(r.title);
    return r.year.trimmed().isEmpty() ? t : QStringLiteral("%1 (%2)").arg(t, r.year.trimmed());
}

QString nfo(const ReleaseInfo &r)
{
    const QString rate = QString::number(r.rating, 'f', 1);
    const QString now = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    QString x = QStringLiteral("<?xml version='1.0' encoding='UTF8'?>\n<movie>\n");
    x += QStringLiteral("    <title>%1</title>\n").arg(esc(r.title));
    if (!r.edition.isEmpty())
        x += QStringLiteral("    <videoassettitle>%1</videoassettitle>\n").arg(esc(r.edition));
    x += QStringLiteral("    <mpaa>Adult Рейтинг: %1</mpaa>\n").arg(rate);
    x += QStringLiteral("    <originaltitle>%1</originaltitle>\n").arg(esc(r.originalTitle));
    x += QStringLiteral("    <sorttitle>%1</sorttitle>\n").arg(esc(r.title));
    x += QStringLiteral("    <plot>%1</plot>\n").arg(esc(r.plot));
    x += QStringLiteral("    <tagline>%1</tagline>\n").arg(esc(r.originalTitle));
    x += QStringLiteral("    <premiered>%1</premiered>\n").arg(r.premiered.isValid() ? r.premiered.toString(Qt::ISODate) : QString());
    x += QStringLiteral("    <year>%1</year>\n").arg(esc(r.year));
    x += QStringLiteral("    <country>%1</country>\n").arg(esc(r.country));
    x += QStringLiteral("    <studio>%1</studio>\n").arg(esc(r.studio));
    x += QStringLiteral("    <director>%1</director>\n").arg(esc(r.director));
    x += QStringLiteral("    <isuserfavorite>false</isuserfavorite>\n");
    for (const QString &g : r.genres)
        x += QStringLiteral("    <genre>%1</genre>\n").arg(esc(g));
    if (!r.setName.isEmpty() || !r.setOverview.isEmpty())
        x += QStringLiteral("    <set>\n        <name>%1</name>\n        <overview>%2</overview>\n    </set>\n")
                 .arg(esc(r.setName), esc(r.setOverview));
    x += QStringLiteral("    <ratings>\n        <rating name=\"%1\" max=\"5\" default=\"true\">\n            <value>%2</value>\n"
                        "            <votes>%3</votes>\n        </rating>\n    </ratings>\n")
             .arg(esc(r.ratingSource), rate).arg(r.votes);
    x += QStringLiteral("    <rating>%1</rating>\n    <votes>%2</votes>\n").arg(rate).arg(r.votes);
    const QString uid = !r.sku.isEmpty() ? r.sku : slug(!r.originalTitle.isEmpty() ? r.originalTitle : r.title);
    x += QStringLiteral("    <uniqueid type=\"custom\" default=\"true\">\n        %1\n    </uniqueid>\n").arg(esc(uid));
    x += QStringLiteral("    <dateadded>%1</dateadded>\n    <playcount>1</playcount>\n    <watched>true</watched>\n"
                        "    <lastplayed>%1</lastplayed>\n").arg(now);
    x += QStringLiteral("    <runtime>%1</runtime>\n").arg(r.runtimeMin > 0 ? QString::number(r.runtimeMin) : QString());
    for (const QString &a : r.actresses)
        x += QStringLiteral("    <actor>\n        <name>%1</name>\n        <thumb>.actors/%2.jpg</thumb>\n    </actor>\n")
                 .arg(esc(a), actorFile(a));
    x += QStringLiteral("</movie>\n");
    x.replace('\n', QStringLiteral("\r\n"));
    return x;
}

QString titleCaseRu(const QString &s)
{
    static const QStringList small = {"в", "во", "и", "с", "со", "на", "о", "об", "обо", "у", "к", "ко", "по", "из", "за",
                                      "для", "от", "до", "а", "но", "или", "не", "ни", "же", "ли", "при", "без", "под", "над"};
    const QStringList words = s.split(' ');
    QStringList out;
    for (int i = 0; i < words.size(); ++i) {
        QString w = words[i];
        if (w.isEmpty()) {
            out << w;
            continue;
        }
        if (i > 0 && small.contains(w.toLower())) {
            out << w.toLower();
            continue;
        }
        // первая буква слова (после кавычек и скобок) — заглавная, остальное как есть (МИЛФА остаётся МИЛФА)
        for (int k = 0; k < w.size(); ++k) {
            if (w[k].isLetter()) {
                w[k] = w[k].toUpper();
                break;
            }
        }
        out << w;
    }
    return out.join(' ');
}

static QString normEn(QString t)
{
    t = t.toLower();
    t.replace(QChar(0x2019), '\'').replace(QChar(0x2018), '\'').replace('`', '\'');
    t.replace(QRegularExpression(QStringLiteral("\\s*[-\\x{2013}\\x{2014}]\\s*")), QStringLiteral("-"));
    return t.simplified();
}

QString genreRu(const QString &english)
{
    static const QJsonObject map = [] {
        QFile f(QStringLiteral(":/ae_genres.json"));
        f.open(QIODevice::ReadOnly);
        return QJsonDocument::fromJson(f.readAll()).object();
    }();
    return map.value(normEn(english)).toString();
}

QString seriesSuggestion(const QString &title)
{
    static const QRegularExpression re(QStringLiteral("^(.*\\S)\\s+(\\d{1,3}|[IVX]{1,5})$"));
    const auto m = re.match(title.trimmed());
    return m.hasMatch() ? m.captured(1) : QString();
}

int parseLengthMinutes(const QString &text)
{
    static const QRegularExpression h(QStringLiteral("(\\d+)\\s*hrs?"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression m(QStringLiteral("(\\d+)\\s*min"), QRegularExpression::CaseInsensitiveOption);
    const int hh = h.match(text).captured(1).toInt();
    const int mm = m.match(text).captured(1).toInt();
    return hh * 60 + mm;
}

QDate parseDate(const QString &text)
{
    // «Nov 17 2026», «November 9, 1992», «Saturday 20th of August 1988», «1988-08-20»
    QString t = text.simplified().remove(',');
    static const QRegularExpression iso(QStringLiteral("(\\d{4}-\\d{2}-\\d{2})"));
    const auto im = iso.match(t);
    if (im.hasMatch())
        return QDate::fromString(im.captured(1), Qt::ISODate);
    t.remove(QRegularExpression(QStringLiteral("\\b(Monday|Tuesday|Wednesday|Thursday|Friday|Saturday|Sunday)\\b"), QRegularExpression::CaseInsensitiveOption));
    t.replace(QRegularExpression(QStringLiteral("(\\d)(st|nd|rd|th)\\b")), QStringLiteral("\\1"));
    t.remove(QRegularExpression(QStringLiteral("\\bof\\b")));
    t.remove(QRegularExpression(QStringLiteral("\\s*\\(.*$")));
    t = t.simplified();
    const QLocale c(QLocale::English);
    for (const QString &f : {QStringLiteral("MMM d yyyy"), QStringLiteral("MMMM d yyyy"), QStringLiteral("d MMMM yyyy"),
                             QStringLiteral("d MMM yyyy"), QStringLiteral("MM/dd/yyyy"), QStringLiteral("yyyy-MM-dd")}) {
        const QDate d = c.toDate(t, f);
        if (d.isValid())
            return d;
    }
    return {};
}

bool fanartFromVideo(const QString &video, const QString &jpg)
{
    if (Tools::ffmpeg().isEmpty())
        return false;
    for (const char *at : {"600", "5"}) {
        QProcess p;
        p.start(Tools::ffmpeg(), {"-hide_banner", "-loglevel", "error", "-y", "-ss", at, "-i", video, "-frames:v", "1", "-q:v", "2", jpg});
        p.waitForFinished(30000);
        if (QFileInfo(jpg).size() > 0)
            return true;
    }
    return false;
}

static QString studioKey(const QString &studio)
{
    QString k = studio.toLower();
    k.remove(QRegularExpression(QStringLiteral("[^a-z0-9а-яё]+")));
    return k;
}

QString studioCountry(const QString &studio)
{
    const QString key = studioKey(studio);
    if (key.isEmpty())
        return {};
    const QString mine = QSettings().value(QStringLiteral("studioCountry/") + key).toString();
    if (!mine.isEmpty())
        return mine;
    // Крупные студии и страна, где они созданы
    static const QList<QPair<QString, QString>> known = {
        {"marcdorcel", "Франция"}, {"dorcel", "Франция"}, {"jacquieetmichel", "Франция"}, {"colmax", "Франция"},
        {"private", "Испания"}, {"cumlouder", "Испания"}, {"erikalust", "Испания"}, {"lustcinema", "Испания"},
        {"roccosiffredi", "Италия"}, {"mariosalieri", "Италия"}, {"salieri", "Италия"},
        {"harmonyfilms", "Великобритания"}, {"bluebirdfilms", "Великобритания"}, {"killergram", "Великобритания"},
        {"vivthomas", "Великобритания"}, {"joybear", "Великобритания"},
        {"magmafilm", "Германия"}, {"videorama", "Германия"}, {"mmv", "Германия"}, {"puaka", "Германия"}, {"inflagranti", "Германия"},
        {"goldlight", "Германия"}, {"muschimovie", "Германия"},
        {"czechcasting", "Чехия"}, {"legalporno", "Чехия"}, {"gonzocom", "Чехия"}, {"czechav", "Чехия"},
        {"brazzers", "Канада"}, {"twistys", "Канада"}, {"bellesa", "Канада"},
        {"brasileirinhas", "Бразилия"},
        {"s1no1style", "Япония"}, {"moodyz", "Япония"}, {"ideapocket", "Япония"}, {"prestige", "Япония"}, {"madonna", "Япония"},
        {"vixen", "США"}, {"blacked", "США"}, {"blackedraw", "США"}, {"tushy", "США"}, {"tushyraw", "США"}, {"deeper", "США"},
        {"slayed", "США"}, {"milfy", "США"}, {"wifey", "США"}, {"elegantangel", "США"}, {"julesjordan", "США"}, {"evilangel", "США"},
        {"realitykings", "США"}, {"naughtyamerica", "США"}, {"bangbros", "США"}, {"digitalplayground", "США"}, {"wicked", "США"},
        {"wickedpictures", "США"}, {"vivid", "США"}, {"newsensations", "США"}, {"girlfriendsfilms", "США"}, {"sweetheartvideo", "США"},
        {"milehigh", "США"}, {"adamandeve", "США"}, {"adameve", "США"}, {"hustler", "США"}, {"penthouse", "США"}, {"zerotolerance", "США"},
        {"3rddegree", "США"}, {"devilsfilm", "США"}, {"diabolic", "США"}, {"redlightdistrict", "США"}, {"kink", "США"}, {"puretaboo", "США"},
        {"adulttime", "США"}, {"burningangel", "США"}, {"lethalhardcore", "США"}, {"hardx", "США"}, {"darkx", "США"}, {"archangel", "США"},
        {"nubiles", "США"}, {"teamskeet", "США"}, {"mylf", "США"}, {"digitalsin", "США"}, {"pornfidelity", "США"},
        {"westcoastproductions", "США"}, {"smashpictures", "США"}, {"xart", "США"}, {"metart", "США"}, {"bratty", "США"},
        {"forbiddenfruitsfilms", "США"}, {"fillyfilms", "США"}, {"sweetsinner", "США"}, {"mommysgirl", "США"}, {"girlsway", "США"},
        {"jayrock", "США"}, {"tabooheat", "США"}, {"familystrokes", "США"}, {"devilsfilmparodies", "США"}};
    for (const auto &p : known)
        if (key == p.first || (p.first.size() >= 8 && key.startsWith(p.first)))
            return p.second;
    return {};
}

void rememberStudioCountry(const QString &studio, const QString &country)
{
    const QString key = studioKey(studio);
    if (!key.isEmpty() && !country.trimmed().isEmpty())
        QSettings().setValue(QStringLiteral("studioCountry/") + key, country.trimmed());
}

QString countryFromAdjective(const QString &text)
{
    static const QList<QPair<QString, QString>> adj = {
        {"american", "США"}, {"canadian", "Канада"}, {"french", "Франция"}, {"czech", "Чехия"}, {"hungarian", "Венгрия"},
        {"spanish", "Испания"}, {"italian", "Италия"}, {"german", "Германия"}, {"british", "Великобритания"},
        {"english", "Великобритания"}, {"japanese", "Япония"}, {"brazilian", "Бразилия"}, {"dutch", "Нидерланды"},
        {"swedish", "Швеция"}, {"russian", "Россия"}, {"australian", "Австралия"}, {"danish", "Дания"}, {"polish", "Польша"}};
    const QString t = text.toLower();
    int best = -1;
    QString country;
    for (const auto &p : adj) {
        const int i = t.indexOf(p.first);
        if (i >= 0 && (best < 0 || i < best)) {
            best = i;
            country = p.second;
        }
    }
    return country;
}

static QString pendingKey(const QString &url)
{
    // Ссылка без параметров и «#…» — так совпадёт и страница сцен, и страница фильма
    QUrl u(url);
    u.setQuery(QString());
    u.setFragment(QString());
    return QString::fromLatin1(u.toString(QUrl::RemoveScheme | QUrl::StripTrailingSlash).toUtf8().toBase64(QByteArray::Base64UrlEncoding));
}

void addPending(const QString &pageUrl, const Pending &p)
{
    QSettings st;
    st.beginGroup(QStringLiteral("aePending/") + pendingKey(pageUrl));
    st.setValue(QStringLiteral("name"), p.name);
    st.setValue(QStringLiteral("dir"), p.dir);
    st.setValue(QStringLiteral("meta"), p.chaptersMeta);
    st.setValue(QStringLiteral("lossless"), p.lossless);
    st.endGroup();
}

bool takePending(const QString &pageUrl, Pending *p)
{
    QSettings st;
    const QString g = QStringLiteral("aePending/") + pendingKey(pageUrl);
    if (!st.contains(g + QStringLiteral("/name")))
        return false;
    p->name = st.value(g + QStringLiteral("/name")).toString();
    p->dir = st.value(g + QStringLiteral("/dir")).toString();
    p->chaptersMeta = st.value(g + QStringLiteral("/meta")).toString();
    p->lossless = st.value(g + QStringLiteral("/lossless"), true).toBool();
    st.remove(g);
    return true;
}

} // namespace Release
