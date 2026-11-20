#pragma once
#include <QMainWindow>

class QStackedWidget;
class QButtonGroup;
class ConvertPage;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void openFiles(const QStringList &files);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    QWidget *buildHeader();

    QStackedWidget *m_pages;
    QButtonGroup *m_nav;
    ConvertPage *m_convert;
};
