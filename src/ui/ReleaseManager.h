#pragma once
#include "Frameless.h"
#include "core/Release.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QNetworkAccessManager;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QTableWidget;
class QTabWidget;
class CloudUploadPage;
class ServerPage;
class IafdChecker;
class ImageSearch;
class QWebEngineView;

// Чтение страницы релиза Adult Empire в скрытом окне: название, описание, студия, режиссёр,
// состав, жанры, обложки, сцены.
class ReleaseFetcher : public QObject
{
    Q_OBJECT
public:
    explicit ReleaseFetcher(QObject *parent = nullptr);
    ~ReleaseFetcher() override;
    void start(const QUrl &page);
    static QString script();

signals:
    void progress(const QString &text);
    void finished(const QJsonObject &data);
    void failed(const QString &reason);

private:
    void extract();
    void galleryStep(int step);   // задняя обложка: открыть галерею обложек и пролистать
    // Кадры для фан-арта: «Больше скриншотов» у первых 4 сцен, по 4 первых кадра
    void shotsStep(int i);
    void collectShots(int i, int attempt);
    void finishShots();
    QJsonArray m_shotButtons;
    QStringList m_shotList;
    bool m_shotsMode = false;
    QWebEngineView *m_view;
    QJsonObject m_data;
    QTimer m_timeout;
    int m_tries = 0;
    bool m_done = false;
};

// «Adult Empire Менеджер»: по ссылке на релиз создаёт папку с NFO, постером, задней обложкой,
// фан-артом, фото актрис и видео с нужным именем. Перед созданием всё можно проверить и исправить.
class ReleaseManagerDialog : public FramedDialog
{
    Q_OBJECT
public:
    ReleaseManagerDialog(const QString &pageUrl = QString(), const QString &video = QString(), QWidget *parent = nullptr,
                         const QString &scenesUrl = QString());

    void showUploadTab();
    void showServerTab();

signals:
    // Видео отдаётся в раздел «Конвертировать»: своё имя, своя папка, главы
    void videoRequested(const QString &input, const QString &name, const QString &dir, bool lossless,
                        const QString &chaptersMeta);
    // Видео нужно скачать: раздел «Загрузить» (вход в аккаунт, выбор качества), потом — в папку релиза
    void downloadRequested(const QString &pageUrl);

private:
    void fetch();
    void fetchScenes(const QString &url);
    void setScenes(const QList<Chapter> &scenes);
    void applyStudioCountry(const QString &studio);
    void chooseFanart();
    // Фан-арт из кадра: без чёрных рамок, 16:9, увеличение до 3840×2160 (Real-ESRGAN, если есть)
    void makeFanart(const QImage &src, const QString &file, std::function<void(bool)> done);
    void setCastGender(QListWidgetItem *it, int gender, int source);   // отметка и значок ♀/♂
    void apply(const QJsonObject &d);
    void translate();
    void loadPreview(QLabel *target, const QString &url);
    void updateTarget();
    ReleaseInfo collect() const;
    void create();
    void runJobs();
    void finishCreate();
    QString releaseDir() const;
    void setBusy(bool busy, const QString &text = QString());

    QTabWidget *m_tabs;
    CloudUploadPage *m_upload;
    ServerPage *m_server;
    QLineEdit *m_url;
    QPushButton *m_fetch;
    QLabel *m_state;

    QLineEdit *m_title, *m_orig, *m_edition, *m_date, *m_year, *m_studio, *m_director, *m_rating, *m_runtime, *m_uid;
    QCheckBox *m_useEdition, *m_caps, *m_useSet;
    QComboBox *m_country;
    QPlainTextEdit *m_plot, *m_plotOrig;
    QLineEdit *m_setName;
    QPlainTextEdit *m_setOverview;
    QListWidget *m_genres, *m_cast;
    QLabel *m_castNote;
    QCheckBox *m_autoPhotos;
    ImageSearch *m_imgSearch = nullptr;
    IafdChecker *m_iafd = nullptr;
    QLineEdit *m_newGenre;
    QLineEdit *m_frontUrl, *m_backUrl, *m_fanartUrl;
    QLabel *m_frontPrev, *m_backPrev, *m_fanartPrev;
    QTableWidget *m_scenes;
    QStringList m_shots;          // кадры сцен со страницы релиза — варианты фан-арта
    QCheckBox *m_fanart4k;
    QCheckBox *m_useScenes;
    QLineEdit *m_scenesUrl;
    QPushButton *m_scenesFetch;
    QCheckBox *m_downloadVideo;

    QLineEdit *m_base, *m_video;
    QRadioButton *m_lossless, *m_convert;
    QLabel *m_target;
    QPushButton *m_create, *m_openDir;

    QString m_studioName, m_directorFound;
    struct Job {
        QString url, file;
        QStringList fallbacks;
        bool placeholder = false;
        QString search;        // найти фото актрисы (1000×1500) по этому имени
        bool portrait = false; // подогнать под 1000×1500 и запомнить
        QString name, libRoot; // актриса и корень медиатеки (поиск фото в своих релизах)
        bool server = false;   // сначала взять фото и анкету с сервера Jellyfin
        bool local = false;    // затем — выбранное раньше или из своей медиатеки
        bool fanart = false;   // кадр для фан-арта: рамки, 16:9, 4K
    };
    QNetworkAccessManager *m_jnet = nullptr;
    QList<Job> m_jobs;
    QStringList m_errors;
    QString m_createdDir;
};
