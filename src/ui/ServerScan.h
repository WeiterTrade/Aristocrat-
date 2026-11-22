#pragma once
#include "Frameless.h"

#include <QJsonObject>
#include <QList>

class QCheckBox;
class QLabel;
class QNetworkAccessManager;
class QProgressBar;
class QPushButton;
class QTableWidget;
class IafdBio;
class ImageSearch;
class ReleaseFetcher;

// «Пробежаться по серверу»: найти на сервере Jellyfin актрис без биографии или фото и фильмы
// с неполными данными (описание, жанры, студия, актрисы, постер, задняя обложка, фан-арт)
// и дописать недостающее. Заполненное раньше не меняется.
class ServerScanDialog : public FramedDialog
{
public:
    explicit ServerScanDialog(QWidget *parent = nullptr);

private:
    struct Entry {
        bool movie = false;
        bool director = false;      // режиссёр: общая картинка
        QString id, name, original;
        int year = 0;
        QJsonObject item;           // карточка из списка
        bool needBio = false, needPhoto = false;
        bool needOverview = false, needGenres = false, needStudio = false, needPeople = false, needCountry = false;
        bool needPoster = false, needBack = false, needFanart = false;
        QString sku;
    };
    void scan();
    void scanMovies();
    void showList();
    void start();
    void next();
    void finishEntry(int row, const QString &status);
    void doPerson(int row);
    void personPhoto(int row);
    void doMovie(int row);
    void movieFromPage(int row, const QJsonObject &d);
    void movieImages(int row, const QJsonObject &d);
    void uploadImage(const QString &id, const QString &type, const QStringList &urls, std::function<void(bool)> done);
    QString missingText(const Entry &e) const;
    void setBusy(bool busy, const QString &text = QString());

    QCheckBox *m_bios, *m_photos, *m_movieData, *m_movieArt;
    QPushButton *m_scan, *m_go, *m_stop;
    QTableWidget *m_table;
    QProgressBar *m_progress;
    QLabel *m_state;

    QNetworkAccessManager *m_net;
    IafdBio *m_iafd = nullptr;
    ImageSearch *m_img = nullptr;
    QList<Entry> m_entries;
    QList<int> m_queue;
    int m_total = 0;
    bool m_stopRequested = false;
};
