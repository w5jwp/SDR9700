#include "RepeaterWidget.h"

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

RepeaterWidget::RepeaterWidget(const Buttons& buttons, QWidget* parent) : QGroupBox(parent)
{
    setObjectName(QStringLiteral("RepeaterWidget"));
    setTitle(QStringLiteral("Repeater"));
    setAccessibleName(QStringLiteral("Repeater"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setMinimumHeight(kLowerControlGroupMinHeight);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kControlGroupMargin, kControlGroupMargin + 5, kControlGroupMargin, kControlGroupMargin);
    layout->setSpacing(0);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(kControlGroupSpacing);
    row->addStretch();
    row->addWidget(buttons.offset);
    row->addWidget(buttons.tone);
    row->addStretch();

    layout->addLayout(row);
}
