#pragma once

#include <QColor>
#include <QString>

namespace UiTheme
{
namespace Color
{
inline constexpr const char* Accent = "#00b4d8";
inline constexpr const char* AccentDark = "#0d4f68";
inline constexpr const char* AccentHover = "#11617d";
inline constexpr const char* AccentBright = "#38d8ff";

inline constexpr const char* Panel = "#1d1f24";
inline constexpr const char* PanelDark = "#1a1e24";
inline constexpr const char* WindowChrome = "#1e1e1e";
inline constexpr const char* ContentBackground = "#1d2329";
inline constexpr const char* MenuBar = "#242b33";
inline constexpr const char* MenuPanel = "#20242b";
inline constexpr const char* Field = "#10151c";
inline constexpr const char* MeterTrough = "#0f141a";

inline constexpr const char* Border = "#333943";
inline constexpr const char* BorderMedium = "#404854";
inline constexpr const char* BorderLight = "#4a4e58";
inline constexpr const char* BorderFocus = "#586476";
inline constexpr const char* Separator = "#304050";
inline constexpr const char* StatusBorder = "#2a303a";
inline constexpr const char* ControlStripBorder = "#303640";

inline constexpr const char* TextPrimary = "#e0e6ec";
inline constexpr const char* TextBright = "#e8f8ff";
inline constexpr const char* TextField = "#f0f5f8";
inline constexpr const char* TextMuted = "#aebdcc";
inline constexpr const char* TextStatusPrimary = "#c8d8e8";
inline constexpr const char* TextStatusSecondary = "#8ea8c0";
inline constexpr const char* TextStatusLabel = "#506070";

inline constexpr const char* Button = "#30333a";
inline constexpr const char* ButtonHover = "#3b3f48";
inline constexpr const char* ButtonHoverBorder = "#6a7080";

inline constexpr const char* PttButton = "#343039";
inline constexpr const char* PttBorder = "#6a4e58";
inline constexpr const char* PttHover = "#463944";
inline constexpr const char* PttHoverBorder = "#8a6870";
inline constexpr const char* PttActive = "#cc2200";
inline constexpr const char* PttActiveBorder = "#ff6644";

inline constexpr const char* Success = "#4dd87a";
inline constexpr const char* Warning = "#ffb84d";
inline constexpr const char* Danger = "#ff4d4d";
inline constexpr const char* White = "#ffffff";

inline constexpr QColor MeterFrame{0x3f, 0x47, 0x52};
inline constexpr QColor MeterTroughQColor{0x0f, 0x14, 0x1a};
inline constexpr QColor MeterBlue{0x1f, 0x76, 0xff};
inline constexpr QColor MeterCyan{0x00, 0xb4, 0xd8};
inline constexpr QColor MeterGreen{0x4d, 0xd8, 0x7a};
inline constexpr QColor MeterAmber{0xff, 0xb8, 0x4d};
inline constexpr QColor MeterRed{0xff, 0x4d, 0x4d};
inline constexpr QColor MeterScaleText{0x7f, 0xa4, 0xc8};
} // namespace Color

namespace Size
{
inline constexpr int DialogContentMargin = 12;
inline constexpr int VfoControlHeight = 46;
inline constexpr int ControlSliderHeight = 24;
inline constexpr int InlineSliderBlockHeight = 42;
inline constexpr int ControlButtonHeight = 40;
inline constexpr int MainWindowMinWidth = 1160;
// The legacy command strip was 263 px taller than its original replacement
// gap. Give 100 px each to the panadapter and waterfall, retain 10 additional
// pixels above the VFO block, and remove the unused 53 px.
inline constexpr int MainWindowMinHeight = 917;
inline constexpr int StatusNetworkWidth = 78;
inline constexpr int StatusTxWidth = 80;
inline constexpr int StatusClockWidth = 92;
inline constexpr int StatusSeparatorWidth = 22;
} // namespace Size

inline QString tableStyle(const QString& objectName)
{
    return QStringLiteral(
               "QTableWidget#%1 { background: %2; border: 1px solid %3; color: %4; gridline-color: %3; outline: 0; }"
               "QTableWidget#%1::item { padding: 4px 8px; border: none; }"
               "QTableWidget#%1::item:alternate { background: %5; }"
               "QTableWidget#%1::item:selected { background: %6; color: %7; }"
               "QHeaderView::section { background: %8; border: 0; border-right: 1px solid %3; "
               "border-bottom: 1px solid %3; color: %9; font-weight: bold; padding: 5px 8px; }")
        .arg(objectName, QLatin1String(Color::Field), QLatin1String(Color::Border), QLatin1String(Color::TextField),
             QLatin1String(Color::PanelDark), QLatin1String(Color::AccentDark), QLatin1String(Color::TextBright),
             QLatin1String(Color::MenuBar), QLatin1String(Color::TextStatusSecondary));
}
} // namespace UiTheme
