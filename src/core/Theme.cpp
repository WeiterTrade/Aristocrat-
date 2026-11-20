#include "Theme.h"
#include "I18n.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QtMath>

namespace Theme {

static QString g_id = QStringLiteral("dark");
static QHash<QString, QString> g_cache;

QStringList ids()
{
    return {"dark", "light", "turquoise", "ruby"};
}

QString name(const QString &id)
{
    if (id == QLatin1String("light")) return T("Светлая");
    if (id == QLatin1String("turquoise")) return T("Бирюзовая");
    if (id == QLatin1String("ruby")) return T("Рубин");
    return T("Тёмная (бордо и золото)");
}

void init()
{
    QString t = qEnvironmentVariable("ARISTOCRAT_THEME");
    if (t.isEmpty())
        t = QSettings().value("general/theme", "dark").toString();
    g_id = ids().contains(t) ? t : QStringLiteral("dark");
    g_cache.clear();
}

QString id() { return g_id; }
bool isLight() { return g_id == QLatin1String("light"); }

static QColor hsl(double h, double s, double l)
{
    return QColor::fromHslF(qBound(0.0, h, 359.9) / 360.0, qBound(0.0, s, 1.0), qBound(0.0, l, 1.0));
}

static QColor transform(const QColor &c)
{
    if (g_id == QLatin1String("dark"))
        return c;
    const QString hex = c.name().toUpper();
    // Особые цвета
    if (hex == QLatin1String("#2A0912"))                       // текст на кнопке-акценте
        return g_id == QLatin1String("turquoise") ? QColor("#062A30") : QColor("#FFFFFF");
    if (hex == QLatin1String("#E07A6A") || hex == QLatin1String("#8FC48A") || hex == QLatin1String("#B3263A"))
        return c;

    const double h = qMax(0.0, c.hslHueF()) * 360.0, s = c.hslSaturationF(), l = c.lightnessF();
    const bool accent = h >= 20 && h <= 58 && s >= 0.25 && l >= 0.2;

    if (g_id == QLatin1String("light")) {
        if (accent) {
            if (l < 0.35) return hsl(40, 0.45, 0.84);                    // выделение — светлое золото
            return hsl(38, qMin(0.75, s), 0.30 + (l - 0.30) * 0.45);      // золото потемнее для светлого фона
        }
        if (l < 0.42) return hsl(35, 0.18, 1.0 - l * 0.85);               // фон и рамки → светлые
        return hsl(340, 0.10, qMax(0.10, 1.0 - l));                        // текст → тёмный
    }
    if (g_id == QLatin1String("turquoise")) {
        if (accent) return hsl(186, qMax(0.6, s), l < 0.35 ? l + 0.05 : qMin(0.62, l));
        if (l < 0.45) return hsl(205, 0.09, qMin(0.42, l + 0.07));        // серо-синий фон, светлее
        return hsl(200, 0.12, l);                                          // текст
    }
    // Рубин
    if (accent) return hsl(348, qMax(0.70, s), l < 0.35 ? l : qMin(0.60, l - 0.05));
    if (l < 0.45) return hsl(345, qMin(0.30, s + 0.08), l);
    return hsl(350, 0.18, l);
}

QColor color(const QString &darkHex)
{
    const QString key = darkHex.toUpper();
    auto it = g_cache.constFind(key);
    if (it != g_cache.constEnd())
        return QColor(it.value());
    const QString out = transform(QColor(darkHex)).name().toUpper();
    g_cache.insert(key, out);
    return QColor(out);
}

// Значки-«картинки» стилей (галочки, стрелки) перекрашиваются и сохраняются во временную папку
static QString themedIconFile(const QString &resource)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/theme-") + g_id;
    QDir().mkpath(dir);
    const QString out = dir + QLatin1Char('/') + resource.section('/', -1);
    QFile in(resource);
    if (!in.open(QIODevice::ReadOnly))
        return resource;
    const QString svg = map(QString::fromUtf8(in.readAll()));
    QFile f(out);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(svg.toUtf8());
    return out;
}

QString map(const QString &text)
{
    QString s = text;
    if (g_id != QLatin1String("dark")) {
        static const QRegularExpression hex(QStringLiteral("#[0-9A-Fa-f]{6}\\b"));
        QString out;
        int last = 0;
        auto it = hex.globalMatch(s);
        while (it.hasNext()) {
            const auto m = it.next();
            out += s.midRef(last, m.capturedStart() - last);
            out += color(m.captured()).name().toUpper();
            last = m.capturedEnd();
        }
        out += s.midRef(last);
        s = out;
    }
    // url(:/icons/x.svg) → перекрашенная копия (для галочек и стрелок в стилях)
    static const QRegularExpression url(QStringLiteral("url\\((:/icons/[^)]+\\.svg)\\)"));
    QString out;
    int last = 0;
    auto it = url.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        out += s.midRef(last, m.capturedStart() - last);
        out += QStringLiteral("url(") + QDir::fromNativeSeparators(themedIconFile(m.captured(1))) + QStringLiteral(")");
        last = m.capturedEnd();
    }
    out += s.midRef(last);
    return out;
}

} // namespace Theme
