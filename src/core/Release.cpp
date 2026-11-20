#include "Release.h"

#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>

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

static const QHash<QChar, QString> &translit()
{
    static const QHash<QChar, QString> tr = [] {
        QHash<QChar, QString> h;
        const QString ru = QStringLiteral("абвгдеёжзийклмнопрстуфхцчшщъыьэюя");
        const QStringList en = {"a", "b", "v", "g", "d", "e", "e", "zh", "z", "i", "y", "k", "l", "m", "n", "o", "p",
                                "r", "s", "t", "u", "f", "h", "ts", "ch", "sh", "sch", "", "y", "", "e", "yu", "ya"};
        for (int i = 0; i < ru.size(); ++i)
            h.insert(ru[i], en[i]);
        return h;
    }();
    return tr;
}

QString actorFile(const QString &name)
{
    QStringList words;
    for (const QString &w : name.simplified().split(' ', Qt::SkipEmptyParts)) {
        QString out;
        for (const QChar c : w) {
            const QChar lc = c.toLower();
            if (translit().contains(lc)) {
                const QString t = translit().value(lc);
                out += (c == lc || t.isEmpty()) ? t : t.left(1).toUpper() + t.mid(1);
            } else {
                out += c;
            }
        }
        if (!out.isEmpty())
            out[0] = out[0].toUpper();
        words << out;
    }
    QString f = words.join('_');
    f.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")));
    f.replace(QRegularExpression(QStringLiteral("_+")), QStringLiteral("_"));
    f.remove(QRegularExpression(QStringLiteral("^_|_$")));
    return f;
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
    const QString t = text.simplified().remove(',');
    const QLocale c(QLocale::English);
    for (const QString &f : {QStringLiteral("MMM d yyyy"), QStringLiteral("MMMM d yyyy"), QStringLiteral("MM/dd/yyyy"),
                             QStringLiteral("yyyy-MM-dd")}) {
        const QDate d = c.toDate(t, f);
        if (d.isValid())
            return d;
    }
    return {};
}

} // namespace Release
