#pragma once
#include "Frameless.h"
#include "core/Release.h"

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QUrl>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QTableWidget;
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
    QWebEngineView *m_view;
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
    ReleaseManagerDialog(const QString &pageUrl = QString(), const QString &video = QString(), QWidget *parent = nullptr);

signals:
    // Видео отдаётся в раздел «Конвертировать»: своё имя, своя папка, главы
    void videoRequested(const QString &input, const QString &name, const QString &dir, bool lossless,
                        const QString &chaptersMeta);

private:
    void fetch();
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
    QLineEdit *m_newGenre;
    QLineEdit *m_frontUrl, *m_backUrl, *m_fanartUrl;
    QLabel *m_frontPrev, *m_backPrev, *m_fanartPrev;
    QTableWidget *m_scenes;
    QCheckBox *m_useScenes;

    QLineEdit *m_base, *m_video;
    QRadioButton *m_lossless, *m_convert;
    QLabel *m_target;
    QPushButton *m_create, *m_openDir;

    QString m_studioName, m_directorFound;
    struct Job { QString url, file, fallbackUrl; bool placeholder = false; };
    QList<Job> m_jobs;
    QStringList m_errors;
    QString m_createdDir;
};
