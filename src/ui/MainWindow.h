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

protected:
    void closeEvent(QCloseEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    QWidget *buildHeader();

    QStackedWidget *m_pages;
    QButtonGroup *m_nav;
    ConvertPage *m_convert;
    DownloadPage *m_download = nullptr;
    QVBoxLayout *m_frameLayout = nullptr;
};
