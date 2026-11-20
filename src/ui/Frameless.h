#pragma once
#include <QDialog>
#include <QObject>
#include <QWidget>

class QLabel;
class QToolButton;
class QVBoxLayout;

// Свои рамки окна: без системного заголовка Windows/Linux.
// Перемещение и изменение размера выполняет сама система (QWindow::startSystemMove/Resize),
// поэтому работают прилипание к краям экрана и несколько мониторов.

// Изменение размера за края: ставится на «рамку» окна (виджет с отступом по краям).
class FrameResizer : public QObject
{
    Q_OBJECT
public:
    FrameResizer(QWidget *window, QWidget *frame, int border = 5);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    Qt::Edges edgesAt(const QPoint &p) const;
    QWidget *m_window;
    QWidget *m_frame;
    int m_border;
};

// Перетаскивание окна за указанный виджет (заголовок, шапку), двойной щелчок — развернуть.
class DragArea : public QObject
{
    Q_OBJECT
public:
    DragArea(QWidget *window, QWidget *area, bool maximizable);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    QWidget *m_window;
    bool m_maximizable;
};

// Кнопки «свернуть / развернуть / закрыть».
class WindowButtons : public QWidget
{
    Q_OBJECT
public:
    WindowButtons(QWidget *window, bool minimize, bool maximize, QWidget *parent = nullptr);
    void updateState();

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    QWidget *m_window;
    QToolButton *m_max = nullptr;
};

// Диалог со своей рамкой и заголовком. Содержимое кладётся в contentLayout().
class FramedDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FramedDialog(QWidget *parent = nullptr, bool resizable = false);
    QVBoxLayout *contentLayout() const { return m_content; }

protected:
    void changeEvent(QEvent *e) override;

private:
    QWidget *m_frame;
    QLabel *m_title;
    QVBoxLayout *m_content;
    WindowButtons *m_buttons;
    bool m_resizable;
};

namespace Ui {
// Сообщения в нашем оформлении (вместо системных QMessageBox).
void message(QWidget *parent, const QString &title, const QString &html);
bool ask(QWidget *parent, const QString &title, const QString &html,
         const QString &yes = QStringLiteral("Да"), const QString &no = QStringLiteral("Нет"));
QString askText(QWidget *parent, const QString &title, const QString &label, const QString &value, bool *ok);
}
