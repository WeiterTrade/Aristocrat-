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
    bool fake = false;     // проверкой найдена ячейка-ловушка — ложный фильм защиты
};

// Таблица фильмов диска. Читает только IFO (несколько секунд), сами VOB не трогает.
QList<DiscTitle> discTitles(const QString &device, const QString &videoTs, QString *error);

// Защита вроде Disney X-Project: десятки фильмов одной длины, в ложных — ячейки с битыми секторами.
// Проверка чтением одного сектора из каждой «спорной» ячейки (как в DVDFab): основной — первый без ловушек.
// Возвращает найденные ячейки-ловушки (абсолютные сектора). status(проверено, всего) — ход проверки.
using Cells = QVector<QPair<quint32, quint32>>;
Cells probeTraps(const QString &device, QList<DiscTitle> &titles, const std::function<void(int, int)> &status);

// Записать фильм (его ячейки по порядку) в один файл MPEG-PS со снятой защитой.
// Битые сектора пропускаются быстро, без многократных повторов.
// progress(процент, прочитано байт, нечитаемых секторов) — после каждого куска (до 512 КБ).
// maxSpeed — скорость привода (кратность DVD), 0 — максимальная; при сбоях снижается сама до 2x.
bool rip(const QString &device, const DiscTitle &title, const QString &outFile, int maxSpeed,
         const std::function<bool(int, qint64, int)> &progress, QString *error, int *badSectors = nullptr);

// Полная копия диска в ISO: меню, все фильмы, дорожки, субтитры, бонусы — со снятой защитой CSS.
// Нечитаемые сектора-ловушки записываются нулями (структура диска сохраняется).
// titles/traps — из discTitles/probeTraps: ячейки основного фильма читаются с повторами, ловушки не читаются,
// остальное — без долгих повторов (сбой → нули).
bool isoBackup(const QString &device, const QString &outIso, int maxSpeed, const QList<DiscTitle> &titles, const Cells &traps,
               const std::function<bool(int, qint64, int)> &progress, QString *error, int *badSectors = nullptr);

// Приводы DVD: «E:» (Windows) или /dev/sr0 (Linux)
QStringList opticalDrives();
// Регион привода: region 0 — не задан; changesLeft — сколько смен осталось (-1 — неизвестно)
struct DriveRegion {
    bool ok = false;
    int region = 0;
    int changesLeft = -1;
};
DriveRegion driveRegion(const QString &device);
// Задать регион привода (1–8). Тратит одну из ограниченных смен. В Linux нужны права администратора.
bool setDriveRegion(const QString &device, int region, QString *error);

// Регионы: «диск: 2 · привод: 5 (смен осталось 4)». Пусто — не удалось узнать (только Windows).
QString regionInfo(const QString &device);

// Журнал копирования DVD (сбои чтения, сторож, скорость) — для разбора проблемных дисков
QString logPath();

} // namespace DvdCss
