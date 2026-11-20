#include "ReleaseManager.h"
#include "BrowserWindow.h"
#include "SceneTools.h"
#include "Widgets.h"
#include "core/I18n.h"
#include "core/Settings.h"
#include "core/Theme.h"
#include "core/Tools.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineView>


// ======================= Чтение страницы =======================

QString ReleaseFetcher::script()
{
    return QStringLiteral(R"JS((function(){
function txt(e){return (e&&(e.innerText||e.textContent)||'').replace(/\s+/g,' ').trim();}
var body=document.body?document.body.innerText:'';
function field(label){var re=new RegExp('(?:^|\\n)\\s*'+label+'\\s*:\\s*([^\\n]+)','i');var m=re.exec(body);return m?m[1].trim():'';}
var r={};
r.docTitle=document.title||'';
var og=document.querySelector('meta[property="og:title"]');r.ogTitle=og?og.content:'';
var ogi=document.querySelector('meta[property="og:image"]');r.ogImage=ogi?ogi.content:'';
var h1=document.querySelector('h1');
r.h1=h1?[].map.call(h1.childNodes,function(n){return n.nodeType==3?n.textContent:'';}).join(' ').replace(/\s+/g,' ').trim():'';
if(!r.h1)r.h1=txt(h1);
r.length=field('Length');r.released=field('Released');r.year=field('Production Year');
r.sku=field('Empire SKU')||field('SKU');r.studio=field('Studio');r.starring=field('Starring');
var m=/\(\d{4}\)\s*([0-5]\.\d{1,2})/.exec(body);r.rating=m?m[1]:'';
var syn=document.querySelector('.synopsis-content, #synopsis-content, .synopsis, #synopsis, [class*="synopsis"]');
r.plot=syn?(syn.innerText||'').trim():'';
if(!r.plot){var md=document.querySelector('meta[name="description"],meta[property="og:description"]');r.plot=md?md.content:'';}
function heading(name){
  var sets=['h1,h2,h3,h4,h5,h6','strong,b,div,span,p,dt'];
  for(var s=0;s<sets.length;s++){var hs=document.querySelectorAll(sets[s]);
    for(var i=0;i<hs.length;i++){var e=hs[i];if(e.closest('nav,header,footer,.navbar,.dropdown-menu'))continue;
      if(e.children.length<=2&&txt(e).toLowerCase()===name.toLowerCase())return e;}}
  return null;}
function sectionAnchors(name){
  var h=heading(name);if(!h)return [];var out=[];var e=h.nextElementSibling;var g=0;
  while(e&&g++<40){if(/^H[1-6]$/.test(e.tagName))break;if(e.tagName=='A')out.push(e);
    [].forEach.call(e.querySelectorAll('a'),function(a){out.push(a);});e=e.nextElementSibling;}
  if(!out.length){var p=h.parentElement;for(var k=0;k<3&&p&&!out.length;k++,p=p.parentElement)
    [].forEach.call(p.querySelectorAll('a'),function(a){out.push(a);});}
  return out;}
function after(a){var s='';var n=a.nextSibling;for(var k=0;k<4&&n;k++,n=n.nextSibling){if(n.nodeType==1&&n.tagName=='A')break;s+=' '+(n.textContent||'');}return s;}
r.cast=[];var seen={};
sectionAnchors('Cast').forEach(function(a){
  var href=a.href||'';var name=txt(a);
  if(!name||name.length>60||!/pornstar|performer|director|\/\d+\//i.test(href))return;
  if(seen[name.toLowerCase()])return;seen[name.toLowerCase()]=1;
  var id=(/\/(\d+)\//.exec(href)||[])[1]||'';
  var img=a.querySelector('img');var photo=img?(img.getAttribute('data-src')||img.currentSrc||img.src):'';
  if(!photo){var im=document.querySelector('img[alt="'+name.replace(/"/g,'')+'"]');if(im)photo=im.getAttribute('data-src')||im.currentSrc||im.src;}
  var dir=/director/i.test(after(a))||/director/i.test(href);
  r.cast.push({name:name,id:id,photo:photo,director:dir});});
r.categories=[];var gseen={};
sectionAnchors('Categories').forEach(function(a){var t=txt(a);if(t&&t.length<60&&!gseen[t]){gseen[t]=1;r.categories.push(t);}});
var urls=[];function add(u){if(u&&/^https?:/i.test(u)&&/\.(jpe?g|png|webp)(\?|$)/i.test(u)&&urls.indexOf(u)<0)urls.push(u);}
[].forEach.call(document.querySelectorAll('a[href]'),function(a){add(a.href);});
[].forEach.call(document.querySelectorAll('img'),function(i){add(i.getAttribute('data-src'));add(i.currentSrc||i.src);
  (i.getAttribute('srcset')||'').split(',').forEach(function(p){add(p.trim().split(' ')[0]);});});
var bgs=[];
[].forEach.call(document.querySelectorAll('*'),function(e){
  for(var k=0;k<e.attributes.length;k++){var v=e.attributes[k].value;if(v&&v.length<600&&/^https?:\/\/.*\.(jpe?g|png|webp)/i.test(v))add(v);}
  if(e.offsetWidth>500){var b=getComputedStyle(e).backgroundImage;var mm=/url\(["']?([^"')]+)["']?\)/.exec(b||'');
    if(mm)bgs.push({u:mm[1],w:e.offsetWidth*e.offsetHeight});}});
r.images=urls;
var sku=r.sku.replace(/\D/g,'');
function pick(ok){var best='';urls.forEach(function(u){if(ok(u)&&(!best||(sku&&u.indexOf(sku)>=0&&best.indexOf(sku)<0)))best=u;});return best;}
r.back=pick(function(u){return /\d+bh\.(jpe?g|webp)/i.test(u);});
r.front=pick(function(u){return /\d+h\.(jpe?g|webp)/i.test(u)&&!/\d+bh\./i.test(u);});
if(!r.front)r.front=r.ogImage;
bgs.sort(function(a,b){return b.w-a.w;});
var big=[].slice.call(document.images).filter(function(i){return i.naturalWidth>=1000&&i.naturalWidth>i.naturalHeight*1.3;})
  .sort(function(a,b){return b.naturalWidth-a.naturalWidth;});
r.fanart=bgs.length?bgs[0].u:(big.length?(big[0].currentSrc||big[0].src):'');
try{r.scenes=JSON.parse(%1);}catch(e){r.scenes=[];}
return JSON.stringify(r);})())JS").arg(QString::fromUtf8(sceneScript()));
}

ReleaseFetcher::ReleaseFetcher(QObject *parent)
    : QObject(parent)
{
    m_view = new QWebEngineView;
    m_view->setAttribute(Qt::WA_DontShowOnScreen);
    m_view->resize(1400, 1000);
    m_view->setPage(new QWebEnginePage(BrowserWindow::profile(), m_view));
    m_view->show();
    connect(m_view, &QWebEngineView::loadFinished, this, [this] {
        if (m_done)
            return;
        emit progress(T("Чтение страницы релиза…"));
        if (QSettings().value(QStringLiteral("sites/adultConfirmed"), false).toBool())
            m_view->page()->runJavaScript(QString::fromUtf8(ageScript()));
        QTimer::singleShot(3000, this, &ReleaseFetcher::extract);
    });
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_done)
            return;
        m_done = true;
        emit failed(T("Страница релиза не открылась. Проверьте ссылку и подключение к интернету."));
    });
}

ReleaseFetcher::~ReleaseFetcher()
{
    delete m_view;
}

void ReleaseFetcher::start(const QUrl &page)
{
    m_tries = 0;
    m_done = false;
    emit progress(T("Открываю страницу релиза…"));
    m_view->load(page);
    m_timeout.start(60000);
}

void ReleaseFetcher::extract()
{
    if (m_done)
        return;
    m_view->page()->runJavaScript(script(), [this](const QVariant &v) {
        if (m_done)
            return;
        const QJsonObject o = QJsonDocument::fromJson(v.toString().toUtf8()).object();
        const bool ok = !o.isEmpty() && (!o.value("sku").toString().isEmpty() || !o.value("cast").toArray().isEmpty()
                                         || !o.value("studio").toString().isEmpty());
        if (ok || ++m_tries >= 6) {
            m_done = true;
            m_timeout.stop();
            if (o.isEmpty())
                emit failed(T("Не удалось прочитать страницу релиза."));
            else
                emit finished(o);
            return;
        }
        // Ещё грузится или открыто окно 18+
        if (QSettings().value(QStringLiteral("sites/adultConfirmed"), false).toBool())
            m_view->page()->runJavaScript(QString::fromUtf8(ageScript()));
        QTimer::singleShot(2500, this, &ReleaseFetcher::extract);
    });
}

// ======================= Окно менеджера =======================

static QLineEdit *edit(QWidget *parent, const QString &placeholder = QString())
{
    auto *e = new QLineEdit(parent);
    e->setPlaceholderText(placeholder);
    return e;
}

static QStringList knownMen()
{
    return QSettings().value(QStringLiteral("ae/men")).toStringList();
}

ReleaseManagerDialog::ReleaseManagerDialog(const QString &pageUrl, const QString &video, QWidget *parent)
    : FramedDialog(parent, true)
{
    setWindowTitle(T("Adult Empire Менеджер"));
    resize(1180, 820);
    QSettings st;

    auto *root = contentLayout();
    root->setContentsMargins(20, 14, 20, 14);
    root->setSpacing(8);

    // Ссылка
    auto *r1 = new QHBoxLayout;
    m_url = edit(this, T("Ссылка на страницу релиза на Adult Empire"));
    m_url->setText(pageUrl);
    m_fetch = Ui::primaryButton(T("Загрузить данные"), this);
    r1->addWidget(new QLabel(T("Релиз:"), this));
    r1->addWidget(m_url, 1);
    r1->addWidget(m_fetch);
    root->addLayout(r1);
    m_state = new QLabel(T("Вставьте ссылку на релиз — программа заполнит все поля, их можно проверить и исправить."), this);
    m_state->setObjectName(QStringLiteral("StatusLabel"));
    m_state->setWordWrap(true);
    root->addWidget(m_state);

    // Прокручиваемая форма
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *page = new QWidget(scroll);
    auto *cols = new QHBoxLayout(page);
    cols->setContentsMargins(0, 0, 8, 0);
    cols->setSpacing(18);
    scroll->setWidget(page);
    root->addWidget(scroll, 1);

    // ---- Левая колонка: данные ----
    auto *left = new QVBoxLayout;
    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(7);
    m_title = edit(page);
    m_caps = new QCheckBox(T("Каждое слово с большой буквы"), page);
    m_caps->setObjectName(QStringLiteral("PlainCheck"));
    m_caps->setChecked(st.value(QStringLiteral("ae/titleCase"), true).toBool());
    auto *titleBox = new QVBoxLayout;
    titleBox->setSpacing(3);
    titleBox->addWidget(m_title);
    titleBox->addWidget(m_caps);
    form->addRow(T("Название:"), titleBox);
    m_orig = edit(page);
    form->addRow(T("Оригинальное название:"), m_orig);
    auto *edRow = new QHBoxLayout;
    m_useEdition = new QCheckBox(page);
    m_useEdition->setObjectName(QStringLiteral("PlainCheck"));
    m_useEdition->setChecked(st.value(QStringLiteral("ae/useEdition"), false).toBool());
    m_edition = edit(page);
    m_edition->setText(st.value(QStringLiteral("ae/edition"), QStringLiteral("Мультиязычное Издание")).toString());
    edRow->addWidget(m_useEdition);
    edRow->addWidget(m_edition, 1);
    form->addRow(T("Издание:"), edRow);
    m_plot = new QPlainTextEdit(page);
    m_plot->setMinimumHeight(120);
    form->addRow(T("Описание:"), m_plot);
    m_plotOrig = new QPlainTextEdit(page);
    m_plotOrig->setReadOnly(true);
    m_plotOrig->setMaximumHeight(70);
    m_plotOrig->setPlaceholderText(T("Описание на английском"));
    form->addRow(T("Оригинал:"), m_plotOrig);

    auto *dateRow = new QHBoxLayout;
    m_date = edit(page, QStringLiteral("2026-01-31"));
    m_year = edit(page);
    m_year->setMaximumWidth(80);
    dateRow->addWidget(m_date, 1);
    dateRow->addWidget(new QLabel(T("Год:"), page));
    dateRow->addWidget(m_year);
    form->addRow(T("Дата выхода:"), dateRow);
    m_country = new QComboBox(page);
    m_country->setEditable(true);
    m_country->addItems({QStringLiteral("США"), QStringLiteral("Германия"), QStringLiteral("Великобритания"), QStringLiteral("Франция"), QStringLiteral("Италия"), QStringLiteral("Испания"), QStringLiteral("Чехия"),
                         QStringLiteral("Венгрия"), QStringLiteral("Нидерланды"), QStringLiteral("Швеция"), QStringLiteral("Канада"), QStringLiteral("Россия"), QStringLiteral("Япония"), QStringLiteral("Бразилия")});
    m_country->setCurrentText(st.value(QStringLiteral("ae/country"), QStringLiteral("США")).toString());
    form->addRow(T("Страна:"), m_country);
    m_studio = edit(page);
    form->addRow(T("Студия:"), m_studio);
    m_director = edit(page, T("Если режиссёра нет — подставится студия"));
    form->addRow(T("Режиссёр:"), m_director);
    auto *numRow = new QHBoxLayout;
    m_rating = edit(page);
    m_rating->setText(QStringLiteral("4.0"));
    m_runtime = edit(page);
    m_uid = edit(page);
    numRow->addWidget(m_rating);
    numRow->addWidget(new QLabel(T("Минут:"), page));
    numRow->addWidget(m_runtime);
    numRow->addWidget(new QLabel(QStringLiteral("uniqueid:"), page));
    numRow->addWidget(m_uid);
    form->addRow(T("Рейтинг (0–5):"), numRow);

    m_useSet = new QCheckBox(T("Добавить в коллекцию (серию) — папка релиза будет внутри папки серии"), page);
    m_useSet->setObjectName(QStringLiteral("PlainCheck"));
    form->addRow(QString(), m_useSet);
    m_setName = edit(page, QStringLiteral("Серия «…»"));
    form->addRow(T("Коллекция:"), m_setName);
    m_setOverview = new QPlainTextEdit(page);
    m_setOverview->setMaximumHeight(70);
    m_setOverview->setPlaceholderText(T("Описание коллекции (необязательно)"));
    form->addRow(T("Описание серии:"), m_setOverview);
    left->addLayout(form);

    auto *gl = new QLabel(T("Жанры (отмеченные попадут в NFO; без перевода — нет в вашем списке):"), page);
    gl->setObjectName(QStringLiteral("Muted"));
    left->addWidget(gl);
    m_genres = new QListWidget(page);
    m_genres->setMinimumHeight(150);
    m_genres->setFlow(QListView::LeftToRight);
    m_genres->setWrapping(true);
    m_genres->setResizeMode(QListView::Adjust);
    m_genres->setSpacing(2);
    left->addWidget(m_genres);
    auto *ng = new QHBoxLayout;
    m_newGenre = edit(page, T("Свой жанр"));
    auto *addG = new QPushButton(T("Добавить"), page);
    ng->addWidget(m_newGenre, 1);
    ng->addWidget(addG);
    left->addLayout(ng);
    cols->addLayout(left, 3);

    // ---- Правая колонка: обложки, состав, сцены ----
    auto *right = new QVBoxLayout;
    auto *imgs = new QGridLayout;
    imgs->setHorizontalSpacing(10);
    auto mkImg = [&](int col, const QString &caption, QLineEdit *&urlEdit, QLabel *&prev) {
        auto *cap = new QLabel(caption, page);
        cap->setObjectName(QStringLiteral("Muted"));
        prev = new QLabel(page);
        prev->setFixedSize(150, 200);
        prev->setAlignment(Qt::AlignCenter);
        prev->setText(T("нет"));
        Ui::themedStyle(prev, QStringLiteral("QLabel{background:#171113;border:1px solid #33282C;border-radius:6px;color:#7D7266;}"));
        urlEdit = edit(page, T("ссылка"));
        urlEdit->setMaximumWidth(150);
        imgs->addWidget(cap, 0, col);
        imgs->addWidget(prev, 1, col);
        imgs->addWidget(urlEdit, 2, col);
        connect(urlEdit, &QLineEdit::editingFinished, this, [this, urlEdit, prev] { loadPreview(prev, urlEdit->text().trimmed()); });
    };
    mkImg(0, T("Постер (передняя обложка)"), m_frontUrl, m_frontPrev);
    mkImg(1, T("Задняя обложка"), m_backUrl, m_backPrev);
    mkImg(2, T("Фан-арт"), m_fanartUrl, m_fanartPrev);
    m_fanartUrl->setPlaceholderText(T("пусто — кадр из видео"));
    right->addLayout(imgs);

    auto *cl = new QLabel(T("Актрисы (снимите отметку с мужчин — программа запомнит их):"), page);
    cl->setObjectName(QStringLiteral("Muted"));
    cl->setWordWrap(true);
    right->addWidget(cl);
    m_cast = new QListWidget(page);
    m_cast->setMinimumHeight(170);
    right->addWidget(m_cast);

    auto *sl = new QHBoxLayout;
    m_useScenes = new QCheckBox(T("Вставить главы (сцены) в видео"), page);
    m_useScenes->setObjectName(QStringLiteral("PlainCheck"));
    m_useScenes->setChecked(st.value(QStringLiteral("ae/scenes"), true).toBool());
    sl->addWidget(m_useScenes);
    sl->addStretch();
    right->addLayout(sl);
    m_scenes = new QTableWidget(0, 2, page);
    m_scenes->setHorizontalHeaderLabels({T("Начало"), T("Название")});
    m_scenes->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_scenes->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_scenes->verticalHeader()->setVisible(false);
    m_scenes->setMinimumHeight(150);
    Ui::themedStyle(m_scenes, QStringLiteral(
        "QTableWidget{background:#171113;gridline-color:#33282C;border:1px solid #33282C;}"
        "QHeaderView::section{background:#261E21;color:#CFC3A8;border:none;border-bottom:1px solid #33282C;padding:5px;}"));
    right->addWidget(m_scenes, 1);
    cols->addLayout(right, 2);

    // ---- Низ: папка и видео ----
    auto *bottom = new QGridLayout;
    bottom->setHorizontalSpacing(8);
    bottom->setVerticalSpacing(6);
    m_base = edit(this);
    m_base->setText(QDir::toNativeSeparators(st.value(QStringLiteral("ae/baseDir"), Settings::outputDir()).toString()));
    auto *pickBase = new QPushButton(T("Изменить…"), this);
    bottom->addWidget(new QLabel(T("Основная папка:"), this), 0, 0);
    bottom->addWidget(m_base, 0, 1);
    bottom->addWidget(pickBase, 0, 2);
    m_video = edit(this, T("Видео релиза (необязательно)"));
    m_video->setText(QDir::toNativeSeparators(video));
    auto *pickVideo = new QPushButton(T("Выбрать…"), this);
    bottom->addWidget(new QLabel(T("Видео:"), this), 1, 0);
    bottom->addWidget(m_video, 1, 1);
    bottom->addWidget(pickVideo, 1, 2);
    auto *modeRow = new QHBoxLayout;
    m_lossless = new QRadioButton(T("Копия без потерь в MKV (быстро)"), this);
    m_convert = new QRadioButton(T("Конвертировать в формат из раздела «Конвертировать»"), this);
    (st.value(QStringLiteral("ae/lossless"), true).toBool() ? m_lossless : m_convert)->setChecked(true);
    modeRow->addWidget(m_lossless);
    modeRow->addWidget(m_convert);
    modeRow->addStretch();
    bottom->addLayout(modeRow, 2, 1);
    bottom->setColumnStretch(1, 1);
    root->addLayout(bottom);

    m_target = new QLabel(this);
    m_target->setObjectName(QStringLiteral("Muted"));
    m_target->setWordWrap(true);
    m_target->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_target);

    auto *btns = new QHBoxLayout;
    m_openDir = new QPushButton(T("Открыть папку релиза"), this);
    m_openDir->setVisible(false);
    auto *close = new QPushButton(T("Закрыть"), this);
    m_create = Ui::primaryButton(T("Создать релиз"), this);
    m_create->setMinimumWidth(200);
    btns->addWidget(m_openDir);
    btns->addStretch();
    btns->addWidget(close);
    btns->addWidget(m_create);
    root->addLayout(btns);

    // ---- Связи ----
    connect(m_fetch, &QPushButton::clicked, this, &ReleaseManagerDialog::fetch);
    connect(m_url, &QLineEdit::returnPressed, this, &ReleaseManagerDialog::fetch);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_create, &QPushButton::clicked, this, &ReleaseManagerDialog::create);
    connect(m_openDir, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_createdDir)); });
    connect(m_caps, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("ae/titleCase"), on);
        if (on) {
            m_title->setText(Release::titleCaseRu(m_title->text()));
            for (int r = 0; r < m_scenes->rowCount(); ++r)
                if (QTableWidgetItem *it = m_scenes->item(r, 1))
                    it->setText(Chapters::titleCase(it->text()));
        }
    });
    connect(addG, &QPushButton::clicked, this, [this] {
        for (const QString &g : m_newGenre->text().split(',', Qt::SkipEmptyParts)) {
            auto *it = new QListWidgetItem(g.trimmed(), m_genres);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
            it->setCheckState(Qt::Checked);
        }
        m_newGenre->clear();
    });
    connect(m_newGenre, &QLineEdit::returnPressed, addG, &QPushButton::click);
    connect(pickBase, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, T("Основная папка для релизов"), m_base->text());
        if (!d.isEmpty()) {
            m_base->setText(QDir::toNativeSeparators(d));
            updateTarget();
        }
    });
    connect(pickVideo, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, T("Видео релиза"), Settings::lastOpenDir(),
                                                       T("Видео (*.mkv *.mp4 *.m4v *.mov *.avi *.wmv *.ts *.webm);;Все файлы (*)"));
        if (!f.isEmpty()) {
            m_video->setText(QDir::toNativeSeparators(f));
            Settings::setLastOpenDir(QFileInfo(f).absolutePath());
            updateTarget();
        }
    });
    for (QLineEdit *e : {m_title, m_year, m_base, m_setName})
        connect(e, &QLineEdit::textChanged, this, &ReleaseManagerDialog::updateTarget);
    connect(m_useSet, &QCheckBox::toggled, this, &ReleaseManagerDialog::updateTarget);
    connect(m_date, &QLineEdit::textChanged, this, [this](const QString &t) {
        if (QRegularExpression(QStringLiteral("^\\d{4}")).match(t).hasMatch())
            m_year->setText(t.left(4));
    });
    // Отметки состава запоминаются: снятая — мужчина, поставленная — актриса
    connect(m_cast, &QListWidget::itemChanged, this, [](QListWidgetItem *it) {
        const QString name = it->data(Qt::UserRole).toString();
        if (name.isEmpty())
            return;
        QStringList men = knownMen();
        men.removeAll(name);
        if (it->checkState() == Qt::Unchecked && !it->data(Qt::UserRole + 1).toBool())
            men << name;
        QSettings().setValue(QStringLiteral("ae/men"), men);
    });

    updateTarget();
    if (!pageUrl.isEmpty())
        QMetaObject::invokeMethod(this, &ReleaseManagerDialog::fetch, Qt::QueuedConnection);
}

void ReleaseManagerDialog::setBusy(bool busy, const QString &text)
{
    m_fetch->setEnabled(!busy);
    m_create->setEnabled(!busy);
    if (!text.isEmpty())
        m_state->setText(text);
}

void ReleaseManagerDialog::fetch()
{
    const QUrl url = QUrl::fromUserInput(m_url->text().trimmed());
    if (!url.isValid() || url.host().isEmpty()) {
        m_state->setText(T("Вставьте ссылку на страницу релиза."));
        return;
    }
    setBusy(true, T("Открываю страницу релиза…"));
    auto *f = new ReleaseFetcher(this);
    connect(f, &ReleaseFetcher::progress, m_state, &QLabel::setText);
    connect(f, &ReleaseFetcher::finished, this, [this, f](const QJsonObject &d) {
        f->deleteLater();
        apply(d);
    });
    connect(f, &ReleaseFetcher::failed, this, [this, f](const QString &reason) {
        f->deleteLater();
        setBusy(false, reason);
    });
    f->start(url);
}

static QString cleanTitle(const QJsonObject &d)
{
    QString t = d.value("docTitle").toString();
    t.remove(QRegularExpression(QStringLiteral("\\s*[|\\-–]\\s*Adult\\s*Empire.*$"), QRegularExpression::CaseInsensitiveOption));
    t.remove(QRegularExpression(QStringLiteral("\\s*\\(\\d{4}\\)\\s*$")));
    if (t.trimmed().isEmpty())
        t = d.value("h1").toString();
    if (t.trimmed().isEmpty())
        t = d.value("ogTitle").toString();
    t.remove(QRegularExpression(QStringLiteral("\\s+-\\s+On Sale.*$"), QRegularExpression::CaseInsensitiveOption));
    t.remove(QRegularExpression(QStringLiteral("\\s*(Porn Videos?|Porn Movies?|DVD)\\s*$"), QRegularExpression::CaseInsensitiveOption));
    return t.simplified();
}

void ReleaseManagerDialog::apply(const QJsonObject &d)
{
    const QString orig = cleanTitle(d);
    m_orig->setText(orig);
    m_title->setText(orig);
    m_plotOrig->setPlainText(d.value("plot").toString());
    m_plot->setPlainText(d.value("plot").toString());

    const QDate rel = Release::parseDate(d.value("released").toString());
    if (rel.isValid())
        m_date->setText(rel.toString(Qt::ISODate));
    const QString py = d.value("year").toString().left(4);
    if (!py.isEmpty() && !rel.isValid())
        m_year->setText(py);
    const QString studio = d.value("studio").toString().simplified();
    m_studio->setText(studio);
    const QString rating = d.value("rating").toString();
    if (!rating.isEmpty())
        m_rating->setText(QString::number(rating.toDouble(), 'f', 1));
    const int mins = Release::parseLengthMinutes(d.value("length").toString());
    m_runtime->setText(mins > 0 ? QString::number(mins) : QString());
    m_uid->setText(d.value("sku").toString().remove(QRegularExpression(QStringLiteral("\\D"))));

    // Состав: режиссёр отдельно, мужчины (запомненные) без отметки
    const QStringList men = knownMen();
    QSignalBlocker block(m_cast);
    m_cast->clear();
    QString director;
    QJsonArray cast = d.value("cast").toArray();
    if (cast.isEmpty()) {
        for (const QString &n : d.value("starring").toString().split(',', Qt::SkipEmptyParts))
            cast.append(QJsonObject{{"name", n.trimmed()}});
    }
    for (const QJsonValue &v : cast) {
        const QJsonObject c = v.toObject();
        const QString name = c.value("name").toString().simplified();
        const bool dir = c.value("director").toBool();
        if (dir && director.isEmpty())
            director = name;
        auto *it = new QListWidgetItem(dir ? name + T("  — режиссёр") : name, m_cast);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setData(Qt::UserRole, name);
        it->setData(Qt::UserRole + 1, dir);
        it->setData(Qt::UserRole + 2, c.value("id").toString());
        it->setData(Qt::UserRole + 3, c.value("photo").toString());
        it->setCheckState(dir || men.contains(name) ? Qt::Unchecked : Qt::Checked);
    }
    m_director->setText(director.isEmpty() ? studio : director);

    // Жанры: из вашего списка (как в генераторе NFO), остальные — без отметки
    m_genres->clear();
    for (const QJsonValue &v : d.value("categories").toArray()) {
        const QString en = v.toString();
        const QString ru = Release::genreRu(en);
        auto *it = new QListWidgetItem(ru.isEmpty() ? en : ru, m_genres);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
        it->setCheckState(ru.isEmpty() ? Qt::Unchecked : Qt::Checked);
        it->setToolTip(en);
        if (ru.isEmpty())
            it->setForeground(Theme::color("#8A7F72"));
    }

    // Обложки
    m_frontUrl->setText(d.value("front").toString());
    m_backUrl->setText(d.value("back").toString());
    m_fanartUrl->setText(d.value("fanart").toString());
    loadPreview(m_frontPrev, m_frontUrl->text());
    loadPreview(m_backPrev, m_backUrl->text());
    loadPreview(m_fanartPrev, m_fanartUrl->text());

    // Сцены
    m_scenes->setRowCount(0);
    for (const QJsonValue &v : d.value("scenes").toArray()) {
        const QJsonObject o = v.toObject();
        const int r = m_scenes->rowCount();
        m_scenes->insertRow(r);
        m_scenes->setItem(r, 0, new QTableWidgetItem(o.value("start").toString()));
        auto *t = new QTableWidgetItem(o.value("title").toString());
        t->setData(Qt::UserRole, o.value("title").toString());
        m_scenes->setItem(r, 1, t);
    }
    m_scenes->sortItems(0);

    updateTarget();
    translate();
}

void ReleaseManagerDialog::translate()
{
    // Переводятся название, описание и названия сцен; имена актрис и режиссёра — нет
    QStringList src;
    src << m_orig->text() << m_plotOrig->toPlainText().simplified();
    for (int r = 0; r < m_scenes->rowCount(); ++r)
        src << m_scenes->item(r, 1)->data(Qt::UserRole).toString();
    setBusy(true, T("Перевожу название и описание на русский…"));
    auto *tr = new Translator(this);
    connect(tr, &Translator::finished, this, [this, tr](const QStringList &out) {
        tr->deleteLater();
        if (out.size() >= 2) {
            m_title->setText(m_caps->isChecked() ? Release::titleCaseRu(out[0]) : out[0]);
            if (!out[1].isEmpty())
                m_plot->setPlainText(out[1]);
        }
        for (int r = 0; r < m_scenes->rowCount() && r + 2 < out.size(); ++r)
            m_scenes->item(r, 1)->setText(m_caps->isChecked() ? Chapters::titleCase(out[r + 2]) : out[r + 2]);
        // Номер в конце названия — часть серии: предлагаем коллекцию
        const QString series = Release::seriesSuggestion(m_title->text());
        if (!series.isEmpty() && m_setName->text().isEmpty()) {
            m_setName->setText(QStringLiteral("Серия «%1»").arg(series));
            m_useSet->setChecked(true);
        }
        setBusy(false, T("Готово. Проверьте поля — всё можно исправить перед созданием релиза."));
        updateTarget();
        if (qEnvironmentVariableIsSet("ARISTOCRAT_TEST_AUTOCREATE"))   // автопроверка сборки
            QTimer::singleShot(1500, this, &ReleaseManagerDialog::create);
    });
    connect(tr, &Translator::failed, this, [this, tr](const QString &reason) {
        tr->deleteLater();
        setBusy(false, reason);
    });
    tr->translate(src);
}

void ReleaseManagerDialog::loadPreview(QLabel *target, const QString &url)
{
    if (url.isEmpty()) {
        target->setPixmap(QPixmap());
        target->setText(T("нет"));
        return;
    }
    target->setText(T("загрузка…"));
    WebFetch::get(QUrl(url), target, [target](const QByteArray &data) {
        QImage img;
        img.loadFromData(data);
        if (img.isNull()) {
            target->setPixmap(QPixmap());
            target->setText(data.isEmpty() ? T("не загрузилось") : T("есть (без просмотра)"));
            return;
        }
        target->setPixmap(QPixmap::fromImage(img.scaled(target->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    });
}

QString ReleaseManagerDialog::releaseDir() const
{
    ReleaseInfo r;
    r.title = m_title->text();
    r.year = m_year->text();
    QString dir = QDir::fromNativeSeparators(m_base->text().trimmed());
    if (m_useSet->isChecked() && !m_setName->text().trimmed().isEmpty())
        dir = QDir(dir).filePath(Release::safeName(m_setName->text()));
    return QDir(dir).filePath(Release::baseName(r));
}

void ReleaseManagerDialog::updateTarget()
{
    if (m_title->text().trimmed().isEmpty()) {
        m_target->setText(QString());
        return;
    }
    m_target->setText(T("Будет создано: %1").arg(QDir::toNativeSeparators(releaseDir())));
}

ReleaseInfo ReleaseManagerDialog::collect() const
{
    ReleaseInfo r;
    r.pageUrl = m_url->text().trimmed();
    r.sku = m_uid->text().trimmed();
    r.title = m_title->text().simplified();
    r.originalTitle = m_orig->text().simplified();
    r.plot = m_plot->toPlainText().simplified();
    r.edition = m_useEdition->isChecked() ? m_edition->text().trimmed() : QString();
    r.premiered = QDate::fromString(m_date->text().trimmed(), Qt::ISODate);
    r.year = m_year->text().trimmed();
    r.country = m_country->currentText().trimmed();
    r.studio = m_studio->text().trimmed();
    r.director = m_director->text().trimmed().isEmpty() ? r.studio : m_director->text().trimmed();
    r.rating = qBound(0.0, m_rating->text().replace(',', '.').toDouble(), 5.0);
    r.runtimeMin = m_runtime->text().toInt();
    for (int i = 0; i < m_genres->count(); ++i)
        if (m_genres->item(i)->checkState() == Qt::Checked)
            r.genres << m_genres->item(i)->text().trimmed();
    for (int i = 0; i < m_cast->count(); ++i) {
        QListWidgetItem *it = m_cast->item(i);
        ReleasePerson p;
        p.name = it->data(Qt::UserRole).toString();
        p.director = it->data(Qt::UserRole + 1).toBool();
        p.id = it->data(Qt::UserRole + 2).toString();
        p.photo = it->data(Qt::UserRole + 3).toString();
        r.cast << p;
        if (it->checkState() == Qt::Checked)
            r.actresses << p.name;
    }
    if (m_useSet->isChecked()) {
        r.setName = m_setName->text().trimmed();
        r.setOverview = m_setOverview->toPlainText().simplified();
    }
    r.frontUrl = m_frontUrl->text().trimmed();
    r.backUrl = m_backUrl->text().trimmed();
    r.fanartUrl = m_fanartUrl->text().trimmed();
    for (int i = 0; i < m_scenes->rowCount(); ++i) {
        Chapter c;
        c.start = Chapters::parseTime(m_scenes->item(i, 0)->text());
        c.title = m_scenes->item(i, 1)->text().trimmed();
        r.scenes << c;
    }
    return r;
}

static QImage placeholderPhoto()
{
    QImage img(200, 300, QImage::Format_RGB32);
    img.fill(QColor(0xc9, 0xce, 0xd6));
    return img;
}

void ReleaseManagerDialog::create()
{
    const ReleaseInfo r = collect();
    if (r.title.isEmpty() || m_base->text().trimmed().isEmpty()) {
        m_state->setText(T("Заполните название и основную папку."));
        return;
    }
    const QString dir = releaseDir();
    const QString base = Release::baseName(r);
    const QString video = QDir::fromNativeSeparators(m_video->text().trimmed());

    QStringList files;
    files << base + QStringLiteral(".nfo");
    if (!r.frontUrl.isEmpty()) files << base + QStringLiteral("-poster.jpg");
    if (!r.backUrl.isEmpty()) files << base + QStringLiteral("-back.jpg");
    if (!r.fanartUrl.isEmpty() || !video.isEmpty()) files << base + QStringLiteral("-fanart.jpg");
    if (!video.isEmpty()) files << base + QLatin1Char('.') + (m_lossless->isChecked() ? QStringLiteral("mkv") : T("формат из раздела «Конвертировать»"));
    if (!r.actresses.isEmpty()) files << QStringLiteral(".actors/ (%1)").arg(r.actresses.size());
    QString html = T("<b>Папка:</b><br>%1<br><br><b>Файлы:</b><br>%2<br><br>Актрисы: %3<br>Режиссёр: %4")
                       .arg(QDir::toNativeSeparators(dir).toHtmlEscaped(), files.join(QStringLiteral("<br>")).toHtmlEscaped(),
                            r.actresses.join(QStringLiteral(", ")).toHtmlEscaped(), r.director.toHtmlEscaped());
    if (QDir(dir).exists())
        html += T("<br><br><span style='color:#E07A6A'>Папка уже существует — файлы с такими же именами будут заменены.</span>");
    if (!qEnvironmentVariableIsSet("ARISTOCRAT_TEST_AUTOCREATE") && !Ui::ask(this, T("Создать релиз?"), html, T("Создать"), T("Изменить")))
        return;

    QSettings st;
    st.setValue(QStringLiteral("ae/baseDir"), QDir::fromNativeSeparators(m_base->text().trimmed()));
    st.setValue(QStringLiteral("ae/country"), r.country);
    st.setValue(QStringLiteral("ae/useEdition"), m_useEdition->isChecked());
    st.setValue(QStringLiteral("ae/edition"), m_edition->text().trimmed());
    st.setValue(QStringLiteral("ae/lossless"), m_lossless->isChecked());
    st.setValue(QStringLiteral("ae/scenes"), m_useScenes->isChecked());

    if (!QDir().mkpath(dir)) {
        m_state->setText(T("Не удалось создать папку: %1").arg(QDir::toNativeSeparators(dir)));
        return;
    }
    m_createdDir = dir;
    m_errors.clear();

    // NFO
    QFile nfo(QDir(dir).filePath(base + QStringLiteral(".nfo")));
    if (nfo.open(QIODevice::WriteOnly | QIODevice::Truncate))
        nfo.write(Release::nfo(r).toUtf8());
    else
        m_errors << base + QStringLiteral(".nfo");

    // Загрузки: обложки и фото актрис
    m_jobs.clear();
    if (!r.frontUrl.isEmpty()) m_jobs << Job{r.frontUrl, QDir(dir).filePath(base + QStringLiteral("-poster.jpg")), {}, false};
    if (!r.backUrl.isEmpty()) m_jobs << Job{r.backUrl, QDir(dir).filePath(base + QStringLiteral("-back.jpg")), {}, false};
    if (!r.fanartUrl.isEmpty()) m_jobs << Job{r.fanartUrl, QDir(dir).filePath(base + QStringLiteral("-fanart.jpg")), {}, false};
    const QDir actors(QDir(dir).filePath(QStringLiteral(".actors")));
    if (!r.actresses.isEmpty())
        QDir().mkpath(actors.path());
    for (const ReleasePerson &p : r.cast) {
        if (!r.actresses.contains(p.name))
            continue;
        const QString file = actors.filePath(Release::actorFile(p.name) + QStringLiteral(".jpg"));
        if (QFileInfo::exists(file))
            continue;
        const QString guess = p.id.isEmpty() ? QString() : QStringLiteral("https://imgs1cdn.adultempire.com/actors/%1h.jpg").arg(p.id);
        m_jobs << Job{p.photo.isEmpty() ? guess : p.photo, file, p.photo.isEmpty() ? QString() : guess, true};
    }
    // Актрисы, добавленные вручную (нет в составе со страницы)
    for (const QString &a : r.actresses) {
        bool found = false;
        for (const ReleasePerson &p : r.cast) found |= p.name == a;
        const QString file = actors.filePath(Release::actorFile(a) + QStringLiteral(".jpg"));
        if (!found && !QFileInfo::exists(file))
            placeholderPhoto().save(file, "JPG", 85);
    }

    // Фан-арта нет — кадр из середины видео
    if (r.fanartUrl.isEmpty() && !video.isEmpty() && !Tools::ffmpeg().isEmpty()) {
        QProcess p;
        const QString fan = QDir(dir).filePath(base + QStringLiteral("-fanart.jpg"));
        for (const char *at : {"600", "5"}) {
            QProcess p;
            p.start(Tools::ffmpeg(), {"-hide_banner", "-loglevel", "error", "-y", "-ss", at, "-i", video, "-frames:v", "1",
                                      "-q:v", "2", fan});
            p.waitForFinished(30000);
            if (QFileInfo(fan).size() > 0)
                break;
        }
    }

    // Главы
    QString meta;
    if (!video.isEmpty() && m_useScenes->isChecked() && !r.scenes.isEmpty()) {
        meta = QDir(QDir::tempPath()).filePath(QStringLiteral("aristocrat-chapters-%1.txt").arg(QDateTime::currentMSecsSinceEpoch()));
        QFile mf(meta);
        if (mf.open(QIODevice::WriteOnly))
            mf.write(Chapters::metadataFile(r.scenes, 0).toUtf8());
    }
    if (!video.isEmpty())
        emit videoRequested(video, base, dir, m_lossless->isChecked(), meta);

    setBusy(true, T("Скачиваю обложки и фото актрис…"));
    runJobs();
}

void ReleaseManagerDialog::runJobs()
{
    if (m_jobs.isEmpty()) {
        finishCreate();
        return;
    }
    const Job job = m_jobs.takeFirst();
    auto saveOrFallback = [job](const QByteArray &data) {
        QImage img;
        if (!data.isEmpty())
            img.loadFromData(data);
        if (img.isNull() && !data.isEmpty() && !Tools::ffmpeg().isEmpty()) {
            // WebP и другие форматы — через FFmpeg
            const QString tmp = job.file + QStringLiteral(".download");
            QFile f(tmp);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(data);
                f.close();
                QProcess p;
                p.start(Tools::ffmpeg(), {"-hide_banner", "-loglevel", "error", "-y", "-i", tmp, "-q:v", "2", job.file});
                p.waitForFinished(30000);
                QFile::remove(tmp);
                if (QFileInfo(job.file).size() > 0)
                    return true;
            }
        }
        if (!img.isNull()) {
            if (img.hasAlphaChannel()) {
                QImage flat(img.size(), QImage::Format_RGB32);
                flat.fill(Qt::white);
                QPainter(&flat).drawImage(0, 0, img);
                img = flat;
            }
            return img.save(job.file, "JPG", 92);
        }
        return false;
    };

    if (job.url.isEmpty()) {
        if (job.placeholder)
            placeholderPhoto().save(job.file, "JPG", 85);
        runJobs();
        return;
    }
    m_state->setText(T("Скачиваю: %1").arg(QFileInfo(job.file).fileName()));
    WebFetch::get(QUrl(job.url), this, [this, job, saveOrFallback](const QByteArray &data) {
        if (!saveOrFallback(data)) {
            if (!job.fallbackUrl.isEmpty()) {
                m_jobs.prepend(Job{job.fallbackUrl, job.file, {}, job.placeholder});
            } else if (job.placeholder) {
                placeholderPhoto().save(job.file, "JPG", 85);   // как «пустое фото» в генераторе NFO
            } else {
                m_errors << QFileInfo(job.file).fileName();
            }
        }
        runJobs();
    });
}

void ReleaseManagerDialog::finishCreate()
{
    setBusy(false);
    m_openDir->setVisible(true);
    QString msg = T("Релиз создан: %1.").arg(QDir::toNativeSeparators(m_createdDir));
    if (!m_video->text().trimmed().isEmpty())
        msg += T(" Видео конвертируется в разделе «Конвертировать» и появится в этой папке.");
    if (!m_errors.isEmpty())
        msg += T(" Не скачались: %1 — ссылку можно исправить и нажать «Создать релиз» ещё раз.").arg(m_errors.join(QStringLiteral(", ")));
    m_state->setText(msg);
}
