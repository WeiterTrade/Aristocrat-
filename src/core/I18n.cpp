#include "I18n.h"

#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSettings>

namespace I18n {

static QString g_lang = QStringLiteral("ru");
static QHash<QString, QString> g_map;

QStringList languages()
{
    return {"ru", "en", "es", "fr", "it", "kk"};
}

QString languageName(const QString &code)
{
    static const QHash<QString, QString> names = {
        {"ru", QStringLiteral("Русский")}, {"en", QStringLiteral("English")}, {"es", QStringLiteral("Español")},
        {"fr", QStringLiteral("Français")}, {"it", QStringLiteral("Italiano")}, {"kk", QStringLiteral("Қазақша")}};
    return names.value(code, code);
}

void init()
{
    QString lang = qEnvironmentVariable("ARISTOCRAT_LANG");   // для проверки сборки
    if (lang.isEmpty())
        lang = QSettings().value("general/language").toString();
    if (lang.isEmpty()) {
        // По умолчанию — язык системы; если он не поддерживается — английский
        lang = QLocale::system().name().left(2).toLower();
        if (!languages().contains(lang))
            lang = QStringLiteral("en");
    }
    g_lang = languages().contains(lang) ? lang : QStringLiteral("en");
    g_map.clear();
    if (g_lang == QLatin1String("ru"))
        return;
    QFile f(QStringLiteral(":/i18n/%1.json").arg(g_lang));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it)
        g_map.insert(it.key(), it.value().toString());
    QLocale::setDefault(QLocale(g_lang));
}

QString language() { return g_lang; }

QString tr(const QString &ru)
{
    if (g_map.isEmpty())
        return ru;
    const QString t = g_map.value(ru);
    return t.isEmpty() ? ru : t;
}

} // namespace I18n
