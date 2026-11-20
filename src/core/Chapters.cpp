#include "Chapters.h"
#include "Tools.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QtMath>

namespace Chapters {

QList<Chapter> read(const QString &file, double *duration)
{
    QList<Chapter> out;
    if (Tools::ffprobe().isEmpty())
        return out;
    QProcess p;
    p.start(Tools::ffprobe(), {"-v", "error", "-print_format", "json", "-show_chapters", "-show_format", file});
    if (!p.waitForFinished(30000)) {
        p.kill();
        return out;
    }
    const QJsonObject root = QJsonDocument::fromJson(p.readAllStandardOutput()).object();
    if (duration)
        *duration = root.value("format").toObject().value("duration").toString().toDouble();
    for (const QJsonValue &v : root.value("chapters").toArray()) {
        const QJsonObject c = v.toObject();
        Chapter ch;
        ch.start = c.value("start_time").toString().toDouble();
        ch.end = c.value("end_time").toString().toDouble();
        ch.title = c.value("tags").toObject().value("title").toString();
        out << ch;
    }
    return out;
}

static QString esc(QString s)
{
    // В файле метаданных FFmpeg спецсимволы экранируются обратной чертой
    s.replace('\\', QStringLiteral("\\\\"));
    for (const QChar c : {QChar('='), QChar(';'), QChar('#')})
        s.replace(c, QStringLiteral("\\") + c);
    s.replace('\n', ' ');
    return s;
}

QString metadataFile(const QList<Chapter> &list, double duration)
{
    QString t = QStringLiteral(";FFMETADATA1\n");
    for (int i = 0; i < list.size(); ++i) {
        const Chapter &c = list[i];
        double end = c.end;
        const double next = i + 1 < list.size() ? list[i + 1].start : (duration > 0 ? duration : c.start + 1);
        if (end <= c.start || end > next)
            end = next;   // главы идут подряд, без наложений
        t += QStringLiteral("[CHAPTER]\nTIMEBASE=1/1000\nSTART=%1\nEND=%2\ntitle=%3\n")
                 .arg(qint64(qRound64(c.start * 1000))).arg(qint64(qRound64(end * 1000))).arg(esc(c.title));
    }
    return t;
}

QStringList muxArgs(const QString &input, const QString &metaFile, const QString &output)
{
    return {"-hide_banner", "-nostdin", "-y", "-loglevel", "error", "-i", input, "-i", metaFile,
            "-map", "0:v?", "-map", "0:a?", "-map", "0:s?", "-map_metadata", "0", "-map_chapters", "1",
            "-c", "copy", output};
}

QStringList remuxArgs(const QString &input, const QString &metaFile, const QString &output)
{
    QStringList a = {"-hide_banner", "-nostdin", "-y", "-loglevel", "error", "-fflags", "+genpts", "-i", input, "-i", metaFile,
                     "-map", "0:v?", "-map", "0:a?", "-map", "0:s?", "-map", "0:t?", "-map_metadata", "0", "-map_chapters", "1",
                     "-c", "copy"};
    // Субтитры MP4 (mov_text) Matroska не принимает как есть — переводим в SRT
    QProcess p;
    p.start(Tools::ffprobe(), {"-v", "error", "-select_streams", "s", "-show_entries", "stream=codec_name", "-of", "csv=p=0", input});
    if (p.waitForFinished(15000) && QString::fromUtf8(p.readAllStandardOutput()).contains(QLatin1String("mov_text")))
        a << "-c:s" << "srt";
    a << output;
    return a;
}

double parseTime(const QString &text, bool *ok)
{
    static const QRegularExpression re(QStringLiteral("^\\s*(?:(\\d{1,2}):)?(\\d{1,2}):(\\d{1,2})(?:[.,](\\d{1,3}))?\\s*$"));
    const auto m = re.match(text);
    if (ok)
        *ok = m.hasMatch();
    if (!m.hasMatch())
        return 0;
    QString ms = m.captured(4);
    while (ms.size() < 3 && !ms.isEmpty()) ms += '0';
    return m.captured(1).toInt() * 3600.0 + m.captured(2).toInt() * 60.0 + m.captured(3).toInt() + ms.toInt() / 1000.0;
}

QString formatTime(double s)
{
    const qint64 ms = qRound64(qMax(0.0, s) * 1000);
    return QStringLiteral("%1:%2:%3.%4")
        .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
        .arg((ms / 60000) % 60, 2, 10, QLatin1Char('0'))
        .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

QString titleCase(const QString &text)
{
    QString out = text.simplified();
    bool start = true;
    for (int i = 0; i < out.size(); ++i) {
        const QChar c = out[i];
        if (c.isLetter()) {
            if (start)
                out[i] = c.toUpper();
            start = false;
        } else if (c.isDigit()) {
            start = false;
        } else {
            // новое слово — после пробела, дефиса, кавычек и скобок
            start = c.isSpace() || c == '-' || c == '(' || c == '"' || c == QChar(0x00AB) || c == '/';
        }
    }
    return out;
}

QString toOgm(const QList<Chapter> &list)
{
    QString t;
    for (int i = 0; i < list.size(); ++i) {
        const QString n = QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0'));
        t += QStringLiteral("CHAPTER%1=%2\nCHAPTER%1NAME=%3\n").arg(n, formatTime(list[i].start), list[i].title);
    }
    return t;
}

QList<Chapter> fromOgm(const QString &text)
{
    QList<Chapter> out;
    static const QRegularExpression time(QStringLiteral("^CHAPTER(\\d+)=(.+)$"));
    static const QRegularExpression name(QStringLiteral("^CHAPTER(\\d+)NAME=(.*)$"));
    for (const QString &line : text.split('\n')) {
        const QString l = line.trimmed();
        auto m = name.match(l);
        if (m.hasMatch()) {
            if (!out.isEmpty()) out.last().title = m.captured(2);
            continue;
        }
        m = time.match(l);
        if (m.hasMatch()) {
            Chapter c;
            c.start = parseTime(m.captured(2));
            out << c;
        }
    }
    return out;
}

} // namespace Chapters
