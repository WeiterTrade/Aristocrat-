#include "ServerScan.h"
#include "ActressPhotos.h"
#include "ReleaseManager.h"
#include "SceneTools.h"
#include "ServerPage.h"
#include "Widgets.h"
#include "core/Gender.h"
#include "core/I18n.h"
#include "core/Release.h"

#include <QBuffer>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <algorithm>
#include <functional>
#include <memory>

namespace {

enum Col { ColCheck = 0, ColType, ColName, ColMissing, ColStatus, ColCount };

// Поиск страницы фильма на Adult Empire через поиск Bing (если в NFO нет номера SKU)
class PageFinder : public QObject
{
public:
    using Callback = std::function<void(const QString &url)>;
    explicit PageFinder(QObject *parent)
        : QObject(parent)
    {
        m_view = new QWebEngineView;
        m_view->setAttribute(Qt::WA_DontShowOnScreen);
        m_view->resize(1200, 900);
        m_view->show();
        connect(m_view, &QWebEngineView::loadFinished, this, [this] {
            const int serial = m_serial;
            QTimer::singleShot(1500, this, [this, serial] { if (serial == m_serial) extract(); });
        });
        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout, this, [this] { done(QString()); });
    }
    ~PageFinder() override { delete m_view; }
    void find(const QString &title, int year, Callback cb)
    {
        ++m_serial;
        m_cb = std::move(cb);
        m_timer.start(25000);
        QString q = QStringLiteral("site:adultempire.com \"%1\"").arg(title);
        if (year > 0)
            q += QStringLiteral(" %1").arg(year);
        m_view->load(QUrl(QStringLiteral("https://www.bing.com/search?q=") + QString::fromLatin1(QUrl::toPercentEncoding(q))));
    }

private:
    void extract()
    {
        static const char *js = R"JS((function(){var out='';[].slice.call(document.querySelectorAll('a[href]')).some(function(a){
 var h=a.href||'';if(/^https?:\/\/(www\.)?adultempire\.com\/\d{4,9}\//i.test(h)){out=h;return true;}return false;});return out;})())JS";
        const int serial = m_serial;
        m_view->page()->runJavaScript(QString::fromUtf8(js), [this, serial](const QVariant &v) {
            if (serial == m_serial)
                done(v.toString());
        });
    }
    void done(const QString &url)
    {
        m_timer.stop();
        ++m_serial;
        auto cb = m_cb;
        m_cb = nullptr;
        if (cb)
            cb(url);
    }
    QWebEngineView *m_view;
    QTimer m_timer;
    Callback m_cb;
    int m_serial = 0;
};

QString norm(QString s)
{
    s = s.toLower();
    s.remove(QRegularExpression(QStringLiteral("[^a-z0-9]+")));
    return s;
}

QStringList coverUrls(const QString &sku, bool back)
{
    QStringList out;
    if (sku.isEmpty())
        return out;
    for (const char *size : {"w=1500&h=2100", "w=1000&h=1400", "w=500&h=700"})
        out << QStringLiteral("https://imgs4cdn.adultempire.com/movie/%1?%2&fit=cover%3")
                   .arg(sku, QLatin1String(size), back ? QStringLiteral("&v=back") : QString());
    return out;
}

QByteArray toJpeg(const QByteArray &data)
{
    QImage img;
    if (!img.loadFromData(data) || img.width() < 200 || img.height() < 200)
        return {};
    if (data.startsWith("\xFF\xD8"))
        return data;
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.convertToFormat(QImage::Format_RGB32).save(&buf, "JPG", 92);
    return out;
}

} // namespace

ServerScanDialog::ServerScanDialog(QWidget *parent)
    : FramedDialog(parent, true)
{
    setWindowTitle(T("Пробежаться по серверу"));
    resize(1100, 740);
    m_net = new QNetworkAccessManager(this);

    auto *root = contentLayout();
    root->setContentsMargins(20, 14, 20, 14);
    root->setSpacing(8);
    auto *info = new QLabel(T("Программа найдёт на сервере актрис без биографии или фото и фильмы с неполными данными и допишет "
                              "недостающее. Уже заполненное не меняется. Данные фильмов берутся с Adult Empire, анкеты — из баз "
                              "(нейросеть — только если там пусто), фото актрис — 1000×1500."), this);
    info->setWordWrap(true);
    root->addWidget(info);

    auto *opts = new QHBoxLayout;
    auto mk = [this, opts](const QString &text, const char *key) {
        auto *c = new QCheckBox(text, this);
        c->setObjectName(QStringLiteral("PlainCheck"));
        c->setChecked(QSettings().value(QStringLiteral("scan/") + QLatin1String(key), true).toBool());
        connect(c, &QCheckBox::toggled, this, [key](bool on) { QSettings().setValue(QStringLiteral("scan/") + QLatin1String(key), on); });
        opts->addWidget(c);
        return c;
    };
    m_bios = mk(T("Биографии актрис"), "bios");
    m_photos = mk(T("Фото актрис"), "photos");
    m_movieData = mk(T("Данные фильмов"), "movieData");
    m_movieArt = mk(T("Обложки фильмов"), "movieArt");
    opts->addStretch();
    m_scan = Ui::primaryButton(T("Сканировать сервер"), this);
    opts->addWidget(m_scan);
    root->addLayout(opts);

    m_table = new QTableWidget(0, ColCount, this);
    m_table->setHorizontalHeaderLabels({QString(), T("Тип"), T("Название"), T("Не хватает"), T("Состояние")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    Ui::themedStyle(m_table, QStringLiteral(
        "QTableWidget{background:#171113;gridline-color:#33282C;border:1px solid #33282C;}"
        "QHeaderView::section{background:#261E21;color:#CFC3A8;border:none;border-bottom:1px solid #33282C;padding:5px;}"));
    root->addWidget(m_table, 1);

    m_progress = new QProgressBar(this);
    m_progress->setVisible(false);
    root->addWidget(m_progress);
    m_state = new QLabel(this);
    m_state->setObjectName(QStringLiteral("StatusLabel"));
    m_state->setWordWrap(true);
    root->addWidget(m_state);

    auto *btns = new QHBoxLayout;
    auto *close = new QPushButton(T("Закрыть"), this);
    m_stop = new QPushButton(T("Остановить"), this);
    m_stop->setVisible(false);
    m_go = Ui::primaryButton(T("Заполнить отмеченное"), this);
    m_go->setEnabled(false);
    btns->addWidget(close);
    btns->addStretch();
    btns->addWidget(m_stop);
    btns->addWidget(m_go);
    root->addLayout(btns);

    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_scan, &QPushButton::clicked, this, &ServerScanDialog::scan);
    connect(m_go, &QPushButton::clicked, this, &ServerScanDialog::start);
    connect(m_stop, &QPushButton::clicked, this, [this] {
        m_stopRequested = true;
        m_state->setText(T("Останавливаю после текущей записи…"));
    });

    if (!Jellyfin::loggedIn())
        m_state->setText(T("Сначала войдите на сервер во вкладке «Сервер»."));
    if (qEnvironmentVariableIsSet("ARISTOCRAT_TEST_SCAN"))
        QTimer::singleShot(500, this, &ServerScanDialog::scan);
}

void ServerScanDialog::setBusy(bool busy, const QString &text)
{
    m_scan->setEnabled(!busy);
    m_go->setEnabled(!busy && !m_entries.isEmpty());
    m_stop->setVisible(busy);
    if (!text.isEmpty())
        m_state->setText(text);
}

QString ServerScanDialog::missingText(const Entry &e) const
{
    QStringList m;
    if (e.needBio) m << T("биография");
    if (e.needPhoto) m << T("фото");
    if (e.needOverview) m << T("описание");
    if (e.needGenres) m << T("жанры");
    if (e.needStudio) m << T("студия");
    if (e.needPeople) m << T("актрисы");
    if (e.needCountry) m << T("страна");
    if (e.needPoster) m << T("постер");
    if (e.needBack) m << T("задняя обложка");
    if (e.needFanart) m << T("фан-арт");
    return m.join(QStringLiteral(", "));
}

void ServerScanDialog::scan()
{
    if (!Jellyfin::loggedIn()) {
        m_state->setText(T("Сначала войдите на сервер во вкладке «Сервер»."));
        return;
    }
    m_entries.clear();
    m_table->setRowCount(0);
    setBusy(true, T("Сканирую актрис на сервере…"));
    if (!m_bios->isChecked() && !m_photos->isChecked()) {
        scanMovies();
        return;
    }
    QNetworkReply *r = Jellyfin::request(m_net, QStringLiteral("GET"),
                                         QStringLiteral("/Persons?personTypes=Actor&fields=Overview&enableImages=true&limit=20000"));
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            setBusy(false, T("Сервер не ответил: %1").arg(r->errorString()));
            return;
        }
        for (const QJsonValue &v : QJsonDocument::fromJson(r->readAll()).object().value("Items").toArray()) {
            const QJsonObject p = v.toObject();
            Entry e;
            e.id = p.value("Id").toString();
            e.name = p.value("Name").toString();
            e.item = p;
            if (e.id.isEmpty() || e.name.isEmpty())
                continue;
            Gender::Source src = Gender::None;
            const Gender::Value g = Gender::known(e.name, &src);
            if (g == Gender::Male || (g == Gender::Unknown && Gender::byFirstName(e.name) == Gender::Male))
                continue;   // мужчины не нужны
            e.needBio = m_bios->isChecked() && p.value("Overview").toString().trimmed().isEmpty();
            e.needPhoto = m_photos->isChecked() && !p.value("ImageTags").toObject().contains("Primary");
            if (e.needBio || e.needPhoto)
                m_entries << e;
        }
        scanMovies();
    });
}

void ServerScanDialog::scanMovies()
{
    if (!m_movieData->isChecked() && !m_movieArt->isChecked()) {
        showList();
        return;
    }
    m_state->setText(T("Сканирую фильмы на сервере…"));
    const QString uid = QSettings().value(QStringLiteral("server/userId")).toString();
    QNetworkReply *r = Jellyfin::request(m_net, QStringLiteral("GET"),
        QStringLiteral("/Users/%1/Items?IncludeItemTypes=Movie&Recursive=true&Fields=Overview,Genres,Studios,People,OriginalTitle,"
                       "ProductionYear,ProviderIds,ProductionLocations&EnableImageTypes=Primary,Backdrop,BoxRear&Limit=20000").arg(uid));
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            setBusy(false, T("Сервер не ответил: %1").arg(r->errorString()));
            return;
        }
        static const QRegularExpression skuRe(QStringLiteral("^\\d{5,9}$"));
        for (const QJsonValue &v : QJsonDocument::fromJson(r->readAll()).object().value("Items").toArray()) {
            const QJsonObject m = v.toObject();
            Entry e;
            e.movie = true;
            e.id = m.value("Id").toString();
            e.name = m.value("Name").toString();
            e.original = m.value("OriginalTitle").toString();
            e.year = m.value("ProductionYear").toInt();
            e.item = m;
            const QJsonObject ids = m.value("ProviderIds").toObject();
            for (const QString &k : ids.keys())
                if (skuRe.match(ids.value(k).toString().trimmed()).hasMatch())
                    e.sku = ids.value(k).toString().trimmed();
            bool actor = false;
            for (const QJsonValue &pv : m.value("People").toArray())
                actor |= pv.toObject().value("Type").toString() == QLatin1String("Actor");
            const QJsonObject tags = m.value("ImageTags").toObject();
            if (m_movieData->isChecked()) {
                e.needOverview = m.value("Overview").toString().trimmed().isEmpty();
                e.needGenres = m.value("Genres").toArray().isEmpty();
                e.needStudio = m.value("Studios").toArray().isEmpty();
                e.needPeople = !actor;
                e.needCountry = m.value("ProductionLocations").toArray().isEmpty();
            }
            if (m_movieArt->isChecked()) {
                e.needPoster = !tags.contains("Primary");
                e.needBack = !tags.contains("BoxRear");
                e.needFanart = m.value("BackdropImageTags").toArray().isEmpty();
            }
            if (e.needOverview || e.needGenres || e.needStudio || e.needPeople || e.needCountry || e.needPoster || e.needBack || e.needFanart)
                m_entries << e;
        }
        showList();
    });
}

void ServerScanDialog::showList()
{
    m_table->setRowCount(0);
    int actresses = 0, movies = 0;
    for (const Entry &e : m_entries) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto *c = new QTableWidgetItem;
        c->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        c->setCheckState(Qt::Checked);
        m_table->setItem(row, ColCheck, c);
        m_table->setItem(row, ColType, new QTableWidgetItem(e.movie ? T("Фильм") : T("Актриса")));
        m_table->setItem(row, ColName, new QTableWidgetItem(e.movie && !e.original.isEmpty() && e.original != e.name
                                                                ? e.name + QStringLiteral("  (") + e.original + QLatin1Char(')')
                                                                : e.name));
        m_table->setItem(row, ColMissing, new QTableWidgetItem(missingText(e)));
        m_table->setItem(row, ColStatus, new QTableWidgetItem);
        (e.movie ? movies : actresses)++;
    }
    setBusy(false, m_entries.isEmpty() ? T("Всё заполнено — дописывать нечего.")
                                       : T("Не хватает данных: актрис — %1, фильмов — %2. Снимите отметки, если что-то трогать не нужно, "
                                           "и нажмите «Заполнить отмеченное».").arg(actresses).arg(movies));
    if (qEnvironmentVariableIsSet("ARISTOCRAT_TEST_SCAN") && !m_entries.isEmpty())
        QTimer::singleShot(500, this, &ServerScanDialog::start);
}

void ServerScanDialog::start()
{
    m_queue.clear();
    for (int r = 0; r < m_table->rowCount(); ++r)
        if (m_table->item(r, ColCheck)->checkState() == Qt::Checked)
            m_queue << r;
    if (m_queue.isEmpty())
        return;
    m_total = m_queue.size();
    m_stopRequested = false;
    m_progress->setRange(0, m_total);
    m_progress->setValue(0);
    m_progress->setVisible(true);
    setBusy(true, T("Заполняю…"));
    next();
}

void ServerScanDialog::next()
{
    m_progress->setValue(m_total - m_queue.size());
    if (m_queue.isEmpty() || m_stopRequested) {
        setBusy(false, m_stopRequested ? T("Остановлено. Готово: %1 из %2.").arg(m_total - m_queue.size()).arg(m_total)
                                       : T("Пробежка завершена: %1 записей.").arg(m_total));
        return;
    }
    const int row = m_queue.takeFirst();
    m_table->scrollToItem(m_table->item(row, ColName));
    m_table->item(row, ColStatus)->setText(T("в работе…"));
    m_state->setText(T("%1 из %2: %3").arg(m_total - m_queue.size()).arg(m_total).arg(m_entries[row].name));
    if (m_entries[row].movie)
        doMovie(row);
    else
        doPerson(row);
}

void ServerScanDialog::finishEntry(int row, const QString &status)
{
    m_table->item(row, ColStatus)->setText(status);
    m_table->item(row, ColCheck)->setCheckState(Qt::Unchecked);
    QTimer::singleShot(0, this, &ServerScanDialog::next);
}

// ---------------- Актрисы ----------------

void ServerScanDialog::doPerson(int row)
{
    Entry &e = m_entries[row];
    if (!e.needBio) {
        personPhoto(row);
        return;
    }
    if (!m_iafd)
        m_iafd = new IafdBio(this);
    m_iafd->fetch(e.name, [this, row](const QJsonObject &data) {
        composeBio(m_net, this, m_entries[row].name, data, [this, row](const QJsonObject &bio) {
            const QString text = bio.value("text").toString();
            if (text.isEmpty()) {
                m_table->item(row, ColStatus)->setText(T("биография: нет данных"));
                personPhoto(row);
                return;
            }
            const QString uid = QSettings().value(QStringLiteral("server/userId")).toString();
            QNetworkReply *r = Jellyfin::request(m_net, QStringLiteral("GET"), QStringLiteral("/Users/%1/Items/%2").arg(uid, m_entries[row].id));
            connect(r, &QNetworkReply::finished, this, [this, r, row, bio, text] {
                r->deleteLater();
                QJsonObject item = QJsonDocument::fromJson(r->readAll()).object();
                if (item.isEmpty()) {
                    m_table->item(row, ColStatus)->setText(T("биография: ошибка сервера"));
                    personPhoto(row);
                    return;
                }
                item.insert("Overview", text);
                if (!bio.value("born").toString().isEmpty() && item.value("PremiereDate").toString().isEmpty())
                    item.insert("PremiereDate", bio.value("born").toString() + QStringLiteral("T00:00:00.0000000Z"));
                if (!bio.value("place").toString().isEmpty() && item.value("ProductionLocations").toArray().isEmpty())
                    item.insert("ProductionLocations", QJsonArray{bio.value("place").toString()});
                QJsonArray tags = item.value("Tags").toArray();
                for (const QJsonValue &a : bio.value("aliases").toArray()) {
                    bool have = false;
                    for (const QJsonValue &t : tags)
                        have |= t.toString().compare(a.toString(), Qt::CaseInsensitive) == 0;
                    if (!have && !a.toString().isEmpty())
                        tags.append(a);
                }
                item.insert("Tags", tags);
                QNetworkReply *u = Jellyfin::request(m_net, QStringLiteral("POST"), QStringLiteral("/Items/") + m_entries[row].id,
                                                     QJsonDocument(item).toJson(QJsonDocument::Compact));
                connect(u, &QNetworkReply::finished, this, [this, u, row, bio] {
                    u->deleteLater();
                    m_table->item(row, ColStatus)->setText(u->error() == QNetworkReply::NoError
                                                               ? T("биография записана (%1)").arg(bio.value("via").toString())
                                                               : T("биография: ошибка записи"));
                    personPhoto(row);
                });
            });
        });
    });
}

void ServerScanDialog::personPhoto(int row)
{
    const Entry e = m_entries[row];
    const QString prev = m_table->item(row, ColStatus)->text();
    const QString prefix = prev == T("в работе…") ? QString() : prev + QStringLiteral("; ");
    if (!e.needPhoto) {
        finishEntry(row, prefix.isEmpty() ? T("готово") : prev);
        return;
    }
    auto upload = [this, row, e, prefix](const QImage &img, const QString &how) {
        QByteArray jpg;
        QBuffer buf(&jpg);
        buf.open(QIODevice::WriteOnly);
        ActressPhotos::fitPortrait(img).save(&buf, "JPG", 92);
        QNetworkReply *u = Jellyfin::request(m_net, QStringLiteral("POST"), QStringLiteral("/Items/%1/Images/Primary").arg(e.id),
                                             jpg.toBase64(), "image/jpeg");
        connect(u, &QNetworkReply::finished, this, [this, u, row, prefix, how] {
            u->deleteLater();
            finishEntry(row, prefix + (u->error() == QNetworkReply::NoError ? T("фото загружено (%1)").arg(how) : T("фото: ошибка записи")));
        });
    };
    // Выбранное раньше или из своей медиатеки
    QString ready = ActressPhotos::cacheFile(e.name);
    if (!QFileInfo::exists(ready))
        ready = ActressPhotos::findInLibrary(e.name, QDir::fromNativeSeparators(QSettings().value(QStringLiteral("ae/baseDir")).toString()));
    if (!ready.isEmpty() && QFileInfo::exists(ready)) {
        upload(QImage(ready), T("медиатека"));
        return;
    }
    // Поиск 1000×1500
    if (!m_img)
        m_img = new ImageSearch(this);
    m_img->search(ActressPhotos::query(e.name), [this, row, e, prefix, upload](const QList<ActressPhotos::Candidate> &list) {
        QList<ActressPhotos::Candidate> good;
        for (const ActressPhotos::Candidate &c : list)
            if (ActressPhotos::score(c) > 0)
                good << c;
        std::stable_sort(good.begin(), good.end(), [](const ActressPhotos::Candidate &a, const ActressPhotos::Candidate &b) {
            return ActressPhotos::score(a) > ActressPhotos::score(b);
        });
        auto tryOne = std::make_shared<std::function<void(int)>>();
        *tryOne = [this, row, e, prefix, upload, good, tryOne](int i) {
            if (i >= good.size() || i >= 10) {
                finishEntry(row, prefix + T("фото: не найдено"));
                return;
            }
            m_img->fetchImage(QUrl(good[i].url), [this, e, upload, tryOne, i](const QByteArray &data) {
                QImage img;
                img.loadFromData(data);
                if (img.width() >= 300 && img.height() >= 400) {
                    if (img.height() >= 1200)
                        ActressPhotos::fitPortrait(img).save(ActressPhotos::cacheFile(e.name), "JPG", 92);
                    upload(img, T("поиск"));
                    return;
                }
                (*tryOne)(i + 1);
            });
        };
        (*tryOne)(0);
    });
}

// ---------------- Фильмы ----------------

void ServerScanDialog::doMovie(int row)
{
    const Entry e = m_entries[row];
    auto open = [this, row](const QString &url) {
        if (url.isEmpty()) {
            finishEntry(row, T("не найден на Adult Empire"));
            return;
        }
        auto *f = new ReleaseFetcher(this);
        connect(f, &ReleaseFetcher::finished, this, [this, f, row](const QJsonObject &d) {
            f->deleteLater();
            movieFromPage(row, d);
        });
        connect(f, &ReleaseFetcher::failed, this, [this, f, row](const QString &) {
            f->deleteLater();
            if (!m_entries[row].sku.isEmpty()) {   // по номеру не открылось — ищем по названию
                m_entries[row].sku.clear();
                doMovie(row);
                return;
            }
            finishEntry(row, T("страница Adult Empire не открылась"));
        });
        f->start(QUrl(url));
    };
    if (!e.sku.isEmpty()) {   // номер из NFO (uniqueid) — прямая ссылка
        const QString pattern = qEnvironmentVariableIsSet("ARISTOCRAT_TEST_AE_PAGE") ? qEnvironmentVariable("ARISTOCRAT_TEST_AE_PAGE")
                                                                                     : QStringLiteral("https://www.adultempire.com/%1/");
        open(pattern.arg(e.sku));
        return;
    }
    const QString title = e.original.isEmpty() ? e.name : e.original;
    auto *pf = new PageFinder(this);
    pf->find(title, e.year, [pf, open](const QString &url) {
        pf->deleteLater();
        open(url);
    });
}

void ServerScanDialog::movieFromPage(int row, const QJsonObject &d)
{
    const Entry e = m_entries[row];
    // Проверка, что страница — тот самый фильм (если искали по названию)
    QString pageTitle = d.value("h1color").toString();
    if (pageTitle.isEmpty())
        pageTitle = d.value("docTitle").toString().section('|', 0, 0);
    const QString a = norm(pageTitle), b = norm(e.original.isEmpty() ? e.name : e.original);
    if (e.sku.isEmpty() && (a.isEmpty() || b.isEmpty() || (!a.contains(b) && !b.contains(a)))) {
        finishEntry(row, T("на Adult Empire нашёлся другой фильм — пропуск"));
        return;
    }
    if (!(e.needOverview || e.needGenres || e.needStudio || e.needPeople || e.needCountry)) {
        movieImages(row, d);
        return;
    }
    auto apply = [this, row, d, e](const QString &overviewRu) {
        const QString uid = QSettings().value(QStringLiteral("server/userId")).toString();
        QNetworkReply *r = Jellyfin::request(m_net, QStringLiteral("GET"), QStringLiteral("/Users/%1/Items/%2").arg(uid, e.id));
        connect(r, &QNetworkReply::finished, this, [this, r, row, d, e, overviewRu] {
            r->deleteLater();
            QJsonObject item = QJsonDocument::fromJson(r->readAll()).object();
            if (item.isEmpty()) {
                finishEntry(row, T("ошибка сервера"));
                return;
            }
            QStringList done;
            if (e.needOverview && !overviewRu.isEmpty()) {
                item.insert("Overview", overviewRu);
                done << T("описание");
            }
            if (e.needGenres) {
                QJsonArray g;
                for (const QJsonValue &c : d.value("categories").toArray()) {
                    const QString ru = Release::genreRu(c.toString());
                    if (!ru.isEmpty() && !g.contains(ru))
                        g.append(ru);
                }
                if (!g.isEmpty()) {
                    item.insert("Genres", g);
                    done << T("жанры");
                }
            }
            const QString studio = d.value("studio").toString().simplified();
            if (e.needStudio && !studio.isEmpty()) {
                item.insert("Studios", QJsonArray{QJsonObject{{"Name", studio}}});
                done << T("студия");
            }
            if (e.needPeople) {
                QJsonArray people = item.value("People").toArray();
                bool hasDirector = false;
                for (const QJsonValue &pv : people)
                    hasDirector |= pv.toObject().value("Type").toString() == QLatin1String("Director");
                int added = 0;
                for (const QJsonValue &cv : d.value("cast").toArray()) {
                    const QJsonObject c = cv.toObject();
                    const QString n = c.value("name").toString().simplified();
                    if (n.isEmpty())
                        continue;
                    if (c.value("director").toBool()) {
                        if (!hasDirector)
                            people.append(QJsonObject{{"Name", n}, {"Type", "Director"}});
                        hasDirector = true;
                        continue;
                    }
                    Gender::Source src;
                    const Gender::Value g = Gender::known(n, &src);
                    if (g == Gender::Male || (g == Gender::Unknown && Gender::byFirstName(n) == Gender::Male))
                        continue;   // мужчин в актрисы не записываем
                    people.append(QJsonObject{{"Name", n}, {"Type", "Actor"}});
                    ++added;
                }
                if (added > 0) {
                    item.insert("People", people);
                    done << T("актрисы");
                }
            }
            if (e.needCountry) {
                // Страна релиза — страна, где создана студия
                QString st = studio;
                if (st.isEmpty())
                    st = item.value("Studios").toArray().at(0).toObject().value("Name").toString();
                const QString country = Release::studioCountry(st);
                if (!country.isEmpty()) {
                    item.insert("ProductionLocations", QJsonArray{country});
                    done << T("страна");
                }
            }
            if (item.value("PremiereDate").toString().isEmpty()) {
                const QDate rel = Release::parseDate(d.value("released").toString());
                if (rel.isValid())
                    item.insert("PremiereDate", rel.toString(Qt::ISODate) + QStringLiteral("T00:00:00.0000000Z"));
            }
            QNetworkReply *u = Jellyfin::request(m_net, QStringLiteral("POST"), QStringLiteral("/Items/") + e.id,
                                                 QJsonDocument(item).toJson(QJsonDocument::Compact));
            connect(u, &QNetworkReply::finished, this, [this, u, row, d, done] {
                u->deleteLater();
                m_table->item(row, ColStatus)->setText(u->error() == QNetworkReply::NoError
                                                           ? (done.isEmpty() ? T("данных на странице нет") : T("записано: %1").arg(done.join(QStringLiteral(", "))))
                                                           : T("ошибка записи"));
                movieImages(row, d);
            });
        });
    };
    const QString plot = d.value("plot").toString().simplified();
    if (!e.needOverview || plot.isEmpty()) {
        apply(QString());
        return;
    }
    auto *tr = new Translator(this);
    connect(tr, &Translator::finished, this, [tr, apply](const QStringList &out) {
        tr->deleteLater();
        apply(out.value(0));
    });
    connect(tr, &Translator::failed, this, [tr, apply, plot](const QString &) {
        tr->deleteLater();
        apply(QString());
    });
    tr->translate({plot});
}

void ServerScanDialog::uploadImage(const QString &id, const QString &type, const QStringList &urls, std::function<void(bool)> done)
{
    if (urls.isEmpty()) {
        done(false);
        return;
    }
    const QString url = urls.first();
    const QStringList rest = urls.mid(1);
    WebFetch::get(QUrl(url), this, [this, id, type, rest, done](const QByteArray &data) {
        const QByteArray jpg = toJpeg(data);
        if (jpg.isEmpty()) {
            uploadImage(id, type, rest, done);
            return;
        }
        QNetworkReply *u = Jellyfin::request(m_net, QStringLiteral("POST"), QStringLiteral("/Items/%1/Images/%2").arg(id, type),
                                             jpg.toBase64(), "image/jpeg");
        connect(u, &QNetworkReply::finished, this, [u, done] {
            u->deleteLater();
            done(u->error() == QNetworkReply::NoError);
        });
    });
}

void ServerScanDialog::movieImages(int row, const QJsonObject &d)
{
    const Entry e = m_entries[row];
    QString sku = d.value("sku").toString().remove(QRegularExpression(QStringLiteral("\\D")));
    if (sku.isEmpty())
        sku = e.sku;
    const QString prev = m_table->item(row, ColStatus)->text();
    const QString prefix = prev == T("в работе…") ? QString() : prev + QStringLiteral("; ");
    struct Step { bool need; QString type; QStringList urls; QString label; };
    QStringList front = coverUrls(sku, false), back = coverUrls(sku, true), fan;
    if (!d.value("front").toString().isEmpty()) front << d.value("front").toString();
    if (!d.value("back").toString().isEmpty()) back << d.value("back").toString();
    if (!d.value("fanart").toString().isEmpty()) fan << d.value("fanart").toString();
    auto steps = std::make_shared<QList<Step>>(QList<Step>{{e.needPoster, QStringLiteral("Primary"), front, T("постер")},
                                                           {e.needBack, QStringLiteral("BoxRear"), back, T("задняя обложка")},
                                                           {e.needFanart, QStringLiteral("Backdrop"), fan, T("фан-арт")}});
    auto got = std::make_shared<QStringList>();
    auto run = std::make_shared<std::function<void()>>();
    *run = [this, row, steps, got, run, prefix, e] {
        while (!steps->isEmpty() && (!steps->first().need || steps->first().urls.isEmpty()))
            steps->removeFirst();
        if (steps->isEmpty()) {
            finishEntry(row, prefix + (got->isEmpty() ? T("обложки: не найдены") : T("обложки: %1").arg(got->join(QStringLiteral(", ")))));
            return;
        }
        const Step s = steps->takeFirst();
        uploadImage(e.id, s.type, s.urls, [got, run, s](bool ok) {
            if (ok)
                *got << s.label;
            (*run)();
        });
    };
    if (!(e.needPoster || e.needBack || e.needFanart)) {
        finishEntry(row, prev == T("в работе…") ? T("готово") : prev);
        return;
    }
    (*run)();
}
