#include "ReceivePanel.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace
{
constexpr int kControlGroupMargin = 5;
constexpr int kControlGroupSpacing = 5;
} // namespace

ReceivePanel::ReceivePanel(const Buttons& buttons, QWidget* parent) : QGroupBox(parent)
{
    setObjectName(QStringLiteral("ControlPanel"));
    setTitle(QStringLiteral("Control"));
    setAccessibleName(QStringLiteral("Control"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kControlGroupMargin, kControlGroupMargin + 2, kControlGroupMargin, 2);
    layout->setSpacing(kControlGroupSpacing);

    auto* gridBox = new QWidget(this);
    gridBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* grid = new QGridLayout(gridBox);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(kControlGroupSpacing);
    auto* buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(0);
    buttonRow->addStretch();
    buttonRow->addWidget(gridBox, 0, Qt::AlignTop);
    buttonRow->addStretch();

    layout->addStretch();
    layout->addLayout(buttonRow);
    layout->addStretch();

    const auto addButton = [grid](QPushButton* button, int row, int column)
    {
        if (button)
        {
            grid->addWidget(button, row, column);
        }
    };
    if (!buttons.offset && !buttons.tone)
    {
        addButton(buttons.compressor, 0, 0);
        addButton(buttons.rit, 0, 1);
        addButton(buttons.xfc, 0, 2);
    }
    else
    {
        addButton(buttons.compressor, 0, 0);
        addButton(buttons.offset, 0, 1);
        addButton(buttons.rit, 0, 2);
        addButton(buttons.tone, 1, 0);
        addButton(buttons.xfc, 1, 1);
    }
}
