#include "PresetEditDialog.h"
#include "core/I18n.h"
#include "Widgets.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

static void selectData(QComboBox *c, const QVariant &v)
{
    const int i = c->findData(v);
    c->setCurrentIndex(i >= 0 ? i : 0);
}

PresetEditDialog::PresetEditDialog(const Preset &p, QWidget *parent)
    : FramedDialog(parent, false), m_base(p)
{
    setWindowTitle(T("Настройки формата"));
    setMinimumWidth(460);

    auto *root = contentLayout();
    root->setContentsMargins(24, 20, 24, 20);
    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);

    m_name = new QLineEdit(p.custom ? p.name : T(p.name) + T(" (своё)"), this);

    m_ext = new QComboBox(this);
    for (const QString &e : {"mp4", "mkv", "mov", "avi", "wmv", "webm", "flv", "mpg", "m4v", "3gp",
                             "mp3", "m4a", "wav", "flac", "ogg", "wma", "opus"})
        m_ext->addItem(e.toUpper(), e);
    selectData(m_ext, p.ext);

    m_vcodec = new QComboBox(this);
    m_vcodec->addItem("H.264", "libx264");
    m_vcodec->addItem("HEVC (H.265)", "libx265");
    m_vcodec->addItem("MPEG-4 (Xvid)", "mpeg4");
    m_vcodec->addItem("VP9", "libvpx-vp9");
    m_vcodec->addItem("MPEG-2", "mpeg2video");
    m_vcodec->addItem("WMV", "wmv2");
    selectData(m_vcodec, p.vcodec);

    m_res = new QComboBox(this);
    m_res->addItem(T("Как в исходнике"), QSize(0, 0));
    for (const QSize &s : {QSize(3840, 2160), QSize(2560, 1440), QSize(1920, 1080), QSize(1280, 720),
                           QSize(1024, 576), QSize(854, 480), QSize(720, 576), QSize(720, 480), QSize(640, 360)})
        m_res->addItem(QStringLiteral("%1*%2").arg(s.width()).arg(s.height()), s);
    selectData(m_res, QSize(p.width, p.height));

    m_fps = new QComboBox(this);
    m_fps->addItem(T("Как в исходнике"), 0.0);
    for (double f : {23.976, 24.0, 25.0, 29.97, 30.0, 50.0, 59.94, 60.0})
        m_fps->addItem(QString::number(f), f);
    selectData(m_fps, p.fps);

    m_vbr = new QComboBox(this);
    m_vbr->addItem(T("Авто (по качеству)"), 0);
    for (int b : {800, 1500, 2500, 4000, 6000, 8000, 12000, 16000, 20000, 30000, 40000})
        m_vbr->addItem(T("%1 кбит/с").arg(b), b);
    selectData(m_vbr, p.vbitrate);

    m_acodec = new QComboBox(this);
    m_acodec->addItem("AAC", "aac");
    m_acodec->addItem("MP3", "libmp3lame");
    m_acodec->addItem("Opus", "libopus");
    m_acodec->addItem("Vorbis", "libvorbis");
    m_acodec->addItem("FLAC", "flac");
    m_acodec->addItem("PCM (WAV)", "pcm_s16le");
    m_acodec->addItem("WMA", "wmav2");
    m_acodec->addItem("MP2", "mp2");
    selectData(m_acodec, p.acodec);

    m_abr = new QComboBox(this);
    m_abr->addItem(T("По умолчанию"), 0);
    for (int b : {64, 96, 128, 160, 192, 256, 320})
        m_abr->addItem(T("%1 кбит/с").arg(b), b);
    selectData(m_abr, p.abitrate);

    form->addRow(T("Название:"), m_name);
    form->addRow(T("Контейнер:"), m_ext);
    if (!p.isAudioOnly()) {
        form->addRow(T("Видеокодек:"), m_vcodec);
        form->addRow(T("Разрешение:"), m_res);
        form->addRow(T("Частота кадров:"), m_fps);
        form->addRow(T("Битрейт видео:"), m_vbr);
    } else {
        m_vcodec->hide(); m_res->hide(); m_fps->hide(); m_vbr->hide();
    }
    form->addRow(T("Аудиокодек:"), m_acodec);
    form->addRow(T("Битрейт звука:"), m_abr);
    root->addLayout(form);

    auto *btns = new QHBoxLayout;
    btns->addStretch();
    auto *cancel = new QPushButton(T("Отмена"), this);
    auto *ok = Ui::primaryButton(T("Сохранить"), this);
    btns->addWidget(cancel);
    btns->addWidget(ok);
    root->addSpacing(12);
    root->addLayout(btns);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
}

Preset PresetEditDialog::preset() const
{
    Preset p = m_base;
    p.name = m_name->text().trimmed().isEmpty() ? T("Свой формат") : m_name->text().trimmed();
    p.ext = m_ext->currentData().toString();
    if (!m_base.isAudioOnly()) {
        p.vcodec = m_vcodec->currentData().toString();
        const QSize s = m_res->currentData().toSize();
        p.width = s.width();
        p.height = s.height();
        p.fps = m_fps->currentData().toDouble();
        p.vbitrate = m_vbr->currentData().toInt();
    }
    p.acodec = m_acodec->currentData().toString();
    p.abitrate = m_abr->currentData().toInt();
    p.badge = p.width > 0 ? QStringLiteral("%1P").arg(p.height) : p.ext.toUpper();
    return p;
}
