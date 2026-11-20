#include "FormatPopup.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "PresetEditDialog.h"
#include "Widgets.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScreen>
#include <QTabBar>
#include <QUuid>
#include <QVBoxLayout>

FormatPopup::FormatPopup(QWidget *parent)
    : QFrame(parent, Qt::Popup)
{
    setObjectName(QStringLiteral("FormatPopup"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(720, 580);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_tabs = new QTabBar(this);
    m_tabs->setExpanding(true);
    m_tabs->setDrawBase(false);
    m_tabs->addTab(T("Недавние"));
    m_tabs->addTab(T("Видео"));
    m_tabs->addTab(T("Аудио"));
    m_tabs->addTab(T("Устройства"));
    m_tabs->setCurrentIndex(1);
    root->addWidget(m_tabs);

    auto *body = new QHBoxLayout;
    body->setSpacing(0);
    m_groups = new QListWidget(this);
    m_groups->setFixedWidth(190);
    m_groups->setIconSize(QSize(26, 26));
    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    body->addWidget(m_groups);
    body->addWidget(m_list, 1);
    root->addLayout(body, 1);

    auto *bottom = new QHBoxLayout;
    bottom->setContentsMargins(16, 10, 16, 12);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(T("Поиск"));
    m_search->setFixedWidth(170);
    m_search->addAction(Ui::icon("search"), QLineEdit::LeadingPosition);
    auto *create = new QPushButton(Ui::icon("add", true), T("Создать свой"), this);
    create->setFlat(true);
    create->setCursor(Qt::PointingHandCursor);
    bottom->addWidget(m_search);
    bottom->addSpacing(20);
    bottom->addWidget(create);
    bottom->addStretch();
    root->addLayout(bottom);

    connect(m_tabs, &QTabBar::currentChanged, this, [this] { reloadGroups(); });
    connect(m_groups, &QListWidget::currentRowChanged, this, [this] { reloadPresets(); });
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &t) {
        if (t.trimmed().isEmpty())
            reloadPresets();
        else
            fillPresets(PresetStore::instance().search(t.trimmed()));
    });
    connect(create, &QPushButton::clicked, this, [this] {
        Preset base = PresetStore::instance().find(m_current);
        if (!base.isValid())
            base = PresetStore::instance().defaultPreset();
        editPreset(base, true);
    });

    reloadGroups();
}

QString FormatPopup::category() const
{
    switch (m_tabs->currentIndex()) {
    case 2: return QStringLiteral("audio");
    case 3: return QStringLiteral("device");
    default: return QStringLiteral("video");
    }
}

void FormatPopup::setCurrent(const QString &presetId)
{
    m_current = presetId;
    const Preset p = PresetStore::instance().find(presetId);
    if (p.isValid()) {
        const int tab = p.category == QLatin1String("audio") ? 2 : p.category == QLatin1String("device") ? 3 : 1;
        m_tabs->setCurrentIndex(tab);
        reloadGroups();
        const QStringList g = PresetStore::instance().groups(p.category);
        m_groups->setCurrentRow(qMax(0, g.indexOf(p.group)));
    }
}

void FormatPopup::reloadGroups()
{
    m_groups->blockSignals(true);
    m_groups->clear();
    if (m_tabs->currentIndex() == 0) {
        m_groups->setVisible(false);
        m_groups->blockSignals(false);
        fillPresets(PresetStore::instance().recent());
        return;
    }
    m_groups->setVisible(true);
    for (const QString &g : PresetStore::instance().groups(category())) {
        auto *it = new QListWidgetItem(QIcon(Ui::formatBadge(g.split(' ').last(), 26)), g);
        m_groups->addItem(it);
    }
    m_groups->blockSignals(false);
    m_groups->setCurrentRow(0);
    reloadPresets();
}

void FormatPopup::reloadPresets()
{
    if (m_tabs->currentIndex() == 0) {
        fillPresets(PresetStore::instance().recent());
        return;
    }
    const QListWidgetItem *it = m_groups->currentItem();
    if (!it)
        return;
    fillPresets(PresetStore::instance().presets(category(), it->text()));
}

void FormatPopup::fillPresets(const QList<Preset> &list)
{
    m_list->clear();
    if (list.isEmpty()) {
        auto *it = new QListWidgetItem(T("Здесь пока пусто"));
        it->setFlags(Qt::NoItemFlags);
        m_list->addItem(it);
        return;
    }
    for (const Preset &p : list) {
        auto *it = new QListWidgetItem;
        it->setSizeHint(QSize(100, 64));
        m_list->addItem(it);

        auto *w = new QWidget;
        w->setAttribute(Qt::WA_StyledBackground, false);
        w->setStyleSheet(QStringLiteral("background: transparent;"));
        w->setCursor(Qt::PointingHandCursor);
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(14, 4, 10, 4);
        h->setSpacing(14);
        const bool cur = p.id == m_current;
        auto *badge = new QLabel(w);
        badge->setPixmap(Ui::formatBadge(p.badge, 38, cur));
        auto *name = new QLabel(m_tabs->currentIndex() == 0 ? p.title() : T(p.name), w);
        Ui::themedStyle(name, cur ? "color:#E8C266;" : "color:#E9E1D2;");
        name->setMinimumWidth(170);
        auto *info = new QLabel(p.resolutionText() + "\n" + p.codecText(), w);
        Ui::themedStyle(info, cur ? "color:#E8C266;" : "color:#A99F90;");
        auto *edit = Ui::iconButton(QStringLiteral("edit"), T("Настроить формат"), w);
        edit->setIcon(Ui::icon("edit", true));
        h->addWidget(badge);
        h->addWidget(name);
        h->addWidget(info, 1);
        h->addWidget(edit);
        m_list->setItemWidget(it, w);

        connect(edit, &QToolButton::clicked, this, [this, p] { editPreset(p, false); });
    }
    m_list->disconnect(SIGNAL(itemClicked(QListWidgetItem*)));
    connect(m_list, &QListWidget::itemClicked, this, [this, list](QListWidgetItem *it) {
        const int row = m_list->row(it);
        if (row >= 0 && row < list.size())
            choose(list.at(row));
    });
}

void FormatPopup::choose(const Preset &p)
{
    PresetStore::instance().markUsed(p.id);
    emit presetChosen(p);
    close();
}

void FormatPopup::editPreset(const Preset &p, bool createNew)
{
    // Окно редактирования модальное — временно скрываем всплывающее окно.
    hide();
    PresetEditDialog dlg(p, parentWidget());
    if (dlg.exec() == QDialog::Accepted) {
        Preset np = dlg.preset();
        if (createNew || !p.custom) {
            np.id = QStringLiteral("custom-") + QUuid::createUuid().toString(QUuid::Id128).left(8);
            np.group = p.group;
            np.category = p.category;
        }
        PresetStore::instance().addCustom(np);
        choose(np);
        return;
    }
    close();
}

void FormatPopup::showBelow(QWidget *anchor)
{
    QPoint pos = anchor->mapToGlobal(QPoint(anchor->width() - width(), anchor->height() + 4));
    QScreen *scr = QGuiApplication::screenAt(pos);
    if (!scr)
        scr = QGuiApplication::primaryScreen();
    const QRect avail = scr->availableGeometry();
    if (pos.y() + height() > avail.bottom())
        pos.setY(anchor->mapToGlobal(QPoint(0, 0)).y() - height() - 4);
    pos.setX(qBound(avail.left(), pos.x(), avail.right() - width()));
    pos.setY(qMax(avail.top(), pos.y()));
    move(pos);
    show();
}
