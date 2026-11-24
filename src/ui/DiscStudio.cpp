#include "DiscStudio.h"
#include "Frameless.h"
#include "Widgets.h"
#include "core/I18n.h"
#include "core/Settings.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSharedPointer>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

// ============================== Раздел с выдвижной панелью ==============================

DiscStudioPage::DiscStudioPage(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    m_side = new QWidget(this);
    m_side->setObjectName(QStringLiteral("DiscSide"));
    Ui::themedStyle(m_side, QStringLiteral("#DiscSide{background:#1F191B; border-right:1px solid #33282C;}"
                                           "QListWidget{background:transparent; border:none; outline:none;}"
                                           "QListWidget::item{padding:10px 8px; border-radius:6px; color:#CFC3A8;}"
                                           "QListWidget::item:selected{background:#4A3828; color:#FFF6DE;}"));
    auto *side = new QVBoxLayout(m_side);
    side->setContentsMargins(8, 8, 8, 8);
    side->setSpacing(6);
    m_burger = Ui::iconButton(QStringLiteral("menu"), T("Свернуть или развернуть разделы"), m_side, 20);
    side->addWidget(m_burger, 0, Qt::AlignLeft);
    m_list = new QListWidget(m_side);
    m_list->setIconSize(QSize(20, 20));
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setTextElideMode(Qt::ElideRight);
    side->addWidget(m_list, 1);
    root->addWidget(m_side);
    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, 1);

    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(180);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) { m_side->setFixedWidth(v.toInt()); });
    m_side->setFixedWidth(240);
    connect(m_burger, &QToolButton::clicked, this, [this] { setExpanded(!m_expanded); });
    connect(m_list, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    setExpanded(QSettings().value(QStringLiteral("disc/sideExpanded"), true).toBool());
}

void DiscStudioPage::addTool(const QString &icon, const QString &title, QWidget *tool)
{
    auto *it = new QListWidgetItem(Ui::icon(icon), m_expanded ? title : QString(), m_list);
    it->setData(Qt::UserRole, title);
    it->setToolTip(title);
    m_stack->addWidget(tool);
    if (m_list->count() == 1)
        m_list->setCurrentRow(0);
}

void DiscStudioPage::setExpanded(bool on)
{
    m_expanded = on;
    QSettings().setValue(QStringLiteral("disc/sideExpanded"), on);
    for (int i = 0; i < m_list->count(); ++i)
        m_list->item(i)->setText(on ? m_list->item(i)->data(Qt::UserRole).toString() : QString());
    m_anim->stop();
    m_anim->setStartValue(m_side->width());
    m_anim->setEndValue(on ? 240 : 56);
    m_anim->start();
}

// ============================== Элементы холста ==============================

namespace {

// Надпись: перетаскивается, двойной щелчок — правка текста
class TextItem : public QGraphicsTextItem
{
public:
    explicit TextItem(const QString &text)
        : QGraphicsTextItem(text)
    {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemIsFocusable);
        auto *shadow = new QGraphicsDropShadowEffect;
        shadow->setBlurRadius(6);
        shadow->setOffset(1, 1);
        shadow->setColor(QColor(0, 0, 0, 220));
        setGraphicsEffect(shadow);
        setZValue(10);
    }

protected:
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e) override
    {
        setTextInteractionFlags(Qt::TextEditorInteraction);
        setFocus();
        QGraphicsTextItem::mouseDoubleClickEvent(e);
    }
    void focusOutEvent(QFocusEvent *e) override
    {
        setTextInteractionFlags(Qt::NoTextInteraction);
        QTextCursor c = textCursor();
        c.clearSelection();
        setTextCursor(c);
        QGraphicsTextItem::focusOutEvent(e);
    }
};

// Заплатка: прямоугольник цвета фона поверх старой надписи; правый нижний угол — изменение размера
class CoverItem : public QGraphicsRectItem
{
public:
    CoverItem(const QRectF &r, const QColor &c)
        : QGraphicsRectItem(QRectF(QPointF(0, 0), r.size()))
    {
        setPos(r.topLeft());
        setBrush(c);
        setPen(Qt::NoPen);
        setFlags(ItemIsMovable | ItemIsSelectable);
        setZValue(5);
        setAcceptHoverEvents(true);
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override
    {
        const QRectF r = rect();
        m_resize = QLineF(e->pos(), r.bottomRight()).length() < 14;
        QGraphicsRectItem::mousePressEvent(e);
    }
    void mouseMoveEvent(QGraphicsSceneMouseEvent *e) override
    {
        if (m_resize) {
            setRect(QRectF(QPointF(0, 0), QSizeF(qMax(8.0, e->pos().x()), qMax(8.0, e->pos().y()))));
            return;
        }
        QGraphicsRectItem::mouseMoveEvent(e);
    }
    void hoverMoveEvent(QGraphicsSceneHoverEvent *e) override
    {
        setCursor(QLineF(e->pos(), rect().bottomRight()).length() < 14 ? Qt::SizeFDiagCursor : Qt::SizeAllCursor);
    }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *o, QWidget *w) override
    {
        QGraphicsRectItem::paint(p, o, w);
        if (isSelected()) {
            p->setPen(QPen(QColor(232, 190, 90), 1, Qt::DashLine));
            p->setBrush(Qt::NoBrush);
            p->drawRect(rect());
            p->fillRect(QRectF(rect().bottomRight() - QPointF(8, 8), QSizeF(8, 8)), QColor(232, 190, 90));
        }
    }

private:
    bool m_resize = false;
};

constexpr int kButtonTag = 1;   // элементы-подсказки (рамки кнопок) не попадают в итоговую картинку

QColor averageColor(const QImage &img, const QRect &r)
{
    const QRect rr = r.intersected(img.rect());
    if (rr.isEmpty())
        return Qt::black;
    qint64 R = 0, G = 0, B = 0, n = 0;
    for (int y = rr.top(); y <= rr.bottom(); y += 2)
        for (int x = rr.left(); x <= rr.right(); x += 2) {
            const QRgb c = img.pixel(x, y);
            R += qRed(c);
            G += qGreen(c);
            B += qBlue(c);
            ++n;
        }
    return n ? QColor(int(R / n), int(G / n), int(B / n)) : Qt::black;
}

} // namespace

// ============================== Редактор меню ==============================

MenuEditor::MenuEditor(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(10);

    auto *title = new QLabel(T("Редактировать ISO (DVD)"), this);
    title->setObjectName(QStringLiteral("SectionTitle"));
    Ui::themedStyle(title, QStringLiteral("QLabel{font-size:18px; color:#E8BE5A;}"));
    root->addWidget(title);

    auto *top = new QHBoxLayout;
    auto *open = Ui::primaryButton(T("Открыть образ ISO…"), this);
    m_file = new QLabel(T("Образ не выбран"), this);
    m_file->setObjectName(QStringLiteral("Muted"));
    m_save = Ui::primaryButton(T("Сохранить новый ISO…"), this);
    m_save->setEnabled(false);
    top->addWidget(open);
    top->addWidget(m_file, 1);
    top->addWidget(m_save);
    root->addLayout(top);

    m_hint = new QLabel(T("Откройте образ DVD (например, сделанный в «Инструменты → Копирование DVD»). Слева — все меню диска. "
                          "Добавляйте надписи на русском, закрашивайте старый текст или замените фон своей картинкой. "
                          "Кнопки, переходы и подсветка кнопок остаются как на диске; движущееся меню станет неподвижной картинкой. "
                          "Исходный образ не меняется — сохраняется новый. Меню Blu-ray пока не поддерживаются."),
                        this);
    m_hint->setObjectName(QStringLiteral("Muted"));
    m_hint->setWordWrap(true);
    root->addWidget(m_hint);

    auto *body = new QHBoxLayout;
    m_menus = new QListWidget(this);
    m_menus->setFixedWidth(220);
    m_menus->setWordWrap(true);
    body->addWidget(m_menus);
    auto *right = new QVBoxLayout;
    auto *tools = new QHBoxLayout;
    auto *addText = new QPushButton(T("Добавить надпись"), this);
    auto *cover = new QPushButton(T("Закрасить область"), this);
    auto *bg = new QPushButton(T("Заменить фон…"), this);
    auto *del = new QPushButton(T("Удалить выбранное"), this);
    auto *reset = new QPushButton(T("Сбросить меню"), this);
    m_size = new QSpinBox(this);
    m_size->setRange(8, 160);
    m_size->setValue(36);
    m_size->setSuffix(T(" пт"));
    m_size->setToolTip(T("Размер шрифта выбранной надписи"));
    m_color = new QToolButton(this);
    m_color->setText(T("Цвет"));
    m_color->setToolTip(T("Цвет надписи или заплатки"));
    auto *showButtons = new QCheckBox(T("Рамки кнопок"), this);
    showButtons->setChecked(true);
    showButtons->setToolTip(T("Показать, где на диске находятся кнопки (в итоговое меню не попадают)"));
    // Два ряда: действия и оформление — чтобы подписи не обрезались в узком окне
    auto *tools2 = new QHBoxLayout;
    for (QWidget *w : {static_cast<QWidget *>(addText), static_cast<QWidget *>(cover), static_cast<QWidget *>(bg),
                       static_cast<QWidget *>(del), static_cast<QWidget *>(reset)}) {
        w->setMinimumWidth(w->sizeHint().width());
        tools->addWidget(w);
        m_editTools << w;
        w->setEnabled(false);
    }
    tools2->addWidget(new QLabel(T("Шрифт:"), this));
    for (QWidget *w : {static_cast<QWidget *>(m_size), static_cast<QWidget *>(m_color), static_cast<QWidget *>(showButtons)}) {
        tools2->addWidget(w);
        m_editTools << w;
        w->setEnabled(false);
    }
    tools->addStretch();
    tools2->addStretch();
    right->addLayout(tools);
    right->addLayout(tools2);
    m_view = new QGraphicsView(this);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setRenderHint(QPainter::SmoothPixmapTransform);
    m_view->setBackgroundBrush(QColor(20, 18, 18));
    right->addWidget(m_view, 1);
    body->addLayout(right, 1);
    root->addLayout(body, 1);

    connect(open, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, T("Образ DVD"), QSettings().value(QStringLiteral("disc/lastDir"), Settings::outputDir()).toString(),
                                                       T("Образы дисков (*.iso)"));
        if (!f.isEmpty())
            openIso(f);
    });
    connect(m_menus, &QListWidget::currentRowChanged, this, &MenuEditor::showMenu);
    connect(addText, &QPushButton::clicked, this, &MenuEditor::addText);
    connect(cover, &QPushButton::clicked, this, &MenuEditor::addCover);
    connect(bg, &QPushButton::clicked, this, &MenuEditor::replaceBackground);
    connect(del, &QPushButton::clicked, this, &MenuEditor::deleteSelected);
    connect(reset, &QPushButton::clicked, this, &MenuEditor::resetMenu);
    connect(m_size, QOverload<int>::of(&QSpinBox::valueChanged), this, &MenuEditor::applyFont);
    connect(m_color, &QToolButton::clicked, this, &MenuEditor::pickColor);
    connect(showButtons, &QCheckBox::toggled, this, [this](bool on) {
        for (Page &p : m_pages)
            if (p.scene)
                for (QGraphicsItem *i : p.scene->items())
                    if (i->data(0).toInt() == kButtonTag)
                        i->setVisible(on);
    });
    connect(m_save, &QPushButton::clicked, this, &MenuEditor::save);
}

MenuEditor::Page *MenuEditor::current()
{
    const int i = m_menus->currentRow();
    return (i >= 0 && i < m_pages.size()) ? &m_pages[i] : nullptr;
}

void MenuEditor::openIso(const QString &path)
{
    QString err;
    const QList<DvdImage::Menu> list = DvdImage::menus(path, &err);
    if (list.isEmpty()) {
        Ui::message(this, T("Редактировать ISO"), T("Не удалось открыть меню диска: %1").arg(err));
        return;
    }
    for (Page &p : m_pages)
        delete p.scene;
    m_pages.clear();
    m_iso = path;
    QSettings().setValue(QStringLiteral("disc/lastDir"), QFileInfo(path).absolutePath());
    m_file->setText(QDir::toNativeSeparators(path));
    m_menus->clear();
    for (const DvdImage::Menu &m : list) {
        Page p;
        p.menu = m;
        m_pages << p;
        QString line = m.title;
        if (!m.buttons.isEmpty())
            line += T(" · кнопок: %1").arg(m.buttons.size());
        if (!m.still)
            line += T(" · движущееся");
        m_menus->addItem(line);
    }
    m_save->setEnabled(true);
    for (QWidget *w : m_editTools)
        w->setEnabled(true);
    m_menus->setCurrentRow(0);
}

void MenuEditor::showMenu(int index)
{
    if (index < 0 || index >= m_pages.size())
        return;
    loadPage(index);
    m_view->setScene(m_pages[index].scene);
    m_view->fitInView(m_pages[index].scene->sceneRect(), Qt::KeepAspectRatio);
}

void MenuEditor::loadPage(int index)
{
    Page &p = m_pages[index];
    if (p.scene)
        return;
    p.scene = new QGraphicsScene(this);
    const DvdImage::VideoInfo vi = DvdImage::videoInfo(m_iso, p.menu);
    p.display = vi.display();
    p.scene->setSceneRect(QRectF(QPointF(0, 0), p.display));
    p.background = p.scene->addPixmap(QPixmap());
    p.background->setZValue(0);
    auto *wait = p.scene->addText(T("Загрузка меню…"));
    wait->setDefaultTextColor(Qt::white);
    wait->setPos(20, 20);
    // Рамки кнопок — в пропорциях экрана
    const double sx = double(p.display.width()) / vi.size.width(), sy = double(p.display.height()) / vi.size.height();
    for (int i = 0; i < p.menu.buttons.size(); ++i) {
        const QRect r = p.menu.buttons[i].rect;
        auto *rect = p.scene->addRect(QRectF(r.x() * sx, r.y() * sy, r.width() * sx, r.height() * sy), QPen(QColor(232, 190, 90), 2, Qt::DashLine));
        rect->setData(0, kButtonTag);
        rect->setZValue(20);
        auto *num = p.scene->addSimpleText(QString::number(i + 1));
        num->setBrush(QColor(232, 190, 90));
        num->setPos(r.x() * sx + 3, r.y() * sy + 1);
        num->setData(0, kButtonTag);
        num->setZValue(20);
    }
    const QString iso = m_iso;
    const DvdImage::Menu menu = p.menu;
    auto img = QSharedPointer<QImage>::create();
    QPointer<QGraphicsScene> scene(p.scene);
    QGraphicsPixmapItem *bg = p.background;
    Ui::runAsync(this, [iso, menu, img] { *img = DvdImage::frame(iso, menu, nullptr); }, [this, img, scene, bg, wait, index] {
        if (!scene)
            return;
        delete wait;
        bg->setPixmap(QPixmap::fromImage(*img));
        if (index < m_pages.size())
            m_pages[index].loaded = true;
        if (m_view->scene() == scene)
            m_view->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
    });
}

void MenuEditor::addText()
{
    Page *p = current();
    if (!p || !p->scene)
        return;
    auto *t = new TextItem(T("Новая надпись"));
    QFont f(QStringLiteral("Arial"));
    f.setBold(true);
    f.setPixelSize(m_size->value());
    t->setFont(f);
    t->setDefaultTextColor(m_textColor);
    t->setPos(p->display.width() / 2 - 120, p->display.height() / 2 - 20);
    p->scene->addItem(t);
    p->scene->clearSelection();
    t->setSelected(true);
    p->edited = true;
}

void MenuEditor::addCover()
{
    Page *p = current();
    if (!p || !p->scene)
        return;
    const QRectF r(p->display.width() / 2 - 150, p->display.height() / 2 - 25, 300, 50);
    const QColor c = averageColor(p->background->pixmap().toImage(), r.toRect());
    auto *item = new CoverItem(r, c);
    p->scene->addItem(item);
    p->scene->clearSelection();
    item->setSelected(true);
    p->edited = true;
}

void MenuEditor::replaceBackground()
{
    Page *p = current();
    if (!p || !p->scene)
        return;
    const QString f = QFileDialog::getOpenFileName(this, T("Картинка для фона"), QSettings().value(QStringLiteral("disc/lastDir")).toString(),
                                                   T("Изображения (*.png *.jpg *.jpeg *.bmp *.webp)"));
    if (f.isEmpty())
        return;
    QImage img(f);
    if (img.isNull())
        return;
    // Заполнить кадр целиком, лишнее — обрезать по центру
    img = img.scaled(p->display, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    img = img.copy((img.width() - p->display.width()) / 2, (img.height() - p->display.height()) / 2, p->display.width(), p->display.height());
    p->background->setPixmap(QPixmap::fromImage(img));
    p->edited = true;
}

void MenuEditor::resetMenu()
{
    const int i = m_menus->currentRow();
    if (i < 0 || i >= m_pages.size())
        return;
    delete m_pages[i].scene;
    m_pages[i].scene = nullptr;
    m_pages[i].edited = false;
    showMenu(i);
}

void MenuEditor::deleteSelected()
{
    Page *p = current();
    if (!p || !p->scene)
        return;
    for (QGraphicsItem *i : p->scene->selectedItems())
        if (i != p->background && i->data(0).toInt() != kButtonTag)
            delete i;
}

void MenuEditor::applyFont()
{
    Page *p = current();
    if (!p || !p->scene)
        return;
    for (QGraphicsItem *i : p->scene->selectedItems())
        if (auto *t = dynamic_cast<QGraphicsTextItem *>(i)) {
            QFont f = t->font();
            f.setPixelSize(m_size->value());
            t->setFont(f);
        }
}

void MenuEditor::pickColor()
{
    const QColor c = QColorDialog::getColor(m_textColor, this, T("Цвет"));
    if (!c.isValid())
        return;
    m_textColor = c;
    Page *p = current();
    if (!p || !p->scene)
        return;
    for (QGraphicsItem *i : p->scene->selectedItems()) {
        if (auto *t = dynamic_cast<QGraphicsTextItem *>(i))
            t->setDefaultTextColor(c);
        else if (auto *r = dynamic_cast<CoverItem *>(i))
            r->setBrush(c);
    }
}

QImage MenuEditor::render(Page &p)
{
    QImage out(p.display, QImage::Format_RGB32);
    out.fill(Qt::black);
    p.scene->clearSelection();
    QList<QGraphicsItem *> hidden;
    for (QGraphicsItem *i : p.scene->items())
        if (i->data(0).toInt() == kButtonTag && i->isVisible()) {
            i->setVisible(false);
            hidden << i;
        }
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    p.scene->render(&painter, QRectF(QPointF(0, 0), p.display), QRectF(QPointF(0, 0), p.display));
    painter.end();
    for (QGraphicsItem *i : hidden)
        i->setVisible(true);
    return out;
}

void MenuEditor::save()
{
    QList<QPair<DvdImage::Menu, QImage>> changes;
    for (Page &p : m_pages)
        if (p.scene && p.edited && p.loaded)
            changes.append({p.menu, render(p)});
    if (changes.isEmpty()) {
        Ui::message(this, T("Редактировать ISO"), T("Ни одно меню не изменено."));
        return;
    }
    const QFileInfo fi(m_iso);
    const QString out = QFileDialog::getSaveFileName(this, T("Сохранить новый ISO"),
                                                     fi.dir().filePath(fi.completeBaseName() + T(" (новое меню).iso")),
                                                     T("Образы дисков (*.iso)"));
    if (out.isEmpty())
        return;
    if (QFileInfo(out).absoluteFilePath() == fi.absoluteFilePath()) {
        Ui::message(this, T("Редактировать ISO"), T("Сохраните под другим именем — исходный образ не перезаписывается."));
        return;
    }
    FramedDialog d(this);
    d.setWindowTitle(T("Сохранение ISO"));
    d.setMinimumWidth(520);
    auto *lay = d.contentLayout();
    lay->setContentsMargins(24, 18, 24, 18);
    auto *state = new QLabel(T("Подготовка…"), &d);
    state->setWordWrap(true);
    auto *bar = new QProgressBar(&d);
    bar->setRange(0, 100);
    lay->addWidget(state);
    lay->addWidget(bar);
    auto ok = QSharedPointer<bool>::create(false);
    auto err = QSharedPointer<QString>::create();
    QPointer<QLabel> st(state);
    QPointer<QProgressBar> pb(bar);
    const QString iso = m_iso;
    Ui::runAsync(&d, [iso, out, changes, ok, err, st, pb] {
        *ok = DvdImage::save(iso, out, changes, [st, pb](int pct, const QString &what) {
            QMetaObject::invokeMethod(pb, [st, pb, pct, what] {
                if (pb)
                    pb->setValue(pct);
                if (st)
                    st->setText(what);
            }, Qt::QueuedConnection);
        }, err.data());
    }, [&d] { d.accept(); });
    d.exec();
    if (*ok) {
        Ui::message(this, T("Редактировать ISO"), T("Новый образ сохранён: %1<br><br>Проверьте его в VLC (Медиа → Открыть диск → DVD с меню) "
                                                     "или запишите на диск.").arg(QDir::toNativeSeparators(out)));
        Ui::showInFolder(out);
    } else {
        QFile::remove(out);
        Ui::message(this, T("Редактировать ISO"), T("Не удалось сохранить: %1").arg(*err));
    }
}
