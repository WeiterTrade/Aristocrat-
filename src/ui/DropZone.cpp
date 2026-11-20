#include "DropZone.h"
#include "core/I18n.h"
#include "Widgets.h"

#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>

DropZone::DropZone(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("DropZone"));
    setFixedSize(500, 330);
    setCursor(Qt::PointingHandCursor);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(30, 50, 30, 30);
    lay->setSpacing(26);

    auto *img = new QLabel(this);
    Ui::svgLabel(img, QStringLiteral(":/icons/drop.svg"), QSize(150, 150));
    img->setAlignment(Qt::AlignCenter);

    auto *text = new QLabel(T("Добавьте или перетащите сюда файлы, чтобы начать конвертацию"), this);
    text->setAlignment(Qt::AlignCenter);
    text->setWordWrap(true);

    lay->addWidget(img, 1);
    lay->addWidget(text);
}

void DropZone::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        emit clicked();
}
