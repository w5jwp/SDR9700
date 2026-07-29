// cppcheck-suppress-file unusedFunction
#pragma once

#include "AppSettings.h"
#include "SpectrumScopeDisplay.h"
#include "MemoryStore.h"
#include "RadioCapabilities.h"
#include "UiTheme.h"

#include <QColor>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QStyleOptionButton>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <functional>

namespace sdr9700::ui::main_window
{
constexpr auto kSpectrumScopeInvertMouseWheelSettingsKey = "spectrumScopeInvertMouseWheel";
constexpr auto kTuningStepHZSettingsKey = "tuningStepHZ";
constexpr auto kSpectrumScopeSpanHZSettingsKey = "spectrumScopeSpanHZ";
constexpr auto kSpectrumScopeCenterLineColorSettingsKey = "spectrumScopeCenterLineColor";
constexpr auto kSpectrumScopeBackgroundColorSettingsKey = "spectrumScopeBackgroundColor";
constexpr auto kSpectrumScopeGridLineColorSettingsKey = "spectrumScopeGridLineColor";
constexpr auto kSpectrumScopeGridDensitySettingsKey = "spectrumScopeGridDensity";
constexpr int kDefaultTuningStepHZ = 100;
constexpr quint64 kDefaultSpectrumScopeSpanHZ = 500000;
const QColor kDefaultSpectrumScopeCenterLineColor(0xf5, 0xf7, 0xf8);
const QColor kDefaultSpectrumScopeBackgroundColor(0x0b, 0x3f, 0x55);
const QColor kDefaultSpectrumScopeGridLineColor(0xc8, 0xf1, 0xf5);
constexpr int kDefaultSpectrumScopeGridDensity = 1;

struct StepPreset
{
    int hz;
    const char* label;
    int radioStep;
};

inline constexpr StepPreset kStepPresets[] = {
    {1, "1 Hz", 0},     {10, "10 Hz", 0},     {100, "100 Hz", 1},   {500, "500 Hz", 2},    {1000, "1 kHz", 3},
    {5000, "5 kHz", 4}, {10000, "10 kHz", 6}, {25000, "25 kHz", 9}, {50000, "50 kHz", 10}, {100000, "100 kHz", 11},
};

struct SpectrumScopeSpanPreset
{
    quint64 hz;
    const char* label;
};

inline constexpr SpectrumScopeSpanPreset kSpectrumScopeSpanPresets[] = {
    {2500, "2.5 kHz"}, {5000, "5 kHz"},     {10000, "10 kHz"},   {25000, "25 kHz"},
    {50000, "50 kHz"}, {100000, "100 kHz"}, {250000, "250 kHz"}, {500000, "500 kHz"},
};

constexpr quint64 kMinimumTuneFrequencyHz = 100000;
constexpr int kSpectrumScopeTuneCommitDelayMs = 70;
constexpr int kSpectrumScopeTuneReleaseDelayMs = 650;
constexpr quint64 kSpectrumScopeFixedPanMinDeltaHz = 1000;
constexpr int kMemoryOffsetCustom = -1;
constexpr auto kNoActiveMemoryLabel = "-";
constexpr auto kMemoryPollIntervalSecondsSettingsKey = "memoryPollIntervalSeconds";
constexpr int kDefaultMemoryPollIntervalSeconds = 600;
constexpr int kMemoryPollIntervalMinSeconds = 30;
constexpr int kMemoryPollIntervalMaxSeconds = 3600;
constexpr int kMemoryTableColumnCount = 9;
constexpr int kMemoryBandColumn = 0;
constexpr int kMemoryNumberColumn = 1;
constexpr int kMemoryNameColumn = 2;
constexpr int kMemoryFrequencyColumn = 3;
constexpr int kMemoryDuplexColumn = 4;
constexpr int kMemoryModeColumn = 5;
constexpr int kMemoryToneColumn = 6;
constexpr int kMemoryFilterColumn = 7;
constexpr int kMemoryIdColumn = 8;
constexpr QSize kMemoryWindowSize(980, 620);
constexpr int kMemoryBandColumnWidth = 70;
constexpr int kMemoryNumberColumnWidth = 76;
constexpr int kMemoryNameColumnWidth = 185;
constexpr int kMemoryFrequencyColumnWidth = 116;
constexpr int kMemoryModeColumnWidth = 66;
constexpr int kMemorySmallColumnWidth = 76;
constexpr int kMemoryDuplexColumnWidth = 96;
constexpr int kMemoryToneColumnWidth = 220;
constexpr QMargins kNoMargins(0, 0, 0, 0);
constexpr int kNoSpacing = 0;
constexpr QMargins kMemoryPanelMargins(8, 8, 8, 8);
constexpr QMargins kMemoryToolbarGroupMargins(8, 6, 8, 6);
constexpr int kMemoryPanelSpacing = 6;
constexpr int kMemoryToolbarSpacing = 8;
constexpr int kMemoryToolbarGroupSpacing = 6;
constexpr QMargins kControlStripMargins(20, 20, 20, 20);
constexpr int kControlRowSpacing = 8;
constexpr int kControlGroupMargin = 8;
constexpr int kControlGroupSpacing = 6;
constexpr QSize kCommandButtonSize(72, UiTheme::Size::ControlButtonHeight);
constexpr QSize kSelectorButtonSize(72, UiTheme::Size::ControlButtonHeight);

struct OffsetPreset
{
    QString label;
    duplexMode_t mode;
    quint64 hz{0};
};

struct TonePreset
{
    ushort tone;
    const char* label;
};

inline constexpr TonePreset kTonePresets[] = {
    {670, "67.0"},   {693, "69.3"},   {719, "71.9"},   {744, "74.4"},   {770, "77.0"},   {797, "79.7"},
    {825, "82.5"},   {854, "85.4"},   {885, "88.5"},   {915, "91.5"},   {948, "94.8"},   {974, "97.4"},
    {1000, "100.0"}, {1035, "103.5"}, {1072, "107.2"}, {1109, "110.9"}, {1148, "114.8"}, {1188, "118.8"},
    {1230, "123.0"}, {1273, "127.3"}, {1318, "131.8"}, {1365, "136.5"}, {1413, "141.3"}, {1462, "146.2"},
    {1514, "151.4"}, {1567, "156.7"}, {1598, "159.8"}, {1622, "162.2"}, {1655, "165.5"}, {1679, "167.9"},
    {1738, "173.8"}, {1773, "177.3"}, {1799, "179.9"}, {1835, "183.5"}, {1862, "186.2"}, {1928, "192.8"},
    {1966, "196.6"}, {1995, "199.5"}, {2035, "203.5"}, {2065, "206.5"}, {2107, "210.7"}, {2181, "218.1"},
    {2257, "225.7"}, {2291, "229.1"}, {2336, "233.6"}, {2418, "241.8"}, {2503, "250.3"}, {2541, "254.1"},
};

inline constexpr ushort kDtcsCodes[] = {
    23,  25,  26,  31,  32,  36,  43,  47,  51,  53,  54,  65,  71,  72,  73,  74,  114, 115, 116, 122, 125,
    131, 132, 134, 143, 145, 152, 155, 156, 162, 165, 172, 174, 205, 212, 223, 225, 226, 243, 244, 245, 246,
    251, 252, 255, 261, 263, 265, 266, 271, 274, 306, 311, 315, 325, 331, 332, 343, 346, 351, 356, 364, 365,
    371, 411, 412, 413, 423, 431, 432, 445, 446, 452, 454, 455, 462, 464, 465, 466, 503, 506, 516, 523, 526,
    532, 546, 565, 606, 612, 624, 627, 631, 632, 654, 662, 664, 703, 712, 723, 731, 732, 734, 746, 754,
};

class ClickableStatusPanel : public QWidget
{
  public:
    explicit ClickableStatusPanel(QWidget* parent = nullptr) : QWidget(parent) {}

    std::function<void()> onClicked;

  protected:
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QWidget::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()) && onClicked)
        {
            onClicked();
        }
    }
};

class TwoLineButton : public QPushButton
{
  public:
    explicit TwoLineButton(QWidget* parent = nullptr) : QPushButton(parent) { setCursor(Qt::PointingHandCursor); }

    void setLines(const QString& primary, const QString& secondary)
    {
        m_primary = primary;
        m_secondary = secondary;
        setText(QString());
        setAccessibleName(QString("%1 %2").arg(primary, secondary));
        update();
    }

    void setSecondary(const QString& secondary)
    {
        m_secondary = secondary;
        update();
    }

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    QString m_primary;
    QString m_secondary;
};

void setSelectorButtonLines(QPushButton* button, const QString& primary, const QString& secondary);
void setCommandButtonActive(QPushButton* button, bool active);
void styleCompactMenu(QMenu* menu);
QString statusLabelStyle(const char* color, bool bold = false);
QColor colorSetting(const char* key, const QColor& defaultColor);
int spectrumScopeGridDensitySetting();
bool availableScreenContains(const QRect& rect);
QRect availableGeometryFor(const QRect& rect);
QRect centeredRectInAvailableGeometry(QSize size, const QRect& available);
int appVolumeSettingValue();
QString bandLabelForHz(quint64 hz);
int vfoBandIndexForHz(quint64 hz);
int radioTuningStepForHz(int hz);
QString preampLevelLabel(int level);
bool agcPresetSelectableForMode(const QString& mode);
QString agcDisplayMode(const QString& radioMode, const QString& reportedAgcMode);
QString toneFrequencyLabel(ushort tone);
QString dtcsCodeLabel(ushort code);
QString toneOptionLabel(rptAccessTxRx_t mode);
QString memoryToneFrequencyLabel(rptAccessTxRx_t mode, ushort value);
QString memoryModeLabel(int mode);
QString memoryFrequencyLabel(quint64 hz);
QVector<OffsetPreset> offsetPresetsForHz(quint64 hz);
QString formatFrequency(quint64 hz);
QString formatOffsetMhz(quint64 hz);
QString offsetModeLabel(duplexMode_t mode, quint64 offsetHz);
bool parseFrequencyText(const QString& input, quint64* hz);
} // namespace sdr9700::ui::main_window
