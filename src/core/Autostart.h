#pragma once
#include <QString>

// Запуск программы вместе с Windows/Linux (ключ --autostart): нужен, пока есть незаконченный апскейл.
namespace Autostart {
void set(bool on);
bool isSet();
bool enabledByUser();            // «Продолжать апскейл после перезагрузки» (по умолчанию да)
void setEnabledByUser(bool on);
}
