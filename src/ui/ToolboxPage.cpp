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
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QAtomicInt>
#include <QPointer>
#include <QEventLoop>
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
