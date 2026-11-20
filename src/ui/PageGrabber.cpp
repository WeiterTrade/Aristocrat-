#include "PageGrabber.h"
#include "BrowserWindow.h"
#include "Widgets.h"
#include "core/I18n.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineView>

// ======================= Сайты =======================

namespace Sites {

static bool testSite(const QUrl &u)   // для автопроверки сборки
{
    return qEnvironmentVariableIsSet("ARISTOCRAT_TEST_LOGIN") && u.host() == QLatin1String("127.0.0.1");
}

static bool isAdultEmpire(const QUrl &u)
{
    if (testSite(u))
        return true;
    const QString h = u.host().toLower();
    return h == QLatin1String("adultempire.com") || h.endsWith(QLatin1String(".adultempire.com"));
}

bool needsLogin(const QUrl &url) { return isAdultEmpire(url); }

QString siteName(const QUrl &url)
{
    if (isAdultEmpire(url))
        return QStringLiteral("Adult Empire");
    return url.host();
}

QUrl loginUrl(const QUrl &url)
{
    if (testSite(url))
        return url.resolved(QUrl(QStringLiteral("/Account/Login")));
    if (isAdultEmpire(url))
        return QUrl(QStringLiteral("https://www.adultempire.com/account/loginpage"));
    return {};
}

QUrl videoPageUrl(const QUrl &url)
{
    if (!isAdultEmpire(url))
        return url;
    QUrl u = url;
    QUrlQuery q(u);
    if (!q.hasQueryItem(QStringLiteral("viewpart"))) {
        q.addQueryItem(QStringLiteral("viewpart"), QStringLiteral("videoplayer"));
        u.setQuery(q);
    }
    return u;
}

bool isLoggedIn(const QUrl &url)
{
    return QSettings().value(QStringLiteral("sites/%1/loggedIn").arg(siteName(url)), false).toBool();
}

void setLoggedIn(const QUrl &url, bool on)
{
    QSettings().setValue(QStringLiteral("sites/%1/loggedIn").arg(siteName(url)), on);
}

} // namespace Sites

// ======================= Скрипты для страницы =======================

// Подтверждение возраста: нажимает «Yes, I am 18+», «Enter», «Мне есть 18» и т.п.
static const char *kAgeScript =
    "(function(){var re=/(yes,?\\s*i\\s*am\\s*(over\\s*)?18|i\\s*am\\s*(over\\s*)?18|i'?m\\s*(over\\s*)?18|over\\s*18|"
    "i\\s*agree|enter\\s*(the\\s*)?site|^enter$|мне\\s*(есть\\s*)?18|я\\s*старше\\s*18|войти\\s*на\\s*сайт)/i;"
    "var els=[].slice.call(document.querySelectorAll('a,button,input[type=submit],input[type=button],[role=button]'));"
    "for(var i=0;i<els.length;i++){var e=els[i];var t=(e.innerText||e.value||'').trim();"
    "if(t&&t.length<40&&re.test(t)&&e.offsetParent!==null){e.click();return 'clicked';}}return 'none';})()";

// Запуск воспроизведения: video.play() и нажатие на кнопки «play»
static const char *kPlayScript =
    "(function(){var n=0;document.querySelectorAll('video').forEach(function(v){try{v.muted=true;var p=v.play();"
    "if(p&&p.catch)p.catch(function(){});n++;}catch(e){}});"
    "var sel='[class*=play i],[id*=play i],[aria-label*=play i],[title*=play i],.vjs-big-play-button,.jw-display-icon-container,"
    ".plyr__control--overlaid';var els=document.querySelectorAll(sel);for(var i=0;i<els.length&&i<4;i++){"
    "var e=els[i];var h=e.getAttribute('href');if(e.tagName==='A'&&h&&h!=='#'&&h.indexOf('javascript')!==0)continue;"
    "if(e.offsetParent!==null&&!/playlist/i.test(String(e.className||''))){try{e.click();n++;}catch(x){}}}return n;})()";

// Заполнение формы входа
static QString loginScript(const QString &user, const QString &pass)
{
    const QString args = QString::fromUtf8(QJsonDocument(QJsonArray{user, pass}).toJson(QJsonDocument::Compact));
    return QStringLiteral(
        "(function(a){var u=a[0],p=a[1];"
        "var pws=[].slice.call(document.querySelectorAll('input[type=password]'));"
        "var pw=pws.filter(function(e){return e.offsetParent!==null;})[0]||pws[0];"
        "if(!pw){var l=[].slice.call(document.querySelectorAll('a')).filter(function(e){"
        "return /log\\s*in|sign\\s*in|войти/i.test(e.innerText||'')||/login|signin/i.test(e.getAttribute('href')||'');})[0];"
        "if(l){l.click();return 'link';}return 'nofield';}"
        "var form=pw.form||document;"
        "var c=[].slice.call(form.querySelectorAll('input')).filter(function(e){return /^(email|text|tel)$/i.test(e.type)&&e!==pw;});"
        "var us=c.filter(function(e){return /user|mail|login|name/i.test((e.name||'')+(e.id||'')+(e.placeholder||''));})[0]||c[0];"
        "if(!us)return 'nouser';"
        "function sv(el,v){var d=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value');d.set.call(el,v);"
        "el.dispatchEvent(new Event('input',{bubbles:true}));el.dispatchEvent(new Event('change',{bubbles:true}));}"
        "sv(us,u);sv(pw,p);"
        "var r=form.querySelector('input[type=checkbox]');if(r&&!r.checked)r.click();"
        "var b=form.querySelector('button[type=submit],input[type=submit]')||[].slice.call(form.querySelectorAll('button')).filter(function(x){"
        "return /log\\s*in|sign\\s*in|войти/i.test(x.innerText||'');})[0];"
        "if(b)b.click();else if(form.submit)form.submit();return 'submitted';})(%1)").arg(args);
}

// Есть ли на странице видимое поле пароля (значит, вход не выполнен)
static const char *kHasPasswordScript =
    "(function(){return [].slice.call(document.querySelectorAll('input[type=password]')).some(function(e){return e.offsetParent!==null;});})()";

// ======================= PageGrabber =======================

PageGrabber::PageGrabber(QObject *parent)
    : QObject(parent)
{
    // Страница работает «за кадром»: окно не показывается на экране
    m_view = new QWebEngineView;
    m_view->setAttribute(Qt::WA_DontShowOnScreen);
    m_view->resize(1280, 800);
    m_view->setPage(new QWebEnginePage(BrowserWindow::profile(), m_view));
    m_view->show();

    connect(m_view, &QWebEngineView::loadFinished, this, &PageGrabber::onLoadFinished);
    connect(BrowserWindow::sniffer(), &MediaSniffer::found, this, &PageGrabber::onFound);

    m_poke.setInterval(1500);
    connect(&m_poke, &QTimer::timeout, this, &PageGrabber::poke);
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!m_found.isEmpty())
            finish();
        else if (m_step == Step::LoginPage || m_step == Step::LoginSubmitted)
            checkLogin();
        else
            fail(T("Видео на странице не найдено. Возможно, нужен вход в аккаунт или у аккаунта нет доступа к этому видео."));
    });
    m_settle.setSingleShot(true);
    m_settle.setInterval(2500);   // ждём ещё немного: первым обычно идёт главный плейлист со всеми качествами
    connect(&m_settle, &QTimer::timeout, this, &PageGrabber::finish);
}

PageGrabber::~PageGrabber()
{
    m_view->page()->triggerAction(QWebEnginePage::Stop);
    delete m_view;
}

void PageGrabber::start(const QUrl &page, const QString &user, const QString &password)
{
    m_target = Sites::videoPageUrl(page);
    m_user = user;
    m_pass = password;
    m_found.clear();
    if (!user.isEmpty() && Sites::loginUrl(page).isValid()) {
        m_step = Step::LoginPage;
        emit progress(T("Вход в аккаунт %1…").arg(Sites::siteName(page)));
        m_timeout.start(30000);
        m_view->load(Sites::loginUrl(page));
    } else {
        openVideoPage();
    }
}

void PageGrabber::openVideoPage()
{
    m_step = Step::VideoPage;
    m_pokes = 0;
    emit progress(T("Открываю страницу с видео…"));
    m_timeout.start(45000);
    m_view->load(m_target);
}

void PageGrabber::onLoadFinished(bool)
{
    switch (m_step) {
    case Step::LoginPage:
        // На странице входа тоже может быть окно 18+
        m_view->page()->runJavaScript(QString::fromUtf8(kAgeScript), [this](const QVariant &r) {
            QTimer::singleShot(r.toString() == QLatin1String("clicked") ? 1500 : 300, this, &PageGrabber::tryLogin);
        });
        break;
    case Step::LoginSubmitted:
        QTimer::singleShot(1200, this, &PageGrabber::checkLogin);
        break;
    case Step::VideoPage:
        emit progress(T("Подтверждение возраста 18+ и запуск плеера…"));
        poke();
        m_poke.start();
        break;
    default:
        break;
    }
}

void PageGrabber::tryLogin()
{
    if (m_step != Step::LoginPage)
        return;
    m_view->page()->runJavaScript(loginScript(m_user, m_pass), [this](const QVariant &r) {
        const QString res = r.toString();
        if (res == QLatin1String("submitted")) {
            m_step = Step::LoginSubmitted;
            emit progress(T("Проверка входа…"));
        } else if (res == QLatin1String("link") && ++m_loginTries < 3) {
            // нажата ссылка «Войти» — ждём страницу с формой
        } else if (++m_loginTries < 4) {
            QTimer::singleShot(1500, this, &PageGrabber::tryLogin);   // форма может появиться позже
        } else {
            fail(T("Не удалось найти форму входа на сайте."), true);
        }
    });
}

void PageGrabber::checkLogin()
{
    if (m_step != Step::LoginSubmitted && m_step != Step::LoginPage)
        return;
    m_view->page()->runJavaScript(QString::fromUtf8(kHasPasswordScript), [this](const QVariant &r) {
        if (m_step != Step::LoginSubmitted && m_step != Step::LoginPage)
            return;
        if (r.toBool()) {
            Sites::setLoggedIn(m_target, false);
            fail(T("Не удалось войти: проверьте email и пароль."), true);
            return;
        }
        Sites::setLoggedIn(m_target, true);
        openVideoPage();
    });
}

void PageGrabber::poke()
{
    if (m_step != Step::VideoPage)
        return;
    if (++m_pokes > 25) {
        m_poke.stop();
        return;
    }
    if (QSettings().value(QStringLiteral("sites/adultConfirmed"), false).toBool()) {
        m_view->page()->runJavaScript(QString::fromUtf8(kAgeScript), [this](const QVariant &r) {
            if (r.toString() == QLatin1String("clicked"))
                emit progress(T("Возраст 18+ подтверждён. Поиск видео…"));
        });
    }
    m_view->page()->runJavaScript(QString::fromUtf8(kPlayScript));
    if (m_pokes == 3)
        emit progress(T("Поиск видео на странице…"));
}

void PageGrabber::onFound(const QUrl &url, const QString &kind, const QUrl &page)
{
    if (m_step != Step::VideoPage)
        return;
    if (page.isValid() && page.host() != m_view->url().host() && page.host() != m_target.host())
        return;
    for (const Found &f : m_found)
        if (f.url == url)
            return;
    m_found << Found{url, kind, page.isValid() ? page : m_view->url()};
    emit progress(T("Видео найдено. Получаю список качеств…"));
    if (kind == QLatin1String("HLS") || kind == QLatin1String("DASH")) {
        if (!m_settle.isActive())
            m_settle.start();
    } else if (!m_settle.isActive()) {
        m_settle.start(6000);   // для обычных файлов подождём: вдруг появится поток с выбором качества
    }
}

void PageGrabber::finish()
{
    if (m_step == Step::Done || m_found.isEmpty())
        return;
    m_step = Step::Done;
    m_poke.stop();
    m_timeout.stop();
    // Предпочитаем плейлист (первый найденный — главный, со всеми качествами)
    Found best = m_found.first();
    for (const Found &f : m_found)
        if ((f.kind == QLatin1String("HLS") || f.kind == QLatin1String("DASH"))
            && !(best.kind == QLatin1String("HLS") || best.kind == QLatin1String("DASH"))) {
            best = f;
            break;
        }
    const QStringList headers = BrowserWindow::headersFor(best.url, best.page);
    const QString title = m_view->title();
    m_view->page()->runJavaScript(QStringLiteral(
        "(function(){var m=document.querySelector('meta[property=\"og:image\"]');"
        "var v=document.querySelector('video');return (m&&m.content)||(v&&v.poster)||'';})()"),
        [this, best, headers, title](const QVariant &thumb) {
            m_view->page()->triggerAction(QWebEnginePage::Stop);
            m_view->setUrl(QUrl(QStringLiteral("about:blank")));   // остановить плеер
            emit finished(best.url, headers, title, thumb.toString());
        });
}

void PageGrabber::fail(const QString &reason, bool login)
{
    if (m_step == Step::Done)
        return;
    m_step = Step::Done;
    m_poke.stop();
    m_timeout.stop();
    m_view->setUrl(QUrl(QStringLiteral("about:blank")));
    emit failed(reason, login);
}

// ======================= SiteLoginDialog =======================

SiteLoginDialog::SiteLoginDialog(const QUrl &url, const QString &error, QWidget *parent)
    : FramedDialog(parent)
{
    setWindowTitle(T("Вход: %1").arg(Sites::siteName(url)));
    setMinimumWidth(480);
    auto *root = contentLayout();
    root->setContentsMargins(26, 20, 26, 20);
    root->setSpacing(12);

    auto *head = new QHBoxLayout;
    auto *logo = new QLabel(this);
    logo->setPixmap(QIcon(QStringLiteral(":/logo/logo-small.svg")).pixmap(44, 44));
    auto *text = new QLabel(T("Видео на %1 доступно после входа в аккаунт. Программа войдёт сама, "
                              "подтвердит возраст и найдёт видео — окно браузера не понадобится.").arg(Sites::siteName(url)), this);
    text->setWordWrap(true);
    head->addWidget(logo, 0, Qt::AlignTop);
    head->addSpacing(8);
    head->addWidget(text, 1);
    root->addLayout(head);

    if (!error.isEmpty()) {
        auto *err = new QLabel(error, this);
        err->setObjectName(QStringLiteral("StatusLabel"));
        err->setWordWrap(true);
        root->addWidget(err);
    }

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(16);
    m_user = new QLineEdit(QSettings().value(QStringLiteral("sites/%1/user").arg(Sites::siteName(url))).toString(), this);
    m_user->setPlaceholderText(T("Email или имя пользователя"));
    m_pass = new QLineEdit(this);
    m_pass->setEchoMode(QLineEdit::Password);
    m_pass->setPlaceholderText(T("Пароль"));
    form->addRow(T("Логин:"), m_user);
    form->addRow(T("Пароль:"), m_pass);
    root->addLayout(form);

    m_adult = new QCheckBox(T("Мне исполнилось 18 лет — подтверждать возраст на сайте автоматически"), this);
    m_adult->setObjectName(QStringLiteral("PlainCheck"));
    m_adult->setChecked(QSettings().value(QStringLiteral("sites/adultConfirmed"), false).toBool());
    root->addWidget(m_adult);
    auto *note = new QLabel(T("Пароль не сохраняется. Программа запоминает только сам вход, как обычный браузер."), this);
    note->setObjectName(QStringLiteral("Muted"));
    note->setWordWrap(true);
    root->addWidget(note);

    auto *btns = new QHBoxLayout;
    auto *skip = new QPushButton(T("Без входа"), this);
    auto *cancel = new QPushButton(T("Отмена"), this);
    auto *ok = Ui::primaryButton(T("Войти и найти видео"), this);
    ok->setDefault(true);
    btns->addWidget(skip);
    btns->addStretch();
    btns->addWidget(cancel);
    btns->addWidget(ok);
    root->addLayout(btns);

    auto update = [this, ok, skip] {
        ok->setEnabled(m_adult->isChecked() && !m_user->text().trimmed().isEmpty() && !m_pass->text().isEmpty());
        skip->setEnabled(m_adult->isChecked());
    };
    connect(m_user, &QLineEdit::textChanged, this, update);
    connect(m_pass, &QLineEdit::textChanged, this, update);
    connect(m_adult, &QCheckBox::toggled, this, update);
    update();
    const QString site = Sites::siteName(url);
    connect(ok, &QPushButton::clicked, this, [this, site] {
        QSettings().setValue(QStringLiteral("sites/%1/user").arg(site), user());
        QSettings().setValue(QStringLiteral("sites/adultConfirmed"), true);
        accept();
    });
    connect(skip, &QPushButton::clicked, this, [this] {
        QSettings().setValue(QStringLiteral("sites/adultConfirmed"), true);
        m_skip = true;
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    if (!m_user->text().isEmpty())
        m_pass->setFocus();
}

QString SiteLoginDialog::user() const { return m_user->text().trimmed(); }
QString SiteLoginDialog::password() const { return m_pass->text(); }

// ======================= GrabDialog =======================

GrabDialog::GrabDialog(const QUrl &page, const QString &user, const QString &pass, QWidget *parent)
    : FramedDialog(parent), m_grabber(new PageGrabber(this))
{
    setWindowTitle(T("Поиск видео — %1").arg(Sites::siteName(page)));
    setMinimumWidth(520);
    auto *root = contentLayout();
    root->setContentsMargins(26, 22, 26, 18);
    root->setSpacing(14);
    m_state = new QLabel(T("Подготовка…"), this);
    m_state->setWordWrap(true);
    auto *bar = new QProgressBar(this);
    bar->setRange(0, 0);   // «бегущая» полоса
    bar->setFixedHeight(6);
    auto *url = new QLabel(fontMetrics().elidedText(page.toString(), Qt::ElideMiddle, 460), this);
    url->setObjectName(QStringLiteral("Muted"));
    root->addWidget(m_state);
    root->addWidget(bar);
    root->addWidget(url);
    auto *btns = new QHBoxLayout;
    btns->addStretch();
    auto *cancel = new QPushButton(T("Отмена"), this);
    btns->addWidget(cancel);
    root->addLayout(btns);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    connect(m_grabber, &PageGrabber::progress, m_state, &QLabel::setText);
    connect(m_grabber, &PageGrabber::finished, this,
            [this](const QUrl &media, const QStringList &headers, const QString &title, const QString &thumb) {
        m_media = media;
        m_headers = headers;
        m_title = title;
        m_thumb = thumb;
        accept();
    });
    connect(m_grabber, &PageGrabber::failed, this, [this](const QString &reason, bool login) {
        m_error = reason;
        m_loginFailed = login;
        done(2);
    });
    QTimer::singleShot(0, this, [this, page, user, pass] { m_grabber->start(page, user, pass); });
}
