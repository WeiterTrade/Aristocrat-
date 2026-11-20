#include "EditDialog.h"
#include "FramePreview.h"
#include "Widgets.h"
#include "core/Settings.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

static QSlider *makeSlider(int min, int max, int value, QWidget *parent)
{
    auto *s = new QSlider(Qt::Horizontal, parent);
    s->setRange(min, max);
    s->setValue(value);
    return s;
}

static QFrame *sidePanel(QWidget *parent)
{
    auto *f = new QFrame(parent);
    f->setObjectName(QStringLiteral("SidePanel"));
    return f;
}

EditDialog::EditDialog(const TaskPtr &task, int initialTab, QWidget *parent)
    : QDialog(parent), m_task(task), m_opt(task->edit)
{
    setWindowTitle(QFileInfo(task->input).fileName());
    resize(1000, 720);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(450);
    connect(&m_debounce, &QTimer::timeout, this, [this] { refreshPreviews(false); });

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 14);

    m_tabs = new QTabWidget(this);
    m_tabs->tabBar()->setExpanding(true);
    m_tabs->tabBar()->setDocumentMode(true);
    root->addWidget(m_tabs, 1);

    // Предпросмотр слева, настройки справа — для всех вкладок один и тот же.
    auto *body = new QWidget(this);
    auto *bodyLay = new QHBoxLayout(body);
    bodyLay->setContentsMargins(18, 8, 18, 0);
    bodyLay->setSpacing(18);

    auto *left = new QVBoxLayout;
    auto *l1 = new QLabel(QStringLiteral("Исходник"), body);
    l1->setObjectName(QStringLiteral("Muted"));
    m_orig = new PreviewLabel(body);
    m_orig->setSourceSize(sourceSize());
    m_orig->setCropRect(m_opt.crop);
    auto *outHead = new QHBoxLayout;
    auto *l2 = new QLabel(QStringLiteral("Результат"), body);
    l2->setObjectName(QStringLiteral("Muted"));
    m_timeLabel = new QLabel(body);
    m_timeLabel->setObjectName(QStringLiteral("Muted"));
    outHead->addWidget(l2);
    outHead->addStretch();
    outHead->addWidget(m_timeLabel);
    m_out = new PreviewLabel(body);
    m_time = makeSlider(0, qMax(1, int(task->info.duration)), qMin(int(task->info.duration / 10), 30), body);
    left->addWidget(l1);
    left->addWidget(m_orig, 1);
    left->addLayout(outHead);
    left->addWidget(m_out, 1);
    left->addWidget(m_time);
    bodyLay->addLayout(left, 3);

    auto *pages = new QTabWidget(body);
    pages->tabBar()->hide();
    pages->setStyleSheet(QStringLiteral("QTabWidget::pane { border: none; }"));
    pages->setFixedWidth(360);
    pages->addTab(buildCropTab(), QString());
    pages->addTab(buildEffectTab(), QString());
    pages->addTab(buildWatermarkTab(), QString());
    pages->addTab(buildSubtitleTab(), QString());
    pages->addTab(buildAudioTab(), QString());
    bodyLay->addWidget(pages);

    // Верхние вкладки только переключают правую панель
    for (const QString &t : {QStringLiteral("Кадрирование"), QStringLiteral("Эффекты"), QStringLiteral("Водяной знак"),
                             QStringLiteral("Субтитры"), QStringLiteral("Аудио")})
        m_tabs->addTab(new QWidget, t);
    m_tabs->setMaximumHeight(m_tabs->tabBar()->sizeHint().height() + 2);
    root->insertWidget(1, body, 10);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this, pages](int i) {
        pages->setCurrentIndex(i);
        m_orig->setCropEditable(i == 0);
    });
    m_tabs->setCurrentIndex(qBound(0, initialTab, 4));
    pages->setCurrentIndex(m_tabs->currentIndex());
    m_orig->setCropEditable(m_tabs->currentIndex() == 0);

    auto *btns = new QHBoxLayout;
    btns->setContentsMargins(18, 0, 18, 0);
    auto *applyAll = new QPushButton(QStringLiteral("Применить ко всем"), this);
    auto *reset = new QPushButton(QStringLiteral("Сбросить всё"), this);
    auto *ok = Ui::primaryButton(QStringLiteral("OK"), this);
    auto *cancel = Ui::primaryButton(QStringLiteral("Отмена"), this);
    ok->setMinimumWidth(140);
    cancel->setMinimumWidth(140);
    btns->addWidget(applyAll);
    btns->addWidget(reset);
    btns->addStretch();
    btns->addWidget(ok);
    btns->addWidget(cancel);
    root->addLayout(btns);

    connect(applyAll, &QPushButton::clicked, this, [this] { m_applyAll = true; accept(); });
    connect(reset, &QPushButton::clicked, this, [this] {
        const double ts = m_opt.trimStart, te = m_opt.trimEnd;
        m_opt = EditOptions();
        m_opt.trimStart = ts;
        m_opt.trimEnd = te;
        accept();
    });
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    connect(m_time, &QSlider::valueChanged, this, [this](int v) {
        m_timeLabel->setText(formatDuration(v) + QStringLiteral(" / ") + formatDuration(m_task->info.duration));
    });
    connect(m_time, &QSlider::sliderReleased, this, [this] { refreshPreviews(true); });
    connect(m_orig, &PreviewLabel::cropChanged, this, [this](const QRect &r) {
        m_opt.crop = r;
        syncCropFields();
        changed();
    });

    m_timeLabel->setText(formatDuration(m_time->value()) + QStringLiteral(" / ") + formatDuration(task->info.duration));
    refreshPreviews(true);
}

QSize EditDialog::sourceSize() const
{
    return QSize(m_task->info.width, m_task->info.height);
}

void EditDialog::changed()
{
    m_debounce.start();
}

void EditDialog::refreshPreviews(bool original)
{
    const int gen = ++m_generation;
    const double at = m_time->value();
    ConversionTask copy = *m_task;
    copy.edit = m_opt;
    if (original) {
        m_orig->setBusy(true);
        auto img = QSharedPointer<QImage>::create();
        Ui::runAsync(this, [copy, at, img] { *img = FramePreview::grab(copy, at, false, 800); },
                     [this, img] { m_orig->setImage(*img); });
    }
    m_out->setBusy(true);
    auto img = QSharedPointer<QImage>::create();
    Ui::runAsync(this, [copy, at, img] { *img = FramePreview::grab(copy, at, true, 800); },
                 [this, img, gen] { if (gen == m_generation) m_out->setImage(*img); });
}

// ---------------- Кадрирование ----------------

QWidget *EditDialog::buildCropTab()
{
    auto *w = sidePanel(this);
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(20, 18, 20, 18);
    lay->setSpacing(12);

    lay->addWidget(new QLabel(QStringLiteral("Поворот:")));
    auto *rot = new QHBoxLayout;
    struct B { const char *icon; const char *tip; int kind; };
    for (const B &b : {B{"rotl", "Повернуть влево на 90°", 0}, B{"rotr", "Повернуть вправо на 90°", 1},
                       B{"fliph", "Отразить по горизонтали", 2}, B{"flipv", "Отразить по вертикали", 3}}) {
        auto *btn = Ui::iconButton(b.icon, QString::fromUtf8(b.tip), w, 26);
        btn->setStyleSheet(QStringLiteral("QToolButton{background:#3A2F33;border:1px solid #4A3E41;border-radius:4px;padding:8px;}"
                                          "QToolButton:hover{border-color:#E2BC5C;}"));
        rot->addWidget(btn);
        const int kind = b.kind;
        connect(btn, &QToolButton::clicked, this, [this, kind] {
            if (kind == 0) m_opt.rotate = (m_opt.rotate + 270) % 360;
            else if (kind == 1) m_opt.rotate = (m_opt.rotate + 90) % 360;
            else if (kind == 2) m_opt.flipH = !m_opt.flipH;
            else m_opt.flipV = !m_opt.flipV;
            m_rotLabel->setText(QStringLiteral("Поворот: %1°%2%3").arg(m_opt.rotate)
                                    .arg(m_opt.flipH ? QStringLiteral(", отражено ↔") : QString())
                                    .arg(m_opt.flipV ? QStringLiteral(", отражено ↕") : QString()));
            changed();
        });
    }
    rot->addStretch();
    lay->addLayout(rot);
    m_rotLabel = new QLabel(QStringLiteral("Поворот: %1°").arg(m_opt.rotate), w);
    m_rotLabel->setObjectName(QStringLiteral("Muted"));
    lay->addWidget(m_rotLabel);

    lay->addSpacing(8);
    lay->addWidget(new QLabel(QStringLiteral("Исходный размер:  %1 * %2").arg(m_task->info.width).arg(m_task->info.height)));

    lay->addSpacing(8);
    lay->addWidget(new QLabel(QStringLiteral("Область кадра (мышью на верхнем кадре или числами):")));
    auto *g = new QGridLayout;
    auto mk = [w](int max) { auto *s = new QSpinBox(w); s->setRange(0, max); s->setSingleStep(2); return s; };
    m_cropW = mk(m_task->info.width);
    m_cropH = mk(m_task->info.height);
    m_cropX = mk(m_task->info.width);
    m_cropY = mk(m_task->info.height);
    g->addWidget(new QLabel(QStringLiteral("Ширина")), 0, 0);
    g->addWidget(m_cropW, 0, 1);
    g->addWidget(new QLabel(QStringLiteral("Высота")), 0, 2);
    g->addWidget(m_cropH, 0, 3);
    g->addWidget(new QLabel(QStringLiteral("X")), 1, 0);
    g->addWidget(m_cropX, 1, 1);
    g->addWidget(new QLabel(QStringLiteral("Y")), 1, 2);
    g->addWidget(m_cropY, 1, 3);
    lay->addLayout(g);
    auto *center = new QPushButton(QStringLiteral("По центру"), w);
    lay->addWidget(center);

    lay->addSpacing(8);
    lay->addWidget(new QLabel(QStringLiteral("Соотношение сторон:")));
    m_aspect = new QComboBox(w);
    m_aspect->addItem(QStringLiteral("Как в оригинале"), 0.0);
    m_aspect->addItem(QStringLiteral("16:9"), 16.0 / 9.0);
    m_aspect->addItem(QStringLiteral("4:3"), 4.0 / 3.0);
    m_aspect->addItem(QStringLiteral("1:1"), 1.0);
    m_aspect->addItem(QStringLiteral("9:16"), 9.0 / 16.0);
    m_aspect->addItem(QStringLiteral("21:9"), 21.0 / 9.0);
    lay->addWidget(m_aspect);
    lay->addStretch();
    auto *reset = new QPushButton(QStringLiteral("Сбросить"), w);
    lay->addWidget(reset);

    syncCropFields();
    auto fromFields = [this] {
        QRect r(m_cropX->value(), m_cropY->value(), m_cropW->value(), m_cropH->value());
        const QSize s = sourceSize();
        if (r.width() < 16 || r.height() < 16 || (r.size() == s && r.topLeft().isNull()))
            m_opt.crop = QRect();
        else
            m_opt.crop = r.intersected(QRect(QPoint(0, 0), s));
        m_orig->setCropRect(m_opt.crop);
        changed();
    };
    for (QSpinBox *sb : {m_cropW, m_cropH, m_cropX, m_cropY})
        connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), this, fromFields);
    connect(center, &QPushButton::clicked, this, [this] {
        const QSize s = sourceSize();
        m_cropX->setValue(((s.width() - m_cropW->value()) / 2) & ~1);
        m_cropY->setValue(((s.height() - m_cropH->value()) / 2) & ~1);
    });
    connect(m_aspect, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { applyAspect(); });
    connect(reset, &QPushButton::clicked, this, [this] {
        m_opt.crop = QRect();
        m_opt.rotate = 0;
        m_opt.flipH = m_opt.flipV = false;
        m_rotLabel->setText(QStringLiteral("Поворот: 0°"));
        m_aspect->setCurrentIndex(0);
        syncCropFields();
        m_orig->setCropRect(m_opt.crop);
        changed();
    });
    return w;
}

void EditDialog::syncCropFields()
{
    if (!m_cropW)
        return;
    const QRect r = m_opt.crop.isNull() ? QRect(QPoint(0, 0), sourceSize()) : m_opt.crop;
    for (QSpinBox *sb : {m_cropW, m_cropH, m_cropX, m_cropY})
        sb->blockSignals(true);
    m_cropW->setValue(r.width());
    m_cropH->setValue(r.height());
    m_cropX->setValue(r.x());
    m_cropY->setValue(r.y());
    for (QSpinBox *sb : {m_cropW, m_cropH, m_cropX, m_cropY})
        sb->blockSignals(false);
}

void EditDialog::applyAspect()
{
    const double ar = m_aspect->currentData().toDouble();
    const QSize s = sourceSize();
    if (ar <= 0 || s.isEmpty()) {
        m_opt.crop = QRect();
    } else {
        // Учитываем неквадратные пиксели
        const double sar = m_task->info.sar > 0 ? m_task->info.sar : 1.0;
        int w = s.width(), h = int(w * sar / ar);
        if (h > s.height()) { h = s.height(); w = int(h * ar / sar); }
        w &= ~1; h &= ~1;
        m_opt.crop = QRect(((s.width() - w) / 2) & ~1, ((s.height() - h) / 2) & ~1, w, h);
    }
    syncCropFields();
    m_orig->setCropRect(m_opt.crop);
    changed();
}

// ---------------- Эффекты ----------------

QWidget *EditDialog::buildEffectTab()
{
    auto *w = sidePanel(this);
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(20, 18, 20, 18);
    lay->setSpacing(10);

    auto row = [&](const QString &name, int value, QSlider *&slider) {
        auto *h = new QHBoxLayout;
        auto *lbl = new QLabel(name, w);
        lbl->setMinimumWidth(90);
        slider = makeSlider(-100, 100, value, w);
        auto *val = new QLabel(QString::number(value), w);
        val->setMinimumWidth(32);
        h->addWidget(lbl);
        h->addWidget(slider, 1);
        h->addWidget(val);
        lay->addLayout(h);
        connect(slider, &QSlider::valueChanged, this, [this, val](int v) { val->setText(QString::number(v)); });
    };
    row(QStringLiteral("Яркость"), m_opt.brightness, m_bright);
    row(QStringLiteral("Контраст"), m_opt.contrast, m_contrast);
    row(QStringLiteral("Насыщенность"), m_opt.saturation, m_sat);

    lay->addSpacing(10);
    m_deint = new QCheckBox(QStringLiteral("Убрать чересстрочность (гребёнку)"), w);
    m_deint->setObjectName(QStringLiteral("PlainCheck"));
    m_deint->setChecked(m_opt.deinterlace);
    m_denoise = new QCheckBox(QStringLiteral("Шумоподавление"), w);
    m_denoise->setObjectName(QStringLiteral("PlainCheck"));
    m_denoise->setChecked(m_opt.denoise);
    lay->addWidget(m_deint);
    if (m_task->info.interlaced) {
        auto *hint = new QLabel(QStringLiteral("Видео чересстрочное — рекомендуется включить."), w);
        hint->setObjectName(QStringLiteral("StatusLabel"));
        hint->setWordWrap(true);
        lay->addWidget(hint);
    }
    lay->addWidget(m_denoise);
    lay->addStretch();
    auto *reset = new QPushButton(QStringLiteral("Сбросить"), w);
    lay->addWidget(reset);

    auto apply = [this] {
        m_opt.brightness = m_bright->value();
        m_opt.contrast = m_contrast->value();
        m_opt.saturation = m_sat->value();
        m_opt.deinterlace = m_deint->isChecked();
        m_opt.denoise = m_denoise->isChecked();
        changed();
    };
    for (QSlider *s : {m_bright, m_contrast, m_sat})
        connect(s, &QSlider::valueChanged, this, apply);
    connect(m_deint, &QCheckBox::toggled, this, apply);
    connect(m_denoise, &QCheckBox::toggled, this, apply);
    connect(reset, &QPushButton::clicked, this, [this] {
        m_bright->setValue(0);
        m_contrast->setValue(0);
        m_sat->setValue(0);
        m_deint->setChecked(false);
        m_denoise->setChecked(false);
    });
    return w;
}

// ---------------- Водяной знак ----------------

QWidget *EditDialog::buildWatermarkTab()
{
    auto *w = sidePanel(this);
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(20, 18, 20, 18);
    lay->setSpacing(10);

    m_wmNone = new QRadioButton(QStringLiteral("Без водяного знака"), w);
    m_wmText = new QRadioButton(QStringLiteral("Текст"), w);
    m_wmImage = new QRadioButton(QStringLiteral("Картинка"), w);
    auto *grp = new QButtonGroup(w);
    grp->addButton(m_wmNone);
    grp->addButton(m_wmText);
    grp->addButton(m_wmImage);

    m_wmTextEdit = new QLineEdit(m_opt.watermarkText, w);
    m_wmTextEdit->setPlaceholderText(QStringLiteral("Текст водяного знака"));
    m_wmImagePath = new QLineEdit(m_opt.watermarkImage, w);
    m_wmImagePath->setReadOnly(true);
    auto *browse = new QPushButton(QStringLiteral("Выбрать…"), w);
    auto *imgRow = new QHBoxLayout;
    imgRow->addWidget(m_wmImagePath, 1);
    imgRow->addWidget(browse);

    m_wmPos = new QComboBox(w);
    m_wmPos->addItems({QStringLiteral("Слева сверху"), QStringLiteral("Справа сверху"), QStringLiteral("Слева снизу"),
                       QStringLiteral("Справа снизу"), QStringLiteral("По центру")});
    m_wmPos->setCurrentIndex(m_opt.watermarkPos);
    m_wmOpacity = makeSlider(5, 100, m_opt.watermarkOpacity, w);
    m_wmSize = new QSpinBox(w);
    m_wmSize->setRange(4, 200);
    m_wmSize->setValue(m_opt.watermarkSize);
    m_wmSize->setToolTip(QStringLiteral("Для текста — размер шрифта, для картинки — % ширины кадра"));

    lay->addWidget(m_wmNone);
    lay->addWidget(m_wmText);
    lay->addWidget(m_wmTextEdit);
    lay->addWidget(m_wmImage);
    lay->addLayout(imgRow);
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Положение:"), m_wmPos);
    form->addRow(QStringLiteral("Прозрачность:"), m_wmOpacity);
    form->addRow(QStringLiteral("Размер:"), m_wmSize);
    lay->addSpacing(8);
    lay->addLayout(form);
    lay->addStretch();

    if (!m_opt.watermarkImage.isEmpty()) m_wmImage->setChecked(true);
    else if (!m_opt.watermarkText.isEmpty()) m_wmText->setChecked(true);
    else m_wmNone->setChecked(true);

    auto apply = [this] {
        m_opt.watermarkText = m_wmText->isChecked() ? m_wmTextEdit->text() : QString();
        m_opt.watermarkImage = m_wmImage->isChecked() ? m_wmImagePath->text() : QString();
        m_opt.watermarkPos = m_wmPos->currentIndex();
        m_opt.watermarkOpacity = m_wmOpacity->value();
        m_opt.watermarkSize = m_wmSize->value();
        changed();
    };
    connect(grp, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked), this, apply);
    connect(m_wmTextEdit, &QLineEdit::textChanged, this, [this, apply] { m_wmText->setChecked(true); apply(); });
    connect(m_wmPos, QOverload<int>::of(&QComboBox::currentIndexChanged), this, apply);
    connect(m_wmOpacity, &QSlider::valueChanged, this, apply);
    connect(m_wmSize, QOverload<int>::of(&QSpinBox::valueChanged), this, apply);
    connect(browse, &QPushButton::clicked, this, [this, apply] {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Картинка для водяного знака"),
                                                       Settings::lastOpenDir(), QStringLiteral("Изображения (*.png *.jpg *.jpeg *.bmp *.webp)"));
        if (f.isEmpty())
            return;
        m_wmImagePath->setText(f);
        if (m_wmSize->value() > 60) m_wmSize->setValue(20);
        m_wmImage->setChecked(true);
        apply();
    });
    return w;
}

// ---------------- Субтитры ----------------

QWidget *EditDialog::buildSubtitleTab()
{
    auto *w = sidePanel(this);
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(20, 18, 20, 18);
    lay->setSpacing(10);

    lay->addWidget(new QLabel(QStringLiteral("Встроенные субтитры:")));
    m_subTrack = new QComboBox(w);
    m_subTrack->addItem(QStringLiteral("Нет"), -1);
    for (const StreamTrack &t : m_task->info.subtitleTracks)
        m_subTrack->addItem(t.label(), t.index);
    m_subTrack->setCurrentIndex(qMax(0, m_subTrack->findData(m_opt.subtitleTrack)));
    lay->addWidget(m_subTrack);

    lay->addSpacing(8);
    lay->addWidget(new QLabel(QStringLiteral("Или файл субтитров (SRT, ASS):")));
    m_subFile = new QLineEdit(m_opt.subtitleFile, w);
    m_subFile->setReadOnly(true);
    auto *browse = new QPushButton(QStringLiteral("Добавить…"), w);
    auto *clear = new QPushButton(QStringLiteral("Убрать"), w);
    auto *row = new QHBoxLayout;
    row->addWidget(browse);
    row->addWidget(clear);
    lay->addWidget(m_subFile);
    lay->addLayout(row);
    auto *note = new QLabel(QStringLiteral("Субтитры впечатываются в изображение и будут видны на любом устройстве."), w);
    note->setObjectName(QStringLiteral("Muted"));
    note->setWordWrap(true);
    lay->addWidget(note);
    lay->addStretch();

    connect(m_subTrack, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        m_opt.subtitleTrack = m_subTrack->currentData().toInt();
        if (m_opt.subtitleTrack >= 0) { m_opt.subtitleFile.clear(); m_subFile->clear(); }
        changed();
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Файл субтитров"), QFileInfo(m_task->input).absolutePath(),
                                                       QStringLiteral("Субтитры (*.srt *.ass *.ssa *.vtt)"));
        if (f.isEmpty())
            return;
        m_opt.subtitleFile = f;
        m_subFile->setText(f);
        m_subTrack->setCurrentIndex(0);
        changed();
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        m_opt.subtitleFile.clear();
        m_subFile->clear();
        changed();
    });
    return w;
}

// ---------------- Аудио ----------------

QWidget *EditDialog::buildAudioTab()
{
    auto *w = sidePanel(this);
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(20, 18, 20, 18);
    lay->setSpacing(10);

    lay->addWidget(new QLabel(QStringLiteral("Звуковая дорожка:")));
    m_audioTrack = new QComboBox(w);
    for (const StreamTrack &t : m_task->info.audioTracks)
        m_audioTrack->addItem(t.label(), t.index);
    m_audioTrack->addItem(QStringLiteral("Без звука"), -1);
    m_audioTrack->setCurrentIndex(qMax(0, m_audioTrack->findData(m_opt.audioTrack)));
    lay->addWidget(m_audioTrack);

    lay->addSpacing(8);
    auto *h = new QHBoxLayout;
    h->addWidget(new QLabel(QStringLiteral("Громкость:")));
    m_volume = makeSlider(0, 300, m_opt.volume, w);
    m_volumeLabel = new QLabel(QStringLiteral("%1%").arg(m_opt.volume), w);
    m_volumeLabel->setMinimumWidth(44);
    h->addWidget(m_volume, 1);
    h->addWidget(m_volumeLabel);
    lay->addLayout(h);
    lay->addStretch();

    connect(m_audioTrack, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        m_opt.audioTrack = m_audioTrack->currentData().toInt();
    });
    connect(m_volume, &QSlider::valueChanged, this, [this](int v) {
        m_opt.volume = v;
        m_volumeLabel->setText(QStringLiteral("%1%").arg(v));
    });
    return w;
}
