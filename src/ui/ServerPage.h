#pragma once
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QWidget>
#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QTableWidget;
class QWebEngineView;

// Данные исполнителя с IAFD: дата и место рождения, псевдонимы, годы, число фильмов
class IafdBio : public QObject
{
public:
    using Callback = std::function<void(const QJsonObject &bio)>;
    explicit IafdBio(QObject *parent = nullptr);
    ~IafdBio() override;
    void fetch(const QString &name, Callback cb);
    static QString searchUrl(const QString &name);

private:
    void step(int stage);
    void done(const QJsonObject &o);
    QWebEngineView *m_view;
    QTimer m_timeout;
    Callback m_cb;
    QString m_name;
    int m_stage = 0, m_serial = 0, m_attempt = 0;
};

// Анкета по шаблону (как в образце): русский текст, латинское имя в скобках
namespace Bio {
QString monthDate(const QDate &d);                  // «9 ноября 1992»
QString build(const QJsonObject &iafd, const QString &ruName, const QString &bornIn);   // три абзаца
QString transliterate(const QString &latin);        // запасной вариант русского написания имени
}

// Вкладка «Сервер»: анкеты актрис на сервере Jellyfin (биография, дата и место рождения, фото)
class ServerPage : public QWidget
{
    Q_OBJECT
public:
    explicit ServerPage(QWidget *parent = nullptr);
    void setRelease(const QString &dir);

private:
    enum Col { ColCheck = 0, ColName, ColStatus, ColRuName, ColBorn, ColPlace, ColTags, ColBio, ColCount };

    QNetworkReply *api(const QString &method, const QString &path, const QByteArray &body = QByteArray(),
                       const QByteArray &type = "application/json");
    QByteArray authHeader() const;
    bool loggedIn() const;
    void login();
    void logout();
    void updateLogin();
    void loadList();
    void lookupNext();
    void prepareRow(int row, const QJsonObject &bio);
    void writeAll();
    void writeNext();
    void editBio(int row);
    void setBusy(bool busy, const QString &text = QString());

    QLineEdit *m_url, *m_user, *m_pass;
    QPushButton *m_login;
    QLabel *m_loginState;
    QComboBox *m_source;
    QLineEdit *m_release;
    QPushButton *m_load, *m_write;
    QCheckBox *m_photos;
    QTableWidget *m_table;
    QLabel *m_state;

    QNetworkAccessManager *m_net;
    IafdBio *m_iafd = nullptr;
    QList<int> m_queue;
    bool m_auto = false;
};
