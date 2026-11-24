#pragma once
#include "core/DvdImage.h"

#include <QHash>
#include <QWidget>

class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QToolButton;
class QVariantAnimation;

// Раздел «Запись»: инструменты для дисков. Слева — выдвижная панель разделов (≡), справа — инструмент.
class DiscStudioPage : public QWidget
{
    Q_OBJECT
public:
    explicit DiscStudioPage(QWidget *parent = nullptr);
    void addTool(const QString &icon, const QString &title, QWidget *tool);

private:
    void setExpanded(bool on);
    QWidget *m_side;
    QListWidget *m_list;
    QStackedWidget *m_stack;
    QToolButton *m_burger;
    QVariantAnimation *m_anim;
    bool m_expanded = true;
};

// Редактор меню DVD в образе ISO: фон меню, надписи поверх, закрашивание старого текста, замена картинки.
// Кнопки, переходы и подсветка остаются оригинальными.
class MenuEditor : public QWidget
{
    Q_OBJECT
public:
    explicit MenuEditor(QWidget *parent = nullptr);
    void openIso(const QString &path);

private:
    struct Page {
        DvdImage::Menu menu;
        QGraphicsScene *scene = nullptr;
        class QGraphicsPixmapItem *background = nullptr;
        QSize display;
        bool loaded = false;
        bool edited = false;
    };
    void showMenu(int index);
    void loadPage(int index);
    void addText();
    void addCover();
    void replaceButtonText(const QRectF &area);
    void replaceBackground();
    void resetMenu();
    void deleteSelected();
    void applyFont();
    void pickColor();
    void save();
    Page *current();
    QImage render(Page &p);

    QString m_iso;
    QList<Page> m_pages;
    QLabel *m_file;
    QLabel *m_hint;
    QListWidget *m_menus;
    QGraphicsView *m_view;
    QPushButton *m_save;
    QSpinBox *m_size;
    QToolButton *m_color;
    QColor m_textColor = Qt::white;
    QList<QWidget *> m_editTools;
};
