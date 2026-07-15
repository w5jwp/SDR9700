#include "TransmitPanel.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace
{
constexpr int kControlGroupMargin = 5;
constexpr int kControlGroupSpacing = 12;
constexpr int kLowerControlGroupMinHeight = 76;
} // namespace

TransmitPanel::TransmitPanel(const Buttons& buttons, QWidget* parent) : QGroupBox(parent)
{
    setObjectName(QStringLiteral("TransmitPanel"));
    setTitle(QStringLiteral("Transmit"));
    setAccessibleName(QStringLiteral("Transmit"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setMinimumHeight(kLowerControlGroupMinHeight);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kControlGroupMargin, kControlGroupMargin + 5, kControlGroupMargin, kControlGroupMargin);
    layout->setSpacing(0);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(kControlGroupSpacing);
    row->addStretch();
    row->addWidget(buttons.compressor);
    row->addWidget(buttons.lanMod);
    row->addStretch();

    layout->addLayout(row);
}
