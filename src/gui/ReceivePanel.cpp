#include "ReceivePanel.h"

#include <QGridLayout>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace
{
constexpr int kControlGroupMargin = 5;
constexpr int kControlGroupSpacing = 8;
} // namespace

ReceivePanel::ReceivePanel(const Buttons& buttons, QWidget* parent) : QGroupBox(parent)
{
    setObjectName(QStringLiteral("ReceivePanel"));
    setTitle(QStringLiteral("Receive"));
    setAccessibleName(QStringLiteral("Receive"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kControlGroupMargin, kControlGroupMargin + 2, kControlGroupMargin, 2);
    layout->setSpacing(kControlGroupSpacing);

    auto* gridBox = new QWidget(this);
    gridBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* grid = new QGridLayout(gridBox);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(kControlGroupSpacing);
    layout->addStretch();
    layout->addWidget(gridBox, 0, Qt::AlignHCenter);
    layout->addStretch();

    grid->addWidget(buttons.agc, 0, 0);
    grid->addWidget(buttons.attenuator, 0, 1);
    grid->addWidget(buttons.noiseBlanker, 0, 2);
    grid->addWidget(buttons.notch, 0, 3);
    grid->addWidget(buttons.noiseReduction, 1, 0);
    grid->addWidget(buttons.preamp, 1, 1);
    grid->addWidget(buttons.rfGain, 1, 2);
    grid->addWidget(buttons.rit, 1, 3);
}
