#include "Widgets.h"
#include "core/Theme.h"

#include <QApplication>
#include <QDesktopServices>
#include <QIconEngine>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>
#include <QFont>
#include <QHash>
#include <QSvgRenderer>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QThreadPool>

namespace Ui {

// Перекрашенный под тему SVG (кэш сбрасывается при смене темы)
static QHash<QString, QByteArray> &svgCache()
{
    static QHash<QString, QByteArray> c;
    return c;
}

static QByteArray themedSvgData(const QString &path)
{
    const QString key = Theme::id() + QLatin1Char('|') + path;
    auto it = svgCache().constFind(key);
    if (it != svgCache().constEnd())
        return it.value();
    QFile f(path);
    QByteArray data;
    if (f.open(QIODevice::ReadOnly))
        data = Theme::map(QString::fromUtf8(f.readAll())).toUtf8();
    svgCache().insert(key, data);
    return data;
}

// Значок, который рисуется в цветах ТЕКУЩЕЙ темы при каждой отрисовке —
// поэтому при смене темы все значки перекрашиваются сами, без перезапуска.
class ThemedIconEngine : public QIconEngine
{
public:
    ThemedIconEngine(const QString &normal, const QString &active) : m_normal(normal), m_active(active) {}
    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        const bool act = !m_active.isEmpty() && (state == QIcon::On || mode == QIcon::Active);
        QSvgRenderer r(themedSvgData(act ? m_active : m_normal));
        if (mode == QIcon::Disabled)
            p->setOpacity(0.4);
        r.render(p, QRectF(rect));
        p->setOpacity(1.0);
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pm(size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }
    QIconEngine *clone() const override { return new ThemedIconEngine(m_normal, m_active); }

private:
    QString m_normal, m_active;
};

QPixmap svgPixmap(const QString &path, const QSize &size)
{
    const qreal dpr = 2.0;
    QPixmap pm(size * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QSvgRenderer r(themedSvgData(path));
    QPainter p(&pm);
    r.render(&p, QRectF(QPointF(0, 0), QSizeF(size)));
    return pm;
}

QIcon icon(const QString &name, bool active)
{
    return QIcon(new ThemedIconEngine(QStringLiteral(":/icons/%1%2.svg").arg(name, active ? QStringLiteral("-active") : QString()), QString()));
}

QIcon navIcon(const QString &name)
{
    return QIcon(new ThemedIconEngine(QStringLiteral(":/icons/%1.svg").arg(name), QStringLiteral(":/icons/%1-active.svg").arg(name)));
}

void themedStyle(QWidget *w, const QString &qss)
{
    w->setProperty("aristoStyle", qss);
    Ui::themedStyle(w, qss);
}

void svgLabel(QLabel *l, const QString &path, const QSize &size)
{
    l->setProperty("aristoSvg", path);
    l->setProperty("aristoSvgSize", size);
    l->setPixmap(svgPixmap(path, size));
}

void refreshTheme()
{
    svgCache().clear();
    for (QWidget *w : QApplication::allWidgets()) {
        const QVariant st = w->property("aristoStyle");
        if (st.isValid())
            Ui::themedStyle(w, st.toString());
        const QVariant svg = w->property("aristoSvg");
        if (svg.isValid())
            if (auto *l = qobject_cast<QLabel *>(w))
                l->setPixmap(svgPixmap(svg.toString(), w->property("aristoSvgSize").toSize()));
        w->update();
    }
}

QToolButton *iconButton(const QString &name, const QString &tooltip, QWidget *parent, int size)
{
    auto *b = new QToolButton(parent);
    b->setObjectName(QStringLiteral("IconButton"));
    b->setIcon(icon(name));
    b->setIconSize(QSize(size, size));
    b->setToolTip(tooltip);
    b->setCursor(Qt::PointingHandCursor);
    b->setAutoRaise(true);
    return b;
}

QLabel *iconLabel(const QString &name, int size, QWidget *parent)
{
    auto *l = new QLabel(parent);
    svgLabel(l, QStringLiteral(":/icons/%1.svg").arg(name), QSize(size, size));
    l->setFixedSize(size, size);
    return l;
}

QPushButton *primaryButton(const QString &text, QWidget *parent)
{
    auto *b = new QPushButton(text, parent);
    b->setObjectName(QStringLiteral("PrimaryButton"));
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QPixmap formatBadge(const QString &text, int size, bool highlighted)
{
    const qreal dpr = 2.0;
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor gold = Theme::color(highlighted ? "#E8C266" : "#CFC3A8");

    // «Лист» с загнутым уголком и полоской плёнки сверху
    const qreal w = size * 0.74, h = size * 0.92;
    const qreal x = (size - w) / 2, y = (size - h) / 2;
    QPainterPath path;
    path.moveTo(x, y);
    path.lineTo(x + w * 0.72, y);
    path.lineTo(x + w, y + w * 0.28);
    path.lineTo(x + w, y + h);
    path.lineTo(x, y + h);
    path.closeSubpath();
    p.setPen(QPen(gold, 1.4));
    p.setBrush(Theme::color("#2A2225"));
    p.drawPath(path);
    p.setBrush(gold);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 3; ++i)
        p.drawRect(QRectF(x + w * 0.12 + i * w * 0.2, y + h * 0.12, w * 0.1, h * 0.08));

    QFont f;
    f.setBold(true);
    f.setPixelSize(int(qMax(7.0, size * (text.size() > 4 ? 0.2 : 0.26))));
    p.setFont(f);
    p.setPen(gold);
    p.drawText(QRectF(x, y + h * 0.42, w, h * 0.5), Qt::AlignCenter, text.left(6));
    return pm;
}

void showInFolder(const QString &file, const QString &fallbackDir)
{
    const QFileInfo fi(file);
    if (file.isEmpty() || !fi.exists()) {
        const QString dir = !fallbackDir.isEmpty() ? fallbackDir : fi.absolutePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        return;
    }
#ifdef Q_OS_WIN
    // Проводнику нужен ровно один аргумент вида /select,"путь" — без лишних кавычек от QProcess
    QProcess p;
    p.setProgram(QStringLiteral("explorer.exe"));
    p.setNativeArguments(QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(fi.absoluteFilePath())));
    p.startDetached();
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
#endif
}

void runAsync(QObject *context, std::function<void()> work, std::function<void()> done)
{
    QPointer<QObject> guard(context);
    QThreadPool::globalInstance()->start([guard, work, done] {
        work();
        if (guard)
            QMetaObject::invokeMethod(guard.data(), [guard, done] { if (guard) done(); }, Qt::QueuedConnection);
    });
}

} // namespace Ui
