#include "TrimDialog.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "FramePreview.h"
#include "Widgets.h"

#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTime>
#include <QTimeEdit>
#include <QVBoxLayout>
#include <QtMath>

static QTime toTime(double sec)
{
    return QTime(0, 0).addMSecs(int(qMax(0.0, sec) * 1000));
}

static double fromTime(const QTime &t)
{
    return QTime(0, 0).msecsTo(t) / 1000.0;
}

static QString fmtTime(double s, bool tenths = true)
{
    const QString base = formatDuration(qFloor(s));
    if (!tenths)
        return base;
    return base + QStringLiteral(".%1").arg(int((s - qFloor(s)) * 10));
}

// ======================= TimelineWidget =======================

TimelineWidget::TimelineWidget(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(118);
    setCursor(Qt::PointingHandCursor);
}

void TimelineWidget::setDuration(double d, double fps)
{
    m_duration = qMax(0.1, d);
    m_fps = fps > 0 ? fps : 25.0;
    m_end = m_duration;
    update();
}

void TimelineWidget::setThumbCount(int n)
{
    m_thumbs = QVector<QImage>(n);
    update();
}

void TimelineWidget::setThumb(int i, const QImage &img)
{
    if (i >= 0 && i < m_thumbs.size()) {
        m_thumbs[i] = img;
        update();
    }
}

void TimelineWidget::setPosition(double t)
{
    t = qBound(0.0, t, m_duration);
    if (qFuzzyCompare(t + 1, m_pos + 1))
        return;
    m_pos = t;
    update();
    emit positionChanged(m_pos);
}

void TimelineWidget::setStart(double t)
{
    m_start = qBound(0.0, t, qMax(0.0, m_end - 0.1));
    update();
    emit rangeChanged(m_start, m_end);
}

void TimelineWidget::setEnd(double t)
{
    m_end = qBound(m_start + 0.1, t, m_duration);
    update();
    emit rangeChanged(m_start, m_end);
}

void TimelineWidget::setActive(Marker m)
{
    if (m_active == m)
        return;
    m_active = m;
    update();
    emit activeChanged(int(m));
}

void TimelineWidget::step(double seconds)
{
    switch (m_active) {
    case Start:
        setStart(m_start + seconds);
        setPosition(m_start);
        break;
    case End:
        setEnd(m_end + seconds);
        setPosition(m_end);
        break;
    default:
        setPosition(m_pos + seconds);
    }
}

QRectF TimelineWidget::stripRect() const
{
    return QRectF(10, 30, width() - 20, height() - 42);
}

double TimelineWidget::timeToX(double t) const
{
    const QRectF r = stripRect();
    return r.left() + r.width() * (t / m_duration);
}

double TimelineWidget::xToTime(double x) const
{
    const QRectF r = stripRect();
    return qBound(0.0, (x - r.left()) / r.width() * m_duration, m_duration);
}

void TimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Theme::color("#171113"));
    const QRectF strip = stripRect();

    // Линейка с отметками времени
    QFont f = font();
    f.setPixelSize(10);
    p.setFont(f);
    const int ticks = qMax(2, int(strip.width() / 110));
    for (int i = 0; i <= ticks * 5; ++i) {
        const double t = m_duration * i / (ticks * 5.0);
        const double x = timeToX(t);
        const bool major = i % 5 == 0;
        p.setPen(Theme::color(major ? "#8C7E6E" : "#4A3E41"));
        p.drawLine(QPointF(x, strip.top() - (major ? 10 : 5)), QPointF(x, strip.top() - 2));
        if (major && i < ticks * 5)
            p.drawText(QPointF(x + 3, strip.top() - 12), fmtTime(t, false));
    }

    // Лента кадров
    p.save();
    p.setClipRect(strip);
    p.fillRect(strip, Theme::color("#0E0A0B"));
    const int n = m_thumbs.size();
    if (n > 0) {
        const double w = strip.width() / n;
        for (int i = 0; i < n; ++i) {
            const QRectF cell(strip.left() + i * w, strip.top(), w, strip.height());
            if (!m_thumbs[i].isNull()) {
                QImage img = m_thumbs[i].scaled(cell.size().toSize() * 2, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                const QRectF src((img.width() - cell.width() * 2) / 2, (img.height() - cell.height() * 2) / 2, cell.width() * 2, cell.height() * 2);
                p.drawImage(cell, img, src);
            }
            p.setPen(Theme::color("#2A2225"));
            p.drawLine(cell.topRight(), cell.bottomRight());
        }
    }
    // Затемнение вырезаемых частей
    const double xs = timeToX(m_start), xe = timeToX(m_end);
    p.fillRect(QRectF(strip.left(), strip.top(), xs - strip.left(), strip.height()), QColor(0, 0, 0, 170));
    p.fillRect(QRectF(xe, strip.top(), strip.right() - xe, strip.height()), QColor(0, 0, 0, 170));
    p.restore();

    // Рамка выбранного фрагмента
    p.setPen(QPen(Theme::color("#E2BC5C"), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(xs, strip.top() + 1, xe - xs, strip.height() - 2));

    auto drawHandle = [&](double x, bool active, bool left) {
        const QRectF h(left ? x - 9 : x, strip.top() - 2, 9, strip.height() + 4);
        QPainterPath path;
        path.addRoundedRect(h, 3, 3);
        p.fillPath(path, Theme::color(active ? "#F7DA8E" : "#C99A38"));
        p.setPen(QPen(Theme::color("#2A0912"), 1.4));
        const double cx = h.center().x();
        for (int k = -1; k <= 1; ++k)
            p.drawLine(QPointF(cx - 2, h.center().y() + k * 5), QPointF(cx + 2, h.center().y() + k * 5));
        if (active) {
            p.setPen(QPen(Theme::color("#FFF6DE"), 1.5));
            p.drawPath(path);
        }
    };
    drawHandle(xs, m_active == Start, true);
    drawHandle(xe, m_active == End, false);

    // Позиция
    const double xp = timeToX(m_pos);
    const QColor pc(m_active == Position ? "#FFF6DE" : "#E9E1D2");
    p.setPen(QPen(pc, 2));
    p.drawLine(QPointF(xp, strip.top() - 6), QPointF(xp, strip.bottom() + 4));
    QPainterPath tri;
    tri.moveTo(xp - 6, strip.top() - 12);
    tri.lineTo(xp + 6, strip.top() - 12);
    tri.lineTo(xp, strip.top() - 4);
    tri.closeSubpath();
    p.fillPath(tri, pc);

    if (hasFocus()) {
        p.setPen(QPen(Theme::color("#5A4630"), 1, Qt::DashLine));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
}

void TimelineWidget::mousePressEvent(QMouseEvent *e)
{
    setFocus();
    const double x = e->pos().x();
    if (qAbs(x - (timeToX(m_start) - 4)) <= 10)
        m_drag = Start;
    else if (qAbs(x - (timeToX(m_end) + 4)) <= 10)
        m_drag = End;
    else
        m_drag = Position;
    setActive(m_drag);
    m_dragging = true;
    mouseMoveEvent(e);
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging)
        return;
    const double t = xToTime(e->pos().x());
    if (m_drag == Start) { setStart(t); setPosition(m_start); }
    else if (m_drag == End) { setEnd(t); setPosition(m_end); }
    else setPosition(t);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = false;
}

void TimelineWidget::keyPressEvent(QKeyEvent *e)
{
    const int k = e->key();
    if (k == Qt::Key_Left || k == Qt::Key_Right) {
        double s;
        if (e->modifiers() & Qt::ShiftModifier) {
            s = 1.0 / m_fps;                      // один кадр
        } else {
            m_repeat = e->isAutoRepeat() ? m_repeat + 1 : 0;
            // Нажатие — 1 секунда; удержание — шаг растёт: 1 → 2 → 5 → 15 → 30 с
            s = m_repeat < 6 ? 1.0 : m_repeat < 16 ? 2.0 : m_repeat < 30 ? 5.0 : m_repeat < 50 ? 15.0 : 30.0;
        }
        step(k == Qt::Key_Left ? -s : s);
        e->accept();
        return;
    }
    if (k == Qt::Key_Home) { setActive(Position); setPosition(0); return; }
    if (k == Qt::Key_End) { setActive(Position); setPosition(m_duration); return; }
    if (k == Qt::Key_Tab) { setActive(Marker((int(m_active) + 1) % 3)); return; }
    QWidget::keyPressEvent(e);
}

void TimelineWidget::keyReleaseEvent(QKeyEvent *e)
{
    if (!e->isAutoRepeat())
        m_repeat = 0;
    QWidget::keyReleaseEvent(e);
}

// ======================= TrimDialog =======================

TrimDialog::TrimDialog(const TaskPtr &task, QWidget *parent)
    : FramedDialog(parent, true), m_task(task)
{
    setWindowTitle(T("Обрезка — ") + QFileInfo(task->input).fileName());
    resize(1000, 720);
    const double dur = task->info.duration;

    auto *root = contentLayout();
    root->setContentsMargins(18, 14, 18, 16);
    root->setSpacing(10);

    m_preview = new PreviewLabel(this);
    root->addWidget(m_preview, 1);

    // Строка позиции и выбора метки
    auto *bar = new QHBoxLayout;
    m_posLabel = new QLabel(this);
    m_posLabel->setObjectName(QStringLiteral("StatusLabel"));
    m_posLabel->setMinimumWidth(230);
    bar->addWidget(m_posLabel);
    bar->addStretch();
    auto *moveLbl = new QLabel(T("Стрелки двигают:"), this);
    moveLbl->setObjectName(QStringLiteral("Muted"));
    bar->addWidget(moveLbl);
    const char *names[3] = {"Позицию", "Начало", "Конец"};
    for (int i = 0; i < 3; ++i) {
        m_modeBtns[i] = new QPushButton(T(names[i]), this);
        m_modeBtns[i]->setObjectName(i == 0 ? QStringLiteral("SegLeft") : i == 2 ? QStringLiteral("SegRight") : QStringLiteral("SegLeft"));
        if (i == 1) m_modeBtns[i]->setStyleSheet(QStringLiteral("border-radius:0;"));
        m_modeBtns[i]->setCheckable(true);
        m_modeBtns[i]->setAutoExclusive(true);
        m_modeBtns[i]->setFocusPolicy(Qt::NoFocus);
        bar->addWidget(m_modeBtns[i]);
    }
    m_modeBtns[0]->setChecked(true);
    root->addLayout(bar);

    m_timeline = new TimelineWidget(this);
    m_timeline->setDuration(dur, task->info.fps);
    m_timeline->setThumbCount(m_thumbCount);
    root->addWidget(m_timeline);

    auto *hint = new QLabel(T("← → — шаг 1 с, удерживайте для быстрой перемотки · Shift + ← → — один кадр · "
                                           "Tab — следующая метка · щёлкните по метке, чтобы двигать её"),
                            this);
    hint->setObjectName(QStringLiteral("Muted"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    m_start = new QTimeEdit(this);
    m_end = new QTimeEdit(this);
    for (QTimeEdit *e : {m_start, m_end}) {
        e->setDisplayFormat(QStringLiteral("HH:mm:ss.zzz"));
        e->setTimeRange(QTime(0, 0), toTime(dur));
        e->setMinimumWidth(130);
    }
    auto *setStart = new QPushButton(T("Начало здесь"), this);
    auto *setEnd = new QPushButton(T("Конец здесь"), this);
    for (QPushButton *b : {setStart, setEnd})
        b->setFocusPolicy(Qt::NoFocus);
    grid->addWidget(new QLabel(T("Начало:")), 0, 0);
    grid->addWidget(m_start, 0, 1);
    grid->addWidget(setStart, 0, 2);
    grid->addWidget(new QLabel(T("Конец:")), 0, 4);
    grid->addWidget(m_end, 0, 5);
    grid->addWidget(setEnd, 0, 6);
    grid->setColumnMinimumWidth(3, 30);
    grid->setColumnStretch(7, 1);
    m_info = new QLabel(this);
    m_info->setObjectName(QStringLiteral("StatusLabel"));
    grid->addWidget(m_info, 0, 8, Qt::AlignRight | Qt::AlignVCenter);
    root->addLayout(grid);

    auto *btns = new QHBoxLayout;
    auto *reset = new QPushButton(T("Сбросить"), this);
    auto *ok = Ui::primaryButton(T("Сохранить"), this);
    auto *cancel = Ui::primaryButton(T("Отмена"), this);
    for (QPushButton *b : {reset, ok, cancel}) {
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumWidth(130);
    }
    btns->addWidget(reset);
    btns->addStretch();
    btns->addWidget(ok);
    btns->addWidget(cancel);
    root->addLayout(btns);

    // Начальные значения
    m_timeline->setStart(task->edit.trimStart);
    m_timeline->setEnd(task->edit.trimEnd > 0 ? task->edit.trimEnd : dur);
    m_timeline->setPosition(task->edit.trimStart);
    syncEditsFromTimeline();

    m_frameTimer.setSingleShot(true);
    m_frameTimer.setInterval(90);
    connect(&m_frameTimer, &QTimer::timeout, this, &TrimDialog::grabFrame);

    connect(m_timeline, &TimelineWidget::positionChanged, this, [this](double t) {
        m_posLabel->setText(T("Позиция:  %1  /  %2").arg(fmtTime(t), fmtTime(m_task->info.duration)));
        requestFrame();
    });
    connect(m_timeline, &TimelineWidget::rangeChanged, this, &TrimDialog::syncEditsFromTimeline);
    connect(m_timeline, &TimelineWidget::activeChanged, this, [this](int m) {
        m_modeBtns[qBound(0, m, 2)]->setChecked(true);
    });
    for (int i = 0; i < 3; ++i)
        connect(m_modeBtns[i], &QPushButton::clicked, this, [this, i] {
            m_timeline->setActive(TimelineWidget::Marker(i));
            if (i == 1) m_timeline->setPosition(m_timeline->start());
            if (i == 2) m_timeline->setPosition(m_timeline->end());
            m_timeline->setFocus();
        });
    connect(m_start, &QTimeEdit::timeChanged, this, [this](const QTime &t) {
        if (!m_syncing) { m_timeline->setStart(fromTime(t)); m_timeline->setPosition(m_timeline->start()); }
    });
    connect(m_end, &QTimeEdit::timeChanged, this, [this](const QTime &t) {
        if (!m_syncing) { m_timeline->setEnd(fromTime(t)); m_timeline->setPosition(m_timeline->end()); }
    });
    connect(setStart, &QPushButton::clicked, this, [this] { m_timeline->setStart(m_timeline->position()); m_timeline->setFocus(); });
    connect(setEnd, &QPushButton::clicked, this, [this] { m_timeline->setEnd(m_timeline->position()); m_timeline->setFocus(); });
    connect(reset, &QPushButton::clicked, this, [this, dur] {
        m_timeline->setStart(0);
        m_timeline->setEnd(dur);
        m_timeline->setActive(TimelineWidget::Position);
        m_timeline->setFocus();
    });
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    m_posLabel->setText(T("Позиция:  %1  /  %2").arg(fmtTime(m_timeline->position()), fmtTime(dur)));
    m_timeline->setFocus();
    grabFrame();
    loadThumbs(0);
}

void TrimDialog::syncEditsFromTimeline()
{
    m_syncing = true;
    m_start->setTime(toTime(m_timeline->start()));
    m_end->setTime(toTime(m_timeline->end()));
    m_syncing = false;
    updateInfo();
}

double TrimDialog::start() const
{
    return m_timeline->start() < 0.05 ? 0.0 : m_timeline->start();
}

double TrimDialog::end() const
{
    const double e = m_timeline->end();
    return e >= m_task->info.duration - 0.05 ? 0.0 : e;
}

void TrimDialog::updateInfo()
{
    m_info->setText(T("Длительность: %1").arg(fmtTime(m_timeline->end() - m_timeline->start())));
}

void TrimDialog::requestFrame()
{
    m_frameTimer.start();
}

// Кадр берётся FFmpeg-ом; пока идёт один запрос, новые не копятся — берётся только последний.
void TrimDialog::grabFrame()
{
    if (m_grabbing) {
        m_pending = true;
        return;
    }
    m_grabbing = true;
    m_pending = false;
    const double at = m_timeline->position();
    ConversionTask copy = *m_task;
    auto img = QSharedPointer<QImage>::create();
    Ui::runAsync(this, [copy, at, img] { *img = FramePreview::grab(copy, at, false, 900); }, [this, img] {
        m_grabbing = false;
        if (!img->isNull())
            m_preview->setImage(*img);
        if (m_pending)
            grabFrame();
    });
}

void TrimDialog::loadThumbs(int index)
{
    if (index >= m_thumbCount)
        return;
    const double at = m_task->info.duration * (index + 0.5) / m_thumbCount;
    ConversionTask copy = *m_task;
    auto img = QSharedPointer<QImage>::create();
    Ui::runAsync(this, [copy, at, img] { *img = FramePreview::grab(copy, at, false, 200); }, [this, img, index] {
        m_timeline->setThumb(index, *img);
        loadThumbs(index + 1);
    });
}
