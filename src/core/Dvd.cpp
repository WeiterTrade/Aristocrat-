#include "Dvd.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QStorageInfo>
#include <algorithm>

namespace Dvd {

QString videoTsDir(const QString &path)
{
    QDir d(path);
    if (d.dirName().compare(QLatin1String("VIDEO_TS"), Qt::CaseInsensitive) == 0)
        return d.absolutePath();
    for (const QString &n : {QStringLiteral("VIDEO_TS"), QStringLiteral("video_ts")})
        if (d.exists(n))
            return d.absoluteFilePath(n);
    return {};
}

QString discName(const QString &videoTs)
{
    QDir up(videoTs);
    up.cdUp();
    const QStorageInfo si(up.absolutePath());
    if (si.isValid() && si.rootPath() == up.absolutePath() && !si.name().isEmpty())
        return si.name();
    return up.dirName().isEmpty() ? QStringLiteral("DVD") : up.dirName();
}

QList<DvdTitle> titles(const QString &videoTs)
{
    QMap<QString, DvdTitle> map;
    QDir d(videoTs);
    for (const QFileInfo &fi : d.entryInfoList({"VTS_*_*.VOB", "vts_*_*.vob"}, QDir::Files, QDir::Name)) {
        const QStringList p = fi.completeBaseName().split('_');
        if (p.size() < 3 || p[2] == QLatin1String("0"))
            continue;   // VTS_xx_0 — меню
        DvdTitle &t = map[p[1]];
        t.vts = p[1];
        t.files << fi.absoluteFilePath();
        t.size += fi.size();
    }
    QList<DvdTitle> list = map.values();
    for (DvdTitle &t : list)
        t.files.sort();
    std::sort(list.begin(), list.end(), [](const DvdTitle &a, const DvdTitle &b) { return a.size > b.size; });
    return list;
}

QStringList drives()
{
    QStringList out;
    for (const QStorageInfo &si : QStorageInfo::mountedVolumes()) {
        if (!si.isValid() || !si.isReady())
            continue;
        const QString v = videoTsDir(si.rootPath());
        if (!v.isEmpty())
            out << v;
    }
    return out;
}

} // namespace Dvd
