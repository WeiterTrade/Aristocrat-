#include "TrimDialog.h"
#include "FramePreview.h"
#include "Widgets.h"

#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTime>
#include <QTimeEdit>
#include <QVBoxLayout>

static QTime toTime(double sec)
{
    return QTime(0, 0).addMSecs(int(qMax(0.0, sec) * 1000));
}

static double fromTime(const QTime &t)
{
    return QTime(0, 0).msecsTo(t) / 1000.0;
}

TrimDialog::TrimDialog(const TaskPtr &task, QWidget *parent)
    : QDialog(parent), m_task(task)
{
    setWindowTitle(QStringLiteral("Обрезка — ") + QFileInfo(task->input).fileName());
    resize(820, 620);
    const double dur = task->info.duration;

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(12);

    m_preview = new PreviewLabel(this);
    root->addWidget(m_preview, 1);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, qMax(1, int(dur * 10)));   // шаг 0,1 с
    m_slider->setValue(int(task->edit.trimStart * 10));
    root->addWidget(m_slider);

    auto *grid = new QGridLayout;
    m_start = new QTimeEdit(this);
    m_end = new QTimeEdit(this);
    for (QTimeEdit *e : {m_start, m_end}) {
        e->setDisplayFormat(QStringLiteral("HH:mm:ss.zzz"));
        e->setTimeRange(QTime(0, 0), toTime(dur));
    }
    m_start->setTime(toTime(task->edit.trimStart));
    m_end->setTime(toTime(task->edit.trimEnd > 0 ? task->edit.trimEnd : dur));
    auto *setStart = new QPushButton(QStringLiteral("Начало здесь"), this);
    auto *setEnd = new QPushButton(QStringLiteral("Конец здесь"), this);
    grid->addWidget(new QLabel(QStringLiteral("Начало:")), 0, 0);
    grid->addWidget(m_start, 0, 1);
    grid->addWidget(setStart, 0, 2);
    grid->addWidget(new QLabel(QStringLiteral("Конец:")), 1, 0);
    grid->addWidget(m_end, 1, 1);
    grid->addWidget(setEnd, 1, 2);
    grid->setColumnStretch(3, 1);
    m_info = new QLabel(this);
    m_info->setObjectName(QStringLiteral("StatusLabel"));
    grid->addWidget(m_info, 0, 3, 2, 1, Qt::AlignRight | Qt::AlignVCenter);
    root->addLayout(grid);

    auto *btns = new QHBoxLayout;
    auto *reset = new QPushButton(QStringLiteral("Сбросить"), this);
    auto *ok = Ui::primaryButton(QStringLiteral("OK"), this);
    auto *cancel = Ui::primaryButton(QStringLiteral("Отмена"), this);
    btns->addWidget(reset);
    btns->addStretch();
    btns->addWidget(ok);
    btns->addWidget(cancel);
    root->addLayout(btns);

    connect(m_slider, &QSlider::sliderReleased, this, &TrimDialog::showFrame);
    connect(m_slider, &QSlider::valueChanged, this, [this] { if (!m_slider->isSliderDown()) showFrame(); });
    connect(setStart, &QPushButton::clicked, this, [this] { m_start->setTime(toTime(m_slider->value() / 10.0)); });
    connect(setEnd, &QPushButton::clicked, this, [this] { m_end->setTime(toTime(m_slider->value() / 10.0)); });
    connect(m_start, &QTimeEdit::timeChanged, this, &TrimDialog::updateInfo);
    connect(m_end, &QTimeEdit::timeChanged, this, &TrimDialog::updateInfo);
    connect(reset, &QPushButton::clicked, this, [this, dur] {
        m_start->setTime(QTime(0, 0));
        m_end->setTime(toTime(dur));
    });
    connect(ok, &QPushButton::clicked, this, [this] {
        if (end() > 0 && end() <= start()) {
            m_info->setText(QStringLiteral("Конец должен быть позже начала"));
            return;
        }
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    updateInfo();
    showFrame();
}

double TrimDialog::start() const
{
    return fromTime(m_start->time());
}

double TrimDialog::end() const
{
    const double e = fromTime(m_end->time());
    return e >= m_task->info.duration - 0.05 ? 0.0 : e;
}

void TrimDialog::updateInfo()
{
    const double e = end() > 0 ? end() : m_task->info.duration;
    m_info->setText(QStringLiteral("Длительность: %1").arg(formatDuration(qMax(0.0, e - start()))));
}

void TrimDialog::showFrame()
{
    const int gen = ++m_generation;
    const double at = m_slider->value() / 10.0;
    ConversionTask copy = *m_task;
    m_preview->setBusy(true);
    auto img = QSharedPointer<QImage>::create();
    Ui::runAsync(this, [copy, at, img] { *img = FramePreview::grab(copy, at, false, 800); },
                 [this, img, gen] { if (gen == m_generation) m_preview->setImage(*img); });
}
