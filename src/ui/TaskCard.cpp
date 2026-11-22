#include "TaskCard.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "EditDialog.h"
#include "FormatPopup.h"
#include "Frameless.h"
#include "FramePreview.h"
#include "OutputSizeDialog.h"
#include "TrimDialog.h"
#include "Widgets.h"
#include "core/Tools.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

static QLabel *infoItem(const QString &iconName, QHBoxLayout *lay, QWidget *parent, int minW = 0)
{
    lay->addWidget(Ui::iconLabel(iconName, 14, parent));
    auto *l = new QLabel(parent);
    if (minW)
        l->setMinimumWidth(minW);
    lay->addWidget(l);
    lay->addSpacing(10);
    return l;
}

TaskCard::TaskCard(const TaskPtr &task, QWidget *parent)
    : QFrame(parent), m_task(task)
{
    setObjectName(QStringLiteral("TaskCard"));
    setAttribute(Qt::WA_StyledBackground);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);   // не растягиваться по высоте

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(24, 20, 10, 20);
    row->setSpacing(18);

    // ---- Миниатюра и инструменты правки ----
    auto *thumbCol = new QVBoxLayout;
    thumbCol->setSpacing(0);
    m_thumb = new QLabel(this);
    m_thumb->setObjectName(QStringLiteral("Thumb"));
    m_thumb->setFixedSize(208, 116);
    m_thumb->setAlignment(Qt::AlignCenter);
    m_thumb->setCursor(Qt::PointingHandCursor);
    m_thumb->setToolTip(T("Открыть в проигрывателе"));
    m_thumb->installEventFilter(this);
    m_editTools = new QWidget(this);
    m_editTools->setObjectName(QStringLiteral("ThumbTools"));
    m_editTools->setAttribute(Qt::WA_StyledBackground);
    m_editTools->setFixedSize(208, 34);
    auto *tools = new QHBoxLayout(m_editTools);
    tools->setContentsMargins(0, 0, 0, 0);
    tools->addStretch();
    auto *cut = Ui::iconButton("cut", T("Обрезать по времени"), m_editTools);
    auto *crop = Ui::iconButton("crop", T("Кадрирование и поворот"), m_editTools);
    auto *fx = Ui::iconButton("effects", T("Эффекты, водяной знак, субтитры"), m_editTools);
    tools->addWidget(cut);
    tools->addSpacing(14);
    tools->addWidget(crop);
    tools->addSpacing(14);
    tools->addWidget(fx);
    tools->addStretch();
    thumbCol->addWidget(m_thumb);
    thumbCol->addWidget(m_editTools);
    thumbCol->addStretch();
    row->addLayout(thumbCol);

    // ---- Исходный файл ----
    auto *srcCol = new QVBoxLayout;
    srcCol->setSpacing(10);
    m_srcName = new QLabel(this);
    m_srcName->setObjectName(QStringLiteral("FileName"));
    m_srcName->setMinimumWidth(120);
    auto *srcBox = new QFrame(this);
    srcBox->setObjectName(QStringLiteral("InfoBox"));
    srcBox->setAttribute(Qt::WA_StyledBackground);
    srcBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *srcBoxLay = new QVBoxLayout(srcBox);
    srcBoxLay->setContentsMargins(14, 8, 14, 10);
    auto *srcCap = new QLabel(T("Источник"), srcBox);
    srcCap->setObjectName(QStringLiteral("InfoCaption"));
    auto *srcLine = new QHBoxLayout;
    srcLine->setSpacing(6);
    m_srcFmt = infoItem("film", srcLine, srcBox);
    m_srcRes = infoItem("res", srcLine, srcBox);
    m_srcDur = infoItem("clock", srcLine, srcBox);
    m_srcSize = infoItem("folder", srcLine, srcBox);
    srcLine->addStretch();
    srcBoxLay->addWidget(srcCap);
    srcBoxLay->addLayout(srcLine);
    srcCol->addWidget(m_srcName);
    srcCol->addWidget(srcBox);
    srcCol->addStretch();
    row->addLayout(srcCol, 4);

    auto *arrow = Ui::iconLabel("arrow", 26, this);
    auto *arrowCol = new QVBoxLayout;
    arrowCol->addSpacing(58);
    arrowCol->addWidget(arrow);
    arrowCol->addStretch();
    row->addLayout(arrowCol);

    // ---- Результат ----
    auto *dstCol = new QVBoxLayout;
    dstCol->setSpacing(10);
    auto *dstHead = new QHBoxLayout;
    m_dstName = new QLabel(this);
    m_dstName->setObjectName(QStringLiteral("TargetName"));
    auto *rename = Ui::iconButton("edit", T("Переименовать"), this);
    rename->setIcon(Ui::icon("edit", true));
    dstHead->addWidget(m_dstName);
    dstHead->addWidget(rename);
    dstHead->addStretch();

    auto *dstRow = new QHBoxLayout;
    dstRow->setSpacing(0);
    auto *dstBox = new QFrame(this);
    dstBox->setObjectName(QStringLiteral("InfoBox"));
    dstBox->setAttribute(Qt::WA_StyledBackground);
    dstBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *dstBoxLay = new QVBoxLayout(dstBox);
    dstBoxLay->setContentsMargins(14, 8, 8, 10);
    auto *dstCap = new QLabel(T("Результат"), dstBox);
    dstCap->setObjectName(QStringLiteral("InfoCaption"));
    auto *dstLine = new QHBoxLayout;
    dstLine->setSpacing(6);
    m_dstFmt = infoItem("film", dstLine, dstBox);
    m_dstRes = infoItem("res", dstLine, dstBox);
    m_dstDur = infoItem("clock", dstLine, dstBox);
    dstLine->addWidget(Ui::iconLabel("folder", 14, dstBox));
    m_dstSize = new QToolButton(dstBox);
    m_dstSize->setObjectName(QStringLiteral("IconButton"));
    m_dstSize->setCursor(Qt::PointingHandCursor);
    m_dstSize->setToolTip(T("Задать желаемый размер файла"));
    Ui::themedStyle(m_dstSize, QStringLiteral("QToolButton{color:#CFC3A8;text-decoration:underline;}"));
    dstLine->addWidget(m_dstSize);
    dstLine->addStretch();
    m_formatBtn = new QToolButton(dstBox);
    m_formatBtn->setObjectName(QStringLiteral("IconButton"));
    m_formatBtn->setIcon(Ui::icon("chevron", true));
    m_formatBtn->setIconSize(QSize(18, 18));
    m_formatBtn->setFixedSize(36, 36);
    Ui::themedStyle(m_formatBtn, QStringLiteral("QToolButton{background:#171113;border-radius:4px;} QToolButton:hover{background:#3A2B30;}"));
    m_formatBtn->setToolTip(T("Выбрать формат"));
    m_formatBtn->setCursor(Qt::PointingHandCursor);
    dstBoxLay->addWidget(dstCap);
    dstBoxLay->addLayout(dstLine);
    dstRow->addWidget(dstBox, 1);
    dstRow->addSpacing(8);
    dstRow->addWidget(m_formatBtn, 0, Qt::AlignVCenter);
    dstRow->addSpacing(10);

    auto *opts = new QHBoxLayout;
    opts->setSpacing(10);
    m_subs = new QComboBox(this);
    m_subs->setMinimumWidth(155);
    m_subs->setToolTip(T("Субтитры"));
    m_audio = new QComboBox(this);
    m_audio->setMinimumWidth(180);
    m_audio->setToolTip(T("Звуковая дорожка"));
    m_upscale = new QComboBox(this);
    m_upscale->setToolTip(T("Апскейл — увеличение разрешения (для старых кассет)"));
    m_upscale->addItem(Ui::icon("upscale"), UpscaleOptions::modeName(UpscaleOptions::None), int(UpscaleOptions::None));
    m_upscale->addItem(Ui::icon("upscale"), UpscaleOptions::modeName(UpscaleOptions::Fast720), int(UpscaleOptions::Fast720));
    m_upscale->addItem(Ui::icon("upscale"), UpscaleOptions::modeName(UpscaleOptions::Fast1080), int(UpscaleOptions::Fast1080));
    m_upscale->addItem(Ui::icon("upscale"), UpscaleOptions::modeName(UpscaleOptions::Fast4K), int(UpscaleOptions::Fast4K));
    m_upscale->addItem(Ui::icon("upscale"), UpscaleOptions::modeName(UpscaleOptions::AI1080), int(UpscaleOptions::AI1080));
    m_upscale->addItem(Ui::icon("upscale"), UpscaleOptions::modeName(UpscaleOptions::AI4K), int(UpscaleOptions::AI4K));
    m_upscale->insertSeparator(m_upscale->count());
    m_upscale->addItem(T("☐ Восстановление VHS"), 100);
    for (QComboBox *c : {m_subs, m_audio, m_upscale}) {
        c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        c->setMinimumContentsLength(12);
    }
    m_subs->setMinimumContentsLength(15);
    m_audio->setMinimumContentsLength(14);
    auto *info = Ui::iconButton("info", QString(), this);
    info->setIcon(Ui::icon("info", true));
    opts->addWidget(m_subs);
    opts->addWidget(m_audio);
    opts->addWidget(m_upscale);
    opts->addWidget(info);
    opts->addStretch();

    dstCol->addLayout(dstHead);
    dstCol->addLayout(dstRow);
    dstCol->addLayout(opts);
    dstCol->addStretch();
    row->addLayout(dstCol, 7);

    // ---- Кнопка действия / прогресс ----
    m_actionStack = new QStackedWidget(this);
    m_actionStack->setFixedWidth(160);
    m_convert = new QPushButton(T("Конвертировать"), this);
    m_convert->setObjectName(QStringLiteral("DarkButton"));
    m_convert->setCursor(Qt::PointingHandCursor);
    m_convert->setMinimumHeight(48);
    m_actionStack->setContentsMargins(0, 32, 0, 0);

    auto *progressPage = new QWidget(this);
    auto *pl = new QVBoxLayout(progressPage);
    pl->setContentsMargins(0, 0, 0, 0);
    m_status = new QLabel(progressPage);
    m_status->setObjectName(QStringLiteral("StatusLabel"));
    m_status->setWordWrap(true);
    m_progress = new QProgressBar(progressPage);
    m_progress->setRange(0, 1000);
    m_progress->setFixedHeight(6);
    m_stop = new QPushButton(T("Стоп"), progressPage);
    pl->addStretch();
    pl->addWidget(m_status);
    pl->addWidget(m_progress);
    pl->addWidget(m_stop);
    pl->addStretch();

    auto *donePage = new QWidget(this);
    auto *dl = new QVBoxLayout(donePage);
    dl->setContentsMargins(0, 0, 0, 0);
    auto *doneLbl = new QLabel(T("✔ Готово"), donePage);
    doneLbl->setObjectName(QStringLiteral("StatusLabel"));
    doneLbl->setAlignment(Qt::AlignCenter);
    m_openFolder = new QPushButton(T("Открыть папку"), donePage);
    dl->addStretch();
    dl->addWidget(doneLbl);
    dl->addWidget(m_openFolder);
    dl->addStretch();

    m_actionStack->addWidget(m_convert);
    m_actionStack->addWidget(progressPage);
    m_actionStack->addWidget(donePage);
    row->addWidget(m_actionStack, 0, Qt::AlignTop);

    auto *close = Ui::iconButton("close", T("Убрать из списка"), this, 14);
    row->addWidget(close, 0, Qt::AlignTop);

    // ---- Сигналы ----
    connect(cut, &QToolButton::clicked, this, &TaskCard::openTrim);
    connect(crop, &QToolButton::clicked, this, [this] { openEdit(0); });
    connect(fx, &QToolButton::clicked, this, [this] { openEdit(1); });
    connect(rename, &QToolButton::clicked, this, &TaskCard::renameTarget);
    connect(m_formatBtn, &QToolButton::clicked, this, &TaskCard::openFormatPopup);
    connect(m_dstSize, &QToolButton::clicked, this, &TaskCard::openSizeDialog);
    connect(m_convert, &QPushButton::clicked, this, [this] { emit convertRequested(m_task); });
    connect(m_stop, &QPushButton::clicked, this, &TaskCard::stopRequested);
    connect(close, &QToolButton::clicked, this, [this] { emit removeRequested(m_task); });
    connect(m_openFolder, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_task->outputPath).absolutePath()));
    });
    connect(info, &QToolButton::clicked, this, [this] {
        const MediaInfo &mi = m_task->info;
        QString t = QStringLiteral("<b>%1</b><br><br>").arg(QFileInfo(m_task->input).fileName().toHtmlEscaped());
        if (mi.hasVideo)
            t += T("Видео: %1, %2×%3, %4 кадр/с%5<br>").arg(mi.videoCodec.toUpper()).arg(mi.width).arg(mi.height)
                     .arg(mi.fps, 0, 'f', 2).arg(mi.interlaced ? T(", чересстрочное") : QString());
        for (const StreamTrack &a : mi.audioTracks)
            t += T("Звук: %1%2<br>").arg(a.details().toHtmlEscaped(), a.isDefault ? T(" (по умолчанию)") : QString());
        for (const StreamTrack &s : mi.subtitleTracks)
            t += T("Субтитры: %1<br>").arg(s.details().toHtmlEscaped());
        t += T("<br>Битрейт: %1 кбит/с<br>Формат на выходе: %2 (%3)")
                 .arg(mi.bitrate / 1000).arg(m_task->preset.title().toHtmlEscaped(), m_task->preset.codecText());
        if (m_task->upscale.mode != UpscaleOptions::None)
            t += T("<br>Апскейл: %1%2").arg(UpscaleOptions::modeName(m_task->upscale.mode),
                                                        m_task->upscale.vhsRestore ? T(" + восстановление VHS") : QString());
        Ui::message(this, T("Сведения о файле"), t);
    });
    connect(m_subs, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
        if (m_updatingCombos)
            return;
        const int v = m_subs->currentData().toInt();
        if (v == -2) {
            const QString f = QFileDialog::getOpenFileName(this, T("Файл субтитров"), QFileInfo(m_task->input).absolutePath(),
                                                           T("Субтитры (*.srt *.ass *.ssa *.vtt)"));
            if (!f.isEmpty()) {
                m_task->edit.subtitleFile = f;
                m_task->edit.subtitleTrack = -1;
            }
        } else {
            m_task->edit.subtitleTrack = v;
            m_task->edit.subtitleFile.clear();
        }
        rebuildTrackCombos();
        emit changed();
    });
    connect(m_audio, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
        m_task->edit.audioTrack = m_audio->currentData().toInt();
        refresh();
        emit changed();
    });
    connect(m_upscale, QOverload<int>::of(&QComboBox::activated), this, [this](int idx) {
        const int v = m_upscale->itemData(idx).toInt();
        if (v == 100) {
            m_task->upscale.vhsRestore = !m_task->upscale.vhsRestore;
        } else {
            const auto mode = UpscaleOptions::Mode(v);
            if ((mode == UpscaleOptions::AI1080 || mode == UpscaleOptions::AI4K) && !Tools::hasRealesrgan()) {
                Ui::message(this, T("Нейросетевой апскейл"),
                            T("Модуль Real-ESRGAN не найден.<br><br>Положите папку realesrgan (с файлом realesrgan-ncnn-vulkan "
                                           "и папкой models) в папку tools рядом с программой. Пока можно использовать быстрый апскейл."));
            }
            m_task->upscale.mode = mode;
        }
        refresh();
        emit changed();
    });

    refresh();
}

void TaskCard::setInfoLoaded()
{
    rebuildTrackCombos();
    refresh();
    loadThumbnail();
}

void TaskCard::loadThumbnail()
{
    if (!m_task->info.hasVideo) {
        m_thumb->setPixmap(Ui::formatBadge(m_task->info.audioTracks.isEmpty() ? "?" : "AUDIO", 80));
        return;
    }
    TaskPtr t = m_task;
    auto img = QSharedPointer<QImage>::create();
    const double at = qMin(t->info.duration * 0.1, 20.0);
    Ui::runAsync(this, [t, at, img] { *img = FramePreview::grab(*t, at, false, 416); }, [this, img] {
        if (img->isNull())
            return;
        m_task->thumbnail = *img;
        QPixmap pm = QPixmap::fromImage(img->scaled(m_thumb->size() * 2, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        pm.setDevicePixelRatio(2.0);
        // Значок «play» поверх миниатюры
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const QSize ls = pm.size() / 2;
        const QPixmap play = Ui::icon("play", true).pixmap(44, 44);
        p.drawPixmap((ls.width() - 44) / 2, (ls.height() - 44) / 2, play);
        p.end();
        m_thumb->setPixmap(pm);
    });
}

bool TaskCard::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == m_thumb && ev->type() == QEvent::MouseButtonRelease && m_task->probed)
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_task->input));
    return QFrame::eventFilter(obj, ev);
}

void TaskCard::rebuildTrackCombos()
{
    m_updatingCombos = true;
    m_subs->clear();
    m_subs->addItem(T("Без субтитров"), -1);
    for (const StreamTrack &t : m_task->info.subtitleTracks)
        m_subs->addItem(t.details(), t.index);
    if (!m_task->edit.subtitleFile.isEmpty())
        m_subs->addItem(QFileInfo(m_task->edit.subtitleFile).fileName(), -3);
    m_subs->addItem(T("+ Добавить субтитры…"), -2);
    if (!m_task->edit.subtitleFile.isEmpty())
        m_subs->setCurrentIndex(m_subs->findData(-3));
    else
        m_subs->setCurrentIndex(qMax(0, m_subs->findData(m_task->edit.subtitleTrack)));

    m_audio->clear();
    for (const StreamTrack &t : m_task->info.audioTracks) {
        m_audio->addItem(Ui::icon("audio"), t.label(), t.index);
        m_audio->setItemData(m_audio->count() - 1, t.details(), Qt::ToolTipRole);
    }
    m_audio->addItem(T("Без звука"), -1);
    m_audio->setCurrentIndex(qMax(0, m_audio->findData(m_task->edit.audioTrack)));
    m_updatingCombos = false;
}

void TaskCard::refresh()
{
    const ConversionTask &t = *m_task;
    const MediaInfo &mi = t.info;
    const QString name = QFileInfo(t.input).fileName();
    m_srcName->setText(fontMetrics().elidedText(name, Qt::ElideMiddle, 360));
    m_srcName->setToolTip(t.input);

    if (!t.probed) {
        m_srcFmt->setText(QFileInfo(t.input).suffix().toUpper());
        m_srcRes->setText(QStringLiteral("…"));
        m_srcDur->setText(QStringLiteral("…"));
        m_srcSize->setText(formatSize(QFileInfo(t.input).size()));
        m_thumb->setText(T("Чтение файла…"));
    } else {
        m_srcFmt->setText(mi.container);
        m_srcRes->setText(mi.hasVideo ? QStringLiteral("%1*%2").arg(mi.width).arg(mi.height) : QStringLiteral("—"));
        m_srcDur->setText(formatDuration(mi.duration));
        m_srcSize->setText(formatSize(mi.size));
    }

    m_dstName->setText(fontMetrics().elidedText(t.outputFileName(), Qt::ElideMiddle, 330));
    m_dstName->setToolTip(t.outputFileName());
    m_dstFmt->setText(t.preset.ext.toUpper());
    const QSize os = t.outputSize();
    m_dstRes->setText(os.isValid() && !os.isEmpty() ? QStringLiteral("%1*%2").arg(os.width()).arg(os.height()) : QStringLiteral("—"));
    m_dstDur->setText(formatDuration(t.effectiveDuration()));
    m_dstSize->setText(t.probed ? formatSize(t.estimatedBytes()) : QStringLiteral("…"));

    // Подписи апскейла
    const bool video = mi.hasVideo && !t.preset.isAudioOnly();
    m_upscale->setEnabled(video);
    const int idx = m_upscale->findData(int(t.upscale.mode));
    m_upscale->setCurrentIndex(qMax(0, idx));
    const int vhsIdx = m_upscale->findData(100);
    if (vhsIdx >= 0)
        m_upscale->setItemText(vhsIdx, (t.upscale.vhsRestore ? QStringLiteral("☑ ") : QStringLiteral("☐ ")) + T("Восстановление VHS"));
    if (t.upscale.vhsRestore && idx >= 0)
        m_upscale->setItemText(idx, UpscaleOptions::modeName(t.upscale.mode) + QStringLiteral(" + VHS"));
    for (int i = 0; i < m_upscale->count(); ++i) {
        const int v = m_upscale->itemData(i).toInt();
        if (i != idx && v >= 0 && v <= 4)
            m_upscale->setItemText(i, UpscaleOptions::modeName(UpscaleOptions::Mode(v)));
    }
    m_editTools->setEnabled(t.probed && mi.hasVideo);

    const bool busy = t.status == ConversionTask::Running || t.status == ConversionTask::Waiting;
    if (busy) {
        m_actionStack->setCurrentIndex(1);
        m_progress->setValue(int(t.progress * 1000));
        m_status->setText(t.status == ConversionTask::Waiting ? T("В очереди")
                                                              : QStringLiteral("%1  %2%").arg(t.message).arg(int(t.progress * 100)));
    } else if (t.status == ConversionTask::Done) {
        m_actionStack->setCurrentIndex(2);
    } else {
        m_actionStack->setCurrentIndex(0);
        m_convert->setEnabled(t.probed && mi.ok);
        if (t.status == ConversionTask::Failed || t.status == ConversionTask::Cancelled) {
            m_convert->setText(t.status == ConversionTask::Failed ? T("Повторить") : T("Продолжить"));
            m_convert->setToolTip(t.message);
        } else {
            m_convert->setText(T("Конвертировать"));
            m_convert->setToolTip(t.probed && !mi.ok ? mi.error : QString());
        }
    }
    for (QWidget *w : {static_cast<QWidget *>(m_formatBtn), static_cast<QWidget *>(m_subs), static_cast<QWidget *>(m_audio),
                       static_cast<QWidget *>(m_upscale), static_cast<QWidget *>(m_dstSize), m_editTools})
        w->setEnabled(!busy && t.probed && (w != m_upscale || video) && (w != m_editTools || mi.hasVideo));

    if (t.probed && !mi.ok) {
        m_status->setText(mi.error);
        m_thumb->setText(T("Ошибка"));
        m_thumb->setToolTip(mi.error);
    }
}

void TaskCard::openFormatPopup()
{
    auto *popup = new FormatPopup(window());
    popup->setCurrent(m_task->preset.id);
    connect(popup, &FormatPopup::presetChosen, this, [this](const Preset &p) {
        m_task->preset = p;
        m_task->customVBitrate = 0;
        m_task->customResolution = QSize();
        refresh();
        emit changed();
    });
    popup->showBelow(m_formatBtn);
}

void TaskCard::openSizeDialog()
{
    OutputSizeDialog dlg(m_task, window());
    if (dlg.exec() == QDialog::Accepted) {
        m_task->customVBitrate = dlg.videoBitrate();
        m_task->customResolution = dlg.resolution();
        refresh();
        emit changed();
    }
}

void TaskCard::openEdit(int tab)
{
    EditDialog dlg(m_task, tab, window());
    if (dlg.exec() == QDialog::Accepted) {
        m_task->edit = dlg.options();
        rebuildTrackCombos();
        refresh();
        if (dlg.applyToAll())
            emit applyEditToAll(m_task->edit);
        emit changed();
    }
}

void TaskCard::openTrim()
{
    TrimDialog dlg(m_task, window());
    if (dlg.exec() == QDialog::Accepted) {
        m_task->edit.trimStart = dlg.start();
        m_task->edit.trimEnd = dlg.end();
        refresh();
        emit changed();
    }
}

void TaskCard::renameTarget()
{
    bool ok = false;
    const QString n = Ui::askText(this, T("Имя файла"), T("Новое имя (без расширения):"),
                                  m_task->outputName, &ok);
    QString clean = n.trimmed();
    for (const QChar c : QStringLiteral("\\/:*?\"<>|"))
        clean.remove(c);
    if (ok && !clean.isEmpty()) {
        m_task->outputName = clean;
        refresh();
    }
}
