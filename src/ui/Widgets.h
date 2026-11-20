#pragma once
#include <QIcon>
#include <QLabel>
#include <QString>
#include <QToolButton>
#include <functional>

class QWidget;
class QPushButton;
class QHBoxLayout;

namespace Ui {

QIcon icon(const QString &name, bool active = false);
QIcon navIcon(const QString &name);  // обычная + золотая (для выбранной вкладки)
QToolButton *iconButton(const QString &name, const QString &tooltip, QWidget *parent = nullptr, int size = 18);
QLabel *iconLabel(const QString &name, int size = 14, QWidget *parent = nullptr);
QPushButton *primaryButton(const QString &text, QWidget *parent = nullptr);

// Значок формата — «плашка» с подписью (MP4, 1080P…), рисуется кодом.
QPixmap formatBadge(const QString &text, int size = 36, bool highlighted = false);

// Запуск функции в фоновом потоке и возврат результата в поток интерфейса.
void runAsync(QObject *context, std::function<void()> work, std::function<void()> done);

} // namespace Ui
