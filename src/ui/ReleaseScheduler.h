#pragma once
// Планировщик релизов Adult Empire: очередь ссылок → по одному: данные, перевод, серия, обложки, актрисы, видео
// (720p; ниже — улучшение до 720p), сцены → перенос в библиотеку: Библиотека/Студия (или «Остальные Студии»)/[Серия «…»]/Релиз.
// В конце — сигнал и список актрис: остаётся заполнить анкеты и записать на сервер.
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>

class ReleaseManagerDialog;
class QTableWidget;
class QVBoxLayout;
class QLineEdit;
class QLabel;
class QDateTimeEdit;

struct SchedItem {
    enum State { Waiting = 0, Scheduled, Running, Video, Moving, Done, Failed };
    int id = 0;
    QString url;
    QString title;            // название релиза (когда уже известно)
    State state = Waiting;
    QDateTime when;           // для «Запустить позже»
    QString dir;              // папка релиза в рабочей папке
    QString dest;             // где релиз в итоге (библиотека)
    QString message;
    bool notified = false;
};

class ReleaseScheduler : public QObject
{
    Q_OBJECT
public:
    static ReleaseScheduler *instance();

    // Подключение к программе (MainWindow): как попросить скачать видео релиза
    std::function<void(const QString &pageUrl)> requestDownload;

    const QList<SchedItem> &items() const { return m_items; }
    void add(const QString &url);
    void remove(int id);
    void startNow();                         // все ожидающие — сразу
    void startAt(const QDateTime &when);     // все ожидающие — в указанное время

    static QString libraryDir();             // куда выгружать (X:/XXX)
    static QString workDir();                // где собирается релиз
    static void setLibraryDir(const QString &d);
    static void setWorkDir(const QString &d);
    static QString stateName(const SchedItem &it);
    static QStringList actressesOf(const QString &releaseDir);   // из NFO

public slots:
    void onVideoDone(const QString &dir, bool ok);
    void onDownloadFailed(const QString &pageUrl, const QString &reason);

signals:
    void changed();
    void batchFinished(const QList<SchedItem> &done);   // очередь пройдена: сигнал + список для анкет

private:
    explicit ReleaseScheduler(QObject *parent = nullptr);
    void tick();
    void process(SchedItem &it);
    void moveToLibrary(int id);
    void finishItem(int id, bool ok, const QString &message);
    SchedItem *find(int id);
    void save();
    void load();

    QList<SchedItem> m_items;
    int m_nextId = 1;
    int m_current = 0;
    QPointer<ReleaseManagerDialog> m_dialog;
    QTimer m_timer;
};

// Вкладка «Планировщик» в менеджере
class SchedulerPage : public QWidget
{
    Q_OBJECT
public:
    explicit SchedulerPage(QWidget *parent = nullptr);

signals:
    void openServer(const QString &releaseDir);   // заполнить анкеты релиза

private:
    void addRow(const QString &text = QString());
    void refresh();

    QVBoxLayout *m_rows;
    QTableWidget *m_table;
    QLineEdit *m_lib, *m_work;
    QDateTimeEdit *m_when;
    QLabel *m_state;
};

namespace DoneSound {
void play();   // мелодия «готово», как у стиральной машины
}
