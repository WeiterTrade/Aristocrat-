#include "OutputSizeDialog.h"
#include "Widgets.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

OutputSizeDialog::OutputSizeDialog(const TaskPtr &task, QWidget *parent)
    : QDialog(parent), m_task(task)
{
    setWindowTitle(QStringLiteral("Желаемый размер файла"));
    setMinimumWidth(620);

    // Размер «по умолчанию» — без пользовательского битрейта
    ConversionTask copy = *task;
    copy.customVBitrate = 0;
    m_defaultMb = qMax(1.0, copy.estimatedBytes() / (1024.0 * 1024.0));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(30, 26, 30, 20);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(16);

    m_size = new QDoubleSpinBox(this);
    m_size->setSuffix(QStringLiteral("  МБ"));
    m_size->setDecimals(2);
    m_size->setRange(1.0, maxSizeMb());
    m_size->setValue(task->estimatedBytes() / (1024.0 * 1024.0));
    m_size->setMinimumWidth(170);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(1, 1000);
    m_slider->setValue(int(m_size->value() / maxSizeMb() * 1000));

    m_res = new QComboBox(this);
    m_res->addItem(QStringLiteral("Авто"), QSize());
    for (const QSize &s : {QSize(3840, 2160), QSize(1920, 1080), QSize(1280, 720), QSize(854, 480), QSize(640, 360)})
        m_res->addItem(QStringLiteral("%1*%2").arg(s.width()).arg(s.height()), s);
    const int ri = m_res->findData(task->customResolution);
    m_res->setCurrentIndex(task->customResolution.isValid() && ri >= 0 ? ri : 0);
    m_res->setEnabled(task->upscale.mode == UpscaleOptions::None && !task->preset.isAudioOnly());

    m_bitrate = new QLabel(this);
    m_bitrate->setObjectName(QStringLiteral("InfoBox"));
    m_bitrate->setAlignment(Qt::AlignCenter);
    m_bitrate->setMinimumWidth(130);
    m_bitrate->setMinimumHeight(34);
    m_quality = new QLabel(this);
    m_quality->setObjectName(QStringLiteral("InfoBox"));
    m_quality->setAlignment(Qt::AlignCenter);
    m_quality->setMinimumHeight(34);

    grid->addWidget(new QLabel(QStringLiteral("Размер файла:")), 0, 0, Qt::AlignRight);
    grid->addWidget(m_size, 0, 1);
    grid->addWidget(m_slider, 0, 2, 1, 2);
    grid->addWidget(new QLabel(QStringLiteral("Разрешение:")), 1, 0, Qt::AlignRight);
    grid->addWidget(m_res, 1, 1);
    grid->addWidget(new QLabel(QStringLiteral("Битрейт:")), 1, 2, Qt::AlignRight);
    grid->addWidget(m_bitrate, 1, 3);
    grid->addWidget(new QLabel(QStringLiteral("Качество:")), 2, 0, Qt::AlignRight);
    grid->addWidget(m_quality, 2, 1);
    grid->setColumnStretch(2, 1);
    root->addLayout(grid);

    auto *btns = new QHBoxLayout;
    btns->addStretch();
    auto *reset = new QPushButton(QStringLiteral("Сбросить"), this);
    reset->setObjectName(QStringLiteral("DarkButton"));
    auto *ok = Ui::primaryButton(QStringLiteral("OK"), this);
    ok->setMinimumWidth(170);
    reset->setMinimumWidth(170);
    btns->addWidget(reset);
    btns->addWidget(ok);
    btns->addStretch();
    root->addSpacing(18);
    root->addLayout(btns);

    connect(m_size, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        m_slider->blockSignals(true);
        m_slider->setValue(int(v / maxSizeMb() * 1000));
        m_slider->blockSignals(false);
        recalcFromSize();
    });
    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        m_size->setValue(v / 1000.0 * maxSizeMb());
    });
    connect(m_res, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateLabels(); });
    connect(reset, &QPushButton::clicked, this, [this] {
        m_res->setCurrentIndex(0);
        m_size->setValue(m_defaultMb);
        m_vbr = 0;
        updateLabels();
    });
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);

    m_vbr = task->customVBitrate;
    recalcFromSize();
    if (task->customVBitrate == 0)
        m_vbr = 0;
    updateLabels();
}

double OutputSizeDialog::maxSizeMb() const
{
    return qMax(2.0, m_defaultMb * 2.0);
}

QSize OutputSizeDialog::resolution() const
{
    return m_res->currentData().toSize();
}

void OutputSizeDialog::recalcFromSize()
{
    const double dur = qMax(1.0, m_task->effectiveDuration());
    const int audio = (m_task->edit.audioTrack >= 0 && !m_task->info.audioTracks.isEmpty())
                          ? (m_task->preset.abitrate > 0 ? m_task->preset.abitrate : 192) : 0;
    const double totalKbps = m_size->value() * 1024.0 * 1024.0 * 8.0 / 1000.0 / dur;
    m_vbr = qMax(100, int(totalKbps) - audio);
    updateLabels();
}

void OutputSizeDialog::updateLabels()
{
    if (m_task->preset.isAudioOnly()) {
        m_bitrate->setText(QStringLiteral("—"));
        m_quality->setText(QStringLiteral("Только звук"));
        return;
    }
    int kbps = m_vbr;
    if (kbps <= 0) {
        ConversionTask copy = *m_task;
        copy.customVBitrate = 0;
        kbps = int(copy.estimatedBytes() * 8.0 / 1000.0 / qMax(1.0, copy.effectiveDuration())) - 192;
    }
    m_bitrate->setText(QStringLiteral("%1 кбит/с").arg(qMax(0, kbps)));
    QSize s = resolution();
    if (!s.isValid() || s.isEmpty())
        s = m_task->outputSize();
    const double fps = m_task->info.fps > 0 ? m_task->info.fps : 25.0;
    const double bpp = (s.width() > 0 && s.height() > 0) ? kbps * 1000.0 / (double(s.width()) * s.height() * fps) : 0.1;
    m_quality->setText(bpp > 0.09 ? QStringLiteral("Высокое") : bpp > 0.045 ? QStringLiteral("Обычное") : QStringLiteral("Низкое"));
}
