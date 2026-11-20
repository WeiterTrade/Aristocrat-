#pragma once
#include <QString>
#include <QStringList>

// Перевод интерфейса. Исходные строки написаны по-русски; для других языков
// перевод берётся из resources/i18n/<код>.json (ключ — русская строка).
namespace I18n {

void init();                       // выбрать язык (из настроек или по системе) и загрузить перевод
QString language();                // ru, en, es, fr, it, kk
QStringList languages();           // все доступные коды
QString languageName(const QString &code);
QString tr(const QString &ru);

} // namespace I18n

inline QString T(const char *ru) { return I18n::tr(QString::fromUtf8(ru)); }
inline QString T(const QString &ru) { return I18n::tr(ru); }
