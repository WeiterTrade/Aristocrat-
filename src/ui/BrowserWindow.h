#pragma once
#include <QNetworkCookie>
#include <QUrl>
#include <QWebEngineUrlRequestInterceptor>
#include "Frameless.h"

class QLineEdit;
class QLabel;
class QComboBox;
class QPushButton;
class QToolButton;
class QWebEngineView;
class QWebEngineProfile;
struct DownloadItem;

// Найденный на странице поток видео
struct SniffedMedia {
    QUrl url;
    QString kind;      // HLS, DASH, MP4…
    QUrl page;         // страница, на которой найден
    qint64 order = 0;
};

// Перехват сетевых запросов встроенного браузера — как это делает Download Helper:
// всё, что похоже на видео (плейлисты .m3u8/.mpd, файлы видео, запросы тега <video>), запоминается.
class MediaSniffer : public QWebEngineUrlRequestInterceptor
{
    Q_OBJECT
public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;
    void interceptRequest(QWebEngineUrlRequestInfo &info) override;

signals:
    void found(const QUrl &url, const QString &kind, const QUrl &page);
};

// Окно встроенного браузера: адресная строка, страница и панель «Скачать» внизу.
class BrowserWindow : public FramedDialog
{
    Q_OBJECT
public:
    explicit BrowserWindow(QWidget *parent = nullptr);
    void open(const QUrl &url);

    static QWebEngineProfile *profile();
    static MediaSniffer *sniffer();
    // Заголовки, с которыми браузер запрашивал поток: Referer, Origin, User-Agent, Cookie
    static QStringList headersFor(const QUrl &media, const QUrl &page);

signals:
    // Пользователь нажал «Скачать»: поток, заголовки (Referer, Cookie, User-Agent), название страницы
    void downloadRequested(const QUrl &media, const QStringList &headers, const QString &title,
                           const QString &site, const QString &thumb, const QString &kind);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void onFound(const QUrl &url, const QString &kind, const QUrl &page);
    void updatePanel();
    void startDownload();

    QWebEngineView *m_view;
    QLineEdit *m_address;
    QLabel *m_found;
    QComboBox *m_list;
    QPushButton *m_download;
    QList<SniffedMedia> m_media;
    qint64 m_counter = 0;
};
