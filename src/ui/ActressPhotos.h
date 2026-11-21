#pragma once
#include "Frameless.h"

#include <QImage>
#include <QList>
#include <QMap>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;
class QWebEngineView;

// Фото актрис для медиатеки: 1000×1500 (2:3), предпочтительно в белье, без наготы.
// Порядок: выбранное раньше → своя медиатека (.actors в папках релизов) → поиск картинок (Bing, безопасный поиск).
namespace ActressPhotos {
struct Candidate {
    QString url, thumb;
    int w = 0, h = 0;
};
QImage fitPortrait(const QImage &src);                       // обрезка до 2:3 (голова сохраняется) → 1000×1500
QString cacheFile(const QString &name);                      // выбранное фото (общее для всех релизов)
QString findInLibrary(const QString &name, const QString &baseDir);
QString query(const QString &name);                          // «Имя Фамилия lingerie»
int score(const Candidate &c);                               // пригодность для автовыбора (0 — не подходит)
bool saveChosen(const QString &name, const QByteArray &data);   // подогнать и сохранить в кэш
}

// Поиск картинок в скрытом окне (Bing: вертикальные, от 1000×1500, безопасный поиск «умеренный»)
class ImageSearch : public QObject
{
public:
    using Callback = std::function<void(const QList<ActressPhotos::Candidate> &)>;
    explicit ImageSearch(QObject *parent = nullptr);
    ~ImageSearch() override;
    void search(const QString &query, Callback cb);
    // Скачать картинку, открыв её в скрытом окне: ошибка (403/404) видна сразу, без долгого ожидания
    void fetchImage(const QUrl &url, std::function<void(const QByteArray &)> cb);

private:
    void extract(int attempt);
    void grab(int attempt);
    std::function<void(const QByteArray &)> m_imgCb;
    bool m_imageMode = false;
    QWebEngineView *m_view;
    QTimer m_timeout;
    Callback m_cb;
    int m_serial = 0;
};

// Окно выбора фото: слева актрисы, справа найденные варианты
class ActressPhotoDialog : public FramedDialog
{
public:
    ActressPhotoDialog(const QStringList &names, const QString &baseDir, QWidget *parent = nullptr);

private:
    void selectActress(int row);
    void runSearch();
    void choose(int index);
    void refreshLeft(int row);

    QStringList m_names;
    QString m_base;
    QListWidget *m_left, *m_grid;
    QLineEdit *m_query;
    QLabel *m_state;
    ImageSearch *m_search;
    QList<ActressPhotos::Candidate> m_cands;
};
