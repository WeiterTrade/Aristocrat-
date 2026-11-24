#pragma once
#include <QImage>
#include <QList>
#include <QPair>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>
#include <functional>

// Образ DVD-Video (.iso без защиты — например, сделанный «Копированием DVD»): меню диска и их правка.
// Меню заменяется «на месте», как в VobBlanker/DVDReMake: новое изображение кодируется в MPEG-2 так, чтобы
// уместиться в те же видеопакеты меню. Секторы не сдвигаются, поэтому кнопки, переходы, звук, подсветка
// кнопок и вся навигация диска остаются как были.
namespace DvdImage {

struct Button {
    QRect rect;       // в пикселях кадра (720×576 / 720×480)
    int up = 0, down = 0, left = 0, right = 0;
};

struct Menu {
    QString title;        // «Главное меню», «Меню сцен (VTS 2)»…
    QString vob;          // VIDEO_TS.VOB или VTS_02_0.VOB
    int vts = 0;          // 0 — меню диска (VMGM)
    QString lang;         // язык блока меню («en», «ru»…)
    QVector<QPair<quint32, quint32>> cells;   // абсолютные секторы образа
    double seconds = 0;
    bool still = false;   // неподвижное меню (одна картинка)
    QList<Button> buttons;
};

struct VideoInfo {
    QSize size;           // кадр в пикселях хранения (720×576)
    bool wide = false;    // 16:9
    bool ntsc = false;
    QSize display() const { return QSize(size.height() * (wide ? 16 : 4) / (wide ? 9 : 3), size.height()); }
};

// Список меню образа. error — что не так (не DVD-Video, нет меню…)
QList<Menu> menus(const QString &iso, QString *error);

// Видеопараметры меню (по заголовку последовательности MPEG-2)
VideoInfo videoInfo(const QString &iso, const Menu &menu);

// Кадр фона меню в пропорциях экрана (16:9 → 1024×576). Нужен ffmpeg.
QImage frame(const QString &iso, const Menu &menu, QString *error);

// Записать копию образа outIso, где у перечисленных меню фон заменён картинками (в пропорциях экрана).
// progress(процент, что делается)
bool save(const QString &iso, const QString &outIso, const QList<QPair<Menu, QImage>> &changes,
          const std::function<void(int, const QString &)> &progress, QString *error);

} // namespace DvdImage
