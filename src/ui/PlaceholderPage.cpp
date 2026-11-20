#include "PlaceholderPage.h"
#include "Widgets.h"

#include <QLabel>
#include <QVBoxLayout>

PlaceholderPage::PlaceholderPage(const QString &icon, const QString &title, const QString &text, QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->addStretch();
    auto *img = new QLabel(this);
    img->setPixmap(Ui::icon(icon, true).pixmap(96, 96));
    img->setAlignment(Qt::AlignCenter);
    auto *t = new QLabel(title, this);
    t->setObjectName(QStringLiteral("PlaceholderTitle"));
    t->setAlignment(Qt::AlignCenter);
    auto *d = new QLabel(text, this);
    d->setObjectName(QStringLiteral("Muted"));
    d->setAlignment(Qt::AlignCenter);
    d->setWordWrap(true);
    d->setMaximumWidth(560);
    lay->addWidget(img);
    lay->addSpacing(12);
    lay->addWidget(t);
    lay->addWidget(d, 0, Qt::AlignHCenter);
    lay->addStretch();
}
