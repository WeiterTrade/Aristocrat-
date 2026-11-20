#pragma once
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

struct Preset {
    QString id;
    QString category;   // video | audio | device
    QString group;      // MP4, HEVC MP4, MP3, Apple…
    QString name;       // "1080P", "Как в исходнике"…
    QString badge;      // короткая подпись на значке
    QString ext;        // расширение файла
    QString vcodec;     // пусто для аудио
    QString acodec;
    int width = 0;      // 0 = как в исходнике
    int height = 0;
    int vbitrate = 0;   // кбит/с, 0 = автоматически (по качеству)
    int abitrate = 0;   // кбит/с, 0 = без сжатия / по умолчанию
    double fps = 0.0;   // 0 = как в исходнике
    bool custom = false;

    bool isValid() const { return !id.isEmpty(); }
    bool isAudioOnly() const { return vcodec.isEmpty(); }
    QString title() const;          // "MP4 · 1080P"
    QString resolutionText() const; // "1920*1080" или "Исходное разрешение"
    QString codecText() const;      // "H264,AAC"

    QJsonObject toJson() const;
    static Preset fromJson(const QJsonObject &o);
};

class PresetStore
{
public:
    static PresetStore &instance();

    const QList<Preset> &all() const { return m_presets; }
    Preset find(const QString &id) const;
    Preset defaultPreset() const;

    QStringList groups(const QString &category) const;
    QList<Preset> presets(const QString &category, const QString &group) const;
    QList<Preset> search(const QString &text) const;

    void addCustom(const Preset &p);
    void removeCustom(const QString &id);

    QList<Preset> recent() const;
    void markUsed(const QString &id);

private:
    PresetStore();
    void loadCustom();
    void saveCustom() const;
    QString customFile() const;

    QList<Preset> m_presets;
};

QString codecDisplayName(const QString &ffmpegCodec);
