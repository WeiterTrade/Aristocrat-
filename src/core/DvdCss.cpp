#include "DvdCss.h"
#include "I18n.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
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

// ======================= Фильмы по таблицам IFO =======================

namespace {

uchar at(const QByteArray &b, int o) { return (o >= 0 && o < b.size()) ? uchar(b[o]) : 0; }
quint16 be16(const QByteArray &b, int o) { return (o + 2 <= b.size()) ? quint16((uchar(b[o]) << 8) | uchar(b[o + 1])) : 0; }
quint32 be32(const QByteArray &b, int o)
{
    return (o + 4 <= b.size()) ? (quint32(uchar(b[o])) << 24) | (uchar(b[o + 1]) << 16) | (uchar(b[o + 2]) << 8) | uchar(b[o + 3]) : 0;
}
int bcd(uchar v) { return (v >> 4) * 10 + (v & 15); }
double bcdTime(const QByteArray &b, int o)
{
    if (o + 4 > b.size())
        return 0;
    const uchar f = uchar(b[o + 3]);
    const double fps = ((f >> 6) == 3) ? 29.97 : 25.0;
    return bcd(uchar(b[o])) * 3600 + bcd(uchar(b[o + 1])) * 60 + bcd(uchar(b[o + 2])) + bcd(f & 0x3f) / fps;
}

QString langOf(const QByteArray &b, int o)
{
    const char c1 = char(at(b, o)), c2 = char(at(b, o + 1));
    if (c1 >= 'a' && c1 <= 'z' && c2 >= 'a' && c2 <= 'z')
        return QString(QLatin1Char(c1)) + QLatin1Char(c2);
    return QStringLiteral("?");
}

// IFO по абсолютному сектору: сначала заголовок, затем весь файл (до 2 МБ)
QByteArray readIfo(Lib &l, void *h, quint32 lba, const char *magic)
{
    QByteArray head;
    if (!readRaw(l, h, lba, 1, head) || !head.startsWith(magic))
        return {};
    const quint32 last = be32(head, 0x1C);   // последний сектор служебной части IFO
    const int n = int(qBound<quint32>(1, last + 1, 1024));
    QByteArray all;
    return readRaw(l, h, lba, n, all) ? all : head;
}

QByteArray readFileIfo(const QString &videoTs, const QString &name)
{
    for (const QString &n : {name, name.toLower(), QString(name).replace(QLatin1String(".IFO"), QLatin1String(".BUP"))}) {
        QFile f(QDir(videoTs).filePath(n));
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray b = f.read(2 * 1024 * 1024);
            if (b.size() > 0x100)
                return b;
        }
    }
    return {};
}

} // namespace

QList<DiscTitle> discTitles(const QString &device, const QString &videoTs, QString *error)
{
    QList<DiscTitle> out;
    Lib &l = lib();
    if (!l.ok) {
        if (error)
            *error = T("libdvdcss не найдена");
        return out;
    }
    void *h = l.open(QFile::encodeName(device).constData());
    if (!h) {
        if (error)
            *error = T("не удалось открыть диск %1").arg(device);
        return out;
    }
    struct Closer {
        Lib &l;
        void *h;
        ~Closer() { l.close(h); }
    } closer{l, h};

    // VIDEO_TS.IFO — с файловой системы (он не зашифрован); не вышло — по каталогу ISO 9660 на диске
    QByteArray vmg = readFileIfo(videoTs, QStringLiteral("VIDEO_TS.IFO"));
    if (!vmg.startsWith("DVDVIDEO-VMG")) {
        QByteArray pvd;
        if (readRaw(l, h, 16, 1, pvd) && pvd.at(0) == 1) {
            const auto *root = reinterpret_cast<const unsigned char *>(pvd.constData()) + 156;
            for (const Entry &d : listDir(l, h, le32(root + 2), le32(root + 10), true))
                if (d.name == QLatin1String("VIDEO_TS"))
                    for (const Entry &f : listDir(l, h, d.lba, d.size, false))
                        if (f.name == QLatin1String("VIDEO_TS.IFO"))
                            vmg = readIfo(l, h, f.lba, "DVDVIDEO-VMG");
        }
    }
    if (!vmg.startsWith("DVDVIDEO-VMG")) {
        if (error)
            *error = T("не удалось прочитать VIDEO_TS.IFO");
        return out;
    }
    const int tt = int(be32(vmg, 0xC4)) * kBlock;   // таблица фильмов (TT_SRPT)
    const int count = be16(vmg, tt);
    QHash<int, QByteArray> vtsIfo;
    QHash<int, quint32> vtsLba;
    for (int i = 0; i < count && i < 99; ++i) {
        const int e = tt + 8 + i * 12;
        DiscTitle t;
        t.number = i + 1;
        t.vts = at(vmg, e + 6);
        const int ttn = at(vmg, e + 7);
        const quint32 lba = be32(vmg, e + 8);
        if (t.vts <= 0 || ttn <= 0)
            continue;
        if (!vtsIfo.contains(t.vts)) {
            QByteArray ifo = readIfo(l, h, lba, "DVDVIDEO-VTS");
            if (!ifo.startsWith("DVDVIDEO-VTS"))
                ifo = readFileIfo(videoTs, QStringLiteral("VTS_%1_0.IFO").arg(t.vts, 2, 10, QLatin1Char('0')));
            vtsIfo.insert(t.vts, ifo);
            vtsLba.insert(t.vts, lba);
        }
        const QByteArray &v = vtsIfo[t.vts];
        if (!v.startsWith("DVDVIDEO-VTS"))
            continue;
        const quint32 vobs = vtsLba[t.vts] + be32(v, 0xC4);   // начало VOB фильмов этого набора
        t.keySector = vobs;
        // Дорожки
        for (int a = 0; a < qMin<int>(be16(v, 0x200), 8); ++a) {
            const int o = 0x202 + a * 8;
            t.audio << langOf(v, o + 2);
            static const char *fmt[] = {"AC3", "?", "MPEG", "MPEG", "LPCM", "?", "DTS", "?"};
            const int ch = (uchar(v[o + 1]) & 7) + 1;
            t.audioFormat << QStringLiteral("%1 %2").arg(QLatin1String(fmt[(uchar(v[o]) >> 5) & 7]),
                                                        ch == 6 ? QStringLiteral("5.1") : ch == 8 ? QStringLiteral("7.1") : QStringLiteral("%1.0").arg(ch));
        }
        for (int s2 = 0; s2 < qMin<int>(be16(v, 0x254), 32); ++s2)
            t.subs << langOf(v, 0x256 + s2 * 6 + 2);
        // Части фильма → программные цепочки (PGC)
        const int ptt = int(be32(v, 0xC8)) * kBlock;
        const int pgcit = int(be32(v, 0xCC)) * kBlock;
        const int titles = be16(v, ptt);
        if (ttn > titles)
            continue;
        const int start = ptt + int(be32(v, ptt + 8 + (ttn - 1) * 4));
        const int end = (ttn < titles) ? ptt + int(be32(v, ptt + 8 + ttn * 4)) : ptt + int(be32(v, ptt + 4)) + 1;
        QList<int> pgcs;
        for (int o = start; o + 4 <= end && o + 4 <= v.size(); o += 4) {
            const int pgcn = be16(v, o);
            if (pgcn > 0 && !pgcs.contains(pgcn))
                pgcs << pgcn;
            ++t.chapters;
        }
        qint64 prevLast = -1;
        for (int pgcn : pgcs) {
            if (pgcn > be16(v, pgcit))
                continue;
            const int pgc = pgcit + int(be32(v, pgcit + 8 + (pgcn - 1) * 8 + 4));
            t.seconds += bcdTime(v, pgc + 4);
            const int cells = at(v, pgc + 3);
            const int cp = pgc + be16(v, pgc + 0xE8);
            for (int c = 0; c < cells; ++c) {
                const int o = cp + c * 24;
                if (o + 24 > v.size())
                    break;
                const uchar flags = uchar(v[o]);
                const int blockMode = flags >> 6, blockType = (flags >> 4) & 3;
                if (blockType == 1 && blockMode > 1)
                    continue;   // другие ракурсы — только первый
                const quint32 first = be32(v, o + 8), last = be32(v, o + 20);
                if (last < first)
                    continue;
                if (prevLast >= 0 && qint64(first) != prevLast + 1)
                    ++t.jumps;
                prevLast = last;
                t.cells.append({vobs + first, vobs + last});
                t.bytes += qint64(last - first + 1) * kBlock;
            }
        }
        if (!t.cells.isEmpty())
            out.append(t);
    }
    // Основной фильм: самый длинный; среди почти равных (фальшивки той же длины) — с ячейками по порядку
    double longest = 0;
    for (const DiscTitle &t : out)
        longest = qMax(longest, t.seconds);
    int best = -1;
    for (int i = 0; i < out.size(); ++i) {
        const DiscTitle &t = out[i];
        if (t.seconds < longest * 0.9)
            continue;
        if (best < 0 || t.jumps < out[best].jumps || (t.jumps == out[best].jumps && t.cells.size() < out[best].cells.size()))
            best = i;
    }
    if (best >= 0)
        out[best].main = true;
    return out;
}

bool rip(const QString &device, const DiscTitle &title, const QString &outFile,
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
    QDir().mkpath(QFileInfo(outFile).absolutePath());
    QFile out(outFile);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail(T("не удалось записать %1").arg(outFile));
    // Ключ CSS фильма — по началу его VOB
    if (l.seek(h, int(title.keySector), kSeekKey) < 0)
        return fail(T("ошибка чтения диска (%1): %2").arg(QStringLiteral("key"), errorText(l, h)));

    const qint64 total = qMax<qint64>(1, title.bytes / kBlock);
    qint64 done = 0;
    int bad = 0, lastPct = -1;
    QByteArray buf(256 * kBlock, Qt::Uninitialized);
    for (const auto &cell : title.cells) {
        quint32 pos = cell.first;
        while (pos <= cell.second) {
            const int n = int(qMin<quint32>(256, cell.second - pos + 1));
            bool ok = l.seek(h, int(pos), kNoFlags) == int(pos) && l.read(h, buf.data(), n, kReadDecrypt) == n;
            if (ok) {
                if (out.write(buf.constData(), qint64(n) * kBlock) != qint64(n) * kBlock)
                    return fail(T("не хватает места на диске для %1").arg(QFileInfo(outFile).fileName()));
            } else {
                // Плохой участок: по 16 секторов; нечитаемые пропускаем (в MPEG-PS их можно просто не писать)
                for (int i = 0; i < n; i += 16) {
                    const int m = qMin(16, n - i);
                    char *dst = buf.data() + i * kBlock;
                    if (l.seek(h, int(pos + i), kNoFlags) == int(pos + i) && l.read(h, dst, m, kReadDecrypt) == m)
                        out.write(dst, qint64(m) * kBlock);
                    else
                        bad += m;
                }
            }
            pos += quint32(n);
            done += n;
            const int pct = int(done * 100 / total);
            if (pct != lastPct) {
                lastPct = pct;
                if (progress && !progress(pct))
                    return fail(T("отменено"));
            }
        }
    }
    if (badSectors)
        *badSectors = bad;
    out.close();
    if (out.size() < 1024 * 1024)
        return fail(T("с диска ничего не прочиталось"));
    return true;
}

} // namespace DvdCss
