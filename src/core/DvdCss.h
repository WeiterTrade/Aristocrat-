#pragma once
#include <QString>
#include <QStringList>
#include <QPair>
#include <QVector>
#include <functional>

// Копирование DVD с защитой CSS через libdvdcss (та же библиотека, что в VLC и HandBrake).
// Библиотека подгружается при работе: tools/libdvdcss-2.dll (Windows) или libdvdcss.so.2 (Linux).
namespace DvdCss {

bool available();          // libdvdcss найдена и загружена
QString libraryPath();     // где найдена (для «Модули программы»)

// Что передать libdvdcss для диска с этой папкой VIDEO_TS: «E:» (Windows), /dev/sr0 (Linux), путь к .iso.
// Пусто — это не диск, а папка на компьютере (снять защиту не с чего).
QString deviceFor(const QString &videoTs);

// Скопировать с диска IFO/BUP и VOB выбранных наборов VTS («01», «02»…) в outVideoTs, снимая защиту.
// progress(процент) вызывается из рабочего потока; вернуть false — отмена.
// badSectors — число нечитаемых секторов (заполнены нулями: так устроены некоторые защиты дисков).
bool backup(const QString &device, const QStringList &vtsSets, const QString &outVideoTs,
            const std::function<bool(int)> &progress, QString *error, int *badSectors = nullptr);

// Фильм на диске по таблицам IFO (как в DVDFab и MakeMKV): какие ячейки диска он проигрывает.
// Защиты вроде Disney X-Project/ARccOS добавляют десятки «фальшивых» фильмов и битые сектора-ловушки:
// настоящий фильм проигрывает ячейки по порядку, фальшивые — вразнобой.
struct DiscTitle {
    int number = 0;        // номер фильма на диске (1…)
    int vts = 0;           // набор VTS_xx
    double seconds = 0;
    int chapters = 0;
    QStringList audio;     // языки дорожек: «ru», «en»…
    QStringList audioFormat; // «AC3 5.1»…
    QStringList subs;
    QVector<QPair<quint32, quint32>> cells;   // абсолютные сектора: первый, последний
    quint32 keySector = 0; // начало VOB фильма — по нему берётся ключ CSS
    qint64 bytes = 0;
    int jumps = 0;         // переходы назад/вразнобой между ячейками (у фальшивых много)
    bool main = false;     // вероятно, основной фильм
};

// Таблица фильмов диска. Читает только IFO (несколько секунд), сами VOB не трогает.
QList<DiscTitle> discTitles(const QString &device, const QString &videoTs, QString *error);

// Записать фильм (его ячейки по порядку) в один файл MPEG-PS со снятой защитой.
// Битые сектора пропускаются быстро, без многократных повторов.
// progress(процент, прочитано байт, пропущено секторов) — после каждого куска (до 512 КБ)
bool rip(const QString &device, const DiscTitle &title, const QString &outFile,
         const std::function<bool(int, qint64, int)> &progress, QString *error, int *badSectors = nullptr);

} // namespace DvdCss
