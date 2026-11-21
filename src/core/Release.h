#pragma once
#include "Chapters.h"

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

// Сведения о релизе для «Менеджера Adult Empire»: из них собираются папка, NFO и обложки.
struct ReleasePerson {
    QString name;
    QString id;        // номер на сайте (для фото)
    QString photo;     // ссылка на фото, если нашлась
    bool director = false;
};

struct ReleaseInfo {
    QString pageUrl;
    QString sku;               // Empire SKU → uniqueid
    QString title;             // русское название (title / sorttitle)
    QString originalTitle;     // originaltitle / tagline
    QString plot;              // описание по-русски
    QString plotOriginal;
    QString edition;           // videoassettitle (пусто — не писать)
    QDate premiered;
    QString year;
    QString country;
    QString studio;
    QString director;
    double rating = 0;
    int votes = 0;
    QString ratingSource = QStringLiteral("Adult Empire");
    int runtimeMin = 0;
    QStringList genres;        // уже по-русски
    QStringList actresses;     // только актрисы, имена не переводятся
    QString setName;           // коллекция (серия)
    QString setOverview;
    QString frontUrl, backUrl, fanartUrl;
    QList<ReleasePerson> cast; // весь состав со страницы
    QList<Chapter> scenes;
};

namespace Release {
QString nfo(const ReleaseInfo &r);                 // текст .nfo (CRLF, как в генераторе)
QString safeName(const QString &s);                // без запрещённых в именах файлов символов
QString baseName(const ReleaseInfo &r);            // «Название (Год)»
QString actorFile(const QString &name);            // Имя_Фамилия
QString titleCaseRu(const QString &s);             // каждое слово с большой, кроме коротких предлогов
QString genreRu(const QString &english);           // жанр Adult Empire → русский (пусто, если нет в списке)
QString seriesSuggestion(const QString &title);    // «Название 3» → «Название»
int parseLengthMinutes(const QString &text);       // «2 hrs. 17 mins.» → 137
QDate parseDate(const QString &text);              // «Nov 17 2026»
bool fanartFromVideo(const QString &video, const QString &jpg);   // кадр из видео, если фан-арта нет

// Страна релиза = страна, где создана студия (Marc Dorcel → Франция), даже если снимали в другой стране.
// Сначала — выбранное вами раньше, потом встроенный справочник; пусто — студия неизвестна.
QString studioCountry(const QString &studio);
void rememberStudioCountry(const QString &studio, const QString &country);
QString countryFromAdjective(const QString &text);   // «an American pornographic film studio» → «США»

// Релиз, который ждёт скачивания видео: после загрузки видео переупаковывается в папку релиза
struct Pending { QString name, dir, chaptersMeta; bool lossless = true; };
void addPending(const QString &pageUrl, const Pending &p);
bool takePending(const QString &pageUrl, Pending *p);
}
