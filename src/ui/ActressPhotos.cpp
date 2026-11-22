#include "ActressPhotos.h"
#include "SceneTools.h"
#include "Widgets.h"
#include "core/I18n.h"
#include "core/Release.h"
#include "core/Theme.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <memory>
#include <QPainter>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>

namespace ActressPhotos {

QImage fitPortrait(const QImage &src)
{
    if (src.isNull())
        return {};
    QImage img = src.convertToFormat(QImage::Format_RGB32);
    const double target = 2.0 / 3.0;
    const double r = double(img.width()) / img.height();
    QRect crop;
    if (r > target) {   // шире — режем бока по центру
        const int w = qRound(img.height() * target);
        crop = QRect((img.width() - w) / 2, 0, w, img.height());
    } else {            // выше — режем больше снизу, чтобы не потерять голову
        const int h = qRound(img.width() / target);
        crop = QRect(0, qRound((img.height() - h) * 0.15), img.width(), h);
    }
    return img.copy(crop).scaled(1000, 1500, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QString cacheFile(const QString &name)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/actress-photos");
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + Release::actorFile(name) + QStringLiteral(".jpg");
}

QString findInLibrary(const QString &name, const QString &baseDir)
{
    if (baseDir.isEmpty() || !QDir(baseDir).exists())
        return {};
    const QString file = Release::actorFile(name) + QStringLiteral(".jpg");
    QString best;
    qint64 bestSize = 0;
    // Папки релизов: База/Релиз/.actors и База/Серия/Релиз/.actors
    QDirIterator it(baseDir, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    int guard = 0;
    while (it.hasNext() && ++guard < 20000) {
        const QString d = it.next();
        if (QFileInfo(d).fileName() != QLatin1String(".actors"))
            continue;
        const QFileInfo f(QDir(d).filePath(file));
        if (f.exists() && f.size() > bestSize && f.size() > 20000) {   // серые «пустые» фото пропускаем
            best = f.absoluteFilePath();
            bestSize = f.size();
        }
    }
    return best;
}

QString query(const QString &name)
{
    return name + QStringLiteral(" порно-актриса");
}

int score(const Candidate &c)
{
    if (c.url.isEmpty())
        return 0;
    if (c.w <= 0 || c.h <= 0)
        return 1;   // размер неизвестен — запасной вариант
    const double r = double(c.h) / c.w;
    if (r < 1.2)
        return 0;   // не портрет
    int s = 10;
    if (c.w >= 1000 && c.h >= 1500)
        s += 20;
    else if (c.h >= 1200)
        s += 8;
    if (r >= 1.4 && r <= 1.6)
        s += 5;
    return s;
}

bool saveChosen(const QString &name, const QByteArray &data)
{
    QImage img;
    img.loadFromData(data);
    if (img.isNull() || img.width() < 200 || img.height() < 300)
        return false;
    return fitPortrait(img).save(cacheFile(name), "JPG", 92);
}

} // namespace ActressPhotos

// ======================= Поиск картинок =======================

ImageSearch::ImageSearch(QObject *parent)
    : QObject(parent)
{
    m_view = new QWebEngineView;
    m_view->setAttribute(Qt::WA_DontShowOnScreen);
    m_view->resize(1400, 1000);
    m_view->setPage(new QWebEnginePage(QWebEngineProfile::defaultProfile(), m_view));
    m_view->show();
    connect(m_view, &QWebEngineView::loadFinished, this, [this](bool ok) {
        const int serial = m_serial;
        if (m_imageMode) {
            if (!ok) {
                ++m_serial;
                m_timeout.stop();
                auto cb = m_imgCb;
                m_imgCb = nullptr;
                if (cb) cb({});
                return;
            }
            QTimer::singleShot(300, this, [this, serial] { if (serial == m_serial) grab(0); });
            return;
        }
        QTimer::singleShot(1500, this, [this, serial] { if (serial == m_serial) extract(0); });
    });
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!m_imageMode && m_cb && !m_bing) {   // Яндекс не ответил вовремя — Bing
            searchBing();
            return;
        }
        ++m_serial;
        if (m_cb) {
            auto cb = m_cb;
            m_cb = nullptr;
            cb({});
        }
        if (m_imgCb) {
            auto cb = m_imgCb;
            m_imgCb = nullptr;
            cb({});
        }
    });
}

ImageSearch::~ImageSearch()
{
    delete m_view;
}

void ImageSearch::fetchImage(const QUrl &url, std::function<void(const QByteArray &)> cb)
{
    ++m_serial;
    m_imageMode = true;
    m_imgCb = std::move(cb);
    m_timeout.start(12000);
    m_view->load(url);
}

void ImageSearch::fetchBest(const ActressPhotos::Candidate &c, std::function<void(const QByteArray &)> cb,
                            std::function<void(int, int)> progress)
{
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    const int serial = ++m_bestSerial;
    QStringList urls{c.url};
    for (const QString &a : c.alts)
        if (!urls.contains(a))
            urls << a;
    while (urls.size() > 4)
        urls.removeLast();
    // Попытки: для каждого адреса — прямой запрос, затем скрытое окно
    const int total = urls.size() * 2;
    auto good = [](const QByteArray &data) {
        QImage img;
        return img.loadFromData(data) && img.width() >= 200 && img.height() >= 300;
    };
    auto step = std::make_shared<std::function<void(int)>>();
    *step = [this, serial, urls, total, good, cb, progress, step, ref = c.referer](int i) {
        if (serial != m_bestSerial)
            return;   // выбрано другое фото
        if (i >= total) {
            cb({});
            return;
        }
        if (progress)
            progress(i + 1, total);
        const QUrl url(urls[i / 2]);
        if (i % 2 == 0) {
            QNetworkRequest rq(url);
            rq.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
                                          "Chrome/124.0 Safari/537.36");
            rq.setRawHeader("Accept", "image/avif,image/webp,image/apng,image/*,*/*;q=0.8");
            const QUrl r = ref.isEmpty() ? QUrl(url.scheme() + QStringLiteral("://") + url.host() + QStringLiteral("/")) : QUrl(ref);
            rq.setRawHeader("Referer", r.toEncoded());
            rq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            rq.setTransferTimeout(10000);
            QNetworkReply *rep = m_net->get(rq);
            connect(rep, &QNetworkReply::finished, this, [this, rep, serial, i, step, good, cb] {
                rep->deleteLater();
                if (serial != m_bestSerial)
                    return;
                const QByteArray data = rep->readAll();
                if (rep->error() == QNetworkReply::NoError && good(data)) {
                    cb(data);
                    return;
                }
                qInfo("photo: direct %s failed: %s", qPrintable(rep->url().host()), qPrintable(rep->errorString()));
                (*step)(i + 1);
            });
            return;
        }
        fetchImage(url, [this, serial, i, step, good, cb](const QByteArray &data) {
            if (serial != m_bestSerial)
                return;
            if (good(data)) {
                cb(data);
                return;
            }
            (*step)(i + 1);
        });
    };
    (*step)(0);
}

void ImageSearch::grab(int attempt)
{
    // Открытая картинка — свой же сайт для страницы, поэтому её можно перерисовать и забрать
    static const char *js = R"JS((function(){var i=document.images[0];if(!i||!i.naturalWidth)return '';
var c=document.createElement('canvas');c.width=i.naturalWidth;c.height=i.naturalHeight;c.getContext('2d').drawImage(i,0,0);
try{return c.toDataURL('image/jpeg',0.95);}catch(e){return 'err';}})())JS";
    const int serial = m_serial;
    m_view->page()->runJavaScript(QString::fromUtf8(js), [this, serial, attempt](const QVariant &v) {
        if (serial != m_serial || !m_imgCb)
            return;
        const QString s = v.toString();
        if (s.isEmpty() && attempt < 6) {
            QTimer::singleShot(400, this, [this, serial, attempt] { if (serial == m_serial) grab(attempt + 1); });
            return;
        }
        m_timeout.stop();
        ++m_serial;
        QByteArray data;
        if (s.startsWith(QLatin1String("data:image")))
            data = QByteArray::fromBase64(s.section(',', 1).toLatin1());
        auto cb = m_imgCb;
        m_imgCb = nullptr;
        cb(data);
    });
}

void ImageSearch::search(const QString &query, Callback cb)
{
    ++m_serial;
    m_imageMode = false;
    m_cb = std::move(cb);
    m_query = query;
    m_bing = false;
    m_timeout.start(30000);
    // Яндекс Картинки: размер ровно 1000×1500
    const QString url = QStringLiteral("https://yandex.ru/images/search?text=%1&isize=eq&iw=1000&ih=1500")
                            .arg(QString::fromLatin1(QUrl::toPercentEncoding(query)));
    m_view->load(QUrl(url));
}

void ImageSearch::searchBing()
{
    // Запасной поиск: Bing, вертикальные от 1000×1500, безопасный поиск «умеренный»
    m_bing = true;
    m_timeout.start(25000);
    QString q = m_query;
    q.replace(QStringLiteral(" порно-актриса"), QStringLiteral(" lingerie"));
    const QString url = QStringLiteral("https://www.bing.com/images/search?q=%1&qft=%2&adlt=moderate&form=IRFLTR")
                            .arg(QString::fromLatin1(QUrl::toPercentEncoding(q)),
                                 QString::fromLatin1(QUrl::toPercentEncoding(
                                     QStringLiteral("+filterui:aspect-tall+filterui:imagesize-custom_1000_1500"))));
    m_view->load(QUrl(url));
}

void ImageSearch::extract(int attempt)
{
    // Яндекс: результаты в data-state блока ImagesApp (оригинал, его размер и неразмытая миниатюра)
    static const char *yandex = R"JS((function(){var out=[];try{var el=document.querySelector('[id^="ImagesApp-"]');
var st=JSON.parse(el.getAttribute('data-state'));var e=st.initialState.serpList.items.entities;
Object.keys(e).map(function(k){return e[k];}).sort(function(a,b){return (a.pos||0)-(b.pos||0);}).forEach(function(x){
 if(!x.origUrl)return;var t=x.imageUnblurred||x.image||'';if(t.indexOf('//')===0)t='https:'+t;
 var alts=[];var vd=x.viewerData||{};[].concat(vd.preview||[],vd.dups||[]).forEach(function(d){
  if(d&&d.url&&d.url!==x.origUrl&&alts.indexOf(d.url)<0&&(!d.w||d.w>=600))alts.push(d.url);});
 var ref=(vd.snippet&&vd.snippet.url)||(x.snippet&&x.snippet.url)||'';
 out.push({u:x.origUrl,t:t,w:x.origWidth||x.width||0,h:x.origHeight||x.height||0,alts:alts,ref:ref});});}catch(err){}
return JSON.stringify(out);})())JS";
    static const char *bing = R"JS((function(){var out=[];
[].forEach.call(document.querySelectorAll('a.iusc'),function(a){try{var m=JSON.parse(a.getAttribute('m')||'{}');
 var li=a.closest('li')||a.parentElement;var info=li?(li.innerText||''):'';var mm=/(\d{3,5})\s*[x×]\s*(\d{3,5})/.exec(info);
 if(m.murl)out.push({u:m.murl,t:m.turl||'',w:mm?+mm[1]:0,h:mm?+mm[2]:0});}catch(e){}});
return JSON.stringify(out);})())JS";
    const int serial = m_serial;
    m_view->page()->runJavaScript(QString::fromUtf8(m_bing ? bing : yandex), [this, serial, attempt](const QVariant &v) {
        if (serial != m_serial || !m_cb)
            return;
        QList<ActressPhotos::Candidate> list;
        for (const QJsonValue &x : QJsonDocument::fromJson(v.toString().toUtf8()).array()) {
            const QJsonObject o = x.toObject();
            ActressPhotos::Candidate c;
            c.url = o.value("u").toString();
            c.thumb = o.value("t").toString();
            c.w = o.value("w").toInt();
            c.h = o.value("h").toInt();
            for (const QJsonValue &a : o.value("alts").toArray())
                c.alts << a.toString();
            c.referer = o.value("ref").toString();
            list << c;
        }
        if (list.isEmpty() && attempt < 3) {
            QTimer::singleShot(1500, this, [this, serial, attempt] { if (serial == m_serial) extract(attempt + 1); });
            return;
        }
        if (list.isEmpty() && !m_bing) {   // Яндекс не ответил (капча и т. п.) — Bing
            qInfo("image search: Yandex gave nothing (%s), trying Bing", qPrintable(m_view->url().host()));
            searchBing();
            return;
        }
        m_timeout.stop();
        auto cb = m_cb;
        m_cb = nullptr;
        cb(list);
    });
}

// ======================= Окно выбора фото =======================

ActressPhotoDialog::ActressPhotoDialog(const QStringList &names, const QString &baseDir, QWidget *parent)
    : FramedDialog(parent, true), m_names(names), m_base(baseDir)
{
    setWindowTitle(T("Фото актрис"));
    resize(1100, 760);
    m_search = new ImageSearch(this);

    auto *root = contentLayout();
    root->setContentsMargins(18, 12, 18, 12);
    auto *hint = new QLabel(T("Нужны фото 1000×1500. Поиск — Яндекс Картинки (ровно 1000×1500), запасной — Bing. "
                              "Щёлкните по фото — оно будет обрезано до 2:3 и запомнено для всех следующих релизов."), this);
    hint->setObjectName(QStringLiteral("Muted"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto *cols = new QHBoxLayout;
    m_left = new QListWidget(this);
    m_left->setIconSize(QSize(60, 90));
    m_left->setFixedWidth(250);
    cols->addWidget(m_left);

    auto *right = new QVBoxLayout;
    auto *qrow = new QHBoxLayout;
    m_query = new QLineEdit(this);
    auto *find = Ui::primaryButton(T("Искать"), this);
    qrow->addWidget(m_query, 1);
    qrow->addWidget(find);
    right->addLayout(qrow);
    m_grid = new QListWidget(this);
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setIconSize(QSize(150, 225));
    m_grid->setGridSize(QSize(166, 262));
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setMovement(QListView::Static);
    m_grid->setWordWrap(true);
    right->addWidget(m_grid, 1);
    m_state = new QLabel(this);
    m_state->setObjectName(QStringLiteral("StatusLabel"));
    right->addWidget(m_state);
    cols->addLayout(right, 1);
    root->addLayout(cols, 1);

    auto *btns = new QHBoxLayout;
    auto *close = Ui::primaryButton(T("Готово"), this);
    btns->addStretch();
    btns->addWidget(close);
    root->addLayout(btns);

    for (int i = 0; i < m_names.size(); ++i) {
        new QListWidgetItem(m_names[i], m_left);
        refreshLeft(i);
    }
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_left, &QListWidget::currentRowChanged, this, &ActressPhotoDialog::selectActress);
    connect(find, &QPushButton::clicked, this, &ActressPhotoDialog::runSearch);
    connect(m_query, &QLineEdit::returnPressed, this, &ActressPhotoDialog::runSearch);
    connect(m_grid, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) { choose(it->data(Qt::UserRole).toInt()); });
    if (!m_names.isEmpty())
        m_left->setCurrentRow(0);
}

void ActressPhotoDialog::refreshLeft(int row)
{
    QListWidgetItem *it = m_left->item(row);
    const QString name = m_names[row];
    QString file = ActressPhotos::cacheFile(name);
    if (!QFileInfo::exists(file))
        file = ActressPhotos::findInLibrary(name, m_base);
    QPixmap pm(file);
    if (pm.isNull()) {
        pm = QPixmap(60, 90);
        pm.fill(QColor(0x33, 0x28, 0x2C));
        it->setText(name + QLatin1Char('\n') + T("фото не выбрано"));
    } else {
        it->setText(name + QLatin1Char('\n') + (file == ActressPhotos::cacheFile(name) ? T("выбрано") : T("из медиатеки")));
    }
    it->setIcon(QIcon(pm.scaled(60, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

void ActressPhotoDialog::selectActress(int row)
{
    if (row < 0 || row >= m_names.size())
        return;
    m_query->setText(ActressPhotos::query(m_names[row]));
    runSearch();
}

void ActressPhotoDialog::runSearch()
{
    m_grid->clear();
    m_cands.clear();
    m_state->setText(T("Ищу фото…"));
    m_search->search(m_query->text().trimmed(), [this](const QList<ActressPhotos::Candidate> &list) {
        m_cands = list;
        m_state->setText(list.isEmpty() ? T("Ничего не нашлось. Измените запрос, например уберите «lingerie».")
                                        : T("Найдено: %1. Щёлкните по подходящему фото.").arg(list.size()));
        for (int i = 0; i < m_cands.size() && i < 40; ++i) {
            const ActressPhotos::Candidate &c = m_cands[i];
            auto *it = new QListWidgetItem(c.w > 0 ? QStringLiteral("%1×%2").arg(c.w).arg(c.h) : QString(), m_grid);
            it->setData(Qt::UserRole, i);
            if (c.w > 0 && (c.w < 1000 || c.h < 1500))
                it->setForeground(Theme::color("#8A7F72"));
            if (!c.thumb.isEmpty())
                WebFetch::get(QUrl(c.thumb), m_grid, [it](const QByteArray &data) {
                    QPixmap pm;
                    if (pm.loadFromData(data))
                        it->setIcon(QIcon(pm.scaled(150, 225, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                });
        }
    });
}

void ActressPhotoDialog::choose(int index)
{
    const int row = m_left->currentRow();
    if (row < 0 || index < 0 || index >= m_cands.size())
        return;
    const QString name = m_names[row];
    const ActressPhotos::Candidate c = m_cands[index];
    m_grid->setCurrentRow(index);
    m_state->setText(T("Скачиваю фото…"));
    m_search->fetchBest(c, [this, name, row](const QByteArray &data) {
        if (ActressPhotos::saveChosen(name, data)) {
            m_state->setText(T("Фото «%1» сохранено (1000×1500).").arg(name));
            refreshLeft(row);
            return;
        }
        m_state->setText(T("Это фото сайт не отдаёт — выберите другое."));
    }, [this](int i, int n) {
        m_state->setText(T("Скачиваю фото… попытка %1 из %2").arg(i).arg(n));
    });
}
