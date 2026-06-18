#include "AboutDialog.h"
#include "AppInfo.h"

#include <QLabel>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

static QPixmap roundedPixmap(const QPixmap& src, int radius)
{
    QPixmap result(src.size());
    result.fill(Qt::transparent);
    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(src.rect()), radius, radius);
    p.setClipPath(path);
    p.drawPixmap(0, 0, src);
    return result;
}

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QString("About %1").arg(APP_NAME));
    setFixedSize(380, 300);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(8);

    auto* iconLabel = new QLabel;
    iconLabel->setAlignment(Qt::AlignCenter);
    QPixmap src(":/images/icons/sdr9700_app_icon.png");
    QPixmap scaled = src.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    iconLabel->setPixmap(roundedPixmap(scaled, 14));
    root->addWidget(iconLabel);

    auto* appLabel = new QLabel(QString("<b style='font-size:18px'>%1</b>").arg(APP_NAME));
    appLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(appLabel);

    auto* verLabel = new QLabel(QString("Version %1").arg(APP_VERSION));
    verLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(verLabel);

    root->addSpacing(8);

    auto* desc = new QLabel("Linux LAN client for the Icom IC-9700\n"
                            "VHF/UHF/SHF transceiver.\n\n"
                            "Built with Qt6 and the IC-9700 UDP remote protocol.");
    desc->setAlignment(Qt::AlignCenter);
    desc->setWordWrap(true);
    root->addWidget(desc);

    root->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    root->addWidget(buttons);
}
