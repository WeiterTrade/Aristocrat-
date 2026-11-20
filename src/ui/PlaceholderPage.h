#pragma once
#include <QWidget>

// Заглушка для разделов, которые будут добавлены позже (Загрузка, Запись, Передача).
class PlaceholderPage : public QWidget
{
    Q_OBJECT
public:
    PlaceholderPage(const QString &icon, const QString &title, const QString &text, QWidget *parent = nullptr);
};
