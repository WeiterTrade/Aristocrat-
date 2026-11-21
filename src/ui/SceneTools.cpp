#include "SceneTools.h"
#include "BrowserWindow.h"
#include "core/I18n.h"

#include <QJsonArray>
#include <algorithm>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUrlQuery>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <QWebEngineDownloadItem>
#include <QCoreApplication>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPointer>

// Подтверждение возраста (как в PageGrabber)
static const char *kAge =
    "(function(){var re=/(yes,?\\s*i\\s*am\\s*(over\\s*)?18|i\\s*am\\s*(over\\s*)?18|i'?m\\s*(over\\s*)?18|over\\s*18|"
    "i\\s*agree|enter\\s*(the\\s*)?site|^enter$)/i;"
    "var els=[].slice.call(document.querySelectorAll('a,button,input[type=submit],[role=button]'));"
    "for(var i=0;i<els.length;i++){var e=els[i];var t=(e.innerText||e.value||'').trim();"
    "if(t&&t.length<40&&re.test(t)&&e.offsetParent!==null){e.click();return 'clicked';}}"
    "window.scrollTo(0,document.body.scrollHeight);return 'none';})()";

// Поиск сцен: текст «чч:мм:сс - чч:мм:сс», название — ближайшая строка рядом с ним
static const char *kScenes =
    "(function(){var re=/(\\d{1,2}:\\d{2}:\\d{2})\\s*[-–—]\\s*(\\d{1,2}:\\d{2}:\\d{2})/;var out=[];var seen={};"
    "var w=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT);var n;"
    "while((n=w.nextNode())){var m=re.exec(n.textContent);if(!m)continue;var el=n.parentElement;var title='';"
    "for(var k=0,c=el;k<6&&c;k++,c=c.parentElement){var lines=(c.innerText||'').split('\\n').map(function(s){return s.trim();})"
    ".filter(function(s){return s&&!re.test(s)&&!/^\\+?\\s*\\d+\\s*more$/i.test(s)&&s.length>2;});"
    "if(lines.length){title=lines[0];break;}}"
    "var key=m[1]+'|'+m[2];if(seen[key])continue;seen[key]=1;out.push({start:m[1],end:m[2],title:title});}"
    // Adult Empire: у кнопки «More Screenshots» начало и конец сцены в секундах — ShowMoreScreens2017(id,start,end,sceneID,url)
    "if(!out.length){var f=function(x){x=+x;var h=Math.floor(x/3600),mi=Math.floor(x%3600/60),se=x%60;"
    "return h+':'+(mi<10?'0':'')+mi+':'+(se<10?'0':'')+se;};var sids={};"
    "[].slice.call(document.querySelectorAll('[onclick*=\"ShowMoreScreens\"]')).forEach(function(e){"
    "var a=/ShowMoreScreens\\w*\\(\\s*\\d+\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)/.exec(e.getAttribute('onclick')||'');"
    "if(!a||sids[a[3]])return;sids[a[3]]=1;var t='';var an=document.querySelector('a[name=\"scene_'+a[3]+'\"]');"
    "if(an){var r=an.nextElementSibling;var h=r&&r.querySelector('h3,h2,h4');if(h)t=h.textContent.trim().replace(/\\s+/g,' ');}"
    "out.push({start:f(a[1]),end:f(a[2]),title:t});});}"
    "return JSON.stringify(out);})()";

const char *sceneScript() { return kScenes; }
const char *ageScript() { return kAge; }

SceneFetcher::SceneFetcher(QObject *parent)
    : QObject(parent)
{
    m_view = new QWebEngineView;
    m_view->setAttribute(Qt::WA_DontShowOnScreen);
    m_view->resize(1280, 900);
    m_view->setPage(new QWebEnginePage(BrowserWindow::profile(), m_view));
    m_view->show();
    connect(m_view, &QWebEngineView::loadFinished, this, &SceneFetcher::onLoad);
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] { extract(); });
}

SceneFetcher::~SceneFetcher()
{
    delete m_view;
}

void SceneFetcher::start(const QUrl &page)
{
    // Сначала обычная страница фильма (сцены перечислены под описанием), затем страница с плеером
    QUrl plain = page;
    QUrlQuery q(plain);
    q.removeAllQueryItems(QStringLiteral("viewpart"));
    plain.setQuery(q);
    m_urls = {plain};
    if (plain != page)
        m_urls << page;
    m_tries = 0;
    emit progress(T("Открываю страницу фильма…"));
    m_view->load(m_urls.takeFirst());
    m_timeout.start(40000);
}

void SceneFetcher::onLoad()
{
    if (m_done)
        return;
    emit progress(T("Чтение списка сцен…"));
    if (QSettings().value(QStringLiteral("sites/adultConfirmed"), false).toBool())
        m_view->page()->runJavaScript(QString::fromUtf8(kAge));
    QTimer::singleShot(2500, this, &SceneFetcher::extract);
}

void SceneFetcher::extract()
{
    if (m_done)
        return;
    m_view->page()->runJavaScript(QString::fromUtf8(kScenes), [this](const QVariant &r) {
        if (m_done)
            return;
        QList<Chapter> list;
        for (const QJsonValue &v : QJsonDocument::fromJson(r.toString().toUtf8()).array()) {
            const QJsonObject o = v.toObject();
            Chapter c;
            c.start = Chapters::parseTime(o.value("start").toString());
            c.end = Chapters::parseTime(o.value("end").toString());
            c.original = o.value("title").toString();
            c.title = c.original;
            list << c;
        }
        if (!list.isEmpty()) {
            m_done = true;
            m_timeout.stop();
            std::sort(list.begin(), list.end(), [](const Chapter &a, const Chapter &b) { return a.start < b.start; });
            emit finished(list);
            return;
        }
        // Ещё не появились: окно 18+, подгрузка списка. Пробуем ещё раз или другую страницу
        if (++m_tries < 6) {
            if (QSettings().value(QStringLiteral("sites/adultConfirmed"), false).toBool())
                m_view->page()->runJavaScript(QString::fromUtf8(kAge));
            QTimer::singleShot(2000, this, &SceneFetcher::extract);
        } else if (!m_urls.isEmpty()) {
            m_tries = 0;
            m_view->load(m_urls.takeFirst());
        } else {
            m_done = true;
            m_timeout.stop();
            emit failed(T("На странице не найдено сцен с тайм-кодами."));
        }
    });
}

// ---------------- Загрузка через WebEngine ----------------
// У WebEngine свой HTTPS (Chromium), поэтому не нужны библиотеки OpenSSL, которых нет в Qt 5 под Windows.

namespace {
struct FetchRequest {
    QUrl url;
    QPointer<QObject> context;
    WebFetch::Callback cb;
};
struct FetchState {
    QWebEnginePage *page = nullptr;
    QList<FetchRequest> queue;
    bool busy = false;
    QTimer *timer = nullptr;
    int serial = 0;
};
FetchState &fetchState()
{
    static FetchState s;
    return s;
}
void fetchNext();
void fetchDone(const QByteArray &data)
{
    FetchState &s = fetchState();
    if (!s.busy)
        return;
    s.timer->stop();
    s.busy = false;
    ++s.serial;
    const FetchRequest r = s.queue.takeFirst();
    if (r.context)
        r.cb(data);
    QTimer::singleShot(0, [] { fetchNext(); });
}
void fetchNext()
{
    FetchState &s = fetchState();
    if (s.busy || s.queue.isEmpty())
        return;
    if (!s.page) {
        s.page = new QWebEnginePage(BrowserWindow::profile(), qApp);
        s.timer = new QTimer(qApp);
        s.timer->setSingleShot(true);
        QObject::connect(s.timer, &QTimer::timeout, [] { fetchDone({}); });
        QObject::connect(BrowserWindow::profile(), &QWebEngineProfile::downloadRequested, qApp,
                         [](QWebEngineDownloadItem *item) {
            FetchState &st = fetchState();
            if (item->page() != st.page || !st.busy)
                return;   // загрузки других страниц нас не касаются
            const int serial = st.serial;
            const QString dir = QDir::temp().filePath(QStringLiteral("aristocrat-fetch"));
            QDir().mkpath(dir);
            const QString name = QStringLiteral("f%1-%2.bin").arg(QCoreApplication::applicationPid()).arg(serial);
            QFile::remove(QDir(dir).filePath(name));
            item->setDownloadDirectory(dir);
            item->setDownloadFileName(name);
            QObject::connect(item, &QWebEngineDownloadItem::finished, qApp, [item, dir, name, serial] {
                QByteArray data;
                QFile f(QDir(dir).filePath(name));
                if (item->state() == QWebEngineDownloadItem::DownloadCompleted && f.open(QIODevice::ReadOnly))
                    data = f.readAll();
                f.close();
                f.remove();
                if (fetchState().serial == serial)
                    fetchDone(data);
            });
            item->accept();
        });
    }
    s.busy = true;
    s.timer->start(25000);   // ошибка сервера (404) не даёт загрузки — ждём недолго
    s.page->download(s.queue.first().url);
}
} // namespace

void WebFetch::get(const QUrl &url, QObject *context, Callback cb)
{
    fetchState().queue << FetchRequest{url, context, std::move(cb)};
    fetchNext();
}

// ---------------- Перевод ----------------

Translator::Translator(QObject *parent)
    : QObject(parent)
{
}

Translator::~Translator() = default;

void Translator::translate(const QStringList &lines, const QString &to)
{
    m_in = lines;
    m_out.clear();
    m_to = to;
    m_index = 0;
    next();
}

void Translator::next()
{
    if (m_index >= m_in.size()) {
        emit finished(m_out);
        return;
    }
    const QStringList batch = m_in.mid(m_index, 40);
    const QByteArray query = "client=gtx&sl=auto&tl=" + QUrl::toPercentEncoding(m_to) + "&dt=t&q="
                             + QUrl::toPercentEncoding(batch.join('\n'));
    WebFetch::get(QUrl::fromEncoded("https://translate.googleapis.com/translate_a/single?" + query), this,
                  [this, batch](const QByteArray &data) {
        // Ответ: [[["перевод","оригинал",…],…],…]
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isArray()) {
            emit failed(T("Сервис перевода недоступен. Проверьте подключение к интернету."));
            return;
        }
        QString joined;
        for (const QJsonValue &p : doc.array().at(0).toArray())
            joined += p.toArray().at(0).toString();
        const QStringList lines = joined.split('\n');
        for (int i = 0; i < batch.size(); ++i)
            m_out << (i < lines.size() && !lines[i].trimmed().isEmpty() ? lines[i].trimmed() : batch[i]);
        m_index += batch.size();
        next();
    });
}
