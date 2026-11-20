#pragma once
#include <QColor>
#include <QString>
#include <QStringList>

// Темы оформления. Все цвета в программе записаны для тёмной темы «бордо и золото»;
// для остальных тем каждый цвет пересчитывается по правилам темы (оттенок акцента, фона, текста).
namespace Theme {

void init();                          // выбрать тему (настройки или ARISTOCRAT_THEME)
QString id();                         // dark, light, turquoise, ruby
QStringList ids();
QString name(const QString &id);
bool isLight();

QColor color(const QString &darkHex); // цвет тёмной темы → цвет текущей темы
QString map(const QString &text);     // заменить все #RRGGBB и url(:/icons/…) в стилях/SVG

} // namespace Theme
