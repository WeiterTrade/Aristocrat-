#include "DvdCss.h"
#include "I18n.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QMutex>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QVector>
#include <cstring>

namespace DvdCss {

namespace {

constexpr int kBlock = 2048;
constexpr int kNoFlags = 0;
constexpr int kReadDecrypt = 1;
constexpr int kSeekKey = 2;

using OpenFn = void *(*)(const char *);
using CloseFn = int (*)(void *);
using SeekFn = int (*)(void *, int, int);
using ReadFn = int (*)(void *, void *, int, int);
using ErrorFn = const char *(*)(void *);

struct Lib {
    QLibrary lib;
    OpenFn open = nullptr;
    CloseFn close = nullptr;
    SeekFn seek = nullptr;
    ReadFn read = nullptr;
    ErrorFn error = nullptr;
    bool tried = false;
    bool ok = false;
};

Lib &lib()
{
    static Lib l;
    static QMutex m;
    QMutexLocker lock(&m);
    if (l.tried)
        return l;
    l.tried = true;
    const QString app = QCoreApplication::applicationDirPath();
    QStringList candidates;
#ifdef Q_OS_WIN
    for (const QString &d : {app + QStringLiteral("/tools"), app})
        candidates << d + QStringLiteral("/libdvdcss-2.dll") << d + QStringLiteral("/libdvdcss.dll");
    candidates << QStringLiteral("libdvdcss-2");
#else
    for (const QString &d : {app + QStringLiteral("/tools"), app, app + QStringLiteral("/../lib")})
        candidates << d + QStringLiteral("/libdvdcss.so.2") << d + QStringLiteral("/libdvdcss.so");
    candidates << QStringLiteral("dvdcss");
#endif
    for (const QString &c : candidates) {
        if (c.contains(QLatin1Char('/')) && !QFileInfo::exists(c))
            continue;
        l.lib.setFileName(c);
        if (!c.contains(QLatin1Char('/')))
            l.lib.setFileNameAndVersion(c, 2);
        if (!l.lib.load())
            continue;
        l.open = reinterpret_cast<OpenFn>(l.lib.resolve("dvdcss_open"));
        l.close = reinterpret_cast<CloseFn>(l.lib.resolve("dvdcss_close"));
        l.seek = reinterpret_cast<SeekFn>(l.lib.resolve("dvdcss_seek"));
        l.read = reinterpret_cast<ReadFn>(l.lib.resolve("dvdcss_read"));
        l.error = reinterpret_cast<ErrorFn>(l.lib.resolve("dvdcss_error"));
        if (l.open && l.close && l.seek && l.read) {
            l.ok = true;
            break;
        }
        l.lib.unload();
    }
    if (l.ok) {
        // Ключи дисков кешируются — повторное чтение того же диска начинается сразу
        const QString cache = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/dvdcss");
        QDir().mkpath(cache);
        qputenv("DVDCSS_CACHE", QDir::toNativeSeparators(cache).toLocal8Bit());
        qputenv("DVDCSS_VERBOSE", "0");
    }
    return l;
}

struct Entry {
    QString name;   // VTS_01_1.VOB
    quint32 lba = 0;
    quint32 size = 0;
};

quint32 le32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (quint32(p[3]) << 24); }

bool readRaw(Lib &l, void *h, quint32 lba, int count, QByteArray &buf)
{
    buf.resize(count * kBlock);
    if (l.seek(h, int(lba), kNoFlags) != int(lba))
        return false;
    return l.read(h, buf.data(), count, kNoFlags) == count;
}

// Записи каталога ISO 9660 (на DVD-Video он всегда есть вместе с UDF)
QVector<Entry> listDir(Lib &l, void *h, quint32 lba, quint32 size, bool wantDirs)
{
    QVector<Entry> out;
    const int sectors = int((size + kBlock - 1) / kBlock);
    QByteArray buf;
    if (sectors <= 0 || sectors > 64 || !readRaw(l, h, lba, sectors, buf))
        return out;
    const auto *p = reinterpret_cast<const unsigned char *>(buf.constData());
    int pos = 0;
    while (pos < buf.size()) {
        const int len = p[pos];
        if (len == 0) {   // записи не пересекают границу сектора
            pos = (pos / kBlock + 1) * kBlock;
            continue;
        }
        if (pos + len > buf.size() || len < 34)
            break;
        const unsigned char *r = p + pos;
        const bool isDir = r[25] & 2;
        const int nameLen = r[32];
        QString name = QString::fromLatin1(reinterpret_cast<const char *>(r + 33), nameLen);
        name = name.section(QLatin1Char(';'), 0, 0);
        if (isDir == wantDirs && nameLen > 0 && r[33] > 1)
            out.append({name.toUpper(), le32(r + 2), le32(r + 10)});
        pos += len;
    }
    return out;
}

QString errorText(Lib &l, void *h)
{
    return (h && l.error) ? QString::fromLocal8Bit(l.error(h)) : QString();
}

} // namespace

bool available() { return lib().ok; }

QString libraryPath() { return available() ? lib().lib.fileName() : QString(); }

QString deviceFor(const QString &videoTs)
{
    QDir up(videoTs);
    up.cdUp();
    const QString root = QDir::cleanPath(up.absolutePath());
    const QStorageInfo si(root);
    if (!si.isValid() || QDir::cleanPath(si.rootPath()) != root)
        return {};
#ifdef Q_OS_WIN
    // «E:/» → «E:» (так libdvdcss открывает привод или смонтированный образ)
    if (root.size() >= 2 && root.at(1) == QLatin1Char(':'))
        return root.left(2);
    return {};
#else
    const QString dev = QString::fromLocal8Bit(si.device());
    if (dev.startsWith(QLatin1String("/dev/")))
        return dev;
    // смонтированный .iso: libdvdcss читает и файлы-образы
    if (dev.endsWith(QLatin1String(".iso"), Qt::CaseInsensitive) && QFileInfo::exists(dev))
        return dev;
    return {};
#endif
}

bool backup(const QString &device, const QStringList &vtsSets, const QString &outVideoTs,
            const std::function<bool(int)> &progress, QString *error, int *badSectors)
{
    Lib &l = lib();
    auto fail = [error](const QString &e) {
        if (error)
            *error = e;
        return false;
    };
    if (!l.ok)
        return fail(T("libdvdcss не найдена"));
    void *h = l.open(QFile::encodeName(device).constData());
    if (!h)
        return fail(T("не удалось открыть диск %1").arg(device));
    struct Closer {
        Lib &l;
        void *h;
        ~Closer() { l.close(h); }
    } closer{l, h};

    // Каталог VIDEO_TS по ISO 9660
    QByteArray pvd;
    if (!readRaw(l, h, 16, 1, pvd) || pvd.at(0) != 1 || pvd.mid(1, 5) != "CD001")
        return fail(T("на диске нет файловой системы ISO 9660: %1").arg(errorText(l, h)));
    const auto *root = reinterpret_cast<const unsigned char *>(pvd.constData()) + 156;
    QVector<Entry> files;
    for (const Entry &d : listDir(l, h, le32(root + 2), le32(root + 10), true))
        if (d.name == QLatin1String("VIDEO_TS"))
            files = listDir(l, h, d.lba, d.size, false);
    if (files.isEmpty())
        return fail(T("на диске не найдена папка VIDEO_TS"));

    // Что копируем: VIDEO_TS.IFO/BUP и наборы выбранных фильмов (без меню VTS_xx_0.VOB)
    QVector<Entry> want;
    qint64 total = 0;
    for (const Entry &e : files) {
        const bool head = e.name.startsWith(QLatin1String("VIDEO_TS.")) && !e.name.endsWith(QLatin1String(".VOB"));
        bool set = false;
        for (const QString &v : vtsSets) {
            const QString pre = QStringLiteral("VTS_%1_").arg(v);
            if (e.name.startsWith(pre) && e.name != pre + QStringLiteral("0.VOB"))
                set = true;
        }
        if (head || set) {
            want.append(e);
            total += e.size;
        }
    }
    if (!QDir().mkpath(outVideoTs))
        return fail(T("не удалось создать папку %1").arg(outVideoTs));

    int bad = 0;
    qint64 done = 0;
    int lastPct = -1;
    QByteArray buf(512 * kBlock, Qt::Uninitialized);
    for (const Entry &e : want) {
        QFile out(QDir(outVideoTs).filePath(e.name));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return fail(T("не удалось записать %1").arg(out.fileName()));
        const bool vob = e.name.endsWith(QLatin1String(".VOB"));
        const quint32 blocks = (e.size + kBlock - 1) / kBlock;
        // VOB: ключ фильма берётся в начале файла, дальше чтение с расшифровкой
        if (l.seek(h, int(e.lba), vob ? kSeekKey : kNoFlags) < 0)
            return fail(T("ошибка чтения диска (%1): %2").arg(e.name, errorText(l, h)));
        quint32 pos = 0;
        while (pos < blocks) {
            const int n = int(qMin<quint32>(512, blocks - pos));
            int got = l.read(h, buf.data(), n, vob ? kReadDecrypt : kNoFlags);
            if (got != n) {
                // Повреждённые/«ловушечные» сектора: читаем по одному, нечитаемые — нулями
                for (int i = 0; i < n; ++i) {
                    char *dst = buf.data() + i * kBlock;
                    if (l.seek(h, int(e.lba + pos + i), kNoFlags) < 0 || l.read(h, dst, 1, vob ? kReadDecrypt : kNoFlags) != 1) {
                        std::memset(dst, 0, kBlock);
                        ++bad;
                    }
                }
                l.seek(h, int(e.lba + pos + n), kNoFlags);
            }
            const qint64 bytes = qMin<qint64>(qint64(n) * kBlock, qint64(e.size) - qint64(pos) * kBlock);
            if (out.write(buf.constData(), bytes) != bytes)
                return fail(T("не хватает места на диске для %1").arg(e.name));
            pos += quint32(n);
            done += bytes;
            const int pct = total > 0 ? int(done * 100 / total) : 100;
            if (pct != lastPct) {
                lastPct = pct;
                if (progress && !progress(pct))
                    return fail(T("отменено"));
            }
        }
    }
    if (badSectors)
        *badSectors = bad;
    return true;
}

} // namespace DvdCss
