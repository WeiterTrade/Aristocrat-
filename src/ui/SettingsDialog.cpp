#include "SettingsDialog.h"
#include "Widgets.h"
#include "core/I18n.h"
#include "core/Settings.h"
#include "core/Theme.h"
#include "core/Tools.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>
#include <functional>

static QCheckBox *check(const QString &text, QWidget *parent)
{
    auto *c = new QCheckBox(text, parent);
    c->setObjectName(QStringLiteral("PlainCheck"));
    return c;
}

static QWidget *page(QWidget *parent, QVBoxLayout *&lay)
{
    auto *w = new QWidget(parent);
    lay = new QVBoxLayout(w);
    lay->setContentsMargins(34, 26, 34, 16);
    lay->setSpacing(14);
    return w;
}

static QString defaultConverted()
{
    QString d = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (d.isEmpty()) d = QDir::homePath();
    return d + QStringLiteral("/Aristocrat Видеоконвертер");
}

static QString defaultDownloaded()
{
    QString d = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (d.isEmpty()) d = QDir::homePath();
    return d + QStringLiteral("/Aristocrat Видеоконвертер");
}

static QString defaultBurned()
{
    return defaultConverted() + QStringLiteral("/DVD");
}

SettingsDialog::SettingsDialog(QWidget *parent, int tab)
    : FramedDialog(parent)
{
    setWindowTitle(T("Настройки"));
    setMinimumSize(760, 600);

    auto *root = contentLayout();
    root->setContentsMargins(0, 0, 0, 18);
    m_tabs = new QTabWidget(this);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->setDocumentMode(true);
    m_tabs->addTab(buildGeneral(), T("Общие"));
    m_tabs->addTab(buildConvert(), T("Конвертация"));
    m_tabs->addTab(buildDownload(), T("Загрузка"));
    m_tabs->addTab(buildLocation(), T("Расположение"));
    root->addWidget(m_tabs, 1);

    auto *btns = new QHBoxLayout;
    btns->setContentsMargins(24, 0, 24, 0);
    auto *reset = new QPushButton(T("Сбросить"), this);
    auto *ok = Ui::primaryButton(T("OK"), this);
    auto *cancel = Ui::primaryButton(T("Отмена"), this);
    for (QPushButton *b : {reset, ok, cancel})
        b->setMinimumWidth(120);
    btns->addWidget(reset);
    btns->addStretch();
    btns->addWidget(ok);
    btns->addWidget(cancel);
    root->addLayout(btns);
    connect(reset, &QPushButton::clicked, this, &SettingsDialog::resetPage);
    connect(ok, &QPushButton::clicked, this, [this] { save(); accept(); });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    load();
    m_tabs->setCurrentIndex(qBound(0, tab, 3));
}

QWidget *SettingsDialog::buildGeneral()
{
    QVBoxLayout *lay;
    QWidget *w = page(this, lay);
    m_sound = check(T("Звуковой сигнал по окончании задачи"), w);
    lay->addWidget(m_sound);

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(30);
    m_language = new QComboBox(w);
    m_language->addItem(T("Как в системе"), QString());
    for (const QString &c : I18n::languages())
        m_language->addItem(I18n::languageName(c), c);
    m_language->setMinimumWidth(220);
    form->addRow(T("Язык:"), m_language);
    m_theme = new QComboBox(w);
    for (const QString &id : Theme::ids())
        m_theme->addItem(Theme::name(id), id);
    form->addRow(T("Тема оформления:"), m_theme);
    lay->addLayout(form);
    auto *note = new QLabel(T("Язык и тема сменятся после перезапуска программы."), w);
    note->setObjectName(QStringLiteral("Muted"));
    lay->addWidget(note);
    lay->addStretch();
    return w;
}

QWidget *SettingsDialog::buildConvert()
{
    QVBoxLayout *lay;
    QWidget *w = page(this, lay);
    m_deleteSource = check(T("Удалять исходный файл после конвертации (в корзину)"), w);
    m_chapters = check(T("Сохранять главы (разметку фильма)"), w);
    m_gpu = check(T("Ускорение видеокартой"), w);
    lay->addWidget(m_deleteSource);
    lay->addWidget(m_chapters);
    lay->addWidget(m_gpu);
    m_gpuInfo = new QLabel(T("Проверка видеокарты…"), w);
    m_gpuInfo->setObjectName(QStringLiteral("Muted"));
    m_gpuInfo->setContentsMargins(26, 0, 0, 0);
    lay->addWidget(m_gpuInfo);
    Ui::runAsync(this, [] { Tools::hardwareAccelerators(); }, [this] {
        const QStringList hw = Tools::hardwareAccelerators();
        m_gpuInfo->setText(hw.isEmpty() ? T("Поддерживаемая видеокарта не найдена — конвертация пойдёт на процессоре.")
                                        : T("Найдено: ") + hw.join(QStringLiteral(", ")));
    });

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(30);
    m_dvd = new QComboBox(w);
    m_dvd->addItem(T("Только основной фильм"), 0);
    m_dvd->addItem(T("Все фильмы на диске"), 1);
    m_convTasks = new QComboBox(w);
    for (int i = 1; i <= 6; ++i)
        m_convTasks->addItem(QString::number(i), i);
    form->addRow(T("Загрузка DVD:"), m_dvd);
    form->addRow(T("Одновременных конвертаций:"), m_convTasks);
    lay->addSpacing(8);
    lay->addLayout(form);
    auto *hint = new QLabel(T("На слабых компьютерах лучше 1–2. Нейросетевой апскейл всегда идёт по одной задаче на видеокарту."), w);
    hint->setObjectName(QStringLiteral("Muted"));
    hint->setWordWrap(true);
    lay->addWidget(hint);
    lay->addStretch();
    return w;
}

QWidget *SettingsDialog::buildDownload()
{
    QVBoxLayout *lay;
    QWidget *w = page(this, lay);
    auto *form = new QFormLayout;
    form->setHorizontalSpacing(30);
    m_dlTasks = new QComboBox(w);
    for (int i = 1; i <= 5; ++i)
        m_dlTasks->addItem(QString::number(i), i);
    form->addRow(T("Одновременных загрузок:"), m_dlTasks);
    lay->addLayout(form);

    m_oneClick = check(T("Быстрый режим: скачивать сразу, без окна выбора"), w);
    lay->addWidget(m_oneClick);
    auto *oc = new QHBoxLayout;
    oc->setContentsMargins(26, 0, 0, 0);
    m_ocType = new QComboBox(w);
    m_ocType->addItem(T("Видео"), "video");
    m_ocType->addItem(T("Только звук (MP3)"), "audio");
    m_ocQuality = new QComboBox(w);
    m_ocQuality->addItem(T("Лучшее"), 0);
    for (int h : {2160, 1440, 1080, 720, 480, 360})
        m_ocQuality->addItem(QStringLiteral("%1p").arg(h), h);
    oc->addWidget(new QLabel(T("Скачивать:"), w));
    oc->addWidget(m_ocType);
    oc->addSpacing(16);
    oc->addWidget(new QLabel(T("Качество:"), w));
    oc->addWidget(m_ocQuality);
    oc->addStretch();
    lay->addLayout(oc);
    connect(m_oneClick, &QCheckBox::toggled, this, [this](bool on) { m_ocType->setEnabled(on); m_ocQuality->setEnabled(on); });

    m_subs = check(T("Скачивать субтитры"), w);
    lay->addWidget(m_subs);
    auto *sl = new QHBoxLayout;
    sl->setContentsMargins(26, 0, 0, 0);
    m_subLang = new QComboBox(w);
    m_subLang->addItem(T("Русские"), "ru");
    m_subLang->addItem(T("Английские"), "en");
    m_subLang->addItem(T("Все доступные"), "all");
    sl->addWidget(new QLabel(T("Язык субтитров:"), w));
    sl->addWidget(m_subLang);
    sl->addStretch();
    lay->addLayout(sl);
    m_embedSubs = check(T("Вшивать субтитры в видеофайл"), w);
    m_embedSubs->setContentsMargins(26, 0, 0, 0);
    lay->addWidget(m_embedSubs);
    connect(m_subs, &QCheckBox::toggled, this, [this](bool on) { m_subLang->setEnabled(on); m_embedSubs->setEnabled(on); });

    m_resume = check(T("Продолжать незавершённые загрузки при запуске"), w);
    lay->addWidget(m_resume);

    // Модуль загрузки
    lay->addSpacing(10);
    auto *mod = new QHBoxLayout;
    m_ytdlpInfo = new QLabel(w);
    m_ytdlpInfo->setObjectName(QStringLiteral("Muted"));
    m_ytdlpInfo->setWordWrap(true);
    auto *update = new QPushButton(T("Обновить модуль загрузки"), w);
    mod->addWidget(m_ytdlpInfo, 1);
    mod->addWidget(update);
    lay->addLayout(mod);
    lay->addStretch();

    auto showVersion = [this] {
        if (Tools::ytdlp().isEmpty()) {
            m_ytdlpInfo->setText(T("Модуль загрузки (yt-dlp) не найден."));
            return;
        }
        auto *p = new QProcess(this);
        connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, p] {
            m_ytdlpInfo->setText(T("Модуль загрузки yt-dlp, версия ") + QString::fromUtf8(p->readAllStandardOutput()).trimmed()
                                 + T(". Сайты часто меняются — если загрузка перестала работать, обновите модуль."));
            p->deleteLater();
        });
        p->start(Tools::ytdlp(), {"--version"});
    };
    showVersion();
    connect(update, &QPushButton::clicked, this, [this, update, showVersion] {
        if (Tools::ytdlp().isEmpty())
            return;
        update->setEnabled(false);
        m_ytdlpInfo->setText(T("Обновление…"));
        auto *p = new QProcess(this);
        p->setProcessChannelMode(QProcess::MergedChannels);
        connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, p, update, showVersion](int code) {
            const QString out = QString::fromUtf8(p->readAll()).trimmed();
            update->setEnabled(true);
            p->deleteLater();
            if (code == 0) {
                showVersion();
            } else {
                const QStringList lines = out.split('\n', Qt::SkipEmptyParts);
                m_ytdlpInfo->setText(T("Не удалось обновить: ") + (lines.isEmpty() ? QString() : lines.last()));
            }
        });
        p->start(Tools::ytdlp(), {"-U"});
    });
    return w;
}

QWidget *SettingsDialog::buildLocation()
{
    QVBoxLayout *lay;
    QWidget *w = page(this, lay);
    auto addRow = [this, w, lay](const QString &label, QLineEdit *&edit, std::function<QString()> def) {
        lay->addWidget(new QLabel(label, w));
        auto *row = new QHBoxLayout;
        edit = new QLineEdit(w);
        auto *change = Ui::primaryButton(T("Изменить"), w);
        auto *reset = new QPushButton(T("Сбросить"), w);
        row->addWidget(edit, 1);
        row->addWidget(change);
        row->addWidget(reset);
        lay->addLayout(row);
        QLineEdit *e = edit;
        connect(change, &QPushButton::clicked, this, [this, e] {
            const QString d = QFileDialog::getExistingDirectory(this, T("Выберите папку"), e->text());
            if (!d.isEmpty())
                e->setText(QDir::toNativeSeparators(d));
        });
        connect(reset, &QPushButton::clicked, this, [e, def] { e->setText(QDir::toNativeSeparators(def())); });
        lay->addSpacing(8);
    };
    addRow(T("Сохранять конвертированные файлы в:"), m_dirConverted, defaultConverted);
    addRow(T("Сохранять загруженные файлы в:"), m_dirDownloaded, defaultDownloaded);
    addRow(T("Сохранять папки и образы DVD в:"), m_dirBurned, defaultBurned);
    lay->addStretch();
    return w;
}

void SettingsDialog::load()
{
    QSettings s;
    m_sound->setChecked(s.value("general/sound", true).toBool());
    m_language->setCurrentIndex(qMax(0, m_language->findData(s.value("general/language").toString())));
    m_theme->setCurrentIndex(qMax(0, m_theme->findData(s.value("general/theme", "dark").toString())));

    m_deleteSource->setChecked(s.value("convert/deleteSource", false).toBool());
    m_chapters->setChecked(s.value("convert/keepChapters", true).toBool());
    m_gpu->setChecked(Settings::hardwareAccel());
    m_dvd->setCurrentIndex(qMax(0, m_dvd->findData(s.value("convert/dvdMode", 0).toInt())));
    m_convTasks->setCurrentIndex(qMax(0, m_convTasks->findData(s.value("convert/maxTasks", 2).toInt())));

    m_dlTasks->setCurrentIndex(qMax(0, m_dlTasks->findData(s.value("download/maxTasks", 3).toInt())));
    m_oneClick->setChecked(s.value("download/oneClick", false).toBool());
    m_ocType->setCurrentIndex(qMax(0, m_ocType->findData(s.value("download/oneClickType", "video").toString())));
    m_ocQuality->setCurrentIndex(qMax(0, m_ocQuality->findData(s.value("download/oneClickQuality", 0).toInt())));
    m_ocType->setEnabled(m_oneClick->isChecked());
    m_ocQuality->setEnabled(m_oneClick->isChecked());
    m_subs->setChecked(s.value("download/subs", true).toBool());
    m_subLang->setCurrentIndex(qMax(0, m_subLang->findData(s.value("download/subLang", "ru").toString())));
    m_embedSubs->setChecked(s.value("download/embedSubs", true).toBool());
    m_subLang->setEnabled(m_subs->isChecked());
    m_embedSubs->setEnabled(m_subs->isChecked());
    m_resume->setChecked(s.value("download/resume", true).toBool());

    m_dirConverted->setText(QDir::toNativeSeparators(Settings::outputDir()));
    m_dirDownloaded->setText(QDir::toNativeSeparators(s.value("download/dir", defaultDownloaded()).toString()));
    m_dirBurned->setText(QDir::toNativeSeparators(s.value("burn/dir", defaultBurned()).toString()));
}

void SettingsDialog::save()
{
    QSettings s;
    s.setValue("general/sound", m_sound->isChecked());
    const QString lang = m_language->currentData().toString();
    m_langChanged = lang != s.value("general/language").toString();
    s.setValue("general/language", lang);
    const QString theme = m_theme->currentData().toString();
    if (theme != s.value("general/theme", "dark").toString())
        m_langChanged = true;
    s.setValue("general/theme", theme);

    s.setValue("convert/deleteSource", m_deleteSource->isChecked());
    s.setValue("convert/keepChapters", m_chapters->isChecked());
    Settings::setHardwareAccel(m_gpu->isChecked());
    s.setValue("convert/dvdMode", m_dvd->currentData().toInt());
    s.setValue("convert/maxTasks", m_convTasks->currentData().toInt());

    s.setValue("download/maxTasks", m_dlTasks->currentData().toInt());
    s.setValue("download/oneClick", m_oneClick->isChecked());
    s.setValue("download/oneClickType", m_ocType->currentData().toString());
    s.setValue("download/oneClickQuality", m_ocQuality->currentData().toInt());
    s.setValue("download/subs", m_subs->isChecked());
    s.setValue("download/subLang", m_subLang->currentData().toString());
    s.setValue("download/embedSubs", m_embedSubs->isChecked());
    s.setValue("download/resume", m_resume->isChecked());

    Settings::setOutputDir(QDir::fromNativeSeparators(m_dirConverted->text().trimmed()));
    s.setValue("download/dir", QDir::fromNativeSeparators(m_dirDownloaded->text().trimmed()));
    s.setValue("burn/dir", QDir::fromNativeSeparators(m_dirBurned->text().trimmed()));
}

void SettingsDialog::resetPage()
{
    switch (m_tabs->currentIndex()) {
    case 0:
        m_sound->setChecked(true);
        m_language->setCurrentIndex(0);
        m_theme->setCurrentIndex(0);
        break;
    case 1:
        m_deleteSource->setChecked(false);
        m_chapters->setChecked(true);
        m_gpu->setChecked(false);
        m_dvd->setCurrentIndex(0);
        m_convTasks->setCurrentIndex(1);
        break;
    case 2:
        m_dlTasks->setCurrentIndex(2);
        m_oneClick->setChecked(false);
        m_subs->setChecked(true);
        m_subLang->setCurrentIndex(0);
        m_embedSubs->setChecked(true);
        m_resume->setChecked(true);
        break;
    default:
        m_dirConverted->setText(QDir::toNativeSeparators(defaultConverted()));
        m_dirDownloaded->setText(QDir::toNativeSeparators(defaultDownloaded()));
        m_dirBurned->setText(QDir::toNativeSeparators(defaultBurned()));
    }
}
