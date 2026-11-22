#include "CloudUpload.h"
#include "Frameless.h"
#include "Widgets.h"
#include "core/I18n.h"
#include "core/Tools.h"
#ifdef HAVE_WEBENGINE
#include "BrowserWindow.h"
#endif

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <atomic>
#include <memory>

// ---------------- Окно входа в Облако Mail.ru (rclone требует пароль для внешних приложений) ----------------

namespace {
class MailruLoginDialog : public FramedDialog
{
public:
    explicit MailruLoginDialog(QWidget *parent)
        : FramedDialog(parent, true)
    {
        setWindowTitle(T("Облако Mail.ru"));
        auto *root = contentLayout();
        root->setContentsMargins(22, 16, 22, 16);
        auto *info = new QLabel(T("Нужен <b>пароль для внешних приложений</b>, а не обычный пароль почты. Создайте его в Mail.ru: "
                                  "значок профиля → «Пароль и безопасность» → «Пароли для внешних приложений» → добавить, "
                                  "доступ — <b>«Полный доступ к Почте, Облаку и Календарю»</b> (вариант «только Облако» не подходит)."), this);
        info->setWordWrap(true);
        root->addWidget(info);
        auto *form = new QFormLayout;
        m_user = new QLineEdit(this);
        m_user->setPlaceholderText(QStringLiteral("name@mail.ru"));
        m_pass = new QLineEdit(this);
        m_pass->setEchoMode(QLineEdit::Password);
        form->addRow(T("Почта:"), m_user);
        form->addRow(T("Пароль приложения:"), m_pass);
        root->addLayout(form);
        auto *btns = new QHBoxLayout;
        auto *cancel = new QPushButton(T("Отмена"), this);
        auto *ok = Ui::primaryButton(T("Подключить"), this);
        btns->addStretch();
        btns->addWidget(cancel);
        btns->addWidget(ok);
        root->addLayout(btns);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(ok, &QPushButton::clicked, this, [this] {
            if (!m_user->text().trimmed().isEmpty() && !m_pass->text().isEmpty())
                accept();
        });
        resize(460, 250);
    }
    QString user() const { return m_user->text().trimmed(); }
    QString pass() const { return m_pass->text(); }

private:
    QLineEdit *m_user, *m_pass;
};
} // namespace

// ---------------- Вкладка «Выгрузить» ----------------

CloudUploadPage::CloudUploadPage(QWidget *parent)
    : QWidget(parent)
{
    m_providers = {
        {QStringLiteral("google"), QStringLiteral("drive"), T("Google Диск")},
        {QStringLiteral("yandex"), QStringLiteral("yandex"), T("Яндекс Диск")},
        {QStringLiteral("mailru"), QStringLiteral("mailru"), T("Облако Mail.ru")},
        {QStringLiteral("onedrive"), QStringLiteral("onedrive"), QStringLiteral("OneDrive")},
        {QStringLiteral("local"), QString(), T("Папка на компьютере (в том числе сетевой или облачный диск)")},
    };

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 14, 20, 14);
    root->setSpacing(8);

    auto *r1 = new QHBoxLayout;
    m_provider = new QComboBox(this);
    for (const Provider &p : m_providers)
        m_provider->addItem(p.name);
    m_connState = new QLabel(this);
    m_connState->setObjectName(QStringLiteral("Muted"));
    m_connect = new QPushButton(this);
    r1->addWidget(new QLabel(T("Куда:"), this));
    r1->addWidget(m_provider, 1);
    r1->addWidget(m_connState);
    r1->addWidget(m_connect);
    root->addLayout(r1);

    auto *r2 = new QHBoxLayout;
    m_up = new QPushButton(T("↑ Наверх"), this);
    m_path = new QLabel(this);
    m_path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_refresh = new QPushButton(T("Обновить"), this);
    m_mkdir = new QPushButton(T("+ Новая папка"), this);
    r2->addWidget(m_up);
    r2->addWidget(m_path, 1);
    r2->addWidget(m_refresh);
    r2->addWidget(m_mkdir);
    root->addLayout(r2);

    m_list = new QListWidget(this);
    m_list->setIconSize(QSize(22, 22));
    m_list->setSpacing(1);
    Ui::themedStyle(m_list, QStringLiteral(
        "QListWidget{background:#171113;border:1px solid #33282C;border-radius:6px;padding:4px;}"
        "QListWidget::item{padding:6px 8px;border-radius:4px;}"
        "QListWidget::item:hover{background:#261E21;}"
        "QListWidget::item:selected{background:#3A2A22;color:#F2E6CC;}"));
    root->addWidget(m_list, 1);

    auto *r3 = new QHBoxLayout;
    m_release = new QLineEdit(this);
    m_release->setPlaceholderText(T("Папка релиза"));
    auto *pick = new QPushButton(T("Выбрать…"), this);
    r3->addWidget(new QLabel(T("Релиз:"), this));
    r3->addWidget(m_release, 1);
    r3->addWidget(pick);
    root->addLayout(r3);
    m_withSeries = new QCheckBox(T("Вместе с папкой серии (Серия «…» / релиз)"), this);
    m_withSeries->setObjectName(QStringLiteral("PlainCheck"));
    m_withSeries->setChecked(QSettings().value(QStringLiteral("upload/withSeries"), true).toBool());
    root->addWidget(m_withSeries);
    m_dest = new QLabel(this);
    m_dest->setObjectName(QStringLiteral("Muted"));
    m_dest->setWordWrap(true);
    root->addWidget(m_dest);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setTextVisible(true);
    m_progress->setVisible(false);
    root->addWidget(m_progress);
    m_state = new QLabel(this);
    m_state->setObjectName(QStringLiteral("StatusLabel"));
    m_state->setWordWrap(true);
    root->addWidget(m_state);

    auto *btns = new QHBoxLayout;
    m_cancel = new QPushButton(T("Остановить"), this);
    m_cancel->setVisible(false);
    m_upload = Ui::primaryButton(T("Выгрузить в эту папку"), this);
    m_upload->setMinimumWidth(220);
    auto *web = new QPushButton(T("Через браузер…"), this);
    web->setToolTip(T("Открыть облако во встроенном браузере в этой папке — туда можно перетащить папку релиза"));
    btns->addWidget(web);
    btns->addStretch();
    btns->addWidget(m_cancel);
    btns->addWidget(m_upload);
    root->addLayout(btns);
    connect(web, &QPushButton::clicked, this, &CloudUploadPage::openWeb);
    connect(m_provider, QOverload<int>::of(&QComboBox::currentIndexChanged), web, [this, web] { web->setVisible(!isLocal()); });
    web->setVisible(!isLocal());

    connect(m_provider, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        QSettings().setValue(QStringLiteral("upload/provider"), current().id);
        providerChanged();
    });
    connect(m_connect, &QPushButton::clicked, this, [this] {
        if (isConnected())
            disconnectCloud();
        else
            connectCloud();
    });
    connect(m_up, &QPushButton::clicked, this, &CloudUploadPage::goUp);
    connect(m_refresh, &QPushButton::clicked, this, &CloudUploadPage::listFolders);
    connect(m_mkdir, &QPushButton::clicked, this, &CloudUploadPage::newFolder);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) { enter(it->data(Qt::UserRole).toString()); });
    connect(m_list, &QListWidget::currentItemChanged, this, &CloudUploadPage::updateDest);
    connect(pick, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, T("Папка релиза"), m_release->text());
        if (!d.isEmpty())
            setRelease(d);
    });
    connect(m_release, &QLineEdit::textChanged, this, &CloudUploadPage::updateDest);
    connect(m_withSeries, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("upload/withSeries"), on);
        updateDest();
    });
    connect(m_upload, &QPushButton::clicked, this, &CloudUploadPage::upload);
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        if (m_proc)
            m_proc->kill();
    });

    const QString last = QSettings().value(QStringLiteral("upload/provider"), QStringLiteral("local")).toString();
    for (int i = 0; i < m_providers.size(); ++i)
        if (m_providers[i].id == last)
            m_provider->setCurrentIndex(i);
    setRelease(QSettings().value(QStringLiteral("ae/lastRelease")).toString());
    providerChanged();

    // Автопроверка сборки: выгрузить последний релиз в указанную папку
    const QString test = qEnvironmentVariable("ARISTOCRAT_TEST_UPLOAD");
    if (!test.isEmpty()) {
        m_provider->setCurrentIndex(m_providers.size() - 1);
        enter(test);
        QTimer::singleShot(2500, this, &CloudUploadPage::upload);
    }
}

const CloudUploadPage::Provider &CloudUploadPage::current() const
{
    return m_providers.at(qBound(0, m_provider->currentIndex(), m_providers.size() - 1));
}

bool CloudUploadPage::isBusy() const
{
    return m_proc && m_proc->state() != QProcess::NotRunning;
}

QString CloudUploadPage::configFile()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/rclone.conf");
}

QStringList CloudUploadPage::rc(const QStringList &args) const
{
    return args + QStringList{QStringLiteral("--config"), configFile()};
}

// Облако в браузере — сразу в текущей папке (где это возможно); вход в аккаунт запоминается
void CloudUploadPage::openWeb()
{
#ifdef HAVE_WEBENGINE
    const QString type = current().type;
    const QString path = QString::fromLatin1(QUrl::toPercentEncoding(m_cwd, "/"));
    QString url;
    if (type == QLatin1String("mailru"))
        url = QStringLiteral("https://cloud.mail.ru/home/") + path;
    else if (type == QLatin1String("yandex"))
        url = QStringLiteral("https://disk.yandex.ru/client/disk/") + path;
    else if (type == QLatin1String("drive"))
        url = QStringLiteral("https://drive.google.com/drive/my-drive");
    else if (type == QLatin1String("onedrive"))
        url = QStringLiteral("https://onedrive.live.com/");
    if (url.isEmpty())
        return;
    auto *b = new BrowserWindow(window());
    b->setAttribute(Qt::WA_DeleteOnClose);
    b->open(QUrl(url));
    b->show();
    m_state->setText(T("Облако открыто в браузере — перетащите туда папку релиза из Проводника."));
#endif
}

QString CloudUploadPage::remotePath(const QString &path) const
{
    return remote() + QLatin1Char(':') + path;
}

bool CloudUploadPage::isConnected() const
{
    if (isLocal())
        return true;
    QSettings st(configFile(), QSettings::IniFormat);
    return st.childGroups().contains(remote());
}

void CloudUploadPage::setRelease(const QString &dir)
{
    if (dir.isEmpty())
        return;
    m_release->setText(QDir::toNativeSeparators(dir));
    const bool inSeries = QFileInfo(dir).dir().dirName().startsWith(QStringLiteral("Серия"));
    m_withSeries->setEnabled(inSeries);
    updateDest();
}

void CloudUploadPage::setBusy(bool busy, const QString &text)
{
    for (QWidget *w : {static_cast<QWidget *>(m_provider), static_cast<QWidget *>(m_connect), static_cast<QWidget *>(m_up),
                       static_cast<QWidget *>(m_refresh), static_cast<QWidget *>(m_mkdir), static_cast<QWidget *>(m_upload)})
        w->setEnabled(!busy);
    if (!text.isEmpty())
        m_state->setText(text);
}

void CloudUploadPage::providerChanged()
{
    const bool local = isLocal();
    const bool conn = isConnected();
    m_connect->setVisible(!local);
    m_connect->setText(conn ? T("Отключить") : T("Подключить"));
    m_connState->setText(local ? QString() : (conn ? T("подключено") : T("не подключено")));
    m_cwd.clear();
    m_list->clear();
    if (!local && Tools::rclone().isEmpty()) {
        m_state->setText(T("Модуль выгрузки в облака (rclone) не найден в папке tools рядом с программой."));
        return;
    }
    if (!conn) {
        m_path->setText(QString());
        m_state->setText(T("Нажмите «Подключить» — откроется браузер, войдите в аккаунт и разрешите доступ. "
                           "Пароль программа не видит и не хранит."));
        if (current().type == QLatin1String("mailru"))
            m_state->setText(T("Нажмите «Подключить» и введите почту и пароль для внешних приложений Mail.ru."));
        updateDest();
        return;
    }
    if (local)
        m_cwd = QSettings().value(QStringLiteral("upload/localDir")).toString();
    else
        m_cwd = QSettings().value(QStringLiteral("upload/dir-") + current().id).toString();
    listFolders();
}

void CloudUploadPage::connectCloud()
{
    if (Tools::rclone().isEmpty())
        return;
    const Provider p = current();
    QStringList args = {QStringLiteral("config"), QStringLiteral("create"), remote(), p.type};
    if (p.type == QLatin1String("mailru")) {
        MailruLoginDialog d(window());
        if (d.exec() != QDialog::Accepted)
            return;
        args << QStringLiteral("user=") + d.user() << QStringLiteral("pass=") + d.pass() << QStringLiteral("--obscure");
    } else {
        if (p.type == QLatin1String("drive"))
            args << QStringLiteral("scope=drive");
        args << QStringLiteral("--auto-confirm");
        m_state->setText(T("Открываю браузер: войдите в аккаунт «%1» и разрешите доступ, затем вернитесь в программу.").arg(p.name));
    }
    setBusy(true);
    m_cancel->setVisible(true);
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int code) {
        const QString out = QString::fromUtf8(m_proc->readAll()).trimmed();
        m_proc->deleteLater();
        m_cancel->setVisible(false);
        setBusy(false);
        if (code == 0 && isConnected()) {
            providerChanged();
            m_state->setText(T("Облако подключено. Выберите папку и нажмите «Выгрузить в эту папку»."));
        } else {
            m_state->setText(T("Не удалось подключить облако. ") + out.section('\n', -1).left(300));
        }
    });
    m_proc->start(Tools::rclone(), rc(args));
}

void CloudUploadPage::disconnectCloud()
{
    if (!Ui::ask(this, T("Отключить облако"), T("Отключить «%1»? Файлы в облаке не удаляются.").arg(current().name),
                 T("Отключить"), T("Отмена")))
        return;
    QProcess p;
    p.start(Tools::rclone(), rc({QStringLiteral("config"), QStringLiteral("delete"), remote()}));
    p.waitForFinished(15000);
    providerChanged();
}

void CloudUploadPage::listFolders()
{
    m_list->clear();
    m_path->setText(isLocal() ? (m_cwd.isEmpty() ? T("Компьютер") : QDir::toNativeSeparators(m_cwd))
                              : current().name + QStringLiteral(" / ") + m_cwd);
    m_up->setEnabled(!m_cwd.isEmpty());
    const QIcon folder = Ui::icon(QStringLiteral("folder"));
    auto add = [this, &folder](const QString &label, const QString &value) {
        auto *it = new QListWidgetItem(folder, label, m_list);
        it->setData(Qt::UserRole, value);
    };

    if (isLocal()) {
        if (m_cwd.isEmpty()) {
            // Диски, в том числе сетевые и облачные (Q-Disk, Яндекс Диск и др. как локальная папка)
            for (const QFileInfo &d : QDir::drives())
                add(QDir::toNativeSeparators(d.absoluteFilePath()), d.absoluteFilePath());
            add(T("Домашняя папка"), QDir::homePath());
#ifndef Q_OS_WIN
            for (const QString &m : {QStringLiteral("/media"), QStringLiteral("/mnt"), QStringLiteral("/run/media")})
                if (QDir(m).exists())
                    add(m, m);
#endif
        } else {
            const QDir d(m_cwd);
            for (const QString &n : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase))
                add(n, d.filePath(n));
        }
        m_state->setText(T("Двойной щелчок — открыть папку. Выгрузка — в открытую папку."));
        updateDest();
        return;
    }

    setBusy(true, T("Загружаю список папок…"));
    auto *p = new QProcess(this);
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, p](int code) {
        const QByteArray out = p->readAllStandardOutput();
        const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed();
        p->deleteLater();
        setBusy(false);
        if (code != 0) {
            m_state->setText(T("Не удалось открыть папку в облаке. ") + err.section('\n', -1).left(300));
            return;
        }
        QStringList names;
        for (const QJsonValue &v : QJsonDocument::fromJson(out).array())
            names << v.toObject().value("Name").toString();
        names.sort(Qt::CaseInsensitive);
        const QIcon folder = Ui::icon(QStringLiteral("folder"));
        for (const QString &n : names) {
            auto *it = new QListWidgetItem(folder, n, m_list);
            it->setData(Qt::UserRole, m_cwd.isEmpty() ? n : m_cwd + QLatin1Char('/') + n);
        }
        m_state->setText(names.isEmpty() ? T("Здесь нет папок. Можно выгрузить сюда или создать новую папку.")
                                         : T("Двойной щелчок — открыть папку. Выгрузка — в открытую папку."));
        updateDest();
    });
    p->start(Tools::rclone(), rc({QStringLiteral("lsjson"), remotePath(m_cwd), QStringLiteral("--dirs-only")}));
}

void CloudUploadPage::enter(const QString &path)
{
    m_cwd = path;
    QSettings().setValue(isLocal() ? QStringLiteral("upload/localDir") : QStringLiteral("upload/dir-") + current().id, m_cwd);
    listFolders();
}

void CloudUploadPage::goUp()
{
    if (isLocal()) {
        QDir d(m_cwd);
        enter(d.cdUp() && d.absolutePath() != m_cwd ? d.absolutePath() : QString());
    } else {
        enter(m_cwd.contains('/') ? m_cwd.section('/', 0, -2) : QString());
    }
}

void CloudUploadPage::newFolder()
{
    if (isLocal() && m_cwd.isEmpty())
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, T("Новая папка"), T("Название папки:"), QLineEdit::Normal, QString(), &ok)
                             .trimmed()
                             .remove(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")));
    if (!ok || name.isEmpty())
        return;
    if (isLocal()) {
        QDir(m_cwd).mkpath(name);
        listFolders();
        return;
    }
    const QString path = m_cwd.isEmpty() ? name : m_cwd + QLatin1Char('/') + name;
    QProcess p;
    p.start(Tools::rclone(), rc({QStringLiteral("mkdir"), remotePath(path)}));
    p.waitForFinished(30000);
    listFolders();
}

void CloudUploadPage::updateDest()
{
    const QString rel = QDir::fromNativeSeparators(m_release->text().trimmed());
    if (rel.isEmpty() || !isConnected() || (isLocal() && m_cwd.isEmpty())) {
        m_dest->setText(QString());
        return;
    }
    QString sub = QFileInfo(rel).fileName();
    if (m_withSeries->isEnabled() && m_withSeries->isChecked())
        sub = QFileInfo(rel).dir().dirName() + QLatin1Char('/') + sub;
    const QString where = isLocal() ? QDir::toNativeSeparators(QDir(m_cwd).filePath(sub))
                                    : current().name + QStringLiteral(" / ") + (m_cwd.isEmpty() ? sub : m_cwd + QLatin1Char('/') + sub);
    m_dest->setText(T("Будет выгружено: %1").arg(where));
}

void CloudUploadPage::upload()
{
    const QString rel = QDir::fromNativeSeparators(m_release->text().trimmed());
    if (rel.isEmpty() || !QFileInfo(rel).isDir()) {
        m_state->setText(T("Выберите папку релиза."));
        return;
    }
    if (!isConnected()) {
        m_state->setText(T("Сначала подключите облако."));
        return;
    }
    if (isLocal() && m_cwd.isEmpty()) {
        m_state->setText(T("Откройте папку, в которую нужно выгрузить релиз."));
        return;
    }
    // Видео ещё конвертируется — предупреждаем
    const QStringList videos = QDir(rel).entryList({QStringLiteral("*.mkv"), QStringLiteral("*.mp4"), QStringLiteral("*.avi"),
                                                    QStringLiteral("*.m4v"), QStringLiteral("*.mov"), QStringLiteral("*.ts")},
                                                   QDir::Files);
    bool partial = false;
    for (const QString &v : videos)
        partial |= v.contains(QLatin1String(".chapters")) || v.contains(QLatin1String(".part"));
    if ((videos.isEmpty() || partial)
        && !Ui::ask(this, T("Выгрузка"), T("В папке релиза нет готового видео (возможно, оно ещё скачивается или конвертируется). Выгрузить без него?"),
                    T("Выгрузить"), T("Отмена")))
        return;

    QString sub = QFileInfo(rel).fileName();
    if (m_withSeries->isEnabled() && m_withSeries->isChecked())
        sub = QFileInfo(rel).dir().dirName() + QLatin1Char('/') + sub;
    const QString dst = isLocal() ? QDir(m_cwd).filePath(sub) : remotePath(m_cwd.isEmpty() ? sub : m_cwd + QLatin1Char('/') + sub);

    m_progress->setValue(0);
    m_progress->setVisible(true);
    m_cancel->setVisible(true);
    setBusy(true, T("Выгружаю «%1»…").arg(QFileInfo(rel).fileName()));

    if (Tools::rclone().isEmpty()) {
        // Без rclone: простое копирование в папку на компьютере
        auto ok = std::make_shared<std::atomic_bool>(true);
        Ui::runAsync(this, [rel, dst, ok] {
            QDirIterator it(rel, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString f = it.next();
                const QString target = QDir(dst).filePath(QDir(rel).relativeFilePath(f));
                QDir().mkpath(QFileInfo(target).absolutePath());
                QFile::remove(target);
                if (!QFile::copy(f, target))
                    *ok = false;
            }
        }, [this, ok] {
            m_cancel->setVisible(false);
            m_progress->setValue(100);
            setBusy(false, *ok ? T("Выгрузка завершена.") : T("Часть файлов скопировать не удалось."));
        });
        return;
    }

    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::readyReadStandardError, this, [this] {
        static const QRegularExpression pct(QStringLiteral("(\\d+)%"));
        static const QRegularExpression eta(QStringLiteral("ETA\\s+([0-9hms-]+)"));
        const QString text = QString::fromUtf8(m_proc->readAllStandardError());
        for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
            const auto m = pct.match(line);
            if (m.hasMatch()) {
                m_progress->setValue(m.captured(1).toInt());
                static const QRegularExpression speed(QStringLiteral("([0-9.]+\\s*[KMG]i?B/s)"));
                const QString e = eta.match(line).captured(1);
                const QString sp = speed.match(line).captured(1);
                if (!e.isEmpty() && e != QLatin1String("-"))
                    m_state->setText(T("Выгрузка… %1%, скорость %2, осталось примерно %3").arg(m.captured(1), sp.isEmpty() ? QStringLiteral("—") : sp, e));
                else
                    m_state->setText(T("Выгрузка… %1%, скорость %2").arg(m.captured(1), sp.isEmpty() ? QStringLiteral("—") : sp));
            }
        }
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, dst](int code, QProcess::ExitStatus st) {
        m_proc->deleteLater();
        m_cancel->setVisible(false);
        if (code == 0 && st == QProcess::NormalExit) {
            m_progress->setValue(100);
            setBusy(false, T("Выгрузка завершена: %1").arg(isLocal() ? QDir::toNativeSeparators(dst) : dst.section(':', 1)));
            if (isLocal())
                Ui::showInFolder(dst);
            else
                listFolders();
        } else {
            setBusy(false, st == QProcess::CrashExit ? T("Выгрузка остановлена.") : T("Выгрузка не удалась. Проверьте подключение к интернету и место в облаке."));
        }
    });
    QStringList args{QStringLiteral("copy"), rel, dst, QStringLiteral("--stats"), QStringLiteral("1s"),
                     QStringLiteral("--stats-one-line"), QStringLiteral("--stats-log-level"), QStringLiteral("NOTICE"),
                     QStringLiteral("--transfers"), QStringLiteral("4"), QStringLiteral("--checkers"), QStringLiteral("8"),
                     QStringLiteral("--buffer-size"), QStringLiteral("64M")};
    if (current().type == QLatin1String("mailru"))
        // «Ускорение по хешу» сначала считает хеш и пишет временную копию файла — на большом видео это долгая пауза
        args << QStringLiteral("--mailru-speedup-enable=false");
    else if (current().type == QLatin1String("drive"))
        args << QStringLiteral("--drive-chunk-size") << QStringLiteral("128M");
    else if (current().type == QLatin1String("onedrive"))
        args << QStringLiteral("--onedrive-chunk-size") << QStringLiteral("100M");
    m_proc->start(Tools::rclone(), rc(args));
}
