#include "Frameless.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "Widgets.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

// ---------------- FrameResizer ----------------

FrameResizer::FrameResizer(QWidget *window, QWidget *frame, int border)
    : QObject(frame), m_window(window), m_frame(frame), m_border(border)
{
    frame->setMouseTracking(true);
    frame->installEventFilter(this);
    // Внутренние области — обычный курсор (иначе курсор «растянуть» остаётся над содержимым)
    for (QObject *c : frame->children())
        if (c->isWidgetType())
            static_cast<QWidget *>(c)->setCursor(Qt::ArrowCursor);
}

Qt::Edges FrameResizer::edgesAt(const QPoint &p) const
{
    Qt::Edges e;
    if (m_window->isMaximized() || m_window->isFullScreen())
        return e;
    const int b = m_border + 2;
    if (p.x() <= b) e |= Qt::LeftEdge;
    if (p.x() >= m_frame->width() - b) e |= Qt::RightEdge;
    if (p.y() <= b) e |= Qt::TopEdge;
    if (p.y() >= m_frame->height() - b) e |= Qt::BottomEdge;
    return e;
}

bool FrameResizer::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj != m_frame)
        return false;
    if (ev->type() == QEvent::ChildAdded) {
        QObject *c = static_cast<QChildEvent *>(ev)->child();
        if (c && c->isWidgetType())
            static_cast<QWidget *>(c)->setCursor(Qt::ArrowCursor);
        return false;
    }
    if (ev->type() == QEvent::MouseMove || ev->type() == QEvent::HoverMove) {
        const QPoint p = m_frame->mapFromGlobal(QCursor::pos());
        const Qt::Edges e = edgesAt(p);
        if (e == (Qt::LeftEdge | Qt::TopEdge) || e == (Qt::RightEdge | Qt::BottomEdge))
            m_frame->setCursor(Qt::SizeFDiagCursor);
        else if (e == (Qt::RightEdge | Qt::TopEdge) || e == (Qt::LeftEdge | Qt::BottomEdge))
            m_frame->setCursor(Qt::SizeBDiagCursor);
        else if (e & (Qt::LeftEdge | Qt::RightEdge))
            m_frame->setCursor(Qt::SizeHorCursor);
        else if (e & (Qt::TopEdge | Qt::BottomEdge))
            m_frame->setCursor(Qt::SizeVerCursor);
        else
            m_frame->unsetCursor();
    } else if (ev->type() == QEvent::Leave) {
        m_frame->unsetCursor();
    } else if (ev->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(ev);
        const Qt::Edges e = edgesAt(me->pos());
        if (me->button() == Qt::LeftButton && e && m_window->windowHandle()) {
            m_window->windowHandle()->startSystemResize(e);
            return true;
        }
    }
    return false;
}

// ---------------- DragArea ----------------

DragArea::DragArea(QWidget *window, QWidget *area, bool maximizable)
    : QObject(area), m_window(window), m_maximizable(maximizable)
{
    area->installEventFilter(this);
}

bool DragArea::eventFilter(QObject *, QEvent *ev)
{
    if (ev->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(ev);
        if (me->button() == Qt::LeftButton && m_window->windowHandle()) {
            m_window->windowHandle()->startSystemMove();
            return true;
        }
    } else if (ev->type() == QEvent::MouseButtonDblClick && m_maximizable) {
        auto *me = static_cast<QMouseEvent *>(ev);
        if (me->button() == Qt::LeftButton) {
            m_window->isMaximized() ? m_window->showNormal() : m_window->showMaximized();
            return true;
        }
    }
    return false;
}

// ---------------- WindowButtons ----------------

static QToolButton *winButton(const QString &icon, const QString &tip, QWidget *parent, bool close = false)
{
    auto *b = new QToolButton(parent);
    b->setIcon(Ui::icon(icon));
    b->setIconSize(QSize(16, 16));
    b->setFixedSize(46, 32);
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus);
    b->setStyleSheet(Theme::map(close ? QStringLiteral("QToolButton{background:transparent;border:none;} QToolButton:hover{background:#B3263A;}")
                                      : QStringLiteral("QToolButton{background:transparent;border:none;} QToolButton:hover{background:#3A2B30;}")));
    return b;
}

WindowButtons::WindowButtons(QWidget *window, bool minimize, bool maximize, QWidget *parent)
    : QWidget(parent), m_window(window)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet(QStringLiteral("background:transparent;"));
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    if (minimize) {
        auto *b = winButton("win-min", T("Свернуть"), this);
        connect(b, &QToolButton::clicked, window, &QWidget::showMinimized);
        lay->addWidget(b);
    }
    if (maximize) {
        m_max = winButton("win-max", T("Развернуть"), this);
        connect(m_max, &QToolButton::clicked, this, [this] {
            m_window->isMaximized() ? m_window->showNormal() : m_window->showMaximized();
        });
        lay->addWidget(m_max);
    }
    auto *c = winButton("win-close", T("Закрыть"), this, true);
    connect(c, &QToolButton::clicked, window, &QWidget::close);
    lay->addWidget(c);
    window->installEventFilter(this);
}

bool WindowButtons::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == m_window && ev->type() == QEvent::WindowStateChange)
        updateState();
    return false;
}

void WindowButtons::updateState()
{
    if (!m_max)
        return;
    const bool maxed = m_window->isMaximized();
    m_max->setIcon(Ui::icon(maxed ? "win-restore" : "win-max"));
    m_max->setToolTip(maxed ? T("Восстановить") : T("Развернуть"));
}

// ---------------- FramedDialog ----------------

FramedDialog::FramedDialog(QWidget *parent, bool resizable)
    : QDialog(parent), m_resizable(resizable)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, false);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_frame = new QWidget(this);
    m_frame->setObjectName(QStringLiteral("WindowFrame"));
    m_frame->setAttribute(Qt::WA_StyledBackground);
    outer->addWidget(m_frame);

    auto *fl = new QVBoxLayout(m_frame);
    const int b = resizable ? 4 : 1;
    fl->setContentsMargins(b, b, b, b);
    fl->setSpacing(0);

    auto *title = new QWidget(m_frame);
    title->setObjectName(QStringLiteral("TitleBar"));
    title->setAttribute(Qt::WA_StyledBackground);
    title->setFixedHeight(36);
    auto *tl = new QHBoxLayout(title);
    tl->setContentsMargins(14, 0, 0, 0);
    tl->setSpacing(8);
    m_title = new QLabel(windowTitle(), title);
    m_title->setObjectName(QStringLiteral("TitleText"));
    m_buttons = new WindowButtons(this, false, resizable, title);
    tl->addWidget(m_title, 1);
    tl->addWidget(m_buttons, 0, Qt::AlignTop);
    fl->addWidget(title);

    auto *body = new QWidget(m_frame);
    body->setObjectName(QStringLiteral("DialogBody"));
    body->setAttribute(Qt::WA_StyledBackground);
    m_content = new QVBoxLayout(body);
    fl->addWidget(body, 1);

    new DragArea(this, title, resizable);
    if (resizable)
        new FrameResizer(this, m_frame, b);
}

void FramedDialog::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::WindowTitleChange && m_title)
        m_title->setText(windowTitle());
    QDialog::changeEvent(e);
}

// ---------------- Сообщения ----------------

namespace Ui {

static FramedDialog *makeBox(QWidget *parent, const QString &title, const QString &html)
{
    auto *d = new FramedDialog(parent);
    d->setWindowTitle(title);
    d->setMinimumWidth(420);
    auto *lay = d->contentLayout();
    lay->setContentsMargins(24, 18, 24, 18);
    lay->setSpacing(16);
    auto *row = new QHBoxLayout;
    auto *logo = new QLabel(d);
    logo->setPixmap(QIcon(QStringLiteral(":/logo/logo-small.svg")).pixmap(40, 40));
    logo->setAlignment(Qt::AlignTop);
    auto *text = new QLabel(html, d);
    text->setWordWrap(true);
    text->setTextFormat(Qt::RichText);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    text->setOpenExternalLinks(true);
    text->setMaximumWidth(560);
    row->addWidget(logo, 0, Qt::AlignTop);
    row->addSpacing(8);
    row->addWidget(text, 1);
    lay->addLayout(row);
    return d;
}

void message(QWidget *parent, const QString &title, const QString &html)
{
    FramedDialog *d = makeBox(parent, title, html);
    auto *btns = new QHBoxLayout;
    btns->addStretch();
    auto *ok = primaryButton(QStringLiteral("OK"), d);
    ok->setMinimumWidth(120);
    btns->addWidget(ok);
    d->contentLayout()->addLayout(btns);
    QObject::connect(ok, &QPushButton::clicked, d, &QDialog::accept);
    d->exec();
    d->deleteLater();
}

bool ask(QWidget *parent, const QString &title, const QString &html, const QString &yes, const QString &no)
{
    FramedDialog *d = makeBox(parent, title, html);
    auto *btns = new QHBoxLayout;
    btns->addStretch();
    auto *n = new QPushButton(no, d);
    auto *y = primaryButton(yes, d);
    n->setMinimumWidth(120);
    y->setMinimumWidth(120);
    btns->addWidget(n);
    btns->addWidget(y);
    d->contentLayout()->addLayout(btns);
    QObject::connect(y, &QPushButton::clicked, d, &QDialog::accept);
    QObject::connect(n, &QPushButton::clicked, d, &QDialog::reject);
    const bool r = d->exec() == QDialog::Accepted;
    d->deleteLater();
    return r;
}

QString askText(QWidget *parent, const QString &title, const QString &label, const QString &value, bool *ok)
{
    auto *d = new FramedDialog(parent);
    d->setWindowTitle(title);
    d->setMinimumWidth(460);
    auto *lay = d->contentLayout();
    lay->setContentsMargins(24, 18, 24, 18);
    lay->setSpacing(12);
    lay->addWidget(new QLabel(label, d));
    auto *edit = new QLineEdit(value, d);
    edit->selectAll();
    lay->addWidget(edit);
    auto *btns = new QHBoxLayout;
    btns->addStretch();
    auto *n = new QPushButton(T("Отмена"), d);
    auto *y = primaryButton(QStringLiteral("OK"), d);
    y->setDefault(true);
    btns->addWidget(n);
    btns->addWidget(y);
    lay->addLayout(btns);
    QObject::connect(y, &QPushButton::clicked, d, &QDialog::accept);
    QObject::connect(n, &QPushButton::clicked, d, &QDialog::reject);
    const bool accepted = d->exec() == QDialog::Accepted;
    if (ok)
        *ok = accepted;
    const QString r = edit->text();
    d->deleteLater();
    return r;
}

} // namespace Ui
