#pragma once

#include "UiTheme.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

namespace sdr9700::ui
{
inline constexpr int kDialogFooterSpacing = 12;

struct DialogFooter
{
    QWidget* widget{nullptr};
    QHBoxLayout* rowLayout{nullptr};
    QDialogButtonBox* buttonBox{nullptr};
};

inline DialogFooter createDialogFooter(QWidget* parent)
{
    auto* container = new QWidget(parent);
    auto* footerLayout = new QVBoxLayout(container);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(0);

    auto* separator = new QWidget(container);
    separator->setObjectName(QStringLiteral("dialogFooterSeparator"));
    separator->setFixedHeight(1);
    separator->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    separator->setStyleSheet(
        QStringLiteral("QWidget { background: %1; }").arg(QLatin1String(UiTheme::Color::BorderMedium)));
    footerLayout->addWidget(separator);

    auto* buttonRow = new QWidget(container);
    buttonRow->setObjectName(QStringLiteral("dialogFooterRow"));
    auto* rowLayout = new QHBoxLayout(buttonRow);
    rowLayout->setContentsMargins(0, kDialogFooterSpacing, 0, kDialogFooterSpacing);
    auto* buttonBox = new QDialogButtonBox(buttonRow);
    buttonBox->setObjectName(QStringLiteral("dialogButtonBox"));
    rowLayout->addWidget(buttonBox, 1);
    footerLayout->addWidget(buttonRow);

    return {container, rowLayout, buttonBox};
}
} // namespace sdr9700::ui
