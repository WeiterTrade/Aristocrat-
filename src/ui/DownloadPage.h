#pragma once
#include "Frameless.h"

#include <QFrame>
#include <QJsonObject>
#include <QHash>
#include <QImage>
#include <QProcess>
#include <QSharedPointer>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QToolButton;
class QComboBox;
class QCheckBox;
class QVBoxLayout;
class QStackedWidget;
class QListWidget;
class QRadioButton;
class QButtonGroup;
class QPlainTextEdit;

// Одна загрузка
struct DownloadItem {
    enum Status { Queued, Downloading, Paused, Done, Failed };
    int id = 0;
    QString url;
    QString title;
    QString site;
    double duration = 0;
    QImage thumb;
    QString thumbUrl;
    int height = 0;            // 0 = лучшее качество
    bool audioOnly = false;
    Status status = Queued;
    double progress = 0;
    qint64 total = 0;
    qint64 done = 0;
    double speed = 0;          // байт/с
    int eta = -1;
    QString filePath;
    QString error;
    QString log;               // вывод yt-dlp (для подробностей ошибки)
    bool pausing = false;
    bool noCookies = false;    // вход из браузера не удался — качаем без него
    bool external = false;     // пришло из расширения браузера
    QStringList headers;       // заголовки запроса из браузера (Referer, Cookie…)
    QString pageUrl;           // страница фильма (для глав)
};
using DownloadPtr = QSharedPointer<DownloadItem>;

// Окно выбора качества после анализа ссылки.
class AnalyzeDialog : public FramedDialog
{
    Q_OBJECT
public:
    AnalyzeDialog(const QString &url, const QStringList &baseArgs, const QStringList &cookieArgs, QWidget *parent = nullptr,
                  const QStringList &headers = QStringList());   // headers — заголовки из встроенного браузера
    ~AnalyzeDialog() override;
    DownloadPtr result() const { return m_item; }

private:
    void run(bool withCookies);
    void onAnalyzed(int code);

    DownloadPtr m_item;
    QProcess *m_proc;
    QStringList m_base;
    QStringList m_cookies;
    QStringList m_headers;
    QPushButton *m_browserBtn = nullptr;
    bool m_usedCookies = false;
    QPlainTextEdit *m_details;
    QLabel *m_thumb;
    QLabel *m_title;
    QLabel *m_state;
    QVBoxLayout *m_options;
    QButtonGroup *m_group;
    QPushButton *m_ok;
};

class DownloadCard : public QFrame
{
    Q_OBJECT
public:
    explicit DownloadCard(const DownloadPtr &item, QWidget *parent = nullptr);
    void refresh();
    DownloadPtr item() const { return m_item; }

signals:
    void pauseResume(const DownloadPtr &item);
    void removeRequested(const DownloadPtr &item);
    void convertRequested(const QString &file);
    void chaptersRequested(const QString &file, const QString &pageUrl);
    void releaseRequested(const QString &file, const QString &pageUrl);

private:
    DownloadPtr m_item;
    QPushButton *m_chapters;
    QPushButton *m_release;
    QLabel *m_thumb;
    QLabel *m_title;
    QLabel *m_meta;
    QProgressBar *m_progress;
    QLabel *m_state;
    QToolButton *m_pause;
    QPushButton *m_open;
    QPushButton *m_convert;
};

// Раздел «Загрузить»: ссылки на YouTube, Rutube, VK, Одноклассники, Mail.ru и прямые ссылки на файлы/потоки.
class DownloadPage : public QWidget
{
    Q_OBJECT
public:
    explicit DownloadPage(QWidget *parent = nullptr);
    bool isBusy() const { return !m_procs.isEmpty(); }
    static QStringList cookieArgs(const QString &browser);
    void reloadSettings();
    void addExternal(const QJsonObject &media);   // видео из расширения браузера
    void showExtensionHelp();
    void openBrowser(const QString &url = QString());
    void grabAndAdd(const QString &url, bool loginSite);   // найти видео на странице (с входом в аккаунт)

signals:
    void convertRequested(const QString &file, bool autoStart);
    void releaseRequested(const QString &file, const QString &pageUrl);
    void chaptersConvert(const QString &input, const QString &name, const QString &dir, const QString &chaptersMeta);

private:
    QWidget *buildSubBar();
    QWidget *buildBottomBar();
    void pasteFromClipboard();
    void enterLinks();
    void addUrl(const QString &url);
    void addItem(const DownloadPtr &item);
    void startNext();
    void startItem(const DownloadPtr &item);
    void pauseResume(const DownloadPtr &item);
    void removeItem(const DownloadPtr &item);
    void onOutput(const DownloadPtr &item);
    void onFinished(const DownloadPtr &item, int code, QProcess::ExitStatus st);
    void saveQueue() const;
    void loadQueue();
    void refreshCard(const DownloadPtr &item);
    void updateView();
    void loadThumb(const DownloadPtr &item);
    QStringList baseArgs(bool withCookies = true) const;
    QString outputDir() const;

    QList<DownloadPtr> m_items;
    QHash<int, DownloadCard *> m_cards;
    int m_nextId = 1;
    QHash<int, QProcess *> m_procs;

    QStackedWidget *m_stack;
    QVBoxLayout *m_cardLayout;
    QVBoxLayout *m_doneLayout;
    QPushButton *m_segActive;
    QPushButton *m_segDone;
    QCheckBox *m_convertMode;
    QToolButton *m_fast;
    QComboBox *m_browser;
    QComboBox *m_outDir;
    QPushButton *m_startAll;
    QPushButton *m_pauseAll;
    QLabel *m_status;
};
