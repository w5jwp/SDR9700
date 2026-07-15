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
constexpr int kTransmitButtonTopSpacing = 18;
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
        pttButton->setStyleSheet(QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: "
                                                "3px; color: %3; font-weight: bold; }"
                                                "QPushButton:hover { background: %4; border-color: %5; }"
                                                "QPushButton:pressed, QPushButton[pttActive=\"true\"] { background: "
                                                "%6; border-color: %7; color: %8; font-weight: bold; }")
                                     .arg(UiTheme::Color::PttButton, UiTheme::Color::PttBorder,
                                          UiTheme::Color::TextField, UiTheme::Color::PttHover,
                                          UiTheme::Color::PttHoverBorder, UiTheme::Color::PttActive,
                                          UiTheme::Color::PttActiveBorder, UiTheme::Color::White));
        layout->addSpacing(kTransmitButtonTopSpacing);
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
