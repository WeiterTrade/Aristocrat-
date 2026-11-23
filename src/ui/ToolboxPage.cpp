#include "ToolboxPage.h"
#include "core/Theme.h"
#include "core/I18n.h"
#include "Widgets.h"
#include "ChaptersDialog.h"
#ifdef HAVE_WEBENGINE
#include "ReleaseManager.h"
#endif
#include "core/MediaInfo.h"
#include "core/FfmpegCommand.h"
#include "core/Dvd.h"
#include "core/Release.h"
#include "core/DvdCss.h"
#include "Frameless.h"
#include "core/Settings.h"
#include "core/Tools.h"

#include <QComboBox>
#include <QScrollArea>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QMenu>
#include <QListWidget>
#include <QRadioButton>
#include <QCheckBox>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QAtomicInt>
#include <QPointer>
#include <QEventLoop>
#include <QUrl>
#include <QDesktopServices>
#include <QSettings>
#include <QTimer>
#include <QElapsedTimer>
#include <QProgressBar>
#include <QDir>
#include <QSharedPointer>
#include <QHBoxLayout>
#include <QVBoxLayout>

static QFrame *card(QWidget *parent, const QString &icon, const QString &title, const QString &text, QVBoxLayout *&body)
{
    auto *f = new QFrame(parent);
    f->setObjectName(QStringLiteral("SidePanel"));
    f->setAttribute(Qt::WA_StyledBackground);
    f->setMinimumWidth(380);
    body = new QVBoxLayout(f);
    body->setContentsMargins(24, 22, 24, 22);
    body->setSpacing(10);
    auto *head = new QHBoxLayout;
    head->addWidget(Ui::iconLabel(icon, 30, f));
    auto *t = new QLabel(title, f);
    Ui::themedStyle(t, QStringLiteral("color:#E8C266;font-size:13pt;"));
    head->addWidget(t, 1);
    body->addLayout(head);
    auto *d = new QLabel(text, f);
    d->setObjectName(QStringLiteral("Muted"));
    d->setWordWrap(true);
    body->addWidget(d);
    return f;
}

ToolboxPage::ToolboxPage(QWidget *parent)
    : QWidget(parent)
{
    // Карточек больше, чем помещается на маленьком экране — страница прокручивается
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("ToolboxContent"));
    scroll->setWidget(content);
    outer->addWidget(scroll);
    auto *root = new QVBoxLayout(content);
    root->setContentsMargins(40, 30, 40, 30);
    auto *grid = new QGridLayout;
    grid->setSpacing(24);

    // ---- Восстановление кассет ----
    QVBoxLayout *b1 = nullptr;
    auto *vhs = card(this, "upscale", T("Восстановление старых видео"),
                     T("Для оцифровок VHS, Video8, MiniDV и DVD: убирает «гребёнку», шум и дрожащую полосу внизу кадра, "
                                    "исправляет пропорции и увеличивает разрешение до 1080p или 4K."),
                     b1);
    auto *mode = new QComboBox(vhs);
    mode->addItem(UpscaleOptions::modeName(UpscaleOptions::Fast1080), int(UpscaleOptions::Fast1080));
    mode->addItem(UpscaleOptions::modeName(UpscaleOptions::Fast4K), int(UpscaleOptions::Fast4K));
    mode->addItem(UpscaleOptions::modeName(UpscaleOptions::AI1080), int(UpscaleOptions::AI1080));
    mode->addItem(UpscaleOptions::modeName(UpscaleOptions::AI4K), int(UpscaleOptions::AI4K));
    mode->setCurrentIndex(Tools::hasRealesrgan() ? 3 : 1);
    auto *pick = Ui::primaryButton(T("Выбрать записи…"), vhs);
    b1->addSpacing(6);
    b1->addWidget(mode);
    b1->addWidget(pick);
    auto *hint = new QLabel(T("Нейросетевой режим заметно лучше, но медленнее: на встроенной графике час записи "
                                           "может обрабатываться много часов. Работу можно прервать и продолжить позже."),
                            vhs);
    hint->setObjectName(QStringLiteral("Muted"));
    hint->setWordWrap(true);
    b1->addWidget(hint);
    b1->addStretch();
    connect(pick, &QPushButton::clicked, this, [this, mode] {
        const QStringList files = QFileDialog::getOpenFileNames(this, T("Записи для восстановления"), Settings::lastOpenDir(),
                                                                T("Видео (*.avi *.mpg *.mpeg *.vob *.mp4 *.mkv *.mov *.dv *.ts *.wmv);;Все файлы (*)"));
        if (files.isEmpty())
            return;
        Settings::setLastOpenDir(QFileInfo(files.first()).absolutePath());
        emit restoreRequested(files, UpscaleOptions::Mode(mode->currentData().toInt()), true);
    });

    // ---- Состояние модулей ----
    QVBoxLayout *b2 = nullptr;
    auto *mods = card(this, "toolbox", T("Модули программы"),
                      T("Внешние программы, которые использует конвертер. Их можно положить в папку tools рядом с программой."),
                      b2);
    auto status = [](const QString &name, const QString &path) {
        return Theme::map(QStringLiteral("<b>%1</b>: %2")).arg(name, path.isEmpty() ? T("<span style='color:#E07A6A'>не найден</span>")
                                                                        : T("<span style='color:#8FC48A'>найден</span><br>"
                                                                                         "<span style='color:#7E746A'>%1</span>").arg(path.toHtmlEscaped()));
    };
    auto *s = new QLabel(status("FFmpeg", Tools::ffmpeg()) + "<br><br>" + status("ffprobe", Tools::ffprobe()) + "<br><br>"
                             + status(T("Real-ESRGAN (нейросетевой апскейл)"), Tools::realesrgan()) + "<br><br>"
                             + status(T("libdvdcss (копирование защищённых DVD)"), DvdCss::libraryPath()),
                         mods);
    s->setWordWrap(true);
    s->setTextInteractionFlags(Qt::TextSelectableByMouse);
    b2->addWidget(s);
    b2->addStretch();

    // ---- Копирование DVD ----
    QVBoxLayout *b3 = nullptr;
    auto *dvd = card(this, "dvd", T("Копирование DVD"),
                     T("Сохраняет фильмы с DVD на компьютер: копия без потерь в MKV со всеми звуковыми дорожками и субтитрами, "
                       "MP4 для любого плеера или конвертация в любой формат. Диски с защитой CSS тоже копируются."),
                     b3);
    auto *pickDvd = Ui::primaryButton(T("Выбрать диск или папку…"), dvd);
    b3->addSpacing(6);
    b3->addWidget(pickDvd);
    auto *dvdNote = new QLabel(DvdCss::available()
                                   ? T("Защита CSS снимается библиотекой libdvdcss (как в VLC и HandBrake). Копируйте свои диски для личного просмотра.")
                                   : T("Для дисков с защитой CSS нужна libdvdcss: положите libdvdcss-2.dll в папку tools рядом с программой."),
                               dvd);
    dvdNote->setObjectName(QStringLiteral("Muted"));
    dvdNote->setWordWrap(true);
    b3->addWidget(dvdNote);
    b3->addStretch();
    connect(pickDvd, &QPushButton::clicked, this, [this, pickDvd] {
        QMenu menu(this);
        for (const QString &v : Dvd::drives()) {
            const QString name = Dvd::discName(v);
            menu.addAction(Ui::icon("dvd", true), QStringLiteral("%1  (%2)").arg(name, QDir::toNativeSeparators(v)),
                           this, [this, v] { openDvd(v); });
        }
        if (!menu.isEmpty())
            menu.addSeparator();
        menu.addAction(T("Выбрать папку VIDEO_TS…"), this, [this] {
            const QString d = QFileDialog::getExistingDirectory(this, T("Папка DVD (VIDEO_TS)"), Settings::lastOpenDir());
            if (!d.isEmpty())
                openDvd(d);
        });
        menu.exec(pickDvd->mapToGlobal(QPoint(0, pickDvd->height())));
    });

    // ---- Главы (сцены) ----
    QVBoxLayout *b4 = nullptr;
    auto *chap = card(this, "film", T("Главы (сцены)"),
                      T("Вставка глав в MKV и MP4, как в MKVToolNix. По ссылке на фильм Adult Empire сцены и тайм-коды "
                        "заполняются сами, названия переводятся на русский — всё можно поправить перед записью."),
                      b4);
    auto *openChap = Ui::primaryButton(T("Открыть редактор глав…"), chap);
    b4->addSpacing(6);
    b4->addWidget(openChap);
    b4->addStretch();
    connect(openChap, &QPushButton::clicked, this, [this] {
        ChaptersDialog d(QString(), QString(), window());
        connect(&d, &ChaptersDialog::convertRequested, this,
                [this](const QString &in, const QString &name, const QString &dir, const QString &meta) {
                    emit releaseVideo(in, name, dir, false, meta);
                });
        d.exec();
    });

    // ---- Adult Empire Менеджер ----
    QVBoxLayout *b5 = nullptr;
    auto *rel = card(this, "folder", T("Adult Empire Менеджер"),
                     T("По ссылке на релиз создаёт папку для медиатеки: NFO с переводом названия и описания, постер, заднюю "
                       "обложку, фан-арт, фото актрис, видео с нужным именем и главами. Готовый релиз выгружается в облако "
                       "(Google Диск, Яндекс Диск, Облако Mail.ru, OneDrive) или в любую папку."),
                     b5);
    auto *openRel = Ui::primaryButton(T("Создать релиз…"), rel);
    auto *upRel = new QPushButton(T("Выгрузить релиз…"), rel);
    auto *relBtns = new QHBoxLayout;
    relBtns->addWidget(openRel);
    relBtns->addWidget(upRel);
    relBtns->addStretch();
    b5->addSpacing(6);
    b5->addLayout(relBtns);
    connect(upRel, &QPushButton::clicked, this, [this] { openRelease(QString(), QString(), QString(), 1); });
    b5->addStretch();
    connect(openRel, &QPushButton::clicked, this, [this] { openRelease(); });
#ifndef HAVE_WEBENGINE
    openRel->setEnabled(false);
    upRel->setEnabled(false);
#endif

    grid->addWidget(vhs, 0, 0);
    grid->addWidget(dvd, 0, 1);
    grid->addWidget(chap, 1, 0);
    grid->addWidget(rel, 1, 1);
    grid->addWidget(mods, 2, 0, 1, 2);
    root->addLayout(grid);
    root->addStretch();
}

void ToolboxPage::openRelease(const QString &url, const QString &video, const QString &scenesUrl, int tab)
{
#ifdef HAVE_WEBENGINE
    auto *d = new ReleaseManagerDialog(url, video, window(), scenesUrl);
    d->setAttribute(Qt::WA_DeleteOnClose);
    const QRect host = window()->geometry();
    d->setMinimizable();   // можно свернуть, пока качается видео релиза
    d->move(host.center() - QPoint(d->width() / 2, d->height() / 2));
    connect(d, &ReleaseManagerDialog::videoRequested, this, &ToolboxPage::releaseVideo);
    connect(d, &ReleaseManagerDialog::downloadRequested, this, &ToolboxPage::releaseDownload);
    if (tab == 1)
        d->showUploadTab();
    else if (tab == 2)
        d->showServerTab();
    else if (tab >= 3)
        d->showTab(tab);
    d->show();
#else
    Q_UNUSED(url)
    Q_UNUSED(video)
    Q_UNUSED(scenesUrl)
    Q_UNUSED(tab)
#endif
}

// ======================= Копирование DVD =======================

namespace {
struct ScannedTitle {
    DvdTitle title;
    MediaInfo info;
};
}

void ToolboxPage::openDvd(const QString &path)
{
    const QString videoTs = Dvd::videoTsDir(path);
    if (videoTs.isEmpty()) {
        Ui::message(this, T("Копирование DVD"), T("В выбранной папке нет папки VIDEO_TS."));
        return;
    }
    // Диск в приводе или смонтированный образ: фильмы — по таблицам IFO, чтение — напрямую с диска (как DVDFab).
    // Так не трогаются «ловушки» защиты, из-за которых чтение через файлы тянется десятки минут.
    if (DvdCss::available()) {
        const QString device = DvdCss::deviceFor(videoTs);
        if (!device.isEmpty()) {
            openDisc(device, videoTs);
            return;
        }
    }
    const QString disc = Dvd::discName(videoTs);
    const QList<DvdTitle> titles = Dvd::titles(videoTs);
    if (titles.isEmpty()) {
        Ui::message(this, T("Копирование DVD"), T("На диске не найдено фильмов (файлов VTS_xx_x.VOB)."));
        return;
    }

    FramedDialog d(this);
    d.setWindowTitle(T("Копирование DVD — %1").arg(disc));
    d.setMinimumSize(640, 520);
    auto *lay = d.contentLayout();
    lay->setContentsMargins(24, 18, 24, 18);
    lay->setSpacing(12);
    auto *state = new QLabel(T("Чтение диска…"), &d);
    state->setObjectName(QStringLiteral("StatusLabel"));
    state->setWordWrap(true);
    lay->addWidget(state);
    auto *list = new QListWidget(&d);
    lay->addWidget(list, 1);
    auto *lossless = new QRadioButton(T("Копия без потерь (MKV, все звуковые дорожки и субтитры) — для VLC, Jellyfin, MPC"), &d);
    auto *compat = new QRadioButton(T("MP4 для любого плеера, включая Windows Media Player и телевизоры (H.264 + AAC)"), &d);
    auto *convert = new QRadioButton(T("Конвертировать в формат, выбранный в разделе «Конвертировать»"), &d);
    lossless->setChecked(true);
    lay->addWidget(lossless);
    lay->addWidget(compat);
    lay->addWidget(convert);
    auto *bar = new QProgressBar(&d);
    bar->setRange(0, 100);
    bar->hide();
    lay->addWidget(bar);
    auto *btns = new QHBoxLayout;
    btns->addStretch();
    auto *cancel = new QPushButton(T("Отмена"), &d);
    auto *go = Ui::primaryButton(T("Начать"), &d);
    go->setEnabled(false);
    btns->addWidget(cancel);
    btns->addWidget(go);
    lay->addLayout(btns);
    connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
    connect(go, &QPushButton::clicked, &d, &QDialog::accept);

    const QString cssDevice = DvdCss::deviceFor(videoTs);
    // Чтение сведений о фильмах и проверка защиты — в фоне
    auto scanned = QSharedPointer<QList<ScannedTitle>>::create();
    auto encrypted = QSharedPointer<bool>::create(false);
    Ui::runAsync(&d, [titles, scanned, encrypted] {
        for (const DvdTitle &t : titles)
            scanned->append({t, MediaInfo::probe(t.input())});
        if (!scanned->isEmpty())
            *encrypted = FfmpegCommand::looksEncrypted(scanned->first().title.input(), scanned->first().info.duration);
    }, [&d, list, state, go, scanned, encrypted, cssDevice] {
        for (int i = 0; i < scanned->size(); ++i) {
            const ScannedTitle &s = scanned->at(i);
            const QString text = T("Фильм %1 · %2 · %3 · звук: %4, субтитры: %5")
                                     .arg(s.title.vts).arg(formatDuration(s.info.duration)).arg(formatSize(s.title.size))
                                     .arg(s.info.audioTracks.size()).arg(s.info.subtitleTracks.size());
            auto *it = new QListWidgetItem(text, list);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            // Основной фильм (самый большой) и всё длиннее 10 минут — отмечены
            it->setCheckState(i == 0 || s.info.duration > 600 ? Qt::Checked : Qt::Unchecked);
            it->setData(Qt::UserRole, s.title.input());
            it->setData(Qt::UserRole + 1, s.title.vts);
        }
        if (*encrypted && !DvdCss::available()) {
            state->setText(T("Диск защищён от копирования (CSS). Чтобы его скопировать, нужна библиотека libdvdcss "
                             "(libdvdcss-2.dll в папке tools рядом с программой)."));
            go->setEnabled(false);
        } else if (*encrypted && cssDevice.isEmpty()) {
            state->setText(T("Файлы защищены (CSS), но это папка на компьютере, а не диск: ключи защиты есть только на самом диске. "
                             "Выберите DVD-привод или смонтированный образ."));
            go->setEnabled(false);
        } else if (*encrypted) {
            state->setText(T("Диск защищён от копирования (CSS) — защита будет снята при чтении. Отметьте фильмы, которые нужно сохранить."));
            go->setEnabled(true);
        } else {
            state->setText(T("Диск без защиты от копирования. Отметьте фильмы, которые нужно сохранить."));
            go->setEnabled(true);
        }
        d.adjustSize();
    });

    // Защищённый диск: выбранные фильмы сначала читаются с диска со снятием защиты во временную папку,
    // дальше — как обычный диск. Временная папка удаляется после конвертации.
    QString tempDir;
    auto run = [&]() -> bool {
        QStringList sets;
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->checkState() == Qt::Checked)
                sets << list->item(i)->data(Qt::UserRole + 1).toString();
        if (sets.isEmpty())
            return false;
        if (!*encrypted)
            return true;
        tempDir = QDir(Settings::outputDir()).filePath(QStringLiteral(".aristocrat-temp/") + Release::safeName(disc));
        const QString outTs = QDir(tempDir).filePath(QStringLiteral("VIDEO_TS"));
        QDir().mkpath(outTs);
#ifdef Q_OS_WIN
        SetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(QFileInfo(tempDir).absolutePath()).utf16()), FILE_ATTRIBUTE_HIDDEN);
#endif
        for (QWidget *w : {static_cast<QWidget *>(list), static_cast<QWidget *>(lossless), static_cast<QWidget *>(compat),
                           static_cast<QWidget *>(convert), static_cast<QWidget *>(go)})
            w->setEnabled(false);
        bar->show();
        bar->setValue(0);
        state->setText(T("Чтение диска со снятием защиты… Это займёт несколько минут (скорость привода)."));
        auto cancelled = QSharedPointer<QAtomicInt>::create(0);
        auto err = QSharedPointer<QString>::create();
        auto bad = QSharedPointer<int>::create(0);
        auto ok = QSharedPointer<bool>::create(false);
        QEventLoop loop;
        auto conn = connect(cancel, &QPushButton::clicked, &d, [cancelled] { cancelled->storeRelaxed(1); });
        QPointer<QProgressBar> pb(bar);
        Ui::runAsync(&d, [cssDevice, sets, outTs, cancelled, err, bad, ok, pb] {
            *ok = DvdCss::backup(cssDevice, sets, outTs, [cancelled, pb](int pct) {
                QMetaObject::invokeMethod(pb, [pb, pct] { if (pb) pb->setValue(pct); }, Qt::QueuedConnection);
                return cancelled->loadRelaxed() == 0;
            }, err.data(), bad.data());
        }, [&loop] { loop.quit(); });
        loop.exec();
        disconnect(conn);
        if (!*ok) {
            QDir(tempDir).removeRecursively();
            if (cancelled->loadRelaxed() == 0)
                Ui::message(this, T("Копирование DVD"), T("Не удалось прочитать диск: %1").arg(*err));
            return false;
        }
        if (*bad > 0)
            state->setText(T("Диск прочитан, нечитаемых секторов: %1 (обычно это ловушки защиты, на фильм не влияют).").arg(*bad));
        // Входы задач — уже расшифрованные файлы
        const QList<DvdTitle> plain = Dvd::titles(outTs);
        for (int i = 0; i < list->count(); ++i)
            for (const DvdTitle &t : plain)
                if (t.vts == list->item(i)->data(Qt::UserRole + 1).toString())
                    list->item(i)->setData(Qt::UserRole, t.input());
        return true;
    };
    connect(go, &QPushButton::clicked, &d, [&d, &run] {
        if (run())
            d.accept();
    });
    disconnect(go, &QPushButton::clicked, &d, &QDialog::accept);

    if (d.exec() != QDialog::Accepted)
        return;
    QList<QPair<QString, QString>> chosen;
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem *it = list->item(i);
        if (it->checkState() == Qt::Checked)
            chosen.append({it->data(Qt::UserRole).toString(),
                           list->count() > 1 ? QStringLiteral("%1 — %2").arg(disc, it->data(Qt::UserRole + 1).toString()) : disc});
    }
    if (!chosen.isEmpty())
        emit dvdRequested(chosen, lossless->isChecked() ? 0 : compat->isChecked() ? 1 : 2, tempDir);
}

void ToolboxPage::openDisc(const QString &device, const QString &videoTs)
{
    const QString disc = Dvd::discName(videoTs);
    FramedDialog d(this);
    d.setWindowTitle(T("Копирование DVD — %1").arg(disc));
    d.setMinimumSize(720, 540);
    auto *lay = d.contentLayout();
    lay->setContentsMargins(24, 18, 24, 18);
    lay->setSpacing(12);
    auto *state = new QLabel(T("Чтение оглавления диска…"), &d);
    state->setObjectName(QStringLiteral("StatusLabel"));
    state->setWordWrap(true);
    lay->addWidget(state);
    auto *list = new QListWidget(&d);
    lay->addWidget(list, 1);
    auto *showAll = new QCheckBox(T("Показать короткие ролики (меньше 2 минут)"), &d);
    lay->addWidget(showAll);
    auto *lossless = new QRadioButton(T("Копия без потерь (MKV, все звуковые дорожки и субтитры) — для VLC, Jellyfin, MPC"), &d);
    auto *compat = new QRadioButton(T("MP4 для любого плеера, включая Windows Media Player и телевизоры (H.264 + AAC)"), &d);
    auto *convert = new QRadioButton(T("Конвертировать в формат, выбранный в разделе «Конвертировать»"), &d);
    auto *iso = new QRadioButton(T("ISO-образ всего диска — полная копия: меню, все фильмы, дорожки, субтитры и бонусы"), &d);
    lossless->setChecked(true);
    lay->addWidget(lossless);
    lay->addWidget(compat);
    lay->addWidget(convert);
    lay->addWidget(iso);
    connect(iso, &QRadioButton::toggled, list, [list](bool on) { list->setEnabled(!on); });
    // Скорость привода: на медленной меньше сбоев (особенно у переносных приводов с питанием по USB)
    auto *speedRow = new QHBoxLayout;
    speedRow->addWidget(new QLabel(T("Скорость привода:"), &d));
    auto *speedBox = new QComboBox(&d);
    speedBox->addItem(T("Максимальная (снижается сама при сбоях)"), 0);
    speedBox->addItem(QStringLiteral("8x"), 8);
    speedBox->addItem(T("4x — для переносных приводов"), 4);
    speedBox->addItem(T("2x — для проблемных дисков"), 2);
    speedBox->setCurrentIndex(qBound(0, QSettings().value(QStringLiteral("dvd/speedIndex"), 0).toInt(), 3));
    connect(speedBox, QOverload<int>::of(&QComboBox::currentIndexChanged), &d, [](int i) { QSettings().setValue(QStringLiteral("dvd/speedIndex"), i); });
    speedRow->addWidget(speedBox);
    speedRow->addStretch();
    lay->addLayout(speedRow);
    auto *region = new QLabel(&d);
    region->setObjectName(QStringLiteral("HintLabel"));
    region->setWordWrap(true);
    region->hide();
    lay->addWidget(region);
    auto regionText = QSharedPointer<QString>::create();
    auto *bar = new QProgressBar(&d);
    bar->setRange(0, 100);
    bar->hide();
    lay->addWidget(bar);
    auto *btns = new QHBoxLayout;
    btns->addStretch();
    auto *cancel = new QPushButton(T("Отмена"), &d);
    auto *go = Ui::primaryButton(T("Начать"), &d);
    go->setEnabled(false);
    btns->addWidget(cancel);
    btns->addWidget(go);
    lay->addLayout(btns);
    connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);

    auto titles = QSharedPointer<QList<DvdCss::DiscTitle>>::create();
    auto err = QSharedPointer<QString>::create();
    auto fill = [list, titles, showAll] {
        list->clear();
        for (int i = 0; i < titles->size(); ++i) {
            const DvdCss::DiscTitle &t = titles->at(i);
            if (t.seconds < 120 && !t.main && !showAll->isChecked())
                continue;
            QStringList audio;
            for (int a = 0; a < t.audio.size(); ++a)
                audio << QStringLiteral("%1 %2").arg(t.audio[a], t.audioFormat.value(a));
            QString text = T("Фильм %1 · %2 · %3 · глав: %4").arg(t.number).arg(formatDuration(t.seconds)).arg(formatSize(t.bytes)).arg(t.chapters);
            if (!audio.isEmpty())
                text += T(" · звук: %1").arg(audio.join(QStringLiteral(", ")));
            if (!t.subs.isEmpty())
                text += T(" · субтитры: %1").arg(t.subs.join(QStringLiteral(", ")));
            if (t.main)
                text = T("★ Основной фильм — ") + text;
            else if (t.jumps > 2)
                text += T(" · ложный (защита диска)");
            auto *it = new QListWidgetItem(text, list);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(t.main ? Qt::Checked : Qt::Unchecked);
            it->setData(Qt::UserRole, i);
        }
    };
    connect(showAll, &QCheckBox::toggled, &d, fill);
    Ui::runAsync(&d, [device, videoTs, titles, err, regionText] {
        *titles = DvdCss::discTitles(device, videoTs, err.data());
        *regionText = DvdCss::regionInfo(device);
    },
                 [&d, titles, err, state, go, fill, region, regionText] {
                     if (!regionText->isEmpty()) {
                         region->setText(*regionText);
                         region->show();
                     }
                     if (titles->isEmpty()) {
                         state->setText(T("Не удалось прочитать оглавление диска: %1").arg(*err));
                         return;
                     }
                     fill();
                     int fakes = 0;
                     for (const auto &t : *titles)
                         fakes += (!t.main && t.jumps > 2) ? 1 : 0;
                     state->setText(fakes > 0 ? T("На диске защита: %1 ложных фильмов. Основной фильм определён и отмечен — "
                                                  "он будет прочитан напрямую с диска со снятием защиты.").arg(fakes)
                                              : T("Основной фильм отмечен. Он будет прочитан напрямую с диска (защита CSS снимается при чтении)."));
                     go->setEnabled(true);
                     d.adjustSize();
                 });

    QList<QPair<QString, QString>> chosen;
    QString tempDir;
    connect(go, &QPushButton::clicked, &d, [&] {
        // Что читаем: выбранные фильмы (во временную папку, дальше — конвертер) или весь диск в ISO
        struct Job {
            QString name, file;
            qint64 total = 0;   // 0 — неизвестно заранее (ISO): остаток считается по проценту
            DvdCss::DiscTitle title;
            bool iso = false;
        };
        QList<Job> jobs;
        const int speed = speedBox->currentData().toInt();
        if (iso->isChecked()) {
            Job j;
            j.name = disc;
            j.iso = true;
            QString target = QDir(Settings::outputDir()).filePath(Release::safeName(disc) + QStringLiteral(".iso"));
            for (int n = 2; QFileInfo::exists(target); ++n)
                target = QDir(Settings::outputDir()).filePath(QStringLiteral("%1 (%2).iso").arg(Release::safeName(disc)).arg(n));
            j.file = target;
            jobs << j;
        } else {
            QList<int> picks;
            for (int i = 0; i < list->count(); ++i)
                if (list->item(i)->checkState() == Qt::Checked)
                    picks << list->item(i)->data(Qt::UserRole).toInt();
            if (picks.isEmpty())
                return;
            tempDir = QDir(Settings::outputDir()).filePath(QStringLiteral(".aristocrat-temp/") + Release::safeName(disc));
            QDir().mkpath(tempDir);
#ifdef Q_OS_WIN
            SetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(QFileInfo(tempDir).absolutePath()).utf16()), FILE_ATTRIBUTE_HIDDEN);
#endif
            for (int k = 0; k < picks.size(); ++k) {
                Job j;
                j.title = titles->at(picks[k]);
                j.name = picks.size() > 1 || !j.title.main ? T("%1 — фильм %2").arg(disc).arg(j.title.number) : disc;
                j.file = QDir(tempDir).filePath(Release::safeName(j.name) + QStringLiteral(".vob"));
                j.total = j.title.bytes;
                jobs << j;
            }
        }
        for (QWidget *w : {static_cast<QWidget *>(list), static_cast<QWidget *>(lossless), static_cast<QWidget *>(compat),
                           static_cast<QWidget *>(convert), static_cast<QWidget *>(iso), static_cast<QWidget *>(go),
                           static_cast<QWidget *>(showAll), static_cast<QWidget *>(speedBox)})
            w->setEnabled(false);
        bar->show();
        auto cancelled = QSharedPointer<QAtomicInt>::create(0);
        auto conn = connect(cancel, &QPushButton::clicked, &d, [cancelled] { cancelled->storeRelaxed(1); });
        for (int k = 0; k < jobs.size(); ++k) {
            const Job job = jobs[k];
            const QString head = job.iso ? T("Полная копия диска в ISO со снятием защиты: %1").arg(QDir::toNativeSeparators(job.file))
                                         : T("Чтение с диска со снятием защиты: %1 (%2 из %3).").arg(job.name).arg(k + 1).arg(jobs.size());
            state->setText(head);
            bar->setValue(0);
            auto e = QSharedPointer<QString>::create();
            auto bad = QSharedPointer<int>::create(0);
            auto ok = QSharedPointer<bool>::create(false);
            // Ход чтения — из рабочего потока; окно раз в секунду показывает скорость, остаток и пропуски
            auto doneBytes = QSharedPointer<QAtomicInteger<qint64>>::create(0);
            auto badNow = QSharedPointer<QAtomicInt>::create(0);
            auto pctNow = QSharedPointer<QAtomicInt>::create(0);
            auto finished = QSharedPointer<QAtomicInt>::create(0);
            QElapsedTimer clock;
            clock.start();
            qint64 lastBytes = 0, lastChange = 0;
            QTimer tick;
            tick.setInterval(1000);
            QPointer<QEventLoop> loop = new QEventLoop(&d);
            connect(&tick, &QTimer::timeout, &d, [&, head, job] {
                const qint64 now = doneBytes->loadRelaxed();
                const qint64 ms = clock.elapsed();
                const int pct = pctNow->loadRelaxed();
                if (now != lastBytes) {
                    lastBytes = now;
                    lastChange = ms;
                }
                bar->setValue(pct);
                const double mbps = ms > 3000 ? double(now) / 1048576.0 / (ms / 1000.0) : 0;
                QString line = job.total > 0 ? T("Прочитано %1 из %2").arg(formatSize(now), formatSize(job.total))
                                             : T("Прочитано %1 (%2%)").arg(formatSize(now)).arg(pct);
                if (mbps > 0) {
                    line += T(" · %1 МБ/с").arg(QString::number(mbps, 'f', 1));
                    const double left = job.total > 0 ? double(job.total - now) / 1048576.0 / mbps
                                                      : (pct > 0 ? ms / 1000.0 * (100 - pct) / pct : 0);
                    if (left > 0)
                        line += T(" · осталось ≈ %1").arg(formatDuration(left));
                }
                if (badNow->loadRelaxed() > 0)
                    line += T(" · нечитаемого: %1").arg(formatSize(qint64(badNow->loadRelaxed()) * 2048));
                if (ms - lastChange > 60000)
                    line += QStringLiteral("\n") + T("Привод не отвечает уже %1 мин — программа даёт ему паузу, снижает скорость и пробует снова. "
                                                     "Можно подождать или нажать «Отмена».").arg((ms - lastChange) / 60000);
                state->setText(head + QStringLiteral("\n") + line);
            });
            tick.start();
            Ui::runAsync(&d, [device, job, speed, cancelled, e, bad, ok, doneBytes, badNow, pctNow] {
                auto cb = [cancelled, doneBytes, badNow, pctNow](int pct, qint64 bytes, int b) {
                    pctNow->storeRelaxed(pct);
                    doneBytes->storeRelaxed(bytes);
                    badNow->storeRelaxed(b);
                    return cancelled->loadRelaxed() == 0;
                };
                *ok = job.iso ? DvdCss::isoBackup(device, job.file, speed, cb, e.data(), bad.data())
                              : DvdCss::rip(device, job.title, job.file, speed, cb, e.data(), bad.data());
                if (!*ok && (cancelled->loadRelaxed() || job.iso))
                    QFile::remove(job.file);   // отменено/не вышло — недочитанный файл не нужен
            }, [loop, finished] {
                finished->storeRelaxed(1);
                if (loop)
                    loop->quit();
            });
            // «Отмена» закрывает окно сразу, даже если привод завис на чтении: поток доработает сам и бросит файл
            auto stop = connect(cancel, &QPushButton::clicked, &d, [loop] { if (loop) loop->quit(); });
            loop->exec();
            disconnect(stop);
            tick.stop();
            delete loop.data();
            if (!finished->loadRelaxed() || !*ok) {
                disconnect(conn);
                if (finished->loadRelaxed() && !tempDir.isEmpty())
                    QDir(tempDir).removeRecursively();
                chosen.clear();
                if (cancelled->loadRelaxed() == 0)
                    Ui::message(this, T("Копирование DVD"), T("Не удалось прочитать диск: %1").arg(*e));
                return;
            }
            const qint64 badBytes = qint64(*bad) * 2048;
            const qint64 whole = job.total > 0 ? job.total : QFileInfo(job.file).size();
            if (whole > 0 && badBytes * 2 > whole) {
                // Больше половины не прочиталось — это не царапины: привод не отдаёт зашифрованные сектора
                disconnect(conn);
                if (!tempDir.isEmpty())
                    QDir(tempDir).removeRecursively();
                QFile::remove(job.file);
                chosen.clear();
                Ui::message(this, T("Копирование DVD"),
                            T("Привод почти ничего не отдал (нечитаемо %1 из %2). Так бывает, когда регион привода не совпадает "
                              "с регионом диска, или приводу не хватает питания по USB. %3\n\nПопробуйте скорость 2x и подключение "
                              "привода прямо к ноутбуку (оба штекера, если их два).")
                                .arg(formatSize(badBytes), formatSize(whole), *regionText));
                return;
            }
            if (badBytes > 0)
                Ui::message(this, T("Копирование DVD"),
                            job.iso ? T("Образ сохранён. Нечитаемых секторов: %1 — записаны нулями. Если диск чистый, это ловушки "
                                        "защиты в неиспользуемых местах диска — на просмотр не влияют.").arg(formatSize(badBytes))
                                    : T("Фильм прочитан. Не прочиталось даже после повторов на малой скорости: %1 — в этих местах "
                                        "возможны короткие рывки.").arg(formatSize(badBytes)));
            if (job.iso) {
                if (badBytes == 0)
                    Ui::message(this, T("Копирование DVD"), T("ISO-образ сохранён: %1").arg(QDir::toNativeSeparators(job.file)));
                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(job.file).absolutePath()));
            } else {
                chosen.append({job.file, job.name});
            }
        }
        disconnect(conn);
        d.accept();
    });

    if (d.exec() != QDialog::Accepted || chosen.isEmpty())
        return;
    emit dvdRequested(chosen, lossless->isChecked() ? 0 : compat->isChecked() ? 1 : 2, tempDir);
}
