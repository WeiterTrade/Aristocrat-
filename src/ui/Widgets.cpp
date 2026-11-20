#include "Widgets.h"
#include "core/Theme.h"

#include <QDesktopServices>
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

// SVG-значок, перекрашенный под текущую тему
static QIcon themedSvg(const QString &path)
{
    static QHash<QString, QIcon> cache;
    auto it = cache.constFind(path);
    if (it != cache.constEnd())
        return it.value();
    QFile f(path);
    QIcon icon;
    if (f.open(QIODevice::ReadOnly)) {
        QSvgRenderer r(Theme::map(QString::fromUtf8(f.readAll())).toUtf8());
        for (int sz : {16, 20, 24, 32, 40, 48, 64, 96, 128, 192}) {
            QPixmap pm(sz, sz);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            r.render(&p, QRectF(0, 0, sz, sz));
            icon.addPixmap(pm);
        }
    }
    cache.insert(path, icon);
    return icon;
}

QPixmap svgPixmap(const QString &path, const QSize &size)
{
    QFile f(path);
    const qreal dpr = 2.0;
    QPixmap pm(size * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    if (f.open(QIODevice::ReadOnly)) {
        QSvgRenderer r(Theme::map(QString::fromUtf8(f.readAll())).toUtf8());
        QPainter p(&pm);
        r.render(&p, QRectF(QPointF(0, 0), QSizeF(size)));
    }
    return pm;
}

QIcon icon(const QString &name, bool active)
{
    return themedSvg(QStringLiteral(":/icons/%1%2.svg").arg(name, active ? QStringLiteral("-active") : QString()));
}

QIcon navIcon(const QString &name)
{
    QIcon i;
    const QIcon n = themedSvg(QStringLiteral(":/icons/%1.svg").arg(name));
    const QIcon a = themedSvg(QStringLiteral(":/icons/%1-active.svg").arg(name));
    for (int s : {24, 32, 48, 64}) {
        i.addPixmap(n.pixmap(s, s), QIcon::Normal, QIcon::Off);
        i.addPixmap(a.pixmap(s, s), QIcon::Normal, QIcon::On);
        i.addPixmap(a.pixmap(s, s), QIcon::Active, QIcon::Off);
        i.addPixmap(a.pixmap(s, s), QIcon::Active, QIcon::On);
    }
    return i;
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
    l->setPixmap(icon(name).pixmap(size, size));
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
