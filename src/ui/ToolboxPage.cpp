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
                             + status(T("Real-ESRGAN (нейросетевой апскейл)"), Tools::realesrgan()),
                         mods);
    s->setWordWrap(true);
    s->setTextInteractionFlags(Qt::TextSelectableByMouse);
    b2->addWidget(s);
    b2->addStretch();

    // ---- Копирование DVD ----
    QVBoxLayout *b3 = nullptr;
    auto *dvd = card(this, "dvd", T("Копирование DVD"),
                     T("Сохраняет фильмы с DVD на компьютер: копия без потерь в MKV со всеми звуковыми дорожками и субтитрами "
                       "или конвертация в любой формат. Подходит для дисков с домашним видео, оцифровками кассет и записями с ТВ."),
                     b3);
    auto *pickDvd = Ui::primaryButton(T("Выбрать диск или папку…"), dvd);
    b3->addSpacing(6);
    b3->addWidget(pickDvd);
    auto *dvdNote = new QLabel(T("Диски с защитой от копирования (CSS) программа не копирует — обход защиты запрещён законом."), dvd);
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
        d.exec();
    });

    // ---- Adult Empire Менеджер ----
    QVBoxLayout *b5 = nullptr;
    auto *rel = card(this, "folder", T("Adult Empire Менеджер"),
                     T("По ссылке на релиз создаёт папку для медиатеки: NFO с переводом названия и описания, постер, заднюю "
                       "обложку, фан-арт, фото актрис, видео с нужным именем и главами. Перед созданием всё можно проверить."),
                     b5);
    auto *openRel = Ui::primaryButton(T("Создать релиз…"), rel);
    b5->addSpacing(6);
    b5->addWidget(openRel);
    b5->addStretch();
    connect(openRel, &QPushButton::clicked, this, [this] { openRelease(); });
#ifndef HAVE_WEBENGINE
    openRel->setEnabled(false);
#endif

    grid->addWidget(vhs, 0, 0);
    grid->addWidget(dvd, 0, 1);
    grid->addWidget(chap, 1, 0);
    grid->addWidget(rel, 1, 1);
    grid->addWidget(mods, 2, 0, 1, 2);
    root->addLayout(grid);
    root->addStretch();
}

void ToolboxPage::openRelease(const QString &url, const QString &video)
{
#ifdef HAVE_WEBENGINE
    auto *d = new ReleaseManagerDialog(url, video, window());
    d->setAttribute(Qt::WA_DeleteOnClose);
    connect(d, &ReleaseManagerDialog::videoRequested, this, &ToolboxPage::releaseVideo);
    d->show();
#else
    Q_UNUSED(url)
    Q_UNUSED(video)
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
    auto *lossless = new QRadioButton(T("Копия без потерь (MKV, все звуковые дорожки и субтитры)"), &d);
    auto *convert = new QRadioButton(T("Конвертировать в формат, выбранный в разделе «Конвертировать»"), &d);
    lossless->setChecked(true);
    lay->addWidget(lossless);
    lay->addWidget(convert);
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

    // Чтение сведений о фильмах и проверка защиты — в фоне
    auto scanned = QSharedPointer<QList<ScannedTitle>>::create();
    auto encrypted = QSharedPointer<bool>::create(false);
    Ui::runAsync(&d, [titles, scanned, encrypted] {
        for (const DvdTitle &t : titles)
            scanned->append({t, MediaInfo::probe(t.input())});
        if (!scanned->isEmpty())
            *encrypted = FfmpegCommand::looksEncrypted(scanned->first().title.input(), scanned->first().info.duration);
    }, [&d, list, state, go, scanned, encrypted] {
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
        if (*encrypted) {
            state->setText(T("Этот диск защищён от копирования (CSS). Программа не снимает защиту — копирование такого диска "
                             "запрещено законом. Диски с домашним видео, оцифровками и записями с ТВ обычно не защищены."));
            go->setEnabled(false);
        } else {
            state->setText(T("Диск без защиты от копирования. Отметьте фильмы, которые нужно сохранить."));
            go->setEnabled(true);
        }
        d.adjustSize();
    });

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
        emit dvdRequested(chosen, lossless->isChecked());
}
