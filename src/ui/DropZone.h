#pragma once
#include <QFrame>
#include <QStringList>

// Пунктирная область «Добавьте или перетащите файлы».
class DropZone : public QFrame
{
    Q_OBJECT
public:
    explicit DropZone(QWidget *parent = nullptr);

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *e) override;
};
