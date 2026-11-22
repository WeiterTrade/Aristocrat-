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
class ImageSearch;
class QWebEngineView;

// Данные исполнителя из бесплатных баз (IAFD, Babepedia, FreeOnes, Boobpedia, Википедия)
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
    void nextSource();
    void merge(const QJsonObject &o);
    void done(const QJsonObject &o);
    QList<QPair<QString, QString>> m_sources;   // имя источника, адрес
    QJsonObject m_merged;
    QStringList m_found, m_aka;
    int m_index = 0;
    QWebEngineView *m_view;
    QTimer m_timeout;
    Callback m_cb;
    QString m_name;
    int m_stage = 0, m_serial = 0, m_attempt = 0;
};

// Сервер Jellyfin для других частей программы (вход берётся из вкладки «Сервер»)
namespace Jellyfin {
bool loggedIn();
QNetworkReply *request(QNetworkAccessManager *net, const QString &method, const QString &path,
                       const QByteArray &body = QByteArray(), const QByteArray &type = "application/json");
// Исполнитель по точному имени (Id, Overview, PremiereDate, ProductionLocations, ImageTags); пусто — не найден
void findPerson(QNetworkAccessManager *net, QObject *ctx, const QString &name, std::function<void(const QJsonObject &)> cb);
void photo(QNetworkAccessManager *net, QObject *ctx, const QString &id, std::function<void(const QByteArray &)> cb);
// Режиссёру — общая картинка «Фотографии режиссёров не добавляются», если у него нет фото и он не снимается
// как актриса в фильмах библиотеки. Результат: 1 — загружено, 0 — не нужно (нет на сервере, есть фото, актриса), -1 — ошибка.
// Включить «Заблокировать» (LockData) у элемента, чтобы обновление метаданных его не переписывало
void lockItem(QNetworkAccessManager *net, QObject *ctx, const QString &id, std::function<void()> done);
void directorPhoto(QNetworkAccessManager *net, QObject *ctx, const QString &name, std::function<void(int)> cb);
}

// Готовая анкета из собранных данных: по шаблону, а если данных нет — нейросетью (если указан ключ).
// Результат: ruName, born (ГГГГ-ММ-ДД), place, aliases[], text, via; пустой text — анкету собрать не удалось.
void composeBio(QNetworkAccessManager *net, QObject *ctx, const QString &name, const QJsonObject &data,
                std::function<void(const QJsonObject &)> cb);

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
    bool loggedIn() const;
    void login();
    void logout();
    void updateLogin();
    void loadList();
    void lookupNext();
    void prepareRow(int row, const QJsonObject &bio);
    void aiRow(int row, const QJsonObject &bio);          // анкета через нейросеть (Claude или Qwen)
    void fillRow(int row, const QString &ruName, const QString &born, const QString &place,
                 const QStringList &aliases, const QString &text, const QString &status);
    void aiSettings();
    void keepOffline(int row, bool thenNext);
    // Изображения релиза на сервер: задняя обложка (Box rear), постер и фан-арт, если их нет, и фото актрис из .actors —
    // Jellyfin сам их не подхватывает
    void pushReleaseImages(int attempt = 0);   // фото актрисы с сервера → .actors релиза
    void writeAll();
    void writeNext();
    void editBio(int row);
    void editActress(int row);              // окно анкеты: вставка биографии и фото, запись на сервер
    void searchPhoto(int row);
    void setBusy(bool busy, const QString &text = QString());

    QLineEdit *m_url, *m_user, *m_pass;
    QPushButton *m_login;
    QLabel *m_loginState;
    QComboBox *m_source;
    QLineEdit *m_release;
    QPushButton *m_load, *m_write;
    QCheckBox *m_photos;
    QCheckBox *m_autoBio;
    QLabel *m_aiState;
    QTableWidget *m_table;
    QLabel *m_state;

    QNetworkAccessManager *m_net;
    IafdBio *m_iafd = nullptr;
    QList<int> m_queue;
    bool m_auto = false;
    bool m_imagesAfterWrite = false;
    bool m_single = false;
    ImageSearch *m_img = nullptr;
};
