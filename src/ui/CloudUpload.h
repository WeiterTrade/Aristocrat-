#pragma once
#include <QPointer>
#include <QProcess>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;

// Вкладка «Выгрузить» Менеджера Adult Empire: папка релиза целиком отправляется в облако
// (Google Диск, Яндекс Диск, Облако Mail.ru, OneDrive — через rclone) или в любую папку на компьютере,
// в том числе на подключённый сетевой/облачный диск. Папку назначения выбирают в списке папок.
class CloudUploadPage : public QWidget
{
    Q_OBJECT
public:
    explicit CloudUploadPage(QWidget *parent = nullptr);
    void setRelease(const QString &dir);
    bool isBusy() const;

private:
    struct Provider {
        QString id;     // имя подключения в rclone: aristo-<id>
        QString type;   // тип rclone (drive, yandex, mailru, onedrive) или пусто — папка на компьютере
        QString name;
    };
    const Provider &current() const;
    bool isLocal() const { return current().type.isEmpty(); }
    QString remote() const { return QStringLiteral("aristo-") + current().id; }
    QString remotePath(const QString &path) const;
    static QString configFile();
    QStringList rc(const QStringList &args) const;   // аргументы + --config
    bool isConnected() const;

    void providerChanged();
    void connectCloud();
    void disconnectCloud();
    void listFolders();
    void enter(const QString &name);
    void goUp();
    void newFolder();
    void upload();
    void updateDest();
    void setBusy(bool busy, const QString &text = QString());

    QComboBox *m_provider;
    QLabel *m_connState;
    QPushButton *m_connect;
    QPushButton *m_up, *m_refresh, *m_mkdir;
    QLabel *m_path;
    QListWidget *m_list;
    QLineEdit *m_release;
    QCheckBox *m_withSeries;
    QLabel *m_dest;
    QProgressBar *m_progress;
    QLabel *m_state;
    QPushButton *m_cancel, *m_upload;

    QList<Provider> m_providers;
    QString m_cwd;                       // текущая папка: путь в облаке или на компьютере
    QPointer<QProcess> m_proc;
};
