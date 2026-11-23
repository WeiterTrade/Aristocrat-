#pragma once
#include <QMainWindow>

class QStackedWidget;
class QButtonGroup;
class ConvertPage;
class DownloadPage;
class QVBoxLayout;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void openFiles(const QStringList &files);
    void showPage(int index);
    void demoTrim();
    void demoSettings(int tab);
    void demoBrowser(const QString &url);
    void demoGrab(const QString &url);
    void demoTheme(const QString &theme);
    void demoDvd(const QString &path);
    void demoChapters(const QString &file, const QString &url);
    void demoUpload();
    void demoCovers(const QString &stampFile);
    void demoServer();
    void demoRelease(const QString &url, const QString &video, const QString &scenes = QString());
    void openSettings(int tab = 0);
    void applyTheme();
    void restartApp();

protected:
    void closeEvent(QCloseEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    QWidget *buildHeader();

    QStackedWidget *m_pages;
    QButtonGroup *m_nav;
    ConvertPage *m_convert;
    class ToolboxPage *m_toolbox = nullptr;
    DownloadPage *m_download = nullptr;
    QVBoxLayout *m_frameLayout = nullptr;
    bool m_forceClose = false;
};
