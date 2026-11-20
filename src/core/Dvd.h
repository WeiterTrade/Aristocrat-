#pragma once
#include <QList>
#include <QString>
#include <QStringList>

// Разбор папки DVD (VIDEO_TS): фильмы (наборы VTS_xx) и их файлы VOB.
struct DvdTitle {
    QString vts;          // «01»
    QStringList files;    // VTS_01_1.VOB, VTS_01_2.VOB…
    qint64 size = 0;
    QString input() const { return QStringLiteral("concat:") + files.join('|'); }
};

namespace Dvd {
QString videoTsDir(const QString &path);       // путь к VIDEO_TS (или пусто)
QString discName(const QString &videoTs);      // имя диска/папки
QList<DvdTitle> titles(const QString &videoTs); // по убыванию размера
QStringList drives();                           // подключённые диски и образы с VIDEO_TS
}
