#pragma once

#include "UiTheme.h"

#include <QLatin1String>
#include <QString>

namespace sdr9700::ui
{
inline QString settingsGroupBoxStyle()
{
    return QStringLiteral("QGroupBox { color: %1; border: 1px solid %2; border-radius: 3px; margin-top: 10px; }"
                          "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; "
                          "padding: 0 4px; left: 8px; }")
        .arg(QLatin1String(UiTheme::Color::TextPrimary), QLatin1String(UiTheme::Color::BorderMedium));
}
} // namespace sdr9700::ui
