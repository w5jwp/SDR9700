#include "MuteWidget.h"

#include "UiTheme.h"

#include <QPushButton>
#include <QSize>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace
{
constexpr int kControlGroupMargin = 8;
constexpr int kLowerControlGroupMinHeight = 76;
constexpr QSize kCommandButtonSize(72, UiTheme::Size::ControlButtonHeight);
} // namespace

MuteWidget::MuteWidget(QPushButton* muteButton, QWidget* parent) : QGroupBox(parent)
{
    setTitle(QStringLiteral("Mute"));
    setAccessibleName(QStringLiteral("Mute"));
    setMaximumWidth(kCommandButtonSize.width() + 2 * kControlGroupMargin);
    setMinimumHeight(kLowerControlGroupMinHeight);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kControlGroupMargin, kControlGroupMargin + 5, kControlGroupMargin, kControlGroupMargin);

    if (muteButton)
    {
        muteButton->setFixedSize(kCommandButtonSize);
        layout->addStretch();
        layout->addWidget(muteButton, 0, Qt::AlignHCenter);
        layout->addStretch();
    }
}
