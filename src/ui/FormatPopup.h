#pragma once
#include "core/Preset.h"

#include <QFrame>

class QTabBar;
class QListWidget;
class QLineEdit;

// Выпадающее окно выбора формата: Недавние / Видео / Аудио / Устройства.
class FormatPopup : public QFrame
{
    Q_OBJECT
public:
    explicit FormatPopup(QWidget *parent = nullptr);
    void setCurrent(const QString &presetId);
    void showBelow(QWidget *anchor);

signals:
    void presetChosen(const Preset &p);

private:
    void reloadGroups();
    void reloadPresets();
    void fillPresets(const QList<Preset> &list);
    void choose(const Preset &p);
    void editPreset(const Preset &p, bool createNew);
    QString category() const;

    QTabBar *m_tabs;
    QListWidget *m_groups;
    QListWidget *m_list;
    QLineEdit *m_search;
    QString m_current;
};
