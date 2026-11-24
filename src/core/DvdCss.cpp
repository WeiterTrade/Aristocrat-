#include "DvdCss.h"
#include "I18n.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QLibrary>
#include <QMutex>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QVector>
#include <cstring>
#include <QElapsedTimer>
#include <QThread>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

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
        for (int a = 0; a < qMin<int>(be16(v, 0x202), 8); ++a) {
            const int o = 0x204 + a * 8;
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

// ======================= Надёжное чтение диска =======================
// Как у DVDFab/ImgBurn/ddrescue: сбой — пауза и повтор (привод раскручивается заново), затем снижение скорости
// привода, затем пропуск по нарастающей с нулями на месте нечитаемого. Длинная полоса сбоев — «перезапуск»
// привода (закрыть/открыть, пауза, минимальная скорость). В конце — второй проход по пропущенному на 2x.

namespace {

// Скорость привода: 0 — максимум; иначе кратность DVD (1x = 1385 КБ/с)
bool setDriveSpeed(const QString &device, int dvdX)
{
#ifdef Q_OS_WIN
    if (device.size() != 2 || device.at(1) != QLatin1Char(':'))
        return false;
    const QString path = QStringLiteral("\\\\.\\") + device;
    HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    struct {
        int requestType;     // CdromSetSpeed
        USHORT readSpeed;    // КБ/с
        USHORT writeSpeed;
        int rotation;        // CdromDefaultRotation
    } sp{0, USHORT(dvdX <= 0 ? 0xFFFF : qMin(65534, dvdX * 1385)), 0xFFFF, 0};
    DWORD got = 0;
    const BOOL ok = DeviceIoControl(h, 0x24060 /* IOCTL_CDROM_SET_SPEED */, &sp, sizeof(sp), nullptr, 0, &got, nullptr);
    CloseHandle(h);
    return ok;
#else
    const int fd = ::open(QFile::encodeName(device).constData(), O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return false;
    const int ok = ::ioctl(fd, 0x5322 /* CDROM_SELECT_SPEED */, dvdX <= 0 ? 0 : dvdX * 8);
    ::close(fd);
    return ok == 0;
#endif
}

struct Range {
    quint32 first = 0, last = 0;
    quint32 key = 0;   // сектор, по которому берётся ключ CSS; 0 — не шифруется (IFO, файловая система)
    // 0 — важное (фильм, IFO): повторы, снижение скорости, второй проход;
    // 1 — второстепенное (ложные фильмы, меню): при первом сбое остаток участка — нулями, без мучений привода;
    // 2 — известная ловушка защиты: не читать вовсе, сразу нули
    int mode = 0;
};

class Engine
{
public:
    Engine(Lib &l, const QString &device, int maxSpeed, const std::function<bool(int, qint64, int)> &progress)
        : l(l), device(device), maxSpeed(maxSpeed), progress(progress)
    {
    }
    ~Engine()
    {
        if (h)
            l.close(h);
        setDriveSpeed(device, 0);   // вернуть приводу обычную скорость
    }
    bool open(QString *err)
    {
        h = l.open(QFile::encodeName(device).constData());
        if (!h && err)
            *err = T("не удалось открыть диск %1").arg(device);
        if (h && maxSpeed > 0)
            setDriveSpeed(device, maxSpeed);
        speed = maxSpeed;
        return h != nullptr;
    }

    // ranges подряд записываются в out; total — сумма секторов
    bool copy(const QVector<Range> &ranges, QFile &out, QString *err, int *badOut)
    {
        total = 0;
        for (const Range &r : ranges)
            total += qint64(r.last) - r.first + 1;
        qint64 outPos = 0;
        QByteArray buf(256 * kBlock, Qt::Uninitialized);
        for (const Range &r : ranges) {
            if (r.mode == 2) {
                const qint64 cnt = qint64(r.last) - r.first + 1;
                if (!writeZeros(out, outPos, int(cnt)))
                    return fail(err, T("не хватает места на диске"));
                outPos += cnt;
                done += cnt;
                traps += cnt;
                if (!report())
                    return fail(err, T("отменено"));
                continue;
            }
            useKey(r.key);
            quint32 pos = r.first;
            quint32 skip = 0;
            qint64 streak = 0;   // секторов сбоя подряд
            while (pos <= r.last) {
                const int n = int(qMin<quint32>(skip ? 16 : 256, r.last - pos + 1));
                bool ok = readAt(pos, n, buf.data(), r.key);
                if (!ok && r.mode == 1) {
                    // Второстепенный участок: ловушка — остаток нулями, дальше
                    const qint64 cnt = qint64(r.last) - pos + 1;
                    if (!writeZeros(out, outPos, int(cnt)))
                        return fail(err, T("не хватает места на диске"));
                    outPos += cnt;
                    done += cnt;
                    traps += cnt;
                    break;
                }
                if (!ok && !skip) {
                    // Первый сбой на месте: пауза (привод перераскрутится) и повтор, затем — медленнее
                    for (int attempt = 0; attempt < 2 && !ok; ++attempt) {
                        QThread::msleep(1500);
                        ok = readAt(pos, n, buf.data(), r.key);
                    }
                    if (!ok && slowDown())
                        ok = readAt(pos, n, buf.data(), r.key);
                    if (!ok && n > 16) {
                        // По 16 секторов: читаемое сохраняем, нечитаемое — нулями
                        for (int i = 0; i < n; i += 16) {
                            const int m = qMin(16, n - i);
                            char *dst = buf.data() + i * kBlock;
                            if (!readAt(pos + quint32(i), m, dst, r.key)) {
                                std::memset(dst, 0, size_t(m) * kBlock);
                                markBad(outPos + i, pos + quint32(i), m, r.key);
                            }
                        }
                        ok = true;   // кусок записан (с нулями на месте плохого)
                        skip = 16;
                    }
                }
                if (ok) {
                    if (!writeAt(out, outPos, buf.constData(), n))
                        return fail(err, T("не хватает места на диске"));
                    pos += quint32(n);
                    outPos += n;
                    done += n;
                    streak = 0;
                    if (skip && n == 16)
                        skip = 0;   // после частично плохого куска — снова большими кусками
                } else {
                    // Пропуск по нарастающей; на месте пропуска — нули (второй проход попробует ещё раз)
                    skip = skip ? qMin<quint32>(skip * 2, 2048) : 16;
                    const quint32 jump = qMin<quint32>(qMax<quint32>(skip, quint32(n)), r.last - pos + 1);
                    markBad(outPos, pos, int(jump), r.key);
                    if (!writeZeros(out, outPos, int(jump)))
                        return fail(err, T("не хватает места на диске"));
                    pos += jump;
                    outPos += jump;
                    done += jump;
                    streak += jump;
                    // Длинная полоса сбоев — это не царапина, а «застрявший» привод: перезапуск и повтор с начала полосы
                    if (streak * kBlock > 48LL * 1024 * 1024 && recoveries < 4) {
                        ++recoveries;
                        pos -= quint32(streak);
                        outPos -= streak;
                        done -= streak;
                        unmarkFrom(outPos);
                        streak = 0;
                        skip = 0;
                        if (!recover(r.key))
                            return fail(err, T("привод перестал отвечать"));
                    }
                }
                if (!report())
                    return fail(err, T("отменено"));
            }
        }
        // Второй проход: пропущенное — по одному сектору на минимальной скорости (до 10 минут)
        if (!bad.isEmpty() && badCount() < 50000) {
            setDriveSpeed(device, 2);
            QElapsedTimer t;
            t.start();
            QVector<Bad> still;
            QByteArray one(kBlock, Qt::Uninitialized);
            for (const Bad &b : qAsConst(bad)) {
                useKey(b.key);
                for (int i = 0; i < b.count; ++i) {
                    if (t.elapsed() > 600000 || !report())
                        still.append({b.out + i, b.lba + quint32(i), 1, b.key});
                    else if (readAt(b.lba + quint32(i), 1, one.data(), b.key))
                        writeAt(out, b.out + i, one.constData(), 1);
                    else
                        still.append({b.out + i, b.lba + quint32(i), 1, b.key});
                }
            }
            bad = still;
        }
        if (badOut)
            *badOut = badCount();
        return true;
    }

private:
    struct Bad {
        qint64 out;
        quint32 lba;
        int count;
        quint32 key;
    };
    Lib &l;
    QString device;
    int maxSpeed;
    int speed = 0;
    std::function<bool(int, qint64, int)> progress;
    void *h = nullptr;
    quint32 curKey = 0xFFFFFFFF;
    qint64 total = 0, done = 0;
    int recoveries = 0;

public:
    qint64 traps = 0;   // секторов ловушек, записанных нулями без чтения/после первого сбоя

private:
    QVector<Bad> bad;

    bool fail(QString *err, const QString &e)
    {
        if (err)
            *err = e;
        return false;
    }
    int badCount() const
    {
        int c = 0;
        for (const Bad &b : bad)
            c += b.count;
        return c;
    }
    bool report() { return !progress || progress(total ? int(done * 100 / total) : 100, done * kBlock, badCount()); }
    void useKey(quint32 key)
    {
        if (key && key != curKey && l.seek(h, int(key), kSeekKey) >= 0)
            curKey = key;
    }
    bool readAt(quint32 lba, int n, char *dst, quint32 key)
    {
        if (l.seek(h, int(lba), kNoFlags) != int(lba))
            return false;
        return l.read(h, dst, n, key ? kReadDecrypt : kNoFlags) == n;
    }
    static bool writeAt(QFile &out, qint64 sector, const char *data, int n)
    {
        return out.seek(sector * kBlock) && out.write(data, qint64(n) * kBlock) == qint64(n) * kBlock;
    }
    static bool writeZeros(QFile &out, qint64 sector, int n)
    {
        static const QByteArray zeros(256 * kBlock, '\0');
        while (n > 0) {
            const int m = qMin(n, 256);
            if (!writeAt(out, sector, zeros.constData(), m))
                return false;
            sector += m;
            n -= m;
        }
        return true;
    }
    void markBad(qint64 out, quint32 lba, int count, quint32 key) { bad.append({out, lba, count, key}); }
    void unmarkFrom(qint64 out)
    {
        while (!bad.isEmpty() && bad.last().out >= out)
            bad.removeLast();
    }
    // Ступени скорости: максимум → 8x → 4x → 2x
    bool slowDown()
    {
        const int next = speed <= 0 ? 8 : speed > 4 ? 4 : speed > 2 ? 2 : 0;
        if (next == 0)
            return false;
        speed = next;
        setDriveSpeed(device, speed);
        QThread::msleep(1000);
        return true;
    }
    bool recover(quint32 key)
    {
        l.close(h);
        h = nullptr;
        QThread::msleep(8000);   // привод останавливается и раскручивается заново
        h = l.open(QFile::encodeName(device).constData());
        if (!h)
            return false;
        speed = 2;
        setDriveSpeed(device, 2);
        curKey = 0xFFFFFFFF;
        useKey(key);
        return true;
    }
};

} // namespace


Cells probeTraps(const QString &device, QList<DiscTitle> &titles, const std::function<void(int, int)> &status)
{
    Cells traps;
    Lib &l = lib();
    double longest = 0;
    for (const DiscTitle &t : titles)
        longest = qMax(longest, t.seconds);
    QList<int> cand;
    for (int i = 0; i < titles.size(); ++i)
        if (titles[i].seconds >= longest * 0.9)
            cand << i;
    if (!l.ok || cand.size() < 2)
        return traps;
    std::sort(cand.begin(), cand.end(), [&](int a, int b) {
        if (titles[a].main != titles[b].main)
            return titles[a].main;
        if (titles[a].jumps != titles[b].jumps)
            return titles[a].jumps < titles[b].jumps;
        return titles[a].cells.size() < titles[b].cells.size();
    });
    // Ячейки, общие для всех кандидатов, ничего не решают — проверяем только «спорные»
    auto keyOf = [](const QPair<quint32, quint32> &c) { return (quint64(c.first) << 32) | c.second; };
    QHash<quint64, int> seen;
    for (int i : cand) {
        QSet<quint64> once;
        for (const auto &c : titles[i].cells)
            once.insert(keyOf(c));
        for (quint64 k : once)
            seen[k] += 1;
    }
    int toCheck = 0;
    for (auto it = seen.cbegin(); it != seen.cend(); ++it)
        toCheck += it.value() < cand.size() ? 1 : 0;
    void *h = l.open(QFile::encodeName(device).constData());
    if (!h)
        return traps;
    QHash<quint64, bool> ok;
    QElapsedTimer clock;
    clock.start();
    int checked = 0, mainIdx = -1;
    QByteArray buf;
    for (int i : cand) {
        bool clean = true;
        for (const auto &c : titles[i].cells) {
            const quint64 k = keyOf(c);
            if (seen.value(k) >= cand.size())
                continue;
            if (!ok.contains(k)) {
                // Начало и середина ячейки: ловушки бывают не с первого сектора
                const bool good = readRaw(l, h, c.first, 1, buf) && readRaw(l, h, c.first + (c.second - c.first) / 2, 1, buf);
                ok.insert(k, good);
                if (!good)
                    traps.append(c);
                if (status)
                    status(++checked, toCheck);
            }
            if (!ok.value(k)) {
                clean = false;
                break;
            }
        }
        if (clean) {
            mainIdx = i;
            break;
        }
        if (clock.elapsed() > 8 * 60 * 1000)
            break;   // не дольше 8 минут
    }
    l.close(h);
    // Ложные — с найденной ловушкой; основной — первый чистый (по порядку правдоподобия)
    for (int i : cand) {
        for (const auto &c : titles[i].cells)
            if (ok.contains(keyOf(c)) && !ok.value(keyOf(c)))
                titles[i].fake = true;
    }
    if (mainIdx >= 0) {
        for (DiscTitle &t : titles)
            t.main = false;
        titles[mainIdx].main = true;
    }
    return traps;
}

bool rip(const QString &device, const DiscTitle &title, const QString &outFile, int maxSpeed,
         const std::function<bool(int, qint64, int)> &progress, QString *error, int *badSectors)
{
    Lib &l = lib();
    if (!l.ok) {
        if (error)
            *error = T("libdvdcss не найдена");
        return false;
    }
    Engine e(l, device, maxSpeed, progress);
    if (!e.open(error))
        return false;
    QDir().mkpath(QFileInfo(outFile).absolutePath());
    QFile out(outFile);
    if (!out.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
        if (error)
            *error = T("не удалось записать %1").arg(outFile);
        return false;
    }
    QVector<Range> ranges;
    for (const auto &c : title.cells)
        ranges.append({c.first, c.second, title.keySector, 0});
    if (!e.copy(ranges, out, error, badSectors))
        return false;
    out.close();
    if (out.size() < 1024 * 1024) {
        if (error)
            *error = T("с диска ничего не прочиталось");
        return false;
    }
    return true;
}

bool isoBackup(const QString &device, const QString &outIso, int maxSpeed, const QList<DiscTitle> &titles, const Cells &traps,
               const std::function<bool(int, qint64, int)> &progress, QString *error, int *badSectors)
{
    Lib &l = lib();
    if (!l.ok) {
        if (error)
            *error = T("libdvdcss не найдена");
        return false;
    }
    // Размер диска и расположение VOB — по ISO 9660 (отдельное короткое открытие)
    quint32 volume = 0;
    QVector<Entry> vobs;
    {
        void *h = l.open(QFile::encodeName(device).constData());
        if (!h) {
            if (error)
                *error = T("не удалось открыть диск %1").arg(device);
            return false;
        }
        QByteArray pvd;
        if (readRaw(l, h, 16, 1, pvd) && pvd.at(0) == 1 && pvd.mid(1, 5) == "CD001") {
            volume = le32(reinterpret_cast<const unsigned char *>(pvd.constData()) + 80);
            const auto *root = reinterpret_cast<const unsigned char *>(pvd.constData()) + 156;
            for (const Entry &d : listDir(l, h, le32(root + 2), le32(root + 10), true))
                if (d.name == QLatin1String("VIDEO_TS"))
                    for (const Entry &f : listDir(l, h, d.lba, d.size, false))
                        if (f.name.endsWith(QLatin1String(".VOB")) && f.size > 0)
                            vobs.append(f);
        }
        l.close(h);
    }
    if (volume < 1000) {
        if (error)
            *error = T("на диске нет файловой системы ISO 9660: %1").arg(device);
        return false;
    }
    std::sort(vobs.begin(), vobs.end(), [](const Entry &a, const Entry &b) { return a.lba < b.lba; });
    // Ячейки фильмов: основной — важные (с повторами), ловушки — не читаем, остальное — без долгих повторов
    Cells important;
    for (const DiscTitle &t : titles)
        if (t.main)
            important += t.cells;
    auto inside = [](const Cells &cs, quint32 a) {
        for (const auto &c : cs)
            if (a >= c.first && a <= c.second)
                return true;
        return false;
    };
    // Весь диск по порядку: VOB — с ключом своего файла (расшифровка), режим — по ячейкам; остальное — важное
    QVector<Range> ranges;
    auto addVob = [&](quint32 a, quint32 b, quint32 key) {
        QVector<quint32> cuts{a, b + 1};
        for (const Cells *cs : std::initializer_list<const Cells *>{&important, &traps})
            for (const auto &c : *cs) {
                if (c.first > a && c.first <= b)
                    cuts << c.first;
                if (c.second >= a && c.second < b)
                    cuts << c.second + 1;
            }
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
        for (int i = 0; i + 1 < cuts.size(); ++i) {
            const quint32 x = cuts[i], y = cuts[i + 1] - 1;
            const int mode = inside(important, x) ? 0 : inside(traps, x) ? 2 : 1;
            if (!ranges.isEmpty() && ranges.last().mode == mode && ranges.last().key == key && ranges.last().last + 1 == x)
                ranges.last().last = y;
            else
                ranges.append({x, y, key, mode});
        }
    };
    quint32 pos = 0;
    for (const Entry &v : vobs) {
        if (v.lba >= volume || v.lba < pos)
            continue;   // ловушки защиты: файлы «за краем» или внахлёст
        const quint32 last = qMin<quint32>(volume - 1, v.lba + (v.size + kBlock - 1) / kBlock - 1);
        if (v.lba > pos)
            ranges.append({pos, v.lba - 1, 0, 0});
        addVob(v.lba, last, v.lba);
        pos = last + 1;
    }
    if (pos < volume)
        ranges.append({pos, volume - 1, 0, 0});

    Engine e(l, device, maxSpeed, progress);
    if (!e.open(error))
        return false;
    QDir().mkpath(QFileInfo(outIso).absolutePath());
    QFile out(outIso);
    if (!out.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
        if (error)
            *error = T("не удалось записать %1").arg(outIso);
        return false;
    }
    return e.copy(ranges, out, error, badSectors);
}


QString regionInfo(const QString &device)
{
#ifdef Q_OS_WIN
    if (device.size() != 2 || device.at(1) != QLatin1Char(':'))
        return {};
    const QString path = QStringLiteral("\\\\.\\") + device;
    HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return {};
    struct {
        UCHAR copySystem, regionData, systemRegion, resetCount;
    } r{};
    DWORD got = 0;
    const BOOL ok = DeviceIoControl(h, 0x335014 /* IOCTL_DVD_GET_REGION */, nullptr, 0, &r, sizeof(r), &got, nullptr);
    CloseHandle(h);
    if (!ok)
        return {};
    // Маски «запрещённых» регионов: бит i = регион i+1
    auto list = [](UCHAR mask) {
        QStringList out;
        for (int i = 0; i < 8; ++i)
            if (!(mask & (1 << i)))
                out << QString::number(i + 1);
        return out;
    };
    const QStringList disc = list(r.regionData), drive = list(r.systemRegion);
    const QString d = disc.size() >= 8 ? T("все") : disc.join(QStringLiteral(", "));
    const QString v = drive.isEmpty() || drive.size() >= 8 ? T("не задан") : drive.join(QStringLiteral(", "));
    QString text = T("Регион диска: %1 · регион привода: %2 (смен осталось: %3)").arg(d, v).arg(int(r.resetCount));
    bool match = disc.size() >= 8 || drive.isEmpty() || drive.size() >= 8;
    for (const QString &x : drive)
        match = match || disc.contains(x);
    if (!match)
        text += T(" — регионы не совпадают: привод может не отдавать зашифрованные сектора");
    return text;
#else
    Q_UNUSED(device)
    return {};
#endif
}

} // namespace DvdCss
