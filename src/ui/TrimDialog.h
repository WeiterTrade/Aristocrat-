#pragma once
#include "Frameless.h"
#include "core/Task.h"

#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
#include <QVector>

class PreviewLabel;
class QTimeEdit;
class QLabel;
class QPushButton;

// Шкала времени: лента кадров, метки начала и конца, текущая позиция.
// Стрелки ← → двигают выбранную метку на 1 секунду; при удержании шаг растёт (быстрая перемотка).
// Shift + стрелка — один кадр. Щелчок по метке выбирает её, по ленте — переносит позицию.
class TimelineWidget : public QWidget
{
    Q_OBJECT
public:
    enum Marker { Position, Start, End };

    explicit TimelineWidget(QWidget *parent = nullptr);
    void setDuration(double d, double fps);
    void setThumbCount(int n);
    void setThumb(int i, const QImage &img);

    double position() const { return m_pos; }
    double start() const { return m_start; }
    double end() const { return m_end; }
    Marker active() const { return m_active; }

    void setPosition(double t);
    void setStart(double t);
    void setEnd(double t);
    void setActive(Marker m);
    void step(double seconds);   // сдвиг выбранной метки

signals:
    void positionChanged(double t);
    void rangeChanged(double start, double end);
    void activeChanged(int marker);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    QSize sizeHint() const override { return QSize(800, 120); }
    bool focusNextPrevChild(bool) override { return false; }   // Tab переключает метки

private:
    QRectF stripRect() const;
    double xToTime(double x) const;
    double timeToX(double t) const;

    double m_duration = 1.0;
    double m_fps = 25.0;
    double m_pos = 0.0;
    double m_start = 0.0;
    double m_end = 1.0;
    Marker m_active = Position;
    Marker m_drag = Position;
    bool m_dragging = false;
    int m_repeat = 0;
    QVector<QImage> m_thumbs;
};

// Окно обрезки по времени.
class TrimDialog : public FramedDialog
{
    Q_OBJECT
public:
    TrimDialog(const TaskPtr &task, QWidget *parent = nullptr);
    double start() const;
    double end() const;   // 0 = до конца

private:
    void requestFrame();
    void grabFrame();
    void loadThumbs(int index);
    void syncEditsFromTimeline();
    void updateInfo();

    TaskPtr m_task;
    PreviewLabel *m_preview;
    TimelineWidget *m_timeline;
    QTimeEdit *m_start;
    QTimeEdit *m_end;
    QLabel *m_posLabel;
    QLabel *m_info;
    QPushButton *m_modeBtns[3];
    QTimer m_frameTimer;
    bool m_grabbing = false;
    bool m_pending = false;
    bool m_syncing = false;
    int m_thumbCount = 14;
};
