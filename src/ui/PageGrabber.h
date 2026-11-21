#pragma once
#include "Frameless.h"

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>

class QWebEngineView;
class QLabel;
class QLineEdit;
class QCheckBox;
class QProgressBar;

// Сайты, для которых программа сама входит в аккаунт и находит видео (без окна браузера).
namespace Sites {
bool needsLogin(const QUrl &url);           // сайт с видео по подписке (Adult Empire)
QString siteName(const QUrl &url);
QUrl loginUrl(const QUrl &url);
QUrl videoPageUrl(const QUrl &url);         // для Adult Empire — страница с плеером (?viewpart=videoplayer)
bool isLoggedIn(const QUrl &url);
void setLoggedIn(const QUrl &url, bool on);
}

// «Невидимый браузер»: открывает страницу, при необходимости входит в аккаунт,
// подтверждает возраст 18+, запускает плеер и перехватывает поток видео (m3u8/mpd/mp4).
class PageGrabber : public QObject
{
    Q_OBJECT
public:
    explicit PageGrabber(QObject *parent = nullptr);
    ~PageGrabber() override;
    void start(const QUrl &page, const QString &user = QString(), const QString &password = QString());
    QWebEngineView *view() const { return m_view; }

signals:
    void progress(const QString &text);
    void finished(const QUrl &media, const QStringList &headers, const QString &title, const QString &thumb);
    void failed(const QString &reason, bool loginFailed);

private:
    enum class Step { Idle, LoginPage, LoginSubmitted, VideoPage, Done };
    void onLoadFinished(bool ok);
    void onFound(const QUrl &url, const QString &kind, const QUrl &page);
    void tryLogin();
    void checkLogin();
    void openVideoPage();
    void poke();          // подтвердить 18+, нажать «воспроизвести»
    void finish();
    void fail(const QString &reason, bool login = false);

    QWebEngineView *m_view;
    QTimer m_poke;
    QTimer m_timeout;
    QTimer m_settle;
    Step m_step = Step::Idle;
    QUrl m_target;
    QString m_user, m_pass;
    int m_loginTries = 0;
    int m_pokes = 0;
    bool m_vodTried = false;
    struct Found { QUrl url; QString kind; QUrl page; };
    QList<Found> m_found;
};

// Окно входа в аккаунт сайта
class SiteLoginDialog : public FramedDialog
{
    Q_OBJECT
public:
    SiteLoginDialog(const QUrl &url, const QString &error, QWidget *parent = nullptr);
    QString user() const;
    QString password() const;
    bool skipLogin() const { return m_skip; }

private:
    QLineEdit *m_user;
    QLineEdit *m_pass;
    QCheckBox *m_adult;
    bool m_skip = false;
};

// Окно хода поиска видео: «Вход…», «Подтверждение 18+…», «Поиск видео…»
class GrabDialog : public FramedDialog
{
    Q_OBJECT
public:
    GrabDialog(const QUrl &page, const QString &user, const QString &pass, QWidget *parent = nullptr);
    QUrl media() const { return m_media; }
    QStringList headers() const { return m_headers; }
    QString pageTitle() const { return m_title; }
    QString thumb() const { return m_thumb; }
    bool loginFailed() const { return m_loginFailed; }
    QString error() const { return m_error; }

private:
    PageGrabber *m_grabber;
    QLabel *m_state;
    QUrl m_media;
    QStringList m_headers;
    QString m_title, m_thumb, m_error;
    bool m_loginFailed = false;
};
