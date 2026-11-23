#pragma once
#include <QString>
#include <QStringList>
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

} // namespace DvdCss
