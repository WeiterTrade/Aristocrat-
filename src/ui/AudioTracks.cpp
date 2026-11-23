#include "AudioTracks.h"
#include "Widgets.h"
#include "core/I18n.h"
#include "core/MediaInfo.h"
#include "core/Tools.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QSharedPointer>
#include <QTimer>
#include <QVBoxLayout>

// Языки дорожек: русское название — код ISO 639-2
static const QList<QPair<QString, QString>> &languages()
{
    static const QList<QPair<QString, QString>> l = {
        {QStringLiteral("Французский"), QStringLiteral("fre")}, {QStringLiteral("Испанский"), QStringLiteral("spa")},
        {QStringLiteral("Итальянский"), QStringLiteral("ita")}, {QStringLiteral("Немецкий"), QStringLiteral("ger")},
        {QStringLiteral("Английский"), QStringLiteral("eng")}, {QStringLiteral("Русский"), QStringLiteral("rus")},
        {QStringLiteral("Португальский"), QStringLiteral("por")}, {QStringLiteral("Чешский"), QStringLiteral("cze")},
        {QStringLiteral("Нидерландский"), QStringLiteral("dut")}, {QStringLiteral("Польский"), QStringLiteral("pol")},
        {QStringLiteral("Венгерский"), QStringLiteral("hun")}};
    return l;
}

// ======================= Вшивание =======================

AudioMuxer *AudioMuxer::instance()
{
    static AudioMuxer *m = new AudioMuxer(qApp);
    return m;
}

AudioMuxer::AudioMuxer(QObject *parent) : QObject(parent) {}

void AudioMuxer::add(const QString &audioFile, const Release::AudioPending &a)
{
    Job j;
    j.file = audioFile;
    j.a = a;
    m_queue.enqueue(j);
    next();
}

void AudioMuxer::next()
{
    if (m_busy || m_queue.isEmpty())
        return;
    run(m_queue.dequeue());
}

void AudioMuxer::run(Job j)
{
    const QString target = QDir(j.a.dir).filePath(j.a.base + QStringLiteral(".mkv"));
    const QFileInfo ti(target);
    // MKV релиза ещё нет или ещё пишется — подождать (видео может качаться и обрабатываться часами)
    if (!ti.exists() || ti.size() != j.lastSize) {
        if (j.waits > 2880) {   // ~24 часа
            emit message(T("Звуковая дорожка «%1» не вшита: нет файла %2").arg(j.a.title, QDir::toNativeSeparators(target)));
            next();
            return;
        }
        j.lastSize = ti.exists() ? ti.size() : -1;
        ++j.waits;
        QTimer::singleShot(30000, this, [this, j] {
            m_queue.prepend(j);
            next();
        });
        if (ti.exists() && j.waits == 1)
            emit message(T("Звуковая дорожка «%1»: жду, пока MKV релиза допишется…").arg(j.a.title));
        next();
        return;
    }
    m_busy = true;
    emit message(T("Вшиваю звуковую дорожку «%1» в %2…").arg(j.a.title, ti.fileName()));
    auto info = QSharedPointer<MediaInfo>::create();
    Ui::runAsync(this, [target, info] { *info = MediaInfo::probe(target); }, [this, j, target, info] {
        const int n = info->audioTracks.size();
        const QString tmp = target + QStringLiteral(".audio.tmp.mkv");
        QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-y"), QStringLiteral("-i"), target, QStringLiteral("-i"), j.file,
                         QStringLiteral("-map"), QStringLiteral("0"), QStringLiteral("-map"), QStringLiteral("1:a:0"),
                         QStringLiteral("-c"), QStringLiteral("copy"), QStringLiteral("-map_metadata"), QStringLiteral("0"),
                         QStringLiteral("-map_chapters"), QStringLiteral("0")};
        // Первая дорожка без названия — «Оригинал»
        if (n >= 1 && info->audioTracks.first().title.trimmed().isEmpty())
            args << QStringLiteral("-metadata:s:a:0") << QStringLiteral("title=Оригинал");
        args << QStringLiteral("-metadata:s:a:%1").arg(n) << QStringLiteral("language=%1").arg(j.a.lang)
             << QStringLiteral("-metadata:s:a:%1").arg(n) << QStringLiteral("title=%1").arg(j.a.title)
             << QStringLiteral("-disposition:a:%1").arg(n) << QStringLiteral("0") << tmp;
        auto *p = new QProcess(this);
        p->setProcessChannelMode(QProcess::MergedChannels);
        connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, p, j, target, tmp](int code, QProcess::ExitStatus st) {
            p->deleteLater();
            m_busy = false;
            if (st == QProcess::NormalExit && code == 0 && QFileInfo(tmp).size() > QFileInfo(target).size() / 2) {
                QFile::remove(target);
                QFile::rename(tmp, target);
                QFile::remove(j.file);
                QDir tempDir = QFileInfo(j.file).dir();
                if (tempDir.dirName() == QLatin1String(".aristocrat-temp") && tempDir.isEmpty())
                    tempDir.removeRecursively();
                emit message(T("Звуковая дорожка «%1» вшита в %2").arg(j.a.title, QFileInfo(target).fileName()));
            } else {
                QFile::remove(tmp);
                emit message(T("Звуковую дорожку «%1» вшить не удалось (файл звука оставлен: %2)")
                                 .arg(j.a.title, QDir::toNativeSeparators(j.file)));
                qWarning("audio mux: %s", p->readAll().right(2000).constData());
            }
            next();
        });
        p->start(Tools::ffmpeg(), args);
    });
}

// ======================= Окно ссылок =======================

AudioTracksDialog::AudioTracksDialog(const QString &dir, const QString &base, std::function<void(const QString &)> request,
                                     QWidget *parent)
    : FramedDialog(parent, true), m_dir(dir), m_base(base), m_request(std::move(request))
{
    setWindowTitle(T("Звуковые дорожки"));
    resize(820, 300);
    auto *lay = contentLayout();
    lay->setContentsMargins(16, 12, 16, 12);
    auto *hint = new QLabel(T("Вставьте ссылки на этот же фильм на других языках (например, Dorcel: французская, испанская версия). "
                              "Программа скачает только звук и вшьёт его в MKV релиза дорожкой «Французский», «Испанский» и т. д.; "
                              "основная дорожка будет называться «Оригинал». Если видео ещё качается — звук вшьётся, когда MKV будет готов."), this);
    hint->setWordWrap(true);
    lay->addWidget(hint);
    m_rows = new QVBoxLayout;
    lay->addLayout(m_rows);
    auto *plus = new QPushButton(QStringLiteral("+"), this);
    plus->setFixedWidth(44);
    plus->setToolTip(T("Ещё одна ссылка"));
    auto *pr = new QHBoxLayout;
    pr->addWidget(plus);
    pr->addStretch();
    lay->addLayout(pr);
    connect(plus, &QPushButton::clicked, this, &AudioTracksDialog::addRow);
    addRow();
    lay->addStretch();
    auto *state = new QLabel(this);
    state->setObjectName(QStringLiteral("StatusLabel"));
    state->setWordWrap(true);
    lay->addWidget(state);
    auto *btns = new QHBoxLayout;
    auto *cancel = new QPushButton(T("Закрыть"), this);
    auto *go = Ui::primaryButton(T("Скачать и вшить"), this);
    btns->addStretch();
    btns->addWidget(cancel);
    btns->addWidget(go);
    lay->addLayout(btns);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(go, &QPushButton::clicked, this, [this, state] {
        int count = 0;
        for (int i = 0; i < m_rows->count(); ++i) {
            QWidget *row = m_rows->itemAt(i)->widget();
            if (!row)
                continue;
            auto *edit = row->findChild<QLineEdit *>();
            auto *lang = row->findChild<QComboBox *>();
            const QString url = edit ? edit->text().trimmed() : QString();
            if (url.isEmpty() || !lang)
                continue;
            Release::addAudioPending(url, Release::AudioPending{m_dir, m_base, lang->currentText(), lang->currentData().toString()});
            if (m_request)
                m_request(url);
            ++count;
        }
        if (!count) {
            state->setText(T("Вставьте хотя бы одну ссылку."));
            return;
        }
        accept();
    });
}

void AudioTracksDialog::addRow()
{
    auto *row = new QWidget(this);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    auto *edit = new QLineEdit(row);
    edit->setPlaceholderText(T("Ссылка на версию фильма на другом языке (Adult Empire)"));
    auto *lang = new QComboBox(row);
    for (const auto &l : languages())
        lang->addItem(l.first, l.second);
    // Язык по ссылке (…-spanish-…, …-french-…) — сам
    connect(edit, &QLineEdit::textChanged, lang, [lang](const QString &t) {
        const auto g = Release::languageOf(QString(t).replace(QLatin1Char('-'), QLatin1Char(' ')));
        if (!g.first.isEmpty()) {
            const int i = lang->findText(g.first);
            if (i >= 0)
                lang->setCurrentIndex(i);
        }
    });
    h->addWidget(edit, 1);
    h->addWidget(lang);
    m_rows->addWidget(row);
}
