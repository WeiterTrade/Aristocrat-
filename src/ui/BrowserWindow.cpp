#include "BrowserWindow.h"
#include "Widgets.h"
#include "core/I18n.h"
#include "core/Theme.h"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <algorithm>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWebEngineCookieStore>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineView>

// ---------------- Перехват запросов ----------------

static QString mediaKind(const QUrl &url, bool mediaRequest)
{
    const QString p = url.path().toLower();
    const QString full = url.toString().toLower();
    static const QRegularExpression skip(QStringLiteral("\\.(ts|m4s|m4a|aac|key|vtt|webvtt|srt|jpe?g|png|gif|webp|svg|js|css|json|woff2?|ico|html?)$"));
    if (skip.match(p).hasMatch())
        return {};
    if (p.endsWith(QLatin1String(".m3u8")) || full.contains(QLatin1String(".m3u8")))
        return QStringLiteral("HLS");
    if (p.endsWith(QLatin1String(".mpd")) || full.contains(QLatin1String(".mpd?")))
        return QStringLiteral("DASH");
    static const QRegularExpression file(QStringLiteral("\\.(mp4|m4v|webm|mov|mkv|flv)$"));
    const auto m = file.match(p);
    if (m.hasMatch())
        return m.captured(1).toUpper();
    if (mediaRequest)
        return QStringLiteral("VIDEO");
    return {};
}

void MediaSniffer::interceptRequest(QWebEngineUrlRequestInfo &info)
{
    const bool media = info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeMedia;
    const QString kind = mediaKind(info.requestUrl(), media);
    if (kind.isEmpty())
        return;
    const QUrl url = info.requestUrl();
    const QUrl page = info.firstPartyUrl();
    // Вызов может прийти не из потока интерфейса — передаём сигнал через очередь
    QMetaObject::invokeMethod(this, [this, url, kind, page] { emit found(url, kind, page); }, Qt::QueuedConnection);
}

// ---------------- Общий профиль браузера (входы на сайты сохраняются) ----------------

static QList<QNetworkCookie> &cookies()
{
    static QList<QNetworkCookie> list;
    return list;
}

static MediaSniffer *sniffer()
{
    static MediaSniffer *s = nullptr;
    if (!s)
        s = new MediaSniffer(BrowserWindow::profile());
    return s;
}

QWebEngineProfile *BrowserWindow::profile()
{
    static QWebEngineProfile *p = nullptr;
    if (p)
        return p;
    p = new QWebEngineProfile(QStringLiteral("aristocrat"));
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/browser");
    p->setPersistentStoragePath(dir);
    p->setCachePath(dir + QStringLiteral("/cache"));
    p->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    // Обычный браузерный User-Agent: без отметки QtWebEngine некоторые сайты работают лучше
    QString ua = p->httpUserAgent();
    ua.remove(QRegularExpression(QStringLiteral(" QtWebEngine/[^ ]+")));
    p->setHttpUserAgent(ua);
    p->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    p->settings()->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);

    QWebEngineCookieStore *store = p->cookieStore();
    QObject::connect(store, &QWebEngineCookieStore::cookieAdded, [](const QNetworkCookie &c) {
        auto &l = cookies();
        for (int i = 0; i < l.size(); ++i)
            if (l[i].hasSameIdentifier(c)) { l[i] = c; return; }
        l << c;
    });
    QObject::connect(store, &QWebEngineCookieStore::cookieRemoved, [](const QNetworkCookie &c) {
        auto &l = cookies();
        for (int i = l.size() - 1; i >= 0; --i)
            if (l[i].hasSameIdentifier(c)) l.removeAt(i);
    });
    store->loadAllCookies();
    p->setUrlRequestInterceptor(sniffer());
    return p;
}

// Страница: ссылки «в новом окне» открываем здесь же
class BrowserPage : public QWebEnginePage
{
public:
    using QWebEnginePage::QWebEnginePage;
protected:
    QWebEnginePage *createWindow(WebWindowType) override { return this; }
};

// ---------------- Окно ----------------

BrowserWindow::BrowserWindow(QWidget *parent)
    : FramedDialog(parent, true)
{
    setWindowTitle(T("Браузер Aristocrat"));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::WindowMinMaxButtonsHint);
    setModal(false);
    // Размер — по экрану (на маленьких экранах окно не должно выходить за край)
    const QRect avail = (parent ? parent->screen() : QGuiApplication::primaryScreen())->availableGeometry();
    resize(qMin(1280, avail.width() - 40), qMin(860, avail.height() - 40));

    auto *root = contentLayout();
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Адресная строка
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("SubBar"));
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *bl = new QHBoxLayout(bar);
    bl->setContentsMargins(10, 6, 10, 6);
    auto *back = Ui::iconButton("rotl", T("Назад"), bar, 18);
    auto *fwd = Ui::iconButton("rotr", T("Вперёд"), bar, 18);
    auto *reload = Ui::iconButton("convert", T("Обновить"), bar, 18);
    m_address = new QLineEdit(bar);
    m_address->setPlaceholderText(T("Адрес страницы с видео"));
    bl->addWidget(back);
    bl->addWidget(fwd);
    bl->addWidget(reload);
    bl->addWidget(m_address, 1);
    root->addWidget(bar);

    m_view = new QWebEngineView(this);
    m_view->setPage(new BrowserPage(profile(), m_view));
    root->addWidget(m_view, 1);

    // Панель «Скачать» под страницей
    auto *panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("BottomBar"));
    panel->setAttribute(Qt::WA_StyledBackground);
    panel->setFixedHeight(64);
    auto *pl = new QHBoxLayout(panel);
    pl->setContentsMargins(18, 0, 18, 0);
    pl->setSpacing(12);
    m_found = new QLabel(panel);
    m_list = new QComboBox(panel);
    m_list->setMinimumWidth(380);
    m_download = Ui::primaryButton(T("Скачать"), panel);
    m_download->setMinimumWidth(170);
    m_download->setMinimumHeight(44);
    pl->addWidget(Ui::iconLabel("download", 22, panel));
    pl->addWidget(m_found, 1);
    pl->addWidget(m_list);
    pl->addWidget(m_download);
    root->addWidget(panel);

    connect(back, &QToolButton::clicked, m_view, &QWebEngineView::back);
    connect(fwd, &QToolButton::clicked, m_view, &QWebEngineView::forward);
    connect(reload, &QToolButton::clicked, m_view, &QWebEngineView::reload);
    connect(m_address, &QLineEdit::returnPressed, this, [this] { open(QUrl::fromUserInput(m_address->text().trimmed())); });
    connect(m_view, &QWebEngineView::urlChanged, this, [this](const QUrl &u) {
        m_address->setText(u.toString());
        // Новая страница — список найденного видео начинается заново
        if (!m_media.isEmpty() && m_media.first().page.host() != u.host()) {
            m_media.clear();
            updatePanel();
        }
    });
    connect(m_view, &QWebEngineView::titleChanged, this, [this](const QString &t) {
        setWindowTitle(t.isEmpty() ? T("Браузер Aristocrat") : t + QStringLiteral(" — Aristocrat"));
    });
    connect(sniffer(), &MediaSniffer::found, this, &BrowserWindow::onFound);
    connect(m_download, &QPushButton::clicked, this, &BrowserWindow::startDownload);
    updatePanel();
}

void BrowserWindow::open(const QUrl &url)
{
    m_media.clear();
    updatePanel();
    m_address->setText(url.toString());
    m_view->load(url);
}

void BrowserWindow::onFound(const QUrl &url, const QString &kind, const QUrl &page)
{
    for (const SniffedMedia &m : m_media)
        if (m.url == url)
            return;
    // Поток с другой вкладки/окна этого профиля не берём
    if (page.isValid() && m_view->url().isValid() && page.host() != m_view->url().host())
        return;
    SniffedMedia m;
    m.url = url;
    m.kind = kind;
    m.page = page.isValid() ? page : m_view->url();
    m.order = ++m_counter;
    m_media << m;
    qInfo("SNIFFED %s %s", qPrintable(kind), qPrintable(url.toString()));
    updatePanel();
}

void BrowserWindow::updatePanel()
{
    m_list->clear();
    // Сначала плейлисты (HLS/DASH) — в них есть выбор качества, затем файлы
    QList<SniffedMedia> sorted = m_media;
    std::stable_sort(sorted.begin(), sorted.end(), [](const SniffedMedia &a, const SniffedMedia &b) {
        const bool pa = a.kind == QLatin1String("HLS") || a.kind == QLatin1String("DASH");
        const bool pb = b.kind == QLatin1String("HLS") || b.kind == QLatin1String("DASH");
        if (pa != pb) return pa;
        return a.order < b.order;
    });
    for (int i = 0; i < sorted.size(); ++i) {
        const SniffedMedia &m = sorted[i];
        QString name = m.url.fileName();
        if (name.isEmpty()) name = m.url.host();
        m_list->addItem(QStringLiteral("%1 · %2").arg(m.kind, name.left(60)), m.url);
        m_list->setItemData(i, m.url.toString(), Qt::ToolTipRole);
    }
    const bool any = !sorted.isEmpty();
    m_list->setVisible(sorted.size() > 1);
    m_download->setEnabled(any);
    m_found->setText(any ? T("Найдено видео: %1. Нажмите «Скачать» и выберите качество.").arg(sorted.size())
                         : T("Откройте страницу и запустите воспроизведение — программа найдёт видео сама."));
}

QStringList BrowserWindow::headersFor(const QUrl &media, const QUrl &page) const
{
    QStringList h;
    h << QStringLiteral("Referer:") + page.toString();
    h << QStringLiteral("Origin:") + page.scheme() + QStringLiteral("://") + page.host()
             + (page.port() > 0 ? QStringLiteral(":%1").arg(page.port()) : QString());
    h << QStringLiteral("User-Agent:") + profile()->httpUserAgent();
    // Cookie для адреса потока (как отправил бы браузер)
    QStringList pairs;
    const QString host = media.host();
    for (const QNetworkCookie &c : cookies()) {
        QString d = c.domain();
        bool match = false;
        if (d.startsWith('.'))
            match = host == d.mid(1) || host.endsWith(d);
        else
            match = host == d || (d.isEmpty() && host == page.host());
        if (!match)
            continue;
        if (c.isSecure() && media.scheme() != QLatin1String("https"))
            continue;
        if (!c.path().isEmpty() && !media.path().startsWith(c.path()))
            continue;
        pairs << QString::fromUtf8(c.name()) + '=' + QString::fromUtf8(c.value());
    }
    if (!pairs.isEmpty())
        h << QStringLiteral("Cookie:") + pairs.join(QStringLiteral("; "));
    return h;
}

void BrowserWindow::startDownload()
{
    if (m_list->count() == 0)
        return;
    const QUrl media = m_list->currentData().toUrl();
    QString kind;
    QUrl page = m_view->url();
    for (const SniffedMedia &m : m_media)
        if (m.url == media) { kind = m.kind; page = m.page; }
    const QStringList headers = headersFor(media, page);
    const QString title = m_view->title();
    const QString site = page.host();
    // Картинка страницы (og:image или постер видео)
    m_view->page()->runJavaScript(QStringLiteral(
        "(function(){var m=document.querySelector('meta[property=\"og:image\"]');"
        "var v=document.querySelector('video');return (m&&m.content)||(v&&v.poster)||'';})()"),
        [this, media, headers, title, site, kind](const QVariant &thumb) {
            emit downloadRequested(media, headers, title, site, thumb.toString(), kind);
        });
}

void BrowserWindow::closeEvent(QCloseEvent *e)
{
    m_view->page()->triggerAction(QWebEnginePage::Stop);
    m_view->setUrl(QUrl(QStringLiteral("about:blank")));
    FramedDialog::closeEvent(e);
}
