#include "Preset.h"
#include "Settings.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

QString codecDisplayName(const QString &c)
{
    static const QHash<QString, QString> names = {
        {"libx264", "H264"}, {"h264", "H264"}, {"libx265", "HEVC"}, {"hevc", "HEVC"},
        {"mpeg4", "MPEG-4"}, {"wmv2", "WMV"}, {"libvpx-vp9", "VP9"}, {"vp9", "VP9"},
        {"mpeg2video", "MPEG-2"}, {"aac", "AAC"}, {"libmp3lame", "MP3"}, {"mp3", "MP3"},
        {"wmav2", "WMA"}, {"libopus", "OPUS"}, {"opus", "OPUS"}, {"libvorbis", "VORBIS"},
        {"flac", "FLAC"}, {"pcm_s16le", "PCM"}, {"mp2", "MP2"}, {"ac3", "AC3"},
    };
    return names.value(c, c.toUpper());
}

QString Preset::title() const
{
    return group + QStringLiteral(" · ") + name;
}

QString Preset::resolutionText() const
{
    if (isAudioOnly())
        return abitrate > 0 ? QStringLiteral("%1 кбит/с").arg(abitrate) : QStringLiteral("Без потерь");
    if (width <= 0 || height <= 0)
        return QStringLiteral("Исходное разрешение");
    return QStringLiteral("%1*%2").arg(width).arg(height);
}

QString Preset::codecText() const
{
    QStringList l;
    if (!vcodec.isEmpty())
        l << codecDisplayName(vcodec);
    if (!acodec.isEmpty())
        l << codecDisplayName(acodec);
    return l.join(',');
}

QJsonObject Preset::toJson() const
{
    QJsonObject o;
    o["id"] = id; o["category"] = category; o["group"] = group; o["name"] = name;
    o["badge"] = badge; o["ext"] = ext; o["vcodec"] = vcodec; o["acodec"] = acodec;
    o["width"] = width; o["height"] = height; o["vbitrate"] = vbitrate; o["abitrate"] = abitrate;
    o["fps"] = fps; o["custom"] = custom;
    return o;
}

Preset Preset::fromJson(const QJsonObject &o)
{
    Preset p;
    p.id = o["id"].toString(); p.category = o["category"].toString(); p.group = o["group"].toString();
    p.name = o["name"].toString(); p.badge = o["badge"].toString(); p.ext = o["ext"].toString();
    p.vcodec = o["vcodec"].toString(); p.acodec = o["acodec"].toString();
    p.width = o["width"].toInt(); p.height = o["height"].toInt();
    p.vbitrate = o["vbitrate"].toInt(); p.abitrate = o["abitrate"].toInt();
    p.fps = o["fps"].toDouble(); p.custom = o["custom"].toBool();
    return p;
}

PresetStore &PresetStore::instance()
{
    static PresetStore s;
    return s;
}

PresetStore::PresetStore()
{
    QFile f(QStringLiteral(":/presets.json"));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).object().value("presets").toArray();
        for (const QJsonValue &v : arr)
            m_presets << Preset::fromJson(v.toObject());
    }
    loadCustom();
}

QString PresetStore::customFile() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/custom_presets.json");
}

void PresetStore::loadCustom()
{
    QFile f(customFile());
    if (!f.open(QIODevice::ReadOnly))
        return;
    for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array()) {
        Preset p = Preset::fromJson(v.toObject());
        p.custom = true;
        if (p.isValid())
            m_presets << p;
    }
}

void PresetStore::saveCustom() const
{
    QJsonArray arr;
    for (const Preset &p : m_presets)
        if (p.custom)
            arr << p.toJson();
    QFile f(customFile());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson());
}

Preset PresetStore::find(const QString &id) const
{
    for (const Preset &p : m_presets)
        if (p.id == id)
            return p;
    return {};
}

Preset PresetStore::defaultPreset() const
{
    Preset p = find(QStringLiteral("mp4-source"));
    return p.isValid() ? p : (m_presets.isEmpty() ? Preset() : m_presets.first());
}

QStringList PresetStore::groups(const QString &category) const
{
    QStringList g;
    for (const Preset &p : m_presets)
        if (p.category == category && !g.contains(p.group))
            g << p.group;
    return g;
}

QList<Preset> PresetStore::presets(const QString &category, const QString &group) const
{
    QList<Preset> l;
    for (const Preset &p : m_presets)
        if (p.category == category && p.group == group)
            l << p;
    return l;
}

QList<Preset> PresetStore::search(const QString &text) const
{
    QList<Preset> l;
    for (const Preset &p : m_presets)
        if (p.title().contains(text, Qt::CaseInsensitive) || p.ext.contains(text, Qt::CaseInsensitive)
            || p.codecText().contains(text, Qt::CaseInsensitive))
            l << p;
    return l;
}

void PresetStore::addCustom(const Preset &preset)
{
    Preset p = preset;
    p.custom = true;
    for (int i = 0; i < m_presets.size(); ++i) {
        if (m_presets[i].id == p.id) {
            m_presets[i] = p;
            saveCustom();
            return;
        }
    }
    m_presets << p;
    saveCustom();
}

void PresetStore::removeCustom(const QString &id)
{
    for (int i = 0; i < m_presets.size(); ++i) {
        if (m_presets[i].id == id && m_presets[i].custom) {
            m_presets.removeAt(i);
            saveCustom();
            return;
        }
    }
}

QList<Preset> PresetStore::recent() const
{
    QList<Preset> l;
    for (const QString &id : Settings::recentPresets()) {
        const Preset p = find(id);
        if (p.isValid())
            l << p;
    }
    return l;
}

void PresetStore::markUsed(const QString &id)
{
    QStringList r = Settings::recentPresets();
    r.removeAll(id);
    r.prepend(id);
    while (r.size() > 10)
        r.removeLast();
    Settings::setRecentPresets(r);
}
