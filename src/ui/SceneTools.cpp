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
    "return JSON.stringify(out);})()";

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

// ---------------- Перевод ----------------

Translator::Translator(QObject *parent)
    : QObject(parent)
{
    m_page = new QWebEnginePage(this);   // отдельный профиль «без следов»
    connect(m_page, &QWebEnginePage::loadFinished, this, [this](bool ok) {
        if (!ok) {
            emit failed(T("Сервис перевода недоступен. Проверьте подключение к интернету."));
            return;
        }
        m_page->toPlainText([this](const QString &text) {
            // Ответ: [[["перевод","оригинал",…],…],…]
            const QJsonArray parts = QJsonDocument::fromJson(text.toUtf8()).array().at(0).toArray();
            QString joined;
            for (const QJsonValue &p : parts)
                joined += p.toArray().at(0).toString();
            const QStringList lines = joined.split('\n');
            const int batchStart = m_index;
            const int batchSize = qMin(40, m_in.size() - batchStart);
            for (int i = 0; i < batchSize; ++i)
                m_out << (i < lines.size() ? lines[i].trimmed() : m_in[batchStart + i]);
            m_index += batchSize;
            next();
        });
    });
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
    m_page->load(QUrl::fromEncoded("https://translate.googleapis.com/translate_a/single?" + query));
}
