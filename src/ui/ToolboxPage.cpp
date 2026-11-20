#include "ToolboxPage.h"
#include "core/I18n.h"
#include "Widgets.h"
#include "core/Settings.h"
#include "core/Tools.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
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
    t->setStyleSheet(QStringLiteral("color:#E8C266;font-size:13pt;"));
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
    auto *root = new QVBoxLayout(this);
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
        return QStringLiteral("<b>%1</b>: %2").arg(name, path.isEmpty() ? T("<span style='color:#E07A6A'>не найден</span>")
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

    grid->addWidget(vhs, 0, 0);
    grid->addWidget(mods, 0, 1);
    root->addLayout(grid);
    root->addStretch();
}
