#include "CoverArt.h"
#include "core/I18n.h"
#include "Widgets.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QWebEngineDownloadItem>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>

namespace CoverArt {

QWebEngineProfile *profile()
{
    static QWebEngineProfile *p = nullptr;
    if (!p) {
        p = new QWebEngineProfile(QStringLiteral("AristocratCovers"), qApp);
        p->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
        p->settings()->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, true);
    }
    return p;
}

void stampPoster(const QString &file, bool k4, bool dvd, QObject *ctx, std::function<void(bool)> done)
{
    if (!k4 && !dvd) {
        done(true);
        return;
    }
    CoverEngine::instance()->stamp(file, k4, dvd, ctx, std::move(done));
}

bool addGenre(const QString &nfoFile, const QString &genre)
{
    QFile f(nfoFile);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QString x = QString::fromUtf8(f.readAll());
    f.close();
    const QString tag = QStringLiteral("<genre>%1</genre>").arg(genre.toHtmlEscaped());
    if (x.contains(tag, Qt::CaseInsensitive))
        return true;
    const QString eol = x.contains(QLatin1String("\r\n")) ? QStringLiteral("\r\n") : QStringLiteral("\n");
    int at = x.indexOf(QLatin1String("<genre>"));
    QString indent = QStringLiteral("  ");
    if (at >= 0) {
        const int ls = x.lastIndexOf(QLatin1Char('\n'), at) + 1;
        indent = x.mid(ls, at - ls);
        at = ls;
    } else {
        at = x.lastIndexOf(QLatin1String("</movie>"));
        if (at < 0)
            return false;
        const int ls = x.lastIndexOf(QLatin1Char('\n'), at) + 1;
        at = ls;
    }
    x.insert(at, indent + tag + eol);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(x.toUtf8());
    return true;
}

} // namespace CoverArt

// ======================= Невидимый генератор =======================

CoverEngine *CoverEngine::instance()
{
    static CoverEngine *e = new CoverEngine(qApp);
    return e;
}

CoverEngine::CoverEngine(QObject *parent) : QObject(parent)
{
    m_page = new QWebEnginePage(CoverArt::profile(), this);
    connect(m_page, &QWebEnginePage::loadFinished, this, [this](bool) {
        m_ready = true;
        next();
    });
    m_page->load(QUrl(QStringLiteral("qrc:/covers/editor.html")));
}

void CoverEngine::stamp(const QString &file, bool k4, bool dvd, QObject *ctx, std::function<void(bool)> done)
{
    Job j;
    j.file = file;
    j.k4 = k4;
    j.dvd = dvd;
    j.ctx = ctx;
    j.hasCtx = ctx != nullptr;
    j.done = std::move(done);
    m_queue.enqueue(j);
    next();
}

void CoverEngine::next()
{
    if (!m_ready || m_busy || m_queue.isEmpty())
        return;
    m_cur = m_queue.dequeue();
    QFile f(m_cur.file);
    if (!f.open(QIODevice::ReadOnly)) {
        finish(false);
        return;
    }
    m_busy = true;
    const QString b64 = QString::fromLatin1(f.readAll().toBase64());
    // Та же функция, что в генераторе: stampPoster(blob, want4k, wantDvd) — со шрифтами генератора
    const QString js = QStringLiteral(
        "window.__ao=null;(async()=>{try{"
        "try{await Promise.all([document.fonts.load('700 40px Orbitron'),document.fonts.load('700 40px \"Playfair Display\"'),"
        "document.fonts.load('800 100px \"Playfair Display\"')]);}catch(e){}"
        "const b=await (await fetch('data:image/jpeg;base64,%1')).blob();"
        "const o=await stampPoster(b,%2,%3);const fr=new FileReader();"
        "fr.onload=()=>{window.__ao=fr.result;};fr.onerror=()=>{window.__ao='ERR';};fr.readAsDataURL(o);"
        "}catch(e){window.__ao='ERR:'+(e&&e.message);}})();0")
                           .arg(b64, m_cur.k4 ? QStringLiteral("true") : QStringLiteral("false"),
                                m_cur.dvd ? QStringLiteral("true") : QStringLiteral("false"));
    m_page->runJavaScript(js);
    poll(0);
}

void CoverEngine::poll(int tries)
{
    m_page->runJavaScript(QStringLiteral("window.__ao"), [this, tries](const QVariant &v) {
        const QString r = v.toString();
        if (r.isEmpty()) {
            if (tries > 200) {   // ~50 с
                finish(false);
                return;
            }
            QTimer::singleShot(250, this, [this, tries] { poll(tries + 1); });
            return;
        }
        if (r.startsWith(QLatin1String("ERR"))) {
            qWarning("CoverEngine: %s", qPrintable(r));
            finish(false);
            return;
        }
        const int comma = r.indexOf(QLatin1Char(','));
        finish(true, QByteArray::fromBase64(r.mid(comma + 1).toLatin1()));
    });
}

void CoverEngine::finish(bool ok, const QByteArray &jpeg)
{
    if (ok && jpeg.size() > 1000) {
        QFile out(m_cur.file);
        ok = out.open(QIODevice::WriteOnly | QIODevice::Truncate) && out.write(jpeg) == jpeg.size();
    } else {
        ok = false;
    }
    Job j = m_cur;
    m_cur = Job();
    m_busy = false;
    if (j.done && (!j.hasCtx || j.ctx))
        j.done(ok);
    next();
}

// ======================= Вкладка «Общая обложка» =======================

CoverCollagePage::CoverCollagePage(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);
    m_state = new QLabel(T("Общая обложка для нескольких частей — тот же генератор, что в скрипте Adult Aristocrat: "
                           "раскладки, темы, подпись, ягоды, значки 4K и DVD."), this);
    m_state->setObjectName(QStringLiteral("StatusLabel"));
    m_state->setWordWrap(true);
    root->addWidget(m_state);
    m_view = new QWebEngineView(this);
    m_view->setPage(new QWebEnginePage(CoverArt::profile(), m_view));
    root->addWidget(m_view, 1);

    connect(m_view, &QWebEngineView::loadFinished, this, [this](bool ok) {
        if (!ok) {
            m_state->setText(T("Генератор обложек не загрузился."));
            return;
        }
        // Оставляем на странице только раздел «Общая обложка для нескольких частей»
        static const char *isolate = R"JS((function(){
var d=[].slice.call(document.querySelectorAll('details')).find(function(x){var s=x.querySelector('summary');return s&&/Общая обложка/.test(s.textContent);});
if(!d)return 0;d.open=true;var n=d;
while(n&&n!==document.body){var p=n.parentElement;if(!p)break;
 [].forEach.call(p.children,function(c){if(c!==n&&c.tagName!=='SCRIPT'&&c.tagName!=='STYLE'&&c.tagName!=='LINK')c.style.display='none';});
 p.style.display=p.style.display==='none'?'':p.style.display;p.style.maxWidth='none';p.style.gridTemplateColumns='1fr';n=p;}
var s=d.querySelector('summary');if(s)s.style.pointerEvents='none';
var b=document.getElementById('cgsave');if(b)b.textContent='Сохранить обложку (JPEG)…';
document.body.style.margin='0';document.body.style.padding='8px';
return 1;})())JS";
        m_view->page()->runJavaScript(QString::fromUtf8(isolate));
    });

    // Сохранение готовой обложки: спросить, куда
    connect(CoverArt::profile(), &QWebEngineProfile::downloadRequested, this, [this](QWebEngineDownloadItem *item) {
        if (!isVisible() || item->page() != m_view->page())
            return;
        QSettings st;
        QString dir = st.value(QStringLiteral("covers/lastDir")).toString();
        if (dir.isEmpty())
            dir = QDir::fromNativeSeparators(st.value(QStringLiteral("ae/baseDir")).toString());
        if (dir.isEmpty())
            dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        const QString path = QFileDialog::getSaveFileName(this, T("Сохранить обложку"), QDir(dir).filePath(item->downloadFileName()),
                                                          T("Изображения JPEG (*.jpg *.jpeg)"));
        if (path.isEmpty()) {
            item->cancel();
            return;
        }
        const QFileInfo fi(path);
        st.setValue(QStringLiteral("covers/lastDir"), fi.absolutePath());
        item->setDownloadDirectory(fi.absolutePath());
        item->setDownloadFileName(fi.fileName());
        connect(item, &QWebEngineDownloadItem::finished, this, [this, item, path] {
            m_state->setText(item->state() == QWebEngineDownloadItem::DownloadCompleted
                                 ? T("Обложка сохранена: %1").arg(QDir::toNativeSeparators(path))
                                 : T("Обложку сохранить не удалось."));
        });
        item->accept();
    });
}

void CoverCollagePage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (!m_loaded) {   // загружаем только при первом открытии вкладки
        m_loaded = true;
        m_view->load(QUrl(QStringLiteral("qrc:/covers/editor.html")));
    }
}
