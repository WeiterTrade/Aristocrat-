#pragma once
#include "core/Task.h"

#include <QImage>
#include <QLabel>

// Получение кадра из видео через FFmpeg (без плеера — работает одинаково на Windows 7 и Linux).
namespace FramePreview {
QImage grab(const ConversionTask &t, double at, bool withFilters, int maxWidth = 640);
}

// Область, показывающая кадр с сохранением пропорций и (по желанию) рамкой кадрирования.
class PreviewLabel : public QLabel
{
    Q_OBJECT
public:
    explicit PreviewLabel(QWidget *parent = nullptr);
    void setImage(const QImage &img);
    void setSourceSize(const QSize &s) { m_source = s; update(); }
    void setCropRect(const QRect &r) { m_crop = r; update(); }
    void setCropEditable(bool on) { m_editable = on; setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor); }
    void setBusy(bool busy) { m_busy = busy; update(); }

signals:
    void cropChanged(const QRect &r);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    QRectF imageRect() const;
    QPointF toSource(const QPointF &p) const;

    QImage m_img;
    QSize m_source;
    QRect m_crop;
    bool m_editable = false;
    bool m_busy = false;
    bool m_dragging = false;
    QPointF m_dragStart;
};
