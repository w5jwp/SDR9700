#include "PttPanel.h"

#include "UiTheme.h"

#include <QPushButton>
#include <QSize>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace
{
constexpr int kControlGroupMargin = 8;
constexpr QSize kCommandButtonSize(72, UiTheme::Size::ControlButtonHeight);
constexpr int kReceiveButtonStackSpacing = 8;
constexpr QSize kPttButtonSize(kCommandButtonSize.width(),
                               UiTheme::Size::ControlButtonHeight * 2 + kReceiveButtonStackSpacing);
} // namespace

PttPanel::PttPanel(QPushButton* pttButton, QPushButton* dtmfButton, QWidget* parent) : QGroupBox(parent)
{
    setTitle(QStringLiteral("Transmit"));
    setAccessibleName(QStringLiteral("Transmit"));
    setMaximumWidth(kCommandButtonSize.width() + 2 * kControlGroupMargin);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kControlGroupMargin, 7, kControlGroupMargin, 2);

    if (pttButton)
    {
        pttButton->setFixedSize(kPttButtonSize);
        pttButton->setProperty("pttButton", true);
        layout->addStretch();
        layout->addWidget(pttButton, 0, Qt::AlignHCenter);

        if (dtmfButton)
        {
            dtmfButton->setFixedSize(kCommandButtonSize);
            layout->addSpacing(6);
            layout->addWidget(dtmfButton, 0, Qt::AlignHCenter);
        }

        layout->addStretch();
    }
}
