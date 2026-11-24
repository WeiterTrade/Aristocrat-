#include "DvdImage.h"
#include "I18n.h"
#include "Tools.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <cstring>

namespace DvdImage {

namespace {

constexpr int kSec = 2048;

quint32 le32(const QByteArray &b, int o)
{
    if (o < 0 || o + 4 > b.size())
        return 0;
    const auto *p = reinterpret_cast<const uchar *>(b.constData()) + o;
    return p[0] | (p[1] << 8) | (p[2] << 16) | (quint32(p[3]) << 24);
}
quint32 be32(const QByteArray &b, int o)
{
    if (o < 0 || o + 4 > b.size())
        return 0;
    const auto *p = reinterpret_cast<const uchar *>(b.constData()) + o;
    return (quint32(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}
int be16(const QByteArray &b, int o)
{
    if (o < 0 || o + 2 > b.size())
        return 0;
    return (uchar(b[o]) << 8) | uchar(b[o + 1]);
}
int at(const QByteArray &b, int o) { return (o >= 0 && o < b.size()) ? uchar(b[o]) : 0; }
int bcd(int v) { return (v >> 4) * 10 + (v & 15); }

struct Entry {
    QString name;
    quint32 lba = 0, size = 0;
};

QByteArray readSectors(QFile &f, quint32 lba, int n)
{
    if (!f.seek(qint64(lba) * kSec))
        return {};
    QByteArray b = f.read(qint64(n) * kSec);
    return b.size() == n * kSec ? b : QByteArray();
}

QVector<Entry> listDir(QFile &f, quint32 lba, quint32 size, bool dirs)
{
    QVector<Entry> out;
    const int n = int((size + kSec - 1) / kSec);
    if (n <= 0 || n > 64)
        return out;
    const QByteArray b = readSectors(f, lba, n);
    int pos = 0;
    while (pos < b.size()) {
        const int len = at(b, pos);
        if (len == 0) {
            pos = (pos / kSec + 1) * kSec;
            continue;
        }
        if (len < 34 || pos + len > b.size())
            break;
        const bool isDir = at(b, pos + 25) & 2;
        const int nl = at(b, pos + 32);
        const QString name = QString::fromLatin1(b.mid(pos + 33, nl)).section(QLatin1Char(';'), 0, 0).toUpper();
        if (isDir == dirs && nl > 0 && at(b, pos + 33) > 1)
            out.append({name, le32(b, pos + 2), le32(b, pos + 10)});
        pos += len;
    }
    return out;
}

QVector<Entry> videoTs(QFile &f)
{
    const QByteArray pvd = readSectors(f, 16, 1);
    if (pvd.size() != kSec || at(pvd, 0) != 1 || pvd.mid(1, 5) != "CD001")
        return {};
    for (const Entry &d : listDir(f, le32(pvd, 156 + 2), le32(pvd, 156 + 10), true))
        if (d.name == QLatin1String("VIDEO_TS"))
            return listDir(f, d.lba, d.size, false);
    return {};
}

QString menuName(int entry, int vts, int index)
{
    if (!(entry & 0x80))
        return vts ? T("Доп. меню %1 (фильм %2)").arg(index).arg(vts) : T("Доп. меню диска %1").arg(index);
    switch (entry & 0x0F) {
    case 2: return T("Главное меню диска");
    case 3: return vts ? T("Меню фильма %1").arg(vts) : T("Меню диска");
    case 4: return T("Меню субтитров (фильм %1)").arg(vts);
    case 5: return T("Меню звука (фильм %1)").arg(vts);
    case 6: return T("Меню ракурсов (фильм %1)").arg(vts);
    case 7: return T("Меню сцен (фильм %1)").arg(vts);
    default: return T("Меню %1").arg(index);
    }
}

// Кнопки — из пакета навигации (PCI) в начале меню
QList<Button> buttonsAt(QFile &f, quint32 lba)
{
    QList<Button> out;
    for (quint32 s = lba; s < lba + 16; ++s) {
        const QByteArray b = readSectors(f, s, 1);
        if (b.size() != kSec || be32(b, 0) != 0x000001BA || be32(b, 14) != 0x000001BB)
            continue;
        const int pci = 0x2D;
        const int hli = pci + 60 + 36;
        if (be16(b, hli) == 0)
            return out;   // кнопок нет
        const int count = qMin(36, at(b, hli + 17));
        const int base = hli + 22 + 24;
        for (int i = 0; i < count; ++i) {
            const int o = base + i * 18;
            const quint32 x = (at(b, o) << 16) | (at(b, o + 1) << 8) | at(b, o + 2);
            const quint32 y = (at(b, o + 3) << 16) | (at(b, o + 4) << 8) | at(b, o + 5);
            Button bt;
            const int xs = int((x >> 12) & 0x3FF), xe = int(x & 0x3FF), ys = int((y >> 12) & 0x3FF), ye = int(y & 0x3FF);
            if (xe <= xs || ye <= ys)
                continue;
            bt.rect = QRect(QPoint(xs, ys), QPoint(xe, ye));
            bt.up = at(b, o + 6) & 0x3F;
            bt.down = at(b, o + 7) & 0x3F;
            bt.left = at(b, o + 8) & 0x3F;
            bt.right = at(b, o + 9) & 0x3F;
            out << bt;
        }
        return out;
    }
    return out;
}

void parseMenus(QFile &f, const QByteArray &ifo, quint32 ifoLba, int vts, const QString &vob, QList<Menu> &out,
                QSet<QString> &seen)
{
    const quint32 vobs = be32(ifo, 0xC0);              // начало VOB меню (в секторах от IFO)
    const quint32 ut = be32(ifo, vts ? 0xD0 : 0xC8);   // таблица меню (PGCI_UT)
    if (!vobs || !ut)
        return;
    const int base = int(ut) * kSec;
    const int lus = qMin(be16(ifo, base), 32);
    for (int i = 0; i < lus; ++i) {
        const int e = base + 8 + i * 8;
        const QString lang = QString::fromLatin1(ifo.mid(e, 2)).trimmed();
        const int lu = base + int(be32(ifo, e + 4));
        const int pgcs = qMin(be16(ifo, lu), 99);
        for (int j = 0; j < pgcs; ++j) {
            const int srp = lu + 8 + j * 8;
            const int pgc = lu + int(be32(ifo, srp + 4));
            Menu m;
            m.vts = vts;
            m.vob = vob;
            m.lang = lang;
            m.title = menuName(at(ifo, srp), vts, j + 1);
            m.seconds = bcd(at(ifo, pgc + 4)) * 3600 + bcd(at(ifo, pgc + 5)) * 60 + bcd(at(ifo, pgc + 6));
            const int cells = at(ifo, pgc + 3);
            const int cp = pgc + be16(ifo, pgc + 0xE8);
            int stillTime = 0;
            for (int c = 0; c < cells; ++c) {
                const int o = cp + c * 24;
                if (o + 24 > ifo.size())
                    break;
                const quint32 first = be32(ifo, o + 8), last = be32(ifo, o + 20);
                if (last < first)
                    continue;
                stillTime = qMax(stillTime, at(ifo, o + 2));
                m.cells.append({ifoLba + vobs + first, ifoLba + vobs + last});
            }
            if (m.cells.isEmpty())
                continue;
            // Одна и та же картинка бывает в нескольких меню (разные языки) — показываем один раз
            QString key;
            for (const auto &c : m.cells)
                key += QStringLiteral("%1-%2;").arg(c.first).arg(c.second);
            if (seen.contains(key))
                continue;
            seen.insert(key);
            m.still = stillTime > 0 || m.seconds < 1;
            if (!lang.isEmpty() && lus > 1)
                m.title += QStringLiteral(" [%1]").arg(lang);
            m.buttons = buttonsAt(f, m.cells.first().first);
            out << m;
        }
    }
}

// --- пакеты видео в меню ---
struct Packet {
    int sector;     // индекс сектора в буфере меню
    int pos;        // начало PES (00 00 01 E0)
    int payload;    // начало данных
    int length;     // длина данных
};
struct Vobu {
    QVector<Packet> video;
    int pictures = 0;
    bool seqEnd = false;
    int capacity = 0;
};

QVector<Vobu> splitVobus(const QByteArray &buf)
{
    QVector<Vobu> out;
    const int sectors = buf.size() / kSec;
    for (int s = 0; s < sectors; ++s) {
        const int base = s * kSec;
        if (be32(buf, base) != 0x000001BA)
            continue;
        int pos = base + 14 + (at(buf, base + 13) & 7);
        if (be32(buf, pos) == 0x000001BB || out.isEmpty())
            out.append(Vobu());   // пакет навигации — начало нового VOBU
        while (pos + 6 <= base + kSec) {
            const quint32 sc = be32(buf, pos);
            if ((sc >> 8) != 1)
                break;
            const int sid = int(sc & 0xFF);
            const int len = be16(buf, pos + 4);
            if (pos + 6 + len > base + kSec)
                break;
            if (sid == 0xE0) {
                const int hdr = 9 + at(buf, pos + 8);
                if (hdr <= 6 + len)
                    out.last().video.append({s, pos, pos + hdr, 6 + len - hdr});
            }
            pos += 6 + len;
        }
    }
    // Сколько кадров и есть ли конец последовательности в исходном видео каждого VOBU
    for (Vobu &v : out) {
        QByteArray es;
        for (const Packet &p : v.video) {
            es += buf.mid(p.payload, p.length);
            v.capacity += p.length;
        }
        for (int i = 0; i + 3 < es.size(); ++i)
            if (at(es, i) == 0 && at(es, i + 1) == 0 && at(es, i + 2) == 1) {
                if (at(es, i + 3) == 0x00)
                    ++v.pictures;
                else if (at(es, i + 3) == 0xB7)
                    v.seqEnd = true;
            }
    }
    return out;
}

QByteArray menuBytes(QFile &f, const Menu &m)
{
    QByteArray all;
    for (const auto &c : m.cells) {
        const int n = int(c.second - c.first + 1);
        if (all.size() + qint64(n) * kSec > 300LL * 1024 * 1024)
            return {};
        all += readSectors(f, c.first, n);
    }
    return all;
}

bool runFfmpeg(const QStringList &args, QString *err)
{
    QProcess p;
    p.start(Tools::ffmpeg(), QStringList{QStringLiteral("-hide_banner"), QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y")} + args);
    if (!p.waitForFinished(120000) || p.exitCode() != 0) {
        if (err)
            *err = QString::fromLocal8Bit(p.readAllStandardError()).trimmed().left(400);
        return false;
    }
    return true;
}

} // namespace

QList<Menu> menus(const QString &iso, QString *error)
{
    QList<Menu> out;
    QFile f(iso);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = T("не удалось открыть файл");
        return out;
    }
    const QVector<Entry> files = videoTs(f);
    if (files.isEmpty()) {
        if (error)
            *error = T("это не образ DVD-Video (нет папки VIDEO_TS)");
        return out;
    }
    QSet<QString> seen;
    QVector<Entry> ifos;
    for (const Entry &e : files)
        if (e.name == QLatin1String("VIDEO_TS.IFO") || (e.name.startsWith(QLatin1String("VTS_")) && e.name.endsWith(QLatin1String("_0.IFO"))))
            ifos << e;
    std::sort(ifos.begin(), ifos.end(), [](const Entry &a, const Entry &b) { return a.name < b.name; });
    for (const Entry &e : ifos) {
        const int n = int(qMin<quint32>((e.size + kSec - 1) / kSec, 2048));
        const QByteArray ifo = readSectors(f, e.lba, n);
        if (e.name == QLatin1String("VIDEO_TS.IFO")) {
            if (ifo.startsWith("DVDVIDEO-VMG"))
                parseMenus(f, ifo, e.lba, 0, QStringLiteral("VIDEO_TS.VOB"), out, seen);
        } else if (ifo.startsWith("DVDVIDEO-VTS")) {
            const int vts = e.name.mid(4, 2).toInt();
            parseMenus(f, ifo, e.lba, vts, QStringLiteral("VTS_%1_0.VOB").arg(vts, 2, 10, QLatin1Char('0')), out, seen);
        }
    }
    if (out.isEmpty() && error)
        *error = T("на диске нет меню");
    return out;
}

VideoInfo videoInfo(const QString &iso, const Menu &menu)
{
    VideoInfo v;
    v.size = QSize(720, 576);
    QFile f(iso);
    if (!f.open(QIODevice::ReadOnly) || menu.cells.isEmpty())
        return v;
    const int n = int(qMin<quint32>(menu.cells.first().second - menu.cells.first().first + 1, 64));
    const QByteArray b = readSectors(f, menu.cells.first().first, n);
    for (int i = 0; i + 8 < b.size(); ++i)
        if (be32(b, i) == 0x000001B3) {
            const int w = (at(b, i + 4) << 4) | (at(b, i + 5) >> 4);
            const int h = ((at(b, i + 5) & 0xF) << 8) | at(b, i + 6);
            if (w > 0 && h > 0)
                v.size = QSize(w, h);
            v.wide = (at(b, i + 7) >> 4) == 3;
            v.ntsc = (at(b, i + 7) & 0xF) == 4 || h == 480;
            break;
        }
    return v;
}

QImage frame(const QString &iso, const Menu &menu, QString *error)
{
    const VideoInfo vi = videoInfo(iso, menu);
    const QSize ds = vi.display();
    QFile f(iso);
    QTemporaryDir tmp;
    if (!f.open(QIODevice::ReadOnly) || !tmp.isValid())
        return QImage();
    // Первая ячейка меню (не больше 40 МБ) — во временный файл
    QFile part(tmp.filePath(QStringLiteral("menu.vob")));
    if (!part.open(QIODevice::WriteOnly))
        return QImage();
    const auto c = menu.cells.first();
    const int n = int(qMin<quint32>(c.second - c.first + 1, 20000));
    part.write(readSectors(f, c.first, n));
    part.close();
    const QString png = tmp.filePath(QStringLiteral("menu.png"));
    runFfmpeg({QStringLiteral("-i"), part.fileName(), QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-vf"),
               QStringLiteral("scale=%1:%2,setsar=1").arg(ds.width()).arg(ds.height()), png},
              error);
    QImage img(png);
    if (img.isNull()) {
        img = QImage(ds, QImage::Format_RGB32);
        img.fill(Qt::black);
    }
    return img.convertToFormat(QImage::Format_RGB32);
}

bool save(const QString &iso, const QString &outIso, const QList<QPair<Menu, QImage>> &changes,
          const std::function<void(int, const QString &)> &progress, QString *error)
{
    auto fail = [error](const QString &e) {
        if (error)
            *error = e;
        return false;
    };
    // 1. Копия образа
    {
        QFile in(iso), out(outIso);
        if (!in.open(QIODevice::ReadOnly))
            return fail(T("не удалось открыть файл"));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return fail(T("не удалось записать %1").arg(outIso));
        const qint64 total = in.size();
        QByteArray buf;
        qint64 done = 0;
        while (!(buf = in.read(8 * 1024 * 1024)).isEmpty()) {
            if (out.write(buf) != buf.size())
                return fail(T("не хватает места на диске"));
            done += buf.size();
            if (progress)
                progress(int(done * 60 / qMax<qint64>(1, total)), T("Копирую образ…"));
        }
    }
    QFile out(outIso);
    if (!out.open(QIODevice::ReadWrite))
        return fail(T("не удалось записать %1").arg(outIso));
    QTemporaryDir tmp;
    if (!tmp.isValid())
        return fail(QStringLiteral("temp"));

    // 2. Каждое меню: новая картинка кодируется в MPEG-2 и укладывается в те же видеопакеты
    for (int k = 0; k < changes.size(); ++k) {
        const Menu &m = changes[k].first;
        if (progress)
            progress(60 + k * 40 / qMax(1, changes.size()), T("Меню «%1»…").arg(m.title));
        const VideoInfo vi = videoInfo(iso, m);
        QByteArray buf = menuBytes(out, m);
        if (buf.isEmpty())
            return fail(T("меню «%1» не прочиталось").arg(m.title));
        QVector<Vobu> vobus = splitVobus(buf);
        const QString png = tmp.filePath(QStringLiteral("bg%1.png").arg(k));
        if (!changes[k].second.save(png))
            return fail(QStringLiteral("png"));
        QHash<QString, QByteArray> cache;   // «кадры-качество» → MPEG-2
        auto encode = [&](int frames, int q, QByteArray *es) -> bool {
            const QString key = QStringLiteral("%1-%2").arg(frames).arg(q);
            if (cache.contains(key)) {
                *es = cache[key];
                return true;
            }
            const QString m2v = tmp.filePath(QStringLiteral("m%1-%2.m2v").arg(k).arg(key));
            QString err;
            if (!runFfmpeg({QStringLiteral("-loop"), QStringLiteral("1"), QStringLiteral("-i"), png,
                            QStringLiteral("-frames:v"), QString::number(frames),
                            QStringLiteral("-vf"), QStringLiteral("scale=%1:%2,format=yuv420p").arg(vi.size.width()).arg(vi.size.height()),
                            QStringLiteral("-c:v"), QStringLiteral("mpeg2video"),
                            QStringLiteral("-r"), vi.ntsc ? QStringLiteral("30000/1001") : QStringLiteral("25"),
                            QStringLiteral("-aspect"), vi.wide ? QStringLiteral("16:9") : QStringLiteral("4:3"),
                            QStringLiteral("-g"), QString::number(qMax(1, frames)), QStringLiteral("-bf"), QStringLiteral("0"),
                            QStringLiteral("-flags"), QStringLiteral("+cgop"),
                            QStringLiteral("-sc_threshold"), QStringLiteral("1000000000"), QStringLiteral("-q:v"), QString::number(q),
                            QStringLiteral("-maxrate"), QStringLiteral("9000k"), QStringLiteral("-bufsize"), QStringLiteral("1835k"),
                            QStringLiteral("-f"), QStringLiteral("mpeg2video"), m2v},
                           &err)) {
                if (error)
                    *error = err;
                return false;
            }
            QFile f(m2v);
            if (!f.open(QIODevice::ReadOnly))
                return false;
            QByteArray b = f.readAll();
            if (b.endsWith(QByteArray::fromHex("000001B7")))
                b.chop(4);
            cache.insert(key, b);
            *es = b;
            return true;
        };
        // Наименьшее сжатие, при котором все VOBU помещаются
        int q = 2;
        for (;; q += (q < 8 ? 1 : 3)) {
            if (q > 31)
                return fail(T("новая картинка меню «%1» не помещается в место старой — упростите её (меньше мелких деталей)").arg(m.title));
            bool fits = true;
            for (const Vobu &v : vobus) {
                if (v.pictures == 0 || v.video.isEmpty())
                    continue;
                QByteArray es;
                if (!encode(v.pictures, q, &es))
                    return fail(T("ffmpeg не смог закодировать меню: %1").arg(error ? *error : QString()));
                // Первый кадр не больше буфера DVD-плеера (224 КБ), иначе бытовой плеер его не покажет
                int second = -1;
                for (int i = 4; i + 3 < es.size(); ++i)
                    if (at(es, i) == 0 && at(es, i + 1) == 0 && at(es, i + 2) == 1 && at(es, i + 3) == 0) {
                        if (second == -2) {
                            second = i;
                            break;
                        }
                        second = -2;
                    }
                const int firstPicture = second > 0 ? second : es.size();
                if (es.size() + (v.seqEnd ? 4 : 0) > v.capacity || firstPicture > 200000) {
                    fits = false;
                    break;
                }
            }
            if (fits)
                break;
        }
        // Запись: первый видеопакет VOBU сохраняет свою метку времени, у остальных метки убираем;
        // пакеты, на которые данных не хватило, становятся пакетами-заполнителями
        for (const Vobu &v : vobus) {
            if (v.pictures == 0 || v.video.isEmpty())
                continue;
            QByteArray es;
            encode(v.pictures, q, &es);
            if (v.seqEnd)
                es += QByteArray::fromHex("000001B7");
            int used = 0;
            for (int i = 0; i < v.video.size(); ++i) {
                const Packet &p = v.video[i];
                char *d = buf.data();
                if (used >= es.size() && i > 0) {
                    const int len = be16(buf, p.pos + 4);
                    d[p.pos + 3] = char(0xBE);
                    std::memset(d + p.pos + 6, 0xFF, size_t(len));
                    continue;
                }
                if (i > 0 && (at(buf, p.pos + 7) & 0x3F) == 0) {
                    d[p.pos + 7] = 0;
                    std::memset(d + p.pos + 9, 0xFF, size_t(p.payload - (p.pos + 9)));
                }
                const int take = qMin(p.length, es.size() - used);
                std::memcpy(d + p.payload, es.constData() + used, size_t(take));
                if (take < p.length)
                    std::memset(d + p.payload + take, 0, size_t(p.length - take));
                used += take;
            }
        }
        // Обратно в образ — те же секторы
        qint64 off = 0;
        for (const auto &c : m.cells) {
            const qint64 len = qint64(c.second - c.first + 1) * kSec;
            if (!out.seek(qint64(c.first) * kSec) || out.write(buf.constData() + off, len) != len)
                return fail(T("не удалось записать %1").arg(outIso));
            off += len;
        }
    }
    if (progress)
        progress(100, T("Готово"));
    return true;
}

} // namespace DvdImage
