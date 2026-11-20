#include "FramePreview.h"
#include "core/I18n.h"
#include "core/FfmpegCommand.h"
#include "core/Tools.h"

#include <QDir>
#include <QFile>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>

namespace FramePreview {

QImage grab(const ConversionTask &t, double at, bool withFilters, int maxWidth)
{
    if (Tools::ffmpeg().isEmpty())
        return {};
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/aristocrat-vc");
    QDir().mkpath(dir);
    const QString out = dir + QStringLiteral("/frame_%1.jpg").arg(QUuid::createUuid().toString(QUuid::Id128));
    QProcess p;
    p.start(Tools::ffmpeg(), FfmpegCommand::previewFrame(t, at, withFilters, maxWidth, out, dir));
    if (!p.waitForFinished(20000))
        p.kill();
    QImage img(out);
    QFile::remove(out);
    return img;
}

} // namespace FramePreview

PreviewLabel::PreviewLabel(QWidget *parent)
    : QLabel(parent)
{
    setObjectName(QStringLiteral("PreviewFrame"));
    setMinimumSize(320, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void PreviewLabel::setImage(const QImage &img)
{
    m_img = img;
    m_busy = false;
    update();
}

QRectF PreviewLabel::imageRect() const
{
    if (m_img.isNull())
        return {};
    QSizeF s = m_img.size();
    s.scale(QSizeF(size()) - QSizeF(4, 4), Qt::KeepAspectRatio);
    return QRectF((width() - s.width()) / 2.0, (height() - s.height()) / 2.0, s.width(), s.height());
}

QPointF PreviewLabel::toSource(const QPointF &p) const
{
    const QRectF r = imageRect();
    if (r.isEmpty() || !m_source.isValid())
        return {};
    const double x = qBound(0.0, (p.x() - r.left()) / r.width(), 1.0) * m_source.width();
    const double y = qBound(0.0, (p.y() - r.top()) / r.height(), 1.0) * m_source.height();
    return {x, y};
}

void PreviewLabel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor("#0E0A0B"));
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF r = imageRect();
    if (!m_img.isNull())
        p.drawImage(r, m_img);
    if (m_busy || m_img.isNull()) {
        p.setPen(QColor("#A99F90"));
        p.drawText(rect(), Qt::AlignCenter, m_busy ? T("Загрузка кадра…") : T("Нет изображения"));
    }
    if (m_editable && !m_img.isNull() && m_source.isValid()) {
        QRectF c = r;
        if (!m_crop.isNull()) {
            const double sx = r.width() / m_source.width(), sy = r.height() / m_source.height();
            c = QRectF(r.left() + m_crop.x() * sx, r.top() + m_crop.y() * sy, m_crop.width() * sx, m_crop.height() * sy);
            // затемняем то, что будет отрезано
            QPainterPath outside;
            outside.addRect(r);
            QPainterPath inside;
            inside.addRect(c);
            p.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 140));
        }
        p.setPen(QPen(QColor("#E2BC5C"), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(c.adjusted(1, 1, -1, -1));
        const QPointF ctr = c.center();
        p.drawLine(ctr + QPointF(-10, 0), ctr + QPointF(10, 0));
        p.drawLine(ctr + QPointF(0, -10), ctr + QPointF(0, 10));
    }
}

void PreviewLabel::mousePressEvent(QMouseEvent *e)
{
    if (!m_editable || m_img.isNull())
        return;
    m_dragging = true;
    m_dragStart = toSource(e->pos());
}

void PreviewLabel::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging)
        return;
    const QPointF cur = toSource(e->pos());
    QRect r = QRectF(m_dragStart, cur).normalized().toRect();
    r.setWidth(r.width() & ~1);
    r.setHeight(r.height() & ~1);
    if (r.width() >= 16 && r.height() >= 16) {
        m_crop = r;
        update();
        emit cropChanged(m_crop);
    }
}

void PreviewLabel::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = false;
}
