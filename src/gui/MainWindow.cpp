#include "MainWindow.h"
#include "BandscopeDisplay.h"
#include "RadioChooserDialog.h"
#include "SettingsDialog.h"
#include "AboutDialog.h"
#include "DialogPlacement.h"
#include "DtmfDialog.h"
#include "FramelessTitleBar.h"
#include "MainTitleBar.h"
#include "MemoryPanel.h"
#include "MetersDialog.h"
#include "PttPanel.h"
#include "ReceivePanel.h"
#include "RepeaterPanel.h"
#include "TransmitPanel.h"
#include "VfoPanel.h"
#include "UiTheme.h"
#include "MemoryStore.h"
#include "AppBuildConfig.h"
#include "AppInfo.h"
#include "AppSettings.h"
#include "LogCategories.h"
#include "RadioCapabilities.h"
#include "backend/IRadioBackend.h"
#ifdef HAVE_HIDAPI
#include "core/IcomRC28Manager.h"
#endif
#include "models/RadioModel.h"
#include "models/VfoModel.h"
#include "models/BandscopeModel.h"

#include <QToolBar>
#include <QAction>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSlider>
#include <QSpinBox>
#include <QPushButton>
#include <QStatusBar>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QColor>
#include <QFormLayout>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QMessageBox>
#include <QCloseEvent>
#include <QEvent>
#include <QFont>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QScreen>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTableWidget>
#include <QStringList>
#include <QTimer>
#include <QDateTime>
#include <QVector>
#include <QWidgetAction>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>
#include <functional>
#include <iterator>
#include <numeric>

namespace
{
QString commandButtonStyle(bool active)
{
    return active ? QStringLiteral("QPushButton { background: %1; border: 1px solid %2; "
                                   "border-radius: 3px; color: %3; font-weight: bold; }"
                                   "QPushButton:hover { background: %4; border-color: %5; }")
                        .arg(UiTheme::Color::AccentDark, UiTheme::Color::Accent, UiTheme::Color::TextBright,
                             UiTheme::Color::AccentHover, UiTheme::Color::AccentBright)
                  : QStringLiteral("QPushButton { background: %1; border: 1px solid %2; "
                                   "border-radius: 3px; color: %3; font-weight: bold; }"
                                   "QPushButton:hover { background: %4; border-color: %5; }")
                        .arg(UiTheme::Color::Button, UiTheme::Color::BorderLight, UiTheme::Color::TextPrimary,
                             UiTheme::Color::ButtonHover, UiTheme::Color::ButtonHoverBorder);
}

QString levelButtonStyle(bool active)
{
    return active ? QStringLiteral("QPushButton { background: %1; border: 1px solid %2; "
                                   "border-radius: 3px; color: %3; font-weight: bold; }"
                                   "QPushButton:hover { background: %4; border-color: %5; }")
                        .arg(UiTheme::Color::Field, UiTheme::Color::Accent, UiTheme::Color::TextBright,
                             UiTheme::Color::ButtonHover, UiTheme::Color::AccentBright)
                  : commandButtonStyle(false);
}

constexpr auto kCloseMemoryWindowOnSelectSettingsKey = "CloseMemoryWindowOnSelect";
constexpr auto kReverseMouseWheelTuningSettingsKey = "ReverseMouseWheelTuning";
constexpr auto kTuningStepHzSettingsKey = "TuningStepHz";
constexpr auto kBandscopeSpanHzSettingsKey = "BandscopeSpanHz";
constexpr auto kBandscopeCenterLineColorSettingsKey = "BandscopeCenterLineColor";
constexpr auto kBandscopeBackgroundColorSettingsKey = "BandscopeBackgroundColor";
constexpr auto kBandscopeGridLineColorSettingsKey = "BandscopeGridLineColor";
constexpr auto kBandscopeGridDensitySettingsKey = "BandscopeGridDensity";
constexpr int kDefaultTuningStepHz = 100;
constexpr quint64 kDefaultBandscopeSpanHz = 500000;
const QColor kDefaultBandscopeCenterLineColor(0xf5, 0xf7, 0xf8);
const QColor kDefaultBandscopeBackgroundColor(0x0b, 0x3f, 0x55);
const QColor kDefaultBandscopeGridLineColor(0xc8, 0xf1, 0xf5);
constexpr int kDefaultBandscopeGridDensity = 1;

struct StepPreset
{
    int hz;
    const char* label;
    int radioStep;
};
constexpr StepPreset kStepPresets[] = {
    {1, "1 Hz", 0},     {10, "10 Hz", 0},     {100, "100 Hz", 1},   {500, "500 Hz", 2},    {1000, "1 kHz", 3},
    {5000, "5 kHz", 4}, {10000, "10 kHz", 6}, {25000, "25 kHz", 9}, {50000, "50 kHz", 10}, {100000, "100 kHz", 11},
};

struct BandscopeSpanPreset
{
    quint64 hz;
    const char* label;
};

constexpr BandscopeSpanPreset kBandscopeSpanPresets[] = {
    {2500, "2.5 kHz"}, {5000, "5 kHz"},     {10000, "10 kHz"},   {25000, "25 kHz"},
    {50000, "50 kHz"}, {100000, "100 kHz"}, {250000, "250 kHz"}, {500000, "500 kHz"},
};

constexpr quint64 kMinimumTuneFrequencyHz = 100000;
constexpr int kBandscopeTuneCommitDelayMs = 70;
constexpr int kBandscopeTuneReleaseDelayMs = 650;
constexpr quint64 kBandscopeFixedPanMinDeltaHz = 1000;
constexpr int kMemoryOffsetCustom = -1;
constexpr auto kNoActiveMemoryLabel = "-";
constexpr int kMemoryTableColumnCount = 8;
constexpr int kMemoryNumberColumn = 0;
constexpr int kMemoryNameColumn = 1;
constexpr int kMemoryFrequencyColumn = 2;
constexpr int kMemoryShiftColumn = 3;
constexpr int kMemoryToneColumn = 4;
constexpr int kMemoryNotesColumn = 5;
constexpr int kMemoryBandKeyColumn = 6;
constexpr int kMemoryIdColumn = 7;
constexpr QSize kMemoryWindowSize(980, 420);
constexpr int kMemoryNumberColumnWidth = 44;
constexpr int kMemoryNameColumnWidth = 220;
constexpr int kMemoryFrequencyColumnWidth = 125;
constexpr int kMemoryShiftColumnWidth = 95;
constexpr int kMemoryToneColumnWidth = 120;
constexpr QMargins kNoMargins(0, 0, 0, 0);
constexpr int kNoSpacing = 0;
constexpr QMargins kMemoryPanelMargins(8, 8, 8, 8);
constexpr QMargins kMemoryToolbarGroupMargins(8, 6, 8, 6);
constexpr int kMemoryPanelSpacing = 6;
constexpr int kMemoryToolbarSpacing = 8;
constexpr int kMemoryToolbarGroupSpacing = 6;
constexpr QMargins kControlStripMargins(8, 14, 8, 16);
constexpr int kControlRowSpacing = 8;
constexpr int kControlGroupMargin = 8;
constexpr int kControlGroupSpacing = 6;
constexpr int kBandscopeSpectrumHeightIncrease = 20;
constexpr int kBandscopeSpectrumHeight760Increase = 40;
constexpr QSize kCommandButtonSize(72, UiTheme::Size::ControlButtonHeight);
constexpr QSize kSelectorButtonSize(72, UiTheme::Size::ControlButtonHeight);

void applyTitleCloseOnlyFlags(QWidget* widget)
{
    widget->setWindowFlags((widget->windowFlags() | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                            Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint) &
                           ~Qt::WindowMinimizeButtonHint & ~Qt::WindowMaximizeButtonHint &
                           ~Qt::WindowMinMaxButtonsHint);
}

QString statusLabelStyle(const char* color, bool bold = false)
{
    return QString("QLabel { color: %1; font-size: 12px;%2 }")
        .arg(QString::fromLatin1(color), bold ? QStringLiteral(" font-weight: bold;") : QString());
}

QColor colorSetting(const char* key, const QColor& defaultColor)
{
    const QColor color(
        AppSettings::instance().value(QString::fromLatin1(key), defaultColor.name(QColor::HexRgb)).toString());
    return color.isValid() ? color : defaultColor;
}

int bandscopeGridDensitySetting()
{
    return qBound(0,
                  AppSettings::instance()
                      .value(QString::fromLatin1(kBandscopeGridDensitySettingsKey), kDefaultBandscopeGridDensity)
                      .toInt(),
                  2);
}

bool availableScreenContains(const QRect& rect)
{
    const QList<QScreen*> screens = QGuiApplication::screens();
    return std::any_of(screens.cbegin(), screens.cend(),
                       [&rect](const QScreen* screen) { return screen && screen->availableGeometry().contains(rect); });
}

QRect availableGeometryFor(const QRect& rect)
{
    const QList<QScreen*> screens = QGuiApplication::screens();
    const auto match = std::find_if(screens.cbegin(), screens.cend(), [&rect](const QScreen* screen)
                                    { return screen && screen->availableGeometry().contains(rect.center()); });
    if (match != screens.cend())
    {
        return (*match)->availableGeometry();
    }

    if (const QScreen* primary = QGuiApplication::primaryScreen())
    {
        return primary->availableGeometry();
    }

    return QRect(0, 0, UiTheme::Size::MainWindowMinWidth, UiTheme::Size::MainWindowMinHeight);
}

QRect centeredRectInAvailableGeometry(QSize size, const QRect& available)
{
    const int targetWidth = available.width() >= UiTheme::Size::MainWindowMinWidth
                                ? qBound(UiTheme::Size::MainWindowMinWidth, size.width(), available.width())
                                : available.width();
    const int targetHeight = available.height() >= UiTheme::Size::MainWindowMinHeight
                                 ? qBound(UiTheme::Size::MainWindowMinHeight, size.height(), available.height())
                                 : available.height();
    size = QSize(targetWidth, targetHeight);

    return QRect(QPoint(available.left() + (available.width() - size.width()) / 2,
                        available.top() + (available.height() - size.height()) / 2),
                 size);
}

int appVolumeSettingValue()
{
    return qBound(0, AppSettings::instance().value(QStringLiteral("VolumeLevel"), 128).toInt(), 255);
}

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
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event)

        QStyleOptionButton option;
        initStyleOption(&option);
        option.text.clear();

        QPainter painter(this);
        style()->drawControl(QStyle::CE_PushButton, &option, &painter, this);

        const QRect content =
            style()->subElementRect(QStyle::SE_PushButtonContents, &option, this).adjusted(2, 3, -2, -3);
        const QColor textColor = isEnabled() ? palette().color(QPalette::ButtonText)
                                             : palette().color(QPalette::Disabled, QPalette::ButtonText);

        QFont primaryFont = font();
        primaryFont.setBold(true);
        primaryFont.setPointSize(9);

        QFont secondaryFont = font();
        secondaryFont.setBold(true);
        secondaryFont.setPointSize(8);

        painter.setPen(textColor);
        painter.setFont(primaryFont);
        painter.drawText(QRect(content.left(), content.top(), content.width(), content.height() / 2 + 2),
                         Qt::AlignHCenter | Qt::AlignBottom, m_primary);

        painter.setFont(secondaryFont);
        painter.drawText(QRect(content.left(), content.center().y() - 1, content.width(), content.height() / 2 + 3),
                         Qt::AlignHCenter | Qt::AlignTop, m_secondary);
    }

  private:
    QString m_primary;
    QString m_secondary;
};

void setSelectorButtonLines(QPushButton* button, const QString& primary, const QString& secondary)
{
    if (auto* selector = dynamic_cast<TwoLineButton*>(button))
    {
        selector->setLines(primary, secondary);
        return;
    }

    button->setText(QString("%1\n%2").arg(primary, secondary));
}

void setCommandButtonActive(QPushButton* button, bool active)
{
    if (!button)
    {
        return;
    }
    QSignalBlocker block(button);
    button->setChecked(active);
    const bool levelControl = button->property("levelControl").toBool();
    button->setStyleSheet(levelControl ? levelButtonStyle(active) : commandButtonStyle(active));
    if (button->property("toggleLabel").isValid())
    {
        if (auto* tlb = dynamic_cast<TwoLineButton*>(button))
        {
            tlb->setSecondary(active ? QStringLiteral("ON") : QStringLiteral("OFF"));
        }
    }
}

void styleCompactMenu(QMenu* menu)
{
    if (!menu)
    {
        return;
    }

    menu->setStyleSheet(QStringLiteral("QMenu { background: %1; border: 1px solid %2; color: %3; }"
                                       "QMenu::item { padding: 5px 18px 5px 10px; }"
                                       "QMenu::item:selected { background: %4; color: %5; }"
                                       "QMenu::separator { height: 1px; background: %6; margin: 3px 8px; }"
                                       "QMenu::indicator { width: 0px; height: 0px; }")
                            .arg(UiTheme::Color::MenuPanel, UiTheme::Color::BorderMedium, UiTheme::Color::TextPrimary,
                                 UiTheme::Color::AccentDark, UiTheme::Color::White, UiTheme::Color::Border));
}

QString bandLabelForHz(quint64 hz)
{
    return sdr9700::radioBandShortLabel(sdr9700::radioBandForFrequency(hz));
}

int vfoBandIndexForHz(quint64 hz)
{
    return sdr9700::radioBandUiIndex(sdr9700::radioBandForFrequency(hz));
}

static_assert(std::size(sdr9700::kRadioUiBandOrder) == 3);

int radioTuningStepForHz(int hz)
{
    const auto preset = std::find_if(std::begin(kStepPresets), std::end(kStepPresets),
                                     [hz](const StepPreset& item) { return item.hz == hz; });
    return preset != std::end(kStepPresets) ? preset->radioStep : -1;
}

QString preampLevelLabel(int level)
{
    switch (qBound(0, level, 3))
    {
    case 1:
        return QStringLiteral("INT");
    case 2:
        return QStringLiteral("EXT");
    case 3:
        return QStringLiteral("INT+EXT");
    default:
        return QStringLiteral("OFF");
    }
}

struct OffsetPreset
{
    QString label;
    duplexMode_t mode;
    quint64 hz;
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

QString toneFrequencyLabel(ushort tone)
{
    const auto preset = std::find_if(std::cbegin(kTonePresets), std::cend(kTonePresets),
                                     [tone](const TonePreset& item) { return item.tone == tone; });
    if (preset != std::cend(kTonePresets))
    {
        return QString::fromLatin1(preset->label);
    }

    return QString::number(tone / 10.0, 'f', 1);
}

QString dtcsCodeLabel(ushort code)
{
    return QString::number(code).rightJustified(3, QLatin1Char('0'));
}


QString toneOptionLabel(rptAccessTxRx_t mode)
{
    switch (mode)
    {
    case ratrTN:
        return QStringLiteral("TONE");
    case ratrNT:
        return QStringLiteral("CTCSS");
    case ratrDN:
        return QStringLiteral("DCS");
    case ratrDD:
        return QStringLiteral("DTCS");
    default:
        return QStringLiteral("Off");
    }
}

// value is the unified toneValue field (stores either tone frequency or DTCS code).
QString memoryToneFrequencyLabel(rptAccessTxRx_t mode, ushort value)
{
    switch (mode)
    {
    case ratrTN:
    case ratrNT:
        return toneFrequencyLabel(value);
    case ratrDN:
    case ratrDD:
        return dtcsCodeLabel(value);
    default:
        return QString();
    }
}

QString memoryToneDisplayLabel(const MemoryRecord& memory)
{
    const auto mode = static_cast<rptAccessTxRx_t>(memory.toneMode);
    if (mode == ratrNN)
    {
        return QStringLiteral("Off");
    }

    const QString option = toneOptionLabel(mode);
    QString value = memoryToneFrequencyLabel(mode, memory.toneValue);
    if (value.isEmpty())
    {
        value = memory.toneFrequency;
    }

    if (value.isEmpty())
    {
        return option;
    }
    return QStringLiteral("%1 (%2)").arg(option, value);
}

QString memoryFrequencyLabel(quint64 hz)
{
    const quint64 mhzInt = hz / 1000000ULL;
    const quint64 khz = (hz % 1000000ULL) / 1000ULL;
    const quint64 hzRem = hz % 1000ULL;
    return QString("%1.%2.%3").arg(mhzInt, 3, 10, QChar('0')).arg(khz, 3, 10, QChar('0')).arg(hzRem, 3, 10, QChar('0'));
}

QVector<OffsetPreset> offsetPresetsForHz(quint64 hz)
{
    switch (sdr9700::radioBandForFrequency(hz))
    {
    case band2m:
        return {
            {QStringLiteral("-0.600"), dmDupMinus, 600000ULL},
            {QStringLiteral("+0.600"), dmDupPlus, 600000ULL},
        };
    case band70cm:
        return {
            {QStringLiteral("+5.000"), dmDupPlus, 5000000ULL},
        };
    case band23cm:
        return {
            {QStringLiteral("-12.000"), dmDupMinus, 12000000ULL},
            {QStringLiteral("-20.000"), dmDupMinus, 20000000ULL},
        };
    default:
        return {};
    }
}

QString formatFrequency(quint64 hz)
{
    const quint64 mhzInt = hz / 1000000ULL;
    const quint64 khz = (hz % 1000000ULL) / 1000ULL;
    const quint64 hzRem = hz % 1000ULL;
    return QString("%1.%2.%3").arg(mhzInt, 3, 10, QChar('0')).arg(khz, 3, 10, QChar('0')).arg(hzRem, 3, 10, QChar('0'));
}

QString formatOffsetMhz(quint64 hz)
{
    return QString::number(hz / 1000000.0, 'f', 3);
}

QString offsetModeLabel(duplexMode_t mode, quint64 offsetHz)
{
    switch (mode)
    {
    case dmDupMinus:
        return QStringLiteral("-") + formatOffsetMhz(offsetHz);
    case dmDupPlus:
        return QStringLiteral("+") + formatOffsetMhz(offsetHz);
    default:
        return QStringLiteral("SIMPLEX");
    }
}

bool parseFrequencyText(const QString& input, quint64* hz)
{
    QString text = input.trimmed();
    while (!text.isEmpty() && (text.back().isLetter() || text.back().isSpace()))
    {
        text.chop(1);
    }
    text = text.trimmed();

    if (text.contains(',') && !text.contains('.'))
    {
        text.replace(',', '.');
    }

    const QStringList parts = text.split('.', Qt::KeepEmptyParts);
    bool ok = false;
    quint64 parsedHz = 0;
    if (parts.size() == 3)
    {
        const quint64 mhz = parts[0].toULongLong(&ok);
        if (!ok)
        {
            return false;
        }
        const quint64 khz = parts[1].toULongLong(&ok);
        if (!ok || parts[1].size() > 3 || khz > 999)
        {
            return false;
        }
        const quint64 hzPart = parts[2].toULongLong(&ok);
        if (!ok || parts[2].size() > 3 || hzPart > 999)
        {
            return false;
        }
        parsedHz = mhz * 1000000ULL + khz * 1000ULL + hzPart;
    }
    else
    {
        const double mhz = text.toDouble(&ok);
        if (!ok)
        {
            return false;
        }
        parsedHz = static_cast<quint64>(mhz * 1000000.0 + 0.5);
    }

    if (parsedHz <= 100000ULL)
    {
        return false;
    }
    *hz = parsedHz;
    return true;
}

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
} // namespace

MainWindow::MainWindow(RadioModel* model, QWidget* parent)
    : QMainWindow(parent), m_model(model), m_vfo(model->vfo()), m_bandscope(model->bandscope())
{
    setWindowFlag(Qt::FramelessWindowHint);
    updateWindowTitle();
    setFixedSize(UiTheme::Size::MainWindowMinWidth, UiTheme::Size::MainWindowMinHeight);

    auto* central = new QWidget(this);
    auto* vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    setCentralWidget(central);

    buildToolBar();
    buildControlPanel(vbox);
    auto* bandscopeDivider = new QWidget(central);
    bandscopeDivider->setObjectName(QStringLiteral("bandscopeDivider"));
    bandscopeDivider->setFixedHeight(6);
    bandscopeDivider->setStyleSheet(
        QStringLiteral("QWidget#bandscopeDivider { background: %1; }").arg(UiTheme::Color::PanelDark));
    vbox->addWidget(bandscopeDivider);
    m_bandscopeDisplay = new BandscopeDisplay(central);
    m_bandscopeDisplay->setInvertMouseWheel(
        AppSettings::instance().value(QString::fromLatin1(kReverseMouseWheelTuningSettingsKey), "False").toBool());
    m_bandscopeDisplay->setVfoMarkerColor(
        colorSetting(kBandscopeCenterLineColorSettingsKey, kDefaultBandscopeCenterLineColor));
    m_bandscopeDisplay->setBackgroundColor(
        colorSetting(kBandscopeBackgroundColorSettingsKey, kDefaultBandscopeBackgroundColor));
    m_bandscopeDisplay->setGridLineColor(
        colorSetting(kBandscopeGridLineColorSettingsKey, kDefaultBandscopeGridLineColor));
    m_bandscopeDisplay->setGridDensity(bandscopeGridDensitySetting());
    QVector<BandscopeDisplay::SpanChoice> spanChoices;
    spanChoices.reserve(static_cast<int>(std::size(kBandscopeSpanPresets)));
    for (const BandscopeSpanPreset& preset : kBandscopeSpanPresets)
    {
        spanChoices.append({preset.hz, QString::fromLatin1(preset.label)});
    }
    m_bandscopeDisplay->setSpanChoices(spanChoices);
    m_bandscopeDisplay->setCurrentSpanHz(AppSettings::instance()
                                             .value(QString::fromLatin1(kBandscopeSpanHzSettingsKey),
                                                    QVariant::fromValue<qulonglong>(kDefaultBandscopeSpanHz))
                                             .toULongLong());
    connect(m_bandscopeDisplay, &BandscopeDisplay::spanSelected, this,
            [this](quint64 hz)
            {
                AppSettings::instance().setValue(QString::fromLatin1(kBandscopeSpanHzSettingsKey),
                                                 QVariant::fromValue<qulonglong>(hz));
                applyBandscopeSettings();
            });
    vbox->addWidget(m_bandscopeDisplay, 1);
    m_bandscopeTuneCommitTimer = new QTimer(this);
    m_bandscopeTuneCommitTimer->setSingleShot(true);
    m_bandscopeTuneCommitTimer->setInterval(kBandscopeTuneCommitDelayMs);
    connect(m_bandscopeTuneCommitTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_pendingBandscopeTuneHz == 0 || !m_model->isReady() || m_controlsLocked)
                {
                    return;
                }
                m_vfo->setFrequencyHz(m_pendingBandscopeTuneHz);
            });
    m_bandscopeTuneReleaseTimer = new QTimer(this);
    m_bandscopeTuneReleaseTimer->setSingleShot(true);
    m_bandscopeTuneReleaseTimer->setInterval(kBandscopeTuneReleaseDelayMs);
    connect(m_bandscopeTuneReleaseTimer, &QTimer::timeout, this,
            [this]()
            {
                m_pendingBandscopeTuneHz = 0;
                m_displayBandscopeTuneHz = 0;
                m_bandscopeDisplayCenterHz = 0;
                m_bandscopeFixedPanStartHz = 0;
                m_bandscopeFixedPanEndHz = 0;
                if (m_bandscope)
                {
                    m_bandscope->clearDisplayCenterHold();
                }
            });
    buildMemoryWindow();
    restoreWindowLayout();
    buildStatusBar();

    connect(m_model, &RadioModel::connectionChanged, this, &MainWindow::onConnectionChanged);
    connect(m_model, &RadioModel::readyChanged, this, &MainWindow::onRadioReadyChanged);
    connect(m_model, &RadioModel::smeterChanged, this, &MainWindow::onSmeterChanged);
    connect(m_model, &RadioModel::powerMeterChanged, this,
            [this](double watts)
            {
                m_txPowerMeterWatts = qBound(0.0, watts, 120.0);
                m_txPowerMeterValid = true;
                if (m_metersDialog)
                {
                    m_metersDialog->setPowerMeter(m_txPowerMeterWatts);
                }
            });
    connect(m_model, &RadioModel::swrChanged, this, &MainWindow::onSwrChanged);
    connect(m_model, &RadioModel::alcChanged, this, &MainWindow::onAlcChanged);
    connect(m_model, &RadioModel::compressionMeterChanged, this,
            [this](double db)
            {
                m_txCompressionDb = qBound(0.0, db, 25.5);
                m_txCompressionValid = true;
                if (m_metersDialog)
                {
                    m_metersDialog->setCompressionMeter(m_txCompressionDb);
                }
            });
    connect(m_model, &RadioModel::voltageMeterChanged, this,
            [this](double volts)
            {
                m_txVoltageVolts = qBound(0.0, volts, 16.0);
                m_txVoltageValid = true;
                if (m_metersDialog)
                {
                    m_metersDialog->setVoltageMeter(m_txVoltageVolts);
                }
            });
    connect(m_model, &RadioModel::currentMeterChanged, this,
            [this](double amps)
            {
                m_txCurrentAmps = qBound(0.0, amps, 20.0);
                m_txCurrentValid = true;
                if (m_metersDialog)
                {
                    m_metersDialog->setCurrentMeter(m_txCurrentAmps);
                }
            });
    connect(m_model, &RadioModel::pttChanged, this, &MainWindow::onPttChanged);
    connect(m_model, &RadioModel::statusMessage, this, &MainWindow::onStatusMessage);
    connect(m_model, &RadioModel::errorOccurred, this, &MainWindow::onError);
    connect(m_model, &RadioModel::networkQualityChanged, this, &MainWindow::updateNetworkQuality);
    connect(m_model, &RadioModel::txAudioLevelChanged, this, &MainWindow::updateTxAudioMeter);

    connect(m_vfo, &VfoModel::frequencyChanged, this, &MainWindow::onFrequencyChanged);
    connect(m_vfo, &VfoModel::modeChanged, this, &MainWindow::onModeChanged);
    connect(m_vfo, &VfoModel::filterChanged, this,
            [this](int low, int high) { m_bandscopeDisplay->setFilterWidth(low, high); });
    connect(m_vfo, &VfoModel::duplexModeChanged, this, &MainWindow::onDuplexModeChanged);
    connect(m_vfo, &VfoModel::repeaterOffsetChanged, this, &MainWindow::onRepeaterOffsetChanged);
    connect(m_vfo, &VfoModel::toneAccessModeChanged, this, &MainWindow::onToneAccessModeChanged);
    connect(m_vfo, &VfoModel::toneFrequencyChanged, this, &MainWindow::onToneFrequencyChanged);
    connect(m_vfo, &VfoModel::dtcsCodeChanged, this, &MainWindow::onDtcsCodeChanged);
    connect(m_vfo, &VfoModel::nrChanged, this, [this](bool on, int) { setCommandButtonActive(m_nrBtn, on); });
    connect(m_vfo, &VfoModel::nbChanged, this, [this](bool on, int) { setCommandButtonActive(m_nbBtn, on); });
    connect(m_vfo, &VfoModel::preampChanged, this, [this](bool) { updatePreampButton(); });
    connect(m_vfo, &VfoModel::preampLevelChanged, this, [this](int) { updatePreampButton(); });
    connect(m_vfo, &VfoModel::attenuatorChanged, this, [this](bool on) { setCommandButtonActive(m_attBtn, on); });
    connect(m_vfo, &VfoModel::autoNotchChanged, this, [this](bool) { updateNotchButton(); });
    connect(m_vfo, &VfoModel::manualNotchChanged, this, [this](bool) { updateNotchButton(); });
    connect(m_vfo, &VfoModel::compressorChanged, this, [this](bool on) { setCommandButtonActive(m_compBtn, on); });
    connect(m_vfo, &VfoModel::ritChanged, this, [this](bool, short) { updateRitButton(); });
    connect(m_vfo, &VfoModel::agcModeChanged, this,
            [this](const QString& mode) { setSelectorButtonLines(m_agcBtn, QStringLiteral("AGC"), mode.toUpper()); });
    connect(m_vfo, &VfoModel::rfGainChanged, this, &MainWindow::onRfGainChanged);
    connect(m_vfo, &VfoModel::squelchChanged, this,
            [this](bool, int level)
            {
                m_squelchValue = level;
                if (m_vfoPanel)
                {
                    m_vfoPanel->setSquelch(level);
                }
                updateSquelchButton();
            });
    connect(m_vfo, &VfoModel::txPowerChanged, this, &MainWindow::onTxPowerChanged);
    if (auto* backend = m_model->backend())
    {
        connect(backend, &IRadioBackend::radioValueUpdated, this,
                [this](Funcs func, const QVariant& value, uchar receiver)
                {
                    if (receiver != 0)
                    {
                        return;
                    }
                    auto* receiverPanel = m_vfoPanel;
                    switch (func)
                    {
                    case funcVFOBandMS:
                        applyActiveVfoFromRadio();
                        break;
                    case funcFreqGet:
                    case funcFreqSet:
                    case funcSelectedFreq:
                    {
                        if (!receiverPanel)
                        {
                            break;
                        }
                        const auto f = value.value<Frequency>();
                        if (f.Hz > 0)
                        {
                            m_vfoFrequencyHz = f.Hz;
                            qInfo(logGui()) << "VFO route: MAIN frequency to VFO" << f.Hz;
                            receiverPanel->setFrequencyText(formatFrequency(f.Hz));
                            receiverPanel->setBandText(bandLabelForHz(f.Hz));
                        }
                        else
                        {
                            receiverPanel->setFrequencyText(QStringLiteral("---.---.---"));
                        }
                        break;
                    }
                    case funcModeGet:
                    case funcModeSet:
                    case funcSelectedMode:
                    {
                        if (!receiverPanel)
                        {
                            break;
                        }
                        const auto mi = value.value<ModeInfo>();
                        qInfo(logGui()) << "VFO route: MAIN mode to VFO" << mi.name.toUpper();
                        receiverPanel->setModeText(mi.name.toUpper());
                        break;
                    }
                    case funcUnselectedFreq:
                    case funcUnselectedMode:
                        // Command 25/26 unselected data is the inactive VFO inside the MAIN band,
                        // not the SUB band. Do not paint the right-hand SUB VFO from it.
                        break;
                    case funcSMeter:
                    {
                        if (receiverPanel && !m_txActive && m_model->isReady())
                        {
                            receiverPanel->setSMeterValue(qBound(0, value.toInt(), 255) * 100 / 255);
                        }
                        break;
                    }
                    case funcRFPower:
                    {
                        const int level = qBound(0, value.toInt(), 255);
                        if (receiverPanel)
                        {
                            receiverPanel->setTxPower(level);
                        }
                        m_txPowerValue = level;
                        updateTxPowerButton();
                        break;
                    }
                    case funcSquelch:
                    {
                        const int level = qBound(0, value.toInt(), 255);
                        if (receiverPanel)
                        {
                            receiverPanel->setSquelch(level);
                        }
                        m_squelchValue = level;
                        updateSquelchButton();
                        break;
                    }
                    case funcALCMeter:
                    {
                        if (!receiverPanel || !m_txActive)
                        {
                            break;
                        }
                        receiverPanel->setAlc(value.toDouble());
                        break;
                    }
                    default:
                        break;
                    }
                });
    }

    resetRadioOwnedControlsForSync();
    m_bandscopeDisplay->setFilterWidth(m_vfo->filterLow(), m_vfo->filterHigh());
    updateSpectrumVfoMarker();

    connect(m_bandscope, &BandscopeModel::spectrumReady, this, &MainWindow::onSpectrumReady);
    connect(m_bandscope, &BandscopeModel::rangeChanged, this,
            [this](double center, double bw)
            {
                m_bandscopeDisplayCenterHz = static_cast<quint64>(std::llround(center * 1e6));
                m_bandscopeDisplay->setFrequencyRange(center - bw / 2, center + bw / 2);
                updateSpectrumVfoMarker();
            });

    connect(m_bandscopeDisplay, &BandscopeDisplay::frequencyClicked, this, &MainWindow::onSpectrumClicked);
    connect(m_bandscopeDisplay, &BandscopeDisplay::panCenterRequested, this,
            [this](double centerMhz) { panBandscopeToCenter(static_cast<quint64>(std::llround(centerMhz * 1e6))); });

    onConnectionChanged(false);

#ifdef HAVE_HIDAPI
    m_icomRC28Manager = new IcomRC28Manager(this);
    refreshIcomRC28EncoderSettings();
    m_icomRC28SnapTimer = new QTimer(this);
    m_icomRC28SnapTimer->setSingleShot(true);
    m_icomRC28SnapTimer->setInterval(600);
    connect(m_icomRC28SnapTimer, &QTimer::timeout, this, &MainWindow::snapIcomRC28FrequencyToKhz);
    for (int i = 0; i < 2; ++i)
    {
        m_icomRC28HoldTimers[i] = new QTimer(this);
        m_icomRC28HoldTimers[i]->setSingleShot(true);
        connect(m_icomRC28HoldTimers[i], &QTimer::timeout, this,
                [this, i]()
                {
                    if (!m_icomRC28ButtonDown[i] || m_icomRC28HoldConsumed[i])
                    {
                        return;
                    }
                    m_icomRC28HoldConsumed[i] = true;
                    const QString field = i == 0 ? QStringLiteral("f1Hold") : QStringLiteral("f2Hold");
                    dispatchIcomRC28Action(IcomRC28Manager::settingsField(field, QStringLiteral("None")));
                });
    }
    connect(m_icomRC28Manager, &IcomRC28Manager::buttonPressed, this, &MainWindow::handleIcomRC28Button);
    connect(m_icomRC28Manager, &IcomRC28Manager::tuneSteps, this, &MainWindow::handleIcomRC28Tune);
    connect(m_icomRC28Manager, &IcomRC28Manager::multipleDevicesDetected, this,
            [this](const QString& deviceName)
            {
                qInfo(logIcomRC28()) << "Multiple devices detected:" << deviceName;
                showToast(QStringLiteral("Duplicate accessory blocked (%1)").arg(deviceName), 8000);
            });
    connect(m_icomRC28Manager, &IcomRC28Manager::connectionChanged, this,
            [this](bool connected, const QString& deviceName)
            {
                qInfo(logIcomRC28()) << (connected ? "Connected" : "Disconnected") << deviceName;
                if (connected)
                {
                    updateIcomRC28Leds();
                }
                showToast(connected ? QStringLiteral("Accessory connected (%1)").arg(deviceName)
                                    : QStringLiteral("Accessory disconnected (%1)").arg(deviceName),
                          4000);
            });
    m_icomRC28Manager->loadSettings();
#endif

    QTimer::singleShot(0, this, &MainWindow::tryAutoConnect);
}

void MainWindow::buildToolBar()
{
    const QString menuStyle =
        QStringLiteral("QMenu { background: %1; border: 1px solid %2; color: %3; }"
                       "QMenu::item { padding: 5px 18px 5px 10px; }"
                       "QMenu::item:selected { background: %4; color: %5; }"
                       "QMenu::separator { height: 1px; background: %6; margin: 3px 8px; }")
            .arg(UiTheme::Color::MenuPanel, UiTheme::Color::BorderMedium, UiTheme::Color::TextPrimary,
                 UiTheme::Color::AccentDark, UiTheme::Color::White, UiTheme::Color::Border);

    m_titleBar = new MainTitleBar(this);
    m_titleBar->setTitle(
        QStringLiteral("<span style='color:#2a82da; font-size:13px; font-weight:bold;'>%1 v%2</span>")
            .arg(QString::fromLatin1(APP_NAME).toHtmlEscaped(), QString::fromLatin1(APP_VERSION).toHtmlEscaped()));

    auto* fileMenu = new QMenu(this);
    fileMenu->setStyleSheet(menuStyle);
    fileMenu->addAction("Connect to Radio", this, [this]() { showRadioChooserDialog(); });
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction("Quit", this, &QWidget::close);
    quitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    m_titleBar->addMenu(QStringLiteral("&File"), fileMenu);

    m_titleBar->addAction(QStringLiteral("&Settings"), this, [this]() { showSettingsDialog(); });

    auto* viewMenu = new QMenu(this);
    viewMenu->setStyleSheet(menuStyle);
    viewMenu->addAction("DTMF", this, &MainWindow::showDtmfDialog);
    viewMenu->addAction("Meters", this, &MainWindow::showMetersDialog);
    m_titleBar->addMenu(QStringLiteral("&View"), viewMenu);

    auto* helpMenu = new QMenu(this);
    helpMenu->setStyleSheet(menuStyle);
    helpMenu->addAction("About", this,
                        [this]()
                        {
                            AboutDialog dlg(this);
                            centerPopupWindow(&dlg);
                            dlg.exec();
                        });
    m_titleBar->addMenu(QStringLiteral("&Help"), helpMenu);

    connect(m_titleBar, &MainTitleBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(m_titleBar, &MainTitleBar::closeRequested, this, &QWidget::close);
    connect(m_titleBar, &MainTitleBar::muteToggled, this, &MainWindow::toggleMute);
    connect(m_titleBar, &MainTitleBar::lockToggled, this, &MainWindow::toggleControlLock);
    connect(m_titleBar, &MainTitleBar::txDurationResetRequested, this,
            [this]()
            {
                if (m_txActive)
                {
                    m_txElapsed.restart();
                }
                m_titleBar->setTxDuration(QStringLiteral("00:00:00"), m_txActive);
            });
    connect(m_titleBar, &MainTitleBar::volumeChanged, this,
            [this](int value)
            {
                if (!m_model || !m_model->isReady() || m_controlsLocked)
                {
                    return;
                }
                const int bounded = qBound(0, value, 255);
                m_currentAfGain = bounded;
                AppSettings::instance().setValue(QStringLiteral("VolumeLevel"), bounded);
                if (auto* backend = m_model->backend())
                {
                    backend->setAfGain(bounded);
                }
            });

    menuBar()->setVisible(false);
    setMenuWidget(m_titleBar);
}

void MainWindow::buildMemoryWindow()
{
    m_memoryWindow = new QDialog(this);
    m_memoryWindow->setWindowTitle("Memory");
    m_memoryWindow->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    m_memoryWindow->setStyleSheet(
        QStringLiteral("QDialog { background: %1; border: 1px solid %2; }")
            .arg(QLatin1String(UiTheme::Color::Panel), QLatin1String(UiTheme::Color::Border)));
    m_memoryWindow->setObjectName("memoryWindow");
    m_memoryWindow->setAttribute(Qt::WA_DeleteOnClose, false);
    m_memoryWindow->resize(kMemoryWindowSize);
    m_memoryWindow->setFixedSize(kMemoryWindowSize);

    auto* panel = new QWidget(m_memoryWindow);
    auto* root = new QVBoxLayout(panel);
    root->setContentsMargins(kMemoryPanelMargins);
    root->setSpacing(kMemoryPanelSpacing);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(kNoMargins);
    toolbar->setSpacing(kMemoryToolbarSpacing);

    auto* filterGroup = new QGroupBox(panel);
    auto* filterLayout = new QHBoxLayout(filterGroup);
    filterLayout->setContentsMargins(kMemoryToolbarGroupMargins);
    filterLayout->setSpacing(kMemoryToolbarGroupSpacing);
    m_memoryBandFilter = new QComboBox(panel);
    m_memoryBandFilter->addItem("All", QString());
    for (const availableBands band : sdr9700::kRadioUiBandOrder)
    {
        const QString label = sdr9700::radioBandShortLabel(band);
        m_memoryBandFilter->addItem(label, label);
    }
    filterLayout->addWidget(m_memoryBandFilter);
    toolbar->addWidget(filterGroup);

    auto* selectGroup = new QGroupBox(panel);
    auto* selectLayout = new QHBoxLayout(selectGroup);
    selectLayout->setContentsMargins(kMemoryToolbarGroupMargins);
    selectLayout->setSpacing(kMemoryToolbarGroupSpacing);
    auto* selectButton = new QPushButton("Select", panel);
    selectLayout->addWidget(selectButton);
    toolbar->addWidget(selectGroup);

    auto* reorderGroup = new QGroupBox(panel);
    auto* reorderLayout = new QHBoxLayout(reorderGroup);
    reorderLayout->setContentsMargins(kMemoryToolbarGroupMargins);
    reorderLayout->setSpacing(kMemoryToolbarGroupSpacing);
    auto* upButton = new QPushButton("Up", panel);
    auto* downButton = new QPushButton("Down", panel);
    reorderLayout->addWidget(upButton);
    reorderLayout->addWidget(downButton);
    toolbar->addWidget(reorderGroup);
    toolbar->addStretch(1);

    auto* memoryGroup = new QGroupBox(panel);
    auto* memoryLayout = new QHBoxLayout(memoryGroup);
    memoryLayout->setContentsMargins(kMemoryToolbarGroupMargins);
    memoryLayout->setSpacing(kMemoryToolbarGroupSpacing);
    auto* addButton = new QPushButton("Add", panel);
    auto* editButton = new QPushButton("Edit", panel);
    auto* copyButton = new QPushButton("Copy", panel);
    auto* removeButton = new QPushButton("Remove", panel);
    memoryLayout->addWidget(addButton);
    memoryLayout->addWidget(editButton);
    memoryLayout->addWidget(copyButton);
    memoryLayout->addWidget(removeButton);
    toolbar->addWidget(memoryGroup);
    toolbar->addStretch(1);

    auto* transferGroup = new QGroupBox(panel);
    auto* transferLayout = new QHBoxLayout(transferGroup);
    transferLayout->setContentsMargins(kMemoryToolbarGroupMargins);
    transferLayout->setSpacing(kMemoryToolbarGroupSpacing);
    auto* importButton = new QPushButton("Import", panel);
    auto* exportButton = new QPushButton("Export", panel);
    transferLayout->addWidget(importButton);
    transferLayout->addWidget(exportButton);
    toolbar->addWidget(transferGroup);
    root->addLayout(toolbar);

    m_memoryTable = new QTableWidget(panel);
    m_memoryTable->setColumnCount(kMemoryTableColumnCount);
    m_memoryTable->setHorizontalHeaderLabels(
        {QStringLiteral("#"), QStringLiteral("Name"), QStringLiteral("Frequency (RX)"), QStringLiteral("Shift"),
         QStringLiteral("Tone"), QStringLiteral("Notes"), QStringLiteral("Band Key"), QStringLiteral("ID")});
    m_memoryTable->setColumnHidden(kMemoryBandKeyColumn, true);
    m_memoryTable->setColumnHidden(kMemoryIdColumn, true);
    m_memoryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_memoryTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_memoryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_memoryTable->setSortingEnabled(false);
    m_memoryTable->setDragDropMode(QAbstractItemView::NoDragDrop);
    m_memoryTable->setShowGrid(true);
    m_memoryTable->setGridStyle(Qt::SolidLine);
    m_memoryTable->setStyleSheet(QStringLiteral("QTableWidget { gridline-color: %1; }").arg(UiTheme::Color::Border));
    m_memoryTable->verticalHeader()->setVisible(false);
    m_memoryTable->horizontalHeader()->setStretchLastSection(false);
    m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryNumberColumn, QHeaderView::Interactive);
    m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryNameColumn, QHeaderView::Interactive);
    m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryFrequencyColumn, QHeaderView::Interactive);
    m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryShiftColumn, QHeaderView::Interactive);
    m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryToneColumn, QHeaderView::Interactive);
    m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryNotesColumn, QHeaderView::Stretch);
    m_memoryTable->setColumnWidth(kMemoryNumberColumn, kMemoryNumberColumnWidth);
    m_memoryTable->setColumnWidth(kMemoryNameColumn, kMemoryNameColumnWidth);
    m_memoryTable->setColumnWidth(kMemoryFrequencyColumn, kMemoryFrequencyColumnWidth);
    m_memoryTable->setColumnWidth(kMemoryShiftColumn, kMemoryShiftColumnWidth);
    m_memoryTable->setColumnWidth(kMemoryToneColumn, kMemoryToneColumnWidth);
    root->addWidget(m_memoryTable, 1);

    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(kNoMargins);
    auto* localNote = new QLabel("These memories are local to SDR9700 and are not saved in the radio.", panel);
    localNote->setStyleSheet("QLabel { color: palette(mid); }");
    m_closeMemoryWindowOnSelectCheck = new QCheckBox("Close after selection", panel);
    m_closeMemoryWindowOnSelectCheck->setChecked(
        AppSettings::instance().value(QString::fromLatin1(kCloseMemoryWindowOnSelectSettingsKey), "True").toBool());
    m_closeMemoryWindowOnSelectCheck->setToolTip("Close the Memories window after selecting a memory.");
    m_closeMemoryWindowOnSelectCheck->setStyleSheet("QCheckBox { color: palette(mid); }");
    m_memoryCountLabel = new QLabel(panel);
    m_memoryCountLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_memoryCountLabel->setStyleSheet("QLabel { color: palette(mid); }");
    auto* closeButton = new QPushButton("Close", panel);
    footer->addWidget(localNote, 1);
    footer->addWidget(m_closeMemoryWindowOnSelectCheck);
    footer->addWidget(m_memoryCountLabel);
    footer->addWidget(closeButton);
    root->addLayout(footer);

    auto* memTitleBar = new FramelessTitleBar(QStringLiteral("Memory"), m_memoryWindow);
    connect(memTitleBar->closeButton(), &QPushButton::clicked, m_memoryWindow, &QWidget::hide);
    connect(closeButton, &QPushButton::clicked, m_memoryWindow, &QWidget::hide);

    auto* windowLayout = new QVBoxLayout(m_memoryWindow);
    windowLayout->setContentsMargins(kNoMargins);
    windowLayout->setSpacing(0);
    windowLayout->addWidget(memTitleBar);
    windowLayout->addWidget(panel, 1);

    connect(m_memoryBandFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::reloadMemoryTable);
    connect(m_closeMemoryWindowOnSelectCheck, &QCheckBox::toggled, this, [](bool checked)
            { AppSettings::instance().setValue(QString::fromLatin1(kCloseMemoryWindowOnSelectSettingsKey), checked); });
    connect(selectButton, &QPushButton::clicked, this, &MainWindow::selectCheckedMemory);
    connect(upButton, &QPushButton::clicked, this, &MainWindow::moveSelectedMemoryUp);
    connect(downButton, &QPushButton::clicked, this, &MainWindow::moveSelectedMemoryDown);
    connect(addButton, &QPushButton::clicked, this, &MainWindow::storeCurrentMemory);
    connect(editButton, &QPushButton::clicked, this, &MainWindow::editSelectedMemory);
    connect(copyButton, &QPushButton::clicked, this, &MainWindow::copySelectedMemory);
    connect(removeButton, &QPushButton::clicked, this, &MainWindow::removeSelectedMemory);
    connect(importButton, &QPushButton::clicked, this,
            [this]()
            {
                const QString path =
                    QFileDialog::getOpenFileName(this, "Import Memories", QString(),
                                                 "SDR9700 memories (*.json);;JSON files (*.json);;All files (*)");
                if (path.isEmpty())
                {
                    return;
                }

                QFile file(path);
                if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                {
                    QMessageBox::warning(this, "Import Memories", "Could not open the selected memory file.");
                    return;
                }

                QJsonParseError error;
                const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
                if (error.error != QJsonParseError::NoError)
                {
                    QMessageBox::warning(this, "Import Memories", "The selected file is not valid JSON.");
                    return;
                }

                QVector<MemoryRecord> existing = loadMemories();
                const QVector<MemoryRecord> imported = memoriesFromDocument(doc);
                for (MemoryRecord memory : imported)
                {
                    auto current = std::find_if(existing.begin(), existing.end(), [&memory](const MemoryRecord& record)
                                                { return record.id == memory.id; });
                    if (current != existing.end())
                    {
                        *current = memory;
                    }
                    else
                    {
                        existing.append(memory);
                    }
                }
                if (!saveMemories(existing))
                {
                    QMessageBox::warning(this, "Import Memories", "Could not save the imported memories.");
                    return;
                }
                reloadMemoryTable();
                showToast(QString("Imported %1 memories").arg(imported.size()));
            });
    connect(exportButton, &QPushButton::clicked, this,
            [this]()
            {
                const QString path =
                    QFileDialog::getSaveFileName(this, "Export Memories", "sdr9700-memories.json",
                                                 "SDR9700 memories (*.json);;JSON files (*.json);;All files (*)");
                if (path.isEmpty())
                {
                    return;
                }

                QSaveFile file(path);
                if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
                {
                    QMessageBox::warning(this, "Export Memories", "Could not write the selected memory file.");
                    return;
                }
                const QByteArray data = memoriesExportDocument(loadMemories()).toJson(QJsonDocument::Indented);
                if (file.write(data) != static_cast<qint64>(data.size()))
                {
                    QMessageBox::warning(this, "Export Memories", "Could not write the selected memory file.");
                    return;
                }
                if (!file.commit())
                {
                    QMessageBox::warning(this, "Export Memories", "Could not save the memory file.");
                    return;
                }
                showToast("Memories exported");
            });

    reloadMemoryTable();
}

void MainWindow::centerPopupWindow(QWidget* popup) const
{
    sdr9700::ui::centerWindowOn(popup, this);
}

void MainWindow::bringDialogToFront(QWidget* dialog) const
{
    if (!dialog)
    {
        return;
    }

    if (dialog->isMinimized())
    {
        dialog->showNormal();
    }
    else
    {
        dialog->show();
    }

    centerPopupWindow(dialog);
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::showSettingsDialog()
{
    if (m_settingsDialog)
    {
        bringDialogToFront(m_settingsDialog);
        return;
    }

#ifdef HAVE_HIDAPI
    auto* dlg = new SettingsDialog(SettingsDialog::Page::AudioDevices, this, m_icomRC28Manager);
#else
    auto* dlg = new SettingsDialog(this);
#endif
    m_settingsDialog = dlg;
    connect(dlg, &QObject::destroyed, this,
            [this, dlg]()
            {
                if (m_settingsDialog == dlg)
                {
                    m_settingsDialog = nullptr;
                }
            });
    connect(dlg, &SettingsDialog::reverseMouseWheelTuningChanged, m_bandscopeDisplay,
            &BandscopeDisplay::setInvertMouseWheel);
    connect(dlg, &SettingsDialog::bandscopeCenterLineColorChanged, m_bandscopeDisplay,
            &BandscopeDisplay::setVfoMarkerColor);
    connect(dlg, &SettingsDialog::bandscopeBackgroundColorChanged, m_bandscopeDisplay,
            &BandscopeDisplay::setBackgroundColor);
    connect(dlg, &SettingsDialog::bandscopeGridLineColorChanged, m_bandscopeDisplay,
            &BandscopeDisplay::setGridLineColor);
    connect(dlg, &SettingsDialog::bandscopeGridDensityChanged, m_bandscopeDisplay, &BandscopeDisplay::setGridDensity);
#ifdef HAVE_HIDAPI
    connect(dlg, &SettingsDialog::icomRC28EncoderSettingsChanged, this,
            [this](const QString&, const QString&)
            {
                refreshIcomRC28EncoderSettings();
                if (!m_icomRC28AutoSnap && m_icomRC28SnapTimer)
                {
                    m_icomRC28SnapTimer->stop();
                }
            });
#endif
    centerPopupWindow(dlg);
    QTimer::singleShot(0, dlg, [this, dlg]() { centerPopupWindow(dlg); });
    QPointer<SettingsDialog> dlgGuard = dlg;
    connect(dlg, &QDialog::finished, this,
            [this, dlgGuard]()
            {
                if (m_settingsDialog == dlgGuard)
                {
                    m_settingsDialog = nullptr;
                }

                m_lanModValue = qBound(0, AppSettings::instance().value("LanModLevel", 128).toInt(), 255);
                if (m_vfoPanel)
                {
                    m_vfoPanel->setLanMod(m_lanModValue);
                }
                m_model->setLanModLevel(m_lanModValue);
                m_bandscopeDisplay->setInvertMouseWheel(
                    AppSettings::instance()
                        .value(QString::fromLatin1(kReverseMouseWheelTuningSettingsKey), "False")
                        .toBool());
#ifdef HAVE_HIDAPI
                refreshIcomRC28EncoderSettings();
#endif
                if (dlgGuard)
                {
                    dlgGuard->deleteLater();
                }
            });
    dlg->setWindowModality(Qt::NonModal);
    dlg->show();
    bringDialogToFront(dlg);
}

void MainWindow::showMemoryWindow()
{
    if (!m_memoryWindow)
    {
        return;
    }
    reloadMemoryTable();
    centerPopupWindow(m_memoryWindow);
    m_memoryWindow->show();
    m_memoryWindow->raise();
    m_memoryWindow->activateWindow();
}

QString MainWindow::selectedMemoryId() const
{
    if (!m_memoryTable)
    {
        return QString();
    }

    const int row = m_memoryTable->currentRow();
    if (row < 0)
    {
        return QString();
    }

    const auto* idItem = m_memoryTable->item(row, kMemoryIdColumn);
    return idItem ? idItem->text() : QString();
}

void MainWindow::selectCheckedMemory()
{
    const QString id = selectedMemoryId();
    if (id.isEmpty())
    {
        QMessageBox::information(this, "Select Memory", "Choose one memory first.");
        return;
    }

    selectMemoryById(id, true);
}

void MainWindow::selectMemoryById(const QString& id, bool showDialogOnFailure)
{
    if (id.isEmpty())
    {
        return;
    }
    if (m_controlsLocked)
    {
        if (showDialogOnFailure)
        {
            QMessageBox::information(this, "Select Memory", "Unlock controls before selecting a memory.");
        }
        else
        {
            showToast("Controls are locked");
        }
        return;
    }

    const QVector<MemoryRecord> memories = loadMemories();
    auto it =
        std::find_if(memories.cbegin(), memories.cend(), [&id](const MemoryRecord& memory) { return memory.id == id; });
    if (it == memories.cend())
    {
        return;
    }
    if (!m_model->isReady() || !m_vfo)
    {
        if (showDialogOnFailure)
        {
            QMessageBox::information(this, "Select Memory",
                                     "Connect to the radio and wait for sync before selecting a memory.");
        }
        else
        {
            showToast("Connect to the radio before selecting a memory");
        }
        return;
    }

    const MemoryRecord& memory = *it;
    setActiveMemory(memory.id, memory.name, memory.receiveHz, memory.duplexMode, memory.offsetHz, memory.toneMode,
                    memory.toneValue);
    m_applyingMemorySelection = true;
    const int generation = ++m_memorySelectionGeneration;
    m_vfo->setFrequencyHz(memory.receiveHz);
    m_vfo->setRepeaterOffsetHz(memory.offsetHz);
    m_vfo->setDuplexMode(static_cast<duplexMode_t>(memory.duplexMode));
    if (isDtcsToneMode(static_cast<rptAccessTxRx_t>(memory.toneMode)))
    {
        m_vfo->setDtcsCode(memory.toneValue);
    }
    else if (memory.toneMode != ratrNN)
    {
        m_vfo->setToneFrequency(memory.toneValue);
    }
    m_vfo->setToneAccessMode(static_cast<rptAccessTxRx_t>(memory.toneMode));
    // Release immediately if the radio was already at every correct setting (no callbacks will fire).
    checkIfMemorySelectionComplete();
    // Fallback: release the guard after 3 s in case the radio never confirms.
    QTimer::singleShot(3000, this,
                       [this, generation]()
                       {
                           if (m_memorySelectionGeneration != generation)
                           {
                               return;
                           }
                           m_applyingMemorySelection = false;
                       });
    showToast(QString("Selected memory: %1").arg(memory.name));
    if (m_memoryWindow && m_closeMemoryWindowOnSelectCheck && m_closeMemoryWindowOnSelectCheck->isChecked())
    {
        m_memoryWindow->hide();
    }
}

void MainWindow::editSelectedMemory()
{
    const QString id = selectedMemoryId();
    if (id.isEmpty())
    {
        QMessageBox::information(this, "Edit Memory", "Choose one memory first.");
        return;
    }
    showMemoryEditor(id);
}

void MainWindow::copySelectedMemory()
{
    const QString id = selectedMemoryId();
    if (id.isEmpty())
    {
        QMessageBox::information(this, "Copy Memory", "Choose one memory first.");
        return;
    }

    QVector<MemoryRecord> memories = loadMemories();
    auto it =
        std::find_if(memories.begin(), memories.end(), [&id](const MemoryRecord& memory) { return memory.id == id; });
    if (it == memories.end())
    {
        return;
    }

    MemoryRecord copy = *it;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.name = (copy.name.isEmpty() ? QStringLiteral("Copy") : QStringLiteral("%1 Copy").arg(copy.name))
                    .left(kMemoryNameMaxChars);

    const int insertIndex = int(std::distance(memories.begin(), it)) + 1;
    memories.insert(insertIndex, copy);
    if (!saveMemories(memories))
    {
        QMessageBox::warning(this, "Copy Memory", "Could not save the copied memory.");
        return;
    }
    reloadMemoryTable();

    for (int row = 0; row < m_memoryTable->rowCount(); ++row)
    {
        const auto* idItem = m_memoryTable->item(row, kMemoryIdColumn);
        if (idItem && idItem->text() == copy.id)
        {
            m_memoryTable->selectRow(row);
            break;
        }
    }
    showToast(QString("Copied memory: %1").arg(copy.name));
}

void MainWindow::removeSelectedMemory()
{
    const QString id = selectedMemoryId();
    if (id.isEmpty())
    {
        QMessageBox::information(this, "Remove Memory", "Choose one memory first.");
        return;
    }

    QVector<MemoryRecord> memories = loadMemories();
    auto it =
        std::find_if(memories.begin(), memories.end(), [&id](const MemoryRecord& memory) { return memory.id == id; });
    if (it == memories.end())
    {
        return;
    }

    if (QMessageBox::question(this, "Remove Memory", QString("Remove memory \"%1\"?").arg(it->name)) !=
        QMessageBox::Yes)
    {
        return;
    }

    memories.erase(it);
    if (!saveMemories(memories))
    {
        QMessageBox::warning(this, "Remove Memory", "Could not remove the selected memory.");
        return;
    }
    reloadMemoryTable();
    showToast("Memory removed");
}

void MainWindow::moveSelectedMemoryUp()
{
    moveSelectedMemory(-1);
}

void MainWindow::moveSelectedMemoryDown()
{
    moveSelectedMemory(1);
}

void MainWindow::moveSelectedMemory(int direction)
{
    const QString id = selectedMemoryId();
    if (id.isEmpty())
    {
        QMessageBox::information(this, "Move Memory", "Choose one memory first.");
        return;
    }

    QVector<MemoryRecord> memories = loadMemories();
    const QString bandFilter = m_memoryBandFilter ? m_memoryBandFilter->currentData().toString() : QString();
    if (!bandFilter.isEmpty())
    {
        QMessageBox::information(this, "Move Memory", "Switch to All memories before reordering.");
        return;
    }

    QVector<int> visibleIndexes;
    for (int i = 0; i < memories.size(); ++i)
    {
        visibleIndexes.append(i);
    }

    int visiblePosition = -1;
    for (int i = 0; i < visibleIndexes.size(); ++i)
    {
        if (memories.at(visibleIndexes.at(i)).id == id)
        {
            visiblePosition = i;
            break;
        }
    }

    const int targetPosition = visiblePosition + direction;
    if (visiblePosition < 0 || targetPosition < 0 || targetPosition >= visibleIndexes.size())
    {
        return;
    }

    std::swap(memories[visibleIndexes.at(visiblePosition)].number, memories[visibleIndexes.at(targetPosition)].number);
    if (!saveMemories(memories))
    {
        QMessageBox::warning(this, "Move Memory", "Could not save the memory order.");
        return;
    }
    reloadMemoryTable();

    for (int row = 0; row < m_memoryTable->rowCount(); ++row)
    {
        const auto* idItem = m_memoryTable->item(row, kMemoryIdColumn);
        if (idItem && idItem->text() == id)
        {
            m_memoryTable->selectRow(row);
            break;
        }
    }
}

void MainWindow::updateWindowTitle()
{
    QString title = QStringLiteral("%1 %2").arg(QString::fromLatin1(APP_NAME), QString::fromLatin1(APP_VERSION));
#if SDR9700_DEBUG_BUILD
    title += QStringLiteral(" (DEBUG)");
#endif
    setWindowTitle(title);
}

void MainWindow::storeCurrentMemory()
{
    showMemoryEditor(QString());
}

void MainWindow::showMemoryEditor(const QString& memoryId)
{
    QDialog dialog(this);
    static constexpr int kMemoryEditorWidth = 460;
    const bool editing = !memoryId.isEmpty();
    dialog.setWindowTitle("Add/Edit Memory");
    applyTitleCloseOnlyFlags(&dialog);
    auto* root = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    form->setHorizontalSpacing(14);
    auto* nameEdit = new QLineEdit(&dialog);
    nameEdit->setMaxLength(kMemoryNameMaxChars);
    nameEdit->setPlaceholderText(QStringLiteral("Maximum %1 characters").arg(kMemoryNameMaxChars));
    auto* frequencyEdit = new QLineEdit(&dialog);
    frequencyEdit->setPlaceholderText("145.000000");
    auto* offsetCombo = new QComboBox(&dialog);
    auto* customOffsetRow = new QWidget(&dialog);
    auto* customOffsetLayout = new QHBoxLayout(customOffsetRow);
    customOffsetLayout->setContentsMargins(0, 0, 0, 0);
    customOffsetLayout->setSpacing(6);
    auto* customOffsetModeCombo = new QComboBox(customOffsetRow);
    customOffsetModeCombo->addItem("+", dmDupPlus);
    customOffsetModeCombo->addItem("-", dmDupMinus);
    auto* customOffsetSpin = new QDoubleSpinBox(customOffsetRow);
    customOffsetSpin->setRange(0.0, 99.999);
    customOffsetSpin->setDecimals(3);
    customOffsetSpin->setSuffix(" MHz");
    customOffsetLayout->addWidget(customOffsetModeCombo);
    customOffsetLayout->addWidget(customOffsetSpin, 1);
    auto* toneOptionCombo = new QComboBox(&dialog);
    toneOptionCombo->addItem("Off", ratrNN);
    toneOptionCombo->addItem("TONE", ratrTN);
    toneOptionCombo->addItem("CTCSS", ratrNT);
    toneOptionCombo->addItem("DCS", ratrDN);
    toneOptionCombo->addItem("DTCS", ratrDD);
    ushort tonePickerValue{0};
    bool tonePickerValid{false};
    auto* toneValueBtn = new QPushButton(QStringLiteral("-- Select --"), &dialog);
    auto* toneValueLabel = new QLabel("Tone Frequency:", &dialog);
    auto setTonePick = [&tonePickerValue, &tonePickerValid, toneValueBtn](ushort v, const QString& label)
    {
        tonePickerValue = v;
        tonePickerValid = true;
        toneValueBtn->setText(label);
    };
    auto clearTonePick = [&tonePickerValue, &tonePickerValid, toneValueBtn]()
    {
        tonePickerValue = 0;
        tonePickerValid = false;
        toneValueBtn->setText(QStringLiteral("-- Select --"));
    };
    auto* notesEdit = new QPlainTextEdit(&dialog);
    notesEdit->setFixedHeight(90);

    auto populateOffsetOptions = [frequencyEdit, offsetCombo]()
    {
        const QVariant currentModeData = offsetCombo->currentData(Qt::UserRole);
        const int currentMode = currentModeData.isValid() ? currentModeData.toInt() : dmSimplex;
        const quint64 currentOffset = offsetCombo->currentData(Qt::UserRole + 1).toULongLong();
        offsetCombo->clear();
        offsetCombo->addItem(QStringLiteral("Simplex"), dmSimplex);
        offsetCombo->setItemData(offsetCombo->count() - 1, 0ULL, Qt::UserRole + 1);

        quint64 hz = 0;
        if (parseFrequencyText(frequencyEdit->text(), &hz))
        {
            for (const OffsetPreset& preset : offsetPresetsForHz(hz))
            {
                offsetCombo->addItem(preset.label, preset.mode);
                offsetCombo->setItemData(offsetCombo->count() - 1, QVariant::fromValue<qulonglong>(preset.hz),
                                         Qt::UserRole + 1);
            }
        }

        offsetCombo->addItem(QStringLiteral("Custom"), kMemoryOffsetCustom);
        offsetCombo->setItemData(offsetCombo->count() - 1, QVariant::fromValue<qulonglong>(currentOffset),
                                 Qt::UserRole + 1);

        for (int i = 0; i < offsetCombo->count(); ++i)
        {
            if (offsetCombo->itemData(i, Qt::UserRole).toInt() == currentMode &&
                offsetCombo->itemData(i, Qt::UserRole + 1).toULongLong() == currentOffset)
            {
                offsetCombo->setCurrentIndex(i);
                return;
            }
        }
        offsetCombo->setCurrentIndex(0);
    };

    auto setOffsetSelection =
        [offsetCombo, customOffsetModeCombo, customOffsetSpin](duplexMode_t mode, quint64 offsetHz)
    {
        for (int i = 0; i < offsetCombo->count(); ++i)
        {
            if (offsetCombo->itemData(i, Qt::UserRole).toInt() == mode &&
                offsetCombo->itemData(i, Qt::UserRole + 1).toULongLong() == offsetHz)
            {
                offsetCombo->setCurrentIndex(i);
                return;
            }
        }
        customOffsetModeCombo->setCurrentIndex(qMax(0, customOffsetModeCombo->findData(mode)));
        customOffsetSpin->setValue(offsetHz / 1000000.0);
        offsetCombo->setCurrentIndex(qMax(0, offsetCombo->findData(kMemoryOffsetCustom)));
    };

    auto updateCustomOffsetVisibility = [&dialog, form, offsetCombo, customOffsetRow]()
    {
        const bool customSelected = offsetCombo->currentData(Qt::UserRole).toInt() == kMemoryOffsetCustom;
        customOffsetRow->setVisible(customSelected);
        if (QWidget* label = form->labelForField(customOffsetRow))
        {
            label->setVisible(customSelected);
        }
        dialog.adjustSize();
        dialog.setFixedWidth(kMemoryEditorWidth);
    };

    populateOffsetOptions();
    updateCustomOffsetVisibility();

    auto populateToneValues = [clearTonePick]() { clearTonePick(); };

    auto updateToneValueVisibility = [&dialog, form, toneOptionCombo, toneValueBtn, toneValueLabel]()
    {
        const auto mode = static_cast<rptAccessTxRx_t>(toneOptionCombo->currentData().toInt());
        const bool toneSelected = mode != ratrNN;
        toneValueLabel->setText(isDtcsToneMode(mode) ? QStringLiteral("Digital Code:")
                                                     : QStringLiteral("Tone Frequency:"));
        toneValueBtn->setVisible(toneSelected);
        toneValueLabel->setVisible(toneSelected);
        dialog.adjustSize();
        dialog.setFixedWidth(kMemoryEditorWidth);
    };

    auto copyCurrentSettings = [this, nameEdit, frequencyEdit, offsetCombo, customOffsetModeCombo, customOffsetSpin,
                                toneOptionCombo, setTonePick, populateToneValues, populateOffsetOptions,
                                setOffsetSelection, updateCustomOffsetVisibility, updateToneValueVisibility]()
    {
        if (!m_model->isReady() || !m_vfo)
        {
            QMessageBox::information(this, "Copy Current Settings",
                                     "Connect to the radio and wait for sync before copying current settings.");
            return;
        }

        frequencyEdit->setText(memoryFrequencyLabel(m_vfo->frequencyHz()));
        if (nameEdit->text().trimmed().isEmpty())
        {
            nameEdit->setText(memoryFrequencyLabel(m_vfo->frequencyHz()));
        }
        populateOffsetOptions();
        setOffsetSelection(m_duplexMode, m_repeaterOffsetHz);
        updateCustomOffsetVisibility();
        toneOptionCombo->setCurrentIndex(qMax(0, toneOptionCombo->findData(m_toneAccessMode)));
        populateToneValues();
        updateToneValueVisibility();
        const bool isDtcs = isDtcsToneMode(m_toneAccessMode);
        const ushort toneValue = isDtcs ? m_dtcsCode : m_toneFrequency;
        const QString toneText = isDtcs ? dtcsCodeLabel(toneValue) : toneFrequencyLabel(toneValue);
        setTonePick(toneValue, toneText);
    };

    populateToneValues();
    connect(toneOptionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
            [populateToneValues, updateToneValueVisibility]()
            {
                populateToneValues();
                updateToneValueVisibility();
            });
    connect(toneValueBtn, &QPushButton::clicked, toneValueBtn,
            [toneValueBtn, toneOptionCombo, setTonePick]()
            {
                const auto mode = static_cast<rptAccessTxRx_t>(toneOptionCombo->currentData().toInt());
                if (mode == ratrNN)
                {
                    return;
                }
                QMenu menu(toneValueBtn);
                styleCompactMenu(&menu);
                auto styleToneGridButton = [](QPushButton* button)
                {
                    button->setFixedSize(54, 24);
                    button->setCursor(Qt::PointingHandCursor);
                    button->setStyleSheet(
                        QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 3px; "
                                       "color: %3; font-size: 11px; }"
                                       "QPushButton:hover { background: %4; border-color: %5; color: %6; }")
                            .arg(UiTheme::Color::Button, UiTheme::Color::BorderLight, UiTheme::Color::TextPrimary,
                                 UiTheme::Color::AccentDark, UiTheme::Color::Accent, UiTheme::Color::White));
                };
                auto* panel = new QWidget(&menu);
                auto* grid = new QGridLayout(panel);
                grid->setContentsMargins(6, 6, 6, 6);
                grid->setHorizontalSpacing(4);
                grid->setVerticalSpacing(4);
                if (isDtcsToneMode(mode))
                {
                    static constexpr int kCols = 6;
                    int idx = 0;
                    for (const ushort code : kDtcsCodes)
                    {
                        const QString label = dtcsCodeLabel(code);
                        auto* btn = new QPushButton(label, panel);
                        styleToneGridButton(btn);
                        connect(btn, &QPushButton::clicked, &menu,
                                [setTonePick, label, code, menuPtr = &menu]()
                                {
                                    setTonePick(code, label);
                                    menuPtr->close();
                                });
                        grid->addWidget(btn, idx / kCols, idx % kCols);
                        ++idx;
                    }
                }
                else
                {
                    static constexpr int kCols = 4;
                    int idx = 0;
                    for (const TonePreset& preset : kTonePresets)
                    {
                        const QString label = QString::fromLatin1(preset.label);
                        const ushort tone = preset.tone;
                        auto* btn = new QPushButton(label, panel);
                        styleToneGridButton(btn);
                        connect(btn, &QPushButton::clicked, &menu,
                                [setTonePick, label, tone, menuPtr = &menu]()
                                {
                                    setTonePick(tone, label);
                                    menuPtr->close();
                                });
                        grid->addWidget(btn, idx / kCols, idx % kCols);
                        ++idx;
                    }
                }
                auto* action = new QWidgetAction(&menu);
                action->setDefaultWidget(panel);
                menu.addAction(action);
                menu.exec(toneValueBtn->mapToGlobal(QPoint(0, toneValueBtn->height())));
            });
    connect(frequencyEdit, &QLineEdit::editingFinished, &dialog,
            [populateOffsetOptions, updateCustomOffsetVisibility]()
            {
                populateOffsetOptions();
                updateCustomOffsetVisibility();
            });
    connect(offsetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, updateCustomOffsetVisibility);

    auto applyMemoryToForm = [nameEdit, frequencyEdit, populateOffsetOptions, setOffsetSelection,
                              updateCustomOffsetVisibility, toneOptionCombo, setTonePick, notesEdit, populateToneValues,
                              updateToneValueVisibility](const MemoryRecord& memory)
    {
        nameEdit->setText(memory.name);
        frequencyEdit->setText(memoryFrequencyLabel(memory.receiveHz));
        populateOffsetOptions();
        setOffsetSelection(static_cast<duplexMode_t>(memory.duplexMode), memory.offsetHz);
        updateCustomOffsetVisibility();
        toneOptionCombo->setCurrentIndex(qMax(0, toneOptionCombo->findData(memory.toneMode)));
        populateToneValues();
        updateToneValueVisibility();
        if (memory.toneMode != ratrNN && memory.toneValue > 0)
        {
            const bool isDtcs = isDtcsToneMode(static_cast<rptAccessTxRx_t>(memory.toneMode));
            const QString toneText = isDtcs ? dtcsCodeLabel(memory.toneValue) : toneFrequencyLabel(memory.toneValue);
            setTonePick(memory.toneValue, toneText);
        }
        notesEdit->setPlainText(memory.notes);
    };

    if (editing)
    {
        const QVector<MemoryRecord> memories = loadMemories();
        auto it = std::find_if(memories.cbegin(), memories.cend(),
                               [&memoryId](const MemoryRecord& memory) { return memory.id == memoryId; });
        if (it == memories.cend())
        {
            QMessageBox::information(this, "Edit Memory", "Select a memory to edit.");
            return;
        }
        applyMemoryToForm(*it);
    }

    form->addRow("Name:", nameEdit);
    form->addRow("Frequency (RX):", frequencyEdit);
    form->addRow("Offset:", offsetCombo);
    form->addRow("Custom Offset:", customOffsetRow);
    form->addRow("Tone Option:", toneOptionCombo);
    form->addRow(toneValueLabel, toneValueBtn);
    form->addRow("Notes:", notesEdit);
    root->addLayout(form);
    updateCustomOffsetVisibility();
    updateToneValueVisibility();

    root->addSpacing(12);
    auto* buttonRow = new QWidget(&dialog);
    auto* buttonRowLayout = new QHBoxLayout(buttonRow);
    buttonRowLayout->setContentsMargins(0, 0, 0, 0);
    auto* copyButton = new QPushButton("Copy Current", buttonRow);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, buttonRow);
    buttonRowLayout->addWidget(copyButton, 0, Qt::AlignLeft);
    buttonRowLayout->addStretch(1);
    buttonRowLayout->addWidget(buttons, 0, Qt::AlignRight);
    root->addWidget(buttonRow);
    dialog.setFixedWidth(kMemoryEditorWidth);
    connect(copyButton, &QPushButton::clicked, &dialog, copyCurrentSettings);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    MemoryRecord memory;
    bool submitted = false;
    connect(buttons->button(QDialogButtonBox::Ok), &QPushButton::clicked, &dialog,
            [&]()
            {
                quint64 receiveHz = 0;
                if (!parseFrequencyText(frequencyEdit->text(), &receiveHz))
                {
                    QMessageBox::warning(&dialog, "Add/Edit Memory", "Enter a valid receive frequency.");
                    frequencyEdit->setFocus();
                    frequencyEdit->selectAll();
                    return;
                }

                const auto toneMode = static_cast<rptAccessTxRx_t>(toneOptionCombo->currentData().toInt());
                bool toneOk = true;
                ushort toneValue = 0;
                if (toneMode != ratrNN)
                {
                    toneOk = tonePickerValid;
                    toneValue = tonePickerValue;
                }
                if (!toneOk)
                {
                    QMessageBox::warning(&dialog, "Add/Edit Memory", "Select a valid tone frequency or DCS/DTCS code.");
                    toneValueBtn->setFocus();
                    return;
                }

                memory.id = editing ? memoryId : QUuid::createUuid().toString(QUuid::WithoutBraces);
                memory.name = nameEdit->text().trimmed();
                if (memory.name.length() > kMemoryNameMaxChars)
                {
                    QMessageBox::warning(
                        &dialog, "Add/Edit Memory",
                        QString("Memory names are limited to %1 characters.").arg(kMemoryNameMaxChars));
                    nameEdit->setFocus();
                    nameEdit->selectAll();
                    return;
                }
                if (memory.name.isEmpty())
                {
                    memory.name = memoryFrequencyLabel(receiveHz);
                }
                if (editing)
                {
                    const QVector<MemoryRecord> existingMemories = loadMemories();
                    auto existingIt =
                        std::find_if(existingMemories.cbegin(), existingMemories.cend(),
                                     [&memoryId](const MemoryRecord& existing) { return existing.id == memoryId; });
                    if (existingIt != existingMemories.cend())
                    {
                        memory.number = existingIt->number;
                    }
                }
                memory.receiveHz = receiveHz;
                memory.band = bandLabelForHz(memory.receiveHz);
                memory.bandKey = memoryBandKeyForHz(memory.receiveHz);
                if (offsetCombo->currentData(Qt::UserRole).toInt() == kMemoryOffsetCustom)
                {
                    memory.duplexMode = customOffsetModeCombo->currentData().toInt();
                    memory.offsetHz = static_cast<quint64>(customOffsetSpin->value() * 1000000.0 + 0.5);
                }
                else
                {
                    memory.duplexMode = offsetCombo->currentData(Qt::UserRole).toInt();
                    memory.offsetHz = offsetCombo->currentData(Qt::UserRole + 1).toULongLong();
                }
                memory.shift = offsetModeLabel(static_cast<duplexMode_t>(memory.duplexMode), memory.offsetHz);
                memory.toneMode = static_cast<int>(toneMode);
                memory.toneValue = toneValue;
                memory.toneOption = toneOptionLabel(toneMode);
                memory.toneFrequency = memoryToneFrequencyLabel(toneMode, toneValue);
                memory.notes = notesEdit->toPlainText().trimmed();
                submitted = true;
                dialog.accept();
            });

    centerPopupWindow(&dialog);
    if (dialog.exec() != QDialog::Accepted || !submitted)
    {
        return;
    }

    QVector<MemoryRecord> memories = loadMemories();
    if (editing)
    {
        auto current = std::find_if(memories.begin(), memories.end(),
                                    [&memory](const MemoryRecord& record) { return record.id == memory.id; });
        if (current != memories.end())
        {
            *current = memory;
        }
    }
    else
    {
        memories.append(memory);
    }
    if (!saveMemories(memories))
    {
        QMessageBox::warning(this, "Add/Edit Memory", "Could not save the memory.");
        return;
    }
    reloadMemoryTable();
    showMemoryWindow();
    showToast(editing ? "Memory updated" : "Memory stored");
}

void MainWindow::reloadMemoryTable()
{
    const QString bandFilter = m_memoryBandFilter ? m_memoryBandFilter->currentData().toString() : QString();
    const QVector<MemoryRecord> memories = loadMemories();
    if (m_memoryPanel)
    {
        m_memoryPanel->setMemories(memories, m_activeMemoryId);
    }

    if (!m_memoryTable)
    {
        return;
    }

    m_memoryTable->setSortingEnabled(false);
    m_memoryTable->setRowCount(0);
    int visibleCount = 0;
    for (const MemoryRecord& memory : memories)
    {
        if (!bandFilter.isEmpty() && memory.band != bandFilter)
        {
            continue;
        }

        const int row = m_memoryTable->rowCount();
        m_memoryTable->insertRow(row);

        auto setItem = [this, row](int column, const QString& text)
        {
            auto* item = new QTableWidgetItem(text);
            m_memoryTable->setItem(row, column, item);
            return item;
        };

        auto* numberItem = setItem(kMemoryNumberColumn, memoryNumberLabel(memory.number));
        numberItem->setData(Qt::UserRole, memory.number);
        numberItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        setItem(kMemoryNameColumn, memory.name);
        auto* frequencyItem = setItem(kMemoryFrequencyColumn, memoryFrequencyLabel(memory.receiveHz));
        frequencyItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(memory.receiveHz));
        setItem(kMemoryShiftColumn, memory.shift);
        auto* toneItem = setItem(kMemoryToneColumn, memoryToneDisplayLabel(memory));
        toneItem->setToolTip(toneItem->text());
        auto* notesItem = setItem(kMemoryNotesColumn, memory.notes);
        notesItem->setToolTip(memory.notes);
        auto* bandKeyItem =
            setItem(kMemoryBandKeyColumn, QStringLiteral("%1").arg(memory.bandKey, 4, 10, QLatin1Char('0')));
        bandKeyItem->setData(Qt::UserRole, memory.bandKey);
        setItem(kMemoryIdColumn, memory.id);
        ++visibleCount;
    }
    if (m_memoryCountLabel)
    {
        const int totalCount = memories.size();
        if (bandFilter.isEmpty())
        {
            m_memoryCountLabel->setText(
                QString("%1 %2 total").arg(totalCount).arg(totalCount == 1 ? "memory" : "memories"));
        }
        else
        {
            m_memoryCountLabel->setText(QString("%1 filtered / %2 total").arg(visibleCount).arg(totalCount));
        }
    }
}

void MainWindow::buildControlPanel(QVBoxLayout* vbox)
{
    auto* strip = new QWidget(centralWidget());
    strip->setObjectName("controlStrip");
    strip->setMinimumWidth(UiTheme::Size::MainWindowMinWidth);
    strip->setFocusPolicy(Qt::StrongFocus);
    strip->setStyleSheet(QStringLiteral("QWidget#controlStrip { background: %1; }"
                                        "QGroupBox { color: %2; border: 1px solid %3; border-radius: 3px; "
                                        "margin-top: 8px; padding-top: 4px; font-size: 10px; font-weight: bold; }"
                                        "QGroupBox::title { subcontrol-origin: border; subcontrol-position: top left; "
                                        "left: 8px; top: -6px; padding: 0 4px; color: %4; background: %1; }"
                                        "QLabel { color: %5; }"
                                        "QLineEdit { background: %6; border: 1px solid %7; border-radius: 3px; "
                                        "color: %8; padding: 0 8px; selection-background-color: %9; }")
                             .arg(UiTheme::Color::Panel, UiTheme::Color::TextStatusPrimary,
                                  UiTheme::Color::BorderMedium, UiTheme::Color::TextStatusSecondary,
                                  UiTheme::Color::TextStatusPrimary, UiTheme::Color::Field, UiTheme::Color::BorderFocus,
                                  UiTheme::Color::TextField, UiTheme::Color::AccentDark));
    auto* root = new QVBoxLayout(strip);
    root->setContentsMargins(kControlStripMargins);
    root->setSpacing(kNoSpacing);

    auto* controlRow = new QHBoxLayout;
    controlRow->setSpacing(kControlRowSpacing);

    auto makeSelectorButton = [strip](const QString& primary, const QString& secondary, const QString& name,
                                      const QString& description) -> QPushButton*
    {
        auto* button = new TwoLineButton(strip);
        button->setCheckable(false);
        button->setFixedSize(kSelectorButtonSize);
        button->setAccessibleDescription(description);
        button->setStyleSheet(commandButtonStyle(false));
        setSelectorButtonLines(button, primary, secondary);
        button->setAccessibleName(name);
        return button;
    };

    m_txPowerValue = 0;
    m_squelchValue = 0;
    m_rfGainValue = 0;
    m_lanModValue = qBound(0, AppSettings::instance().value("LanModLevel", 128).toInt(), 255);
    m_vfoPanel = new VfoPanel(QStringLiteral("VFO"), strip);
    m_vfoPanel->setFrequencyReadOnly(false);
    m_vfoPanel->setFrequencyText(QStringLiteral("---.---.---"));
    m_vfoPanel->setBandText(QStringLiteral("--"));
    m_vfoPanel->setModeText(QStringLiteral("--"));
    m_vfoPanel->setMemoryName(QString::fromLatin1(kNoActiveMemoryLabel), QStringLiteral("No active memory"));
    m_vfoPanel->setTxPower(0);
    m_vfoPanel->setLanMod(m_lanModValue);
    const int appVolume = appVolumeSettingValue();
    m_currentAfGain = appVolume;
    if (m_titleBar)
    {
        m_titleBar->setVolume(appVolume);
    }
    m_vfoPanel->setSquelch(0);
    m_memoryPanel = new MemoryPanel(strip);

    m_agcBtn = makeSelectorButton("AGC", QStringLiteral("MID"), "AGC mode", "Select AGC time constant.");
    m_attBtn = makeSelectorButton("ATT", QStringLiteral("OFF"), "Attenuator", "Toggle receiver attenuator.");
    m_attBtn->setCheckable(true);
    m_attBtn->setProperty("toggleLabel", "ATT");
    m_nbBtn = makeSelectorButton("NB", QStringLiteral("OFF"), "Noise blanker", "Toggle noise blanker.");
    m_nbBtn->setCheckable(true);
    m_nbBtn->setProperty("toggleLabel", "NB");
    m_notchBtn = makeSelectorButton("NOTCH", QStringLiteral("OFF"), "Notch", "Select notch filter mode.");
    m_nrBtn = makeSelectorButton("NR", QStringLiteral("OFF"), "Noise reduction", "Toggle noise reduction.");
    m_nrBtn->setCheckable(true);
    m_nrBtn->setProperty("toggleLabel", "NR");
    m_preBtn = makeSelectorButton("PRE", QStringLiteral("OFF"), "Preamp", "Select receiver preamp.");
    m_ritBtn = makeSelectorButton("RIT", QStringLiteral("OFF"), "RIT", "Set receiver incremental tuning offset.");
    m_rfGainBtn = makeSelectorButton("RF GAIN", QStringLiteral("OFF"), "RF gain", "Set receiver RF gain.");
    m_rfGainBtn->setProperty("levelControl", true);
    m_agcBtn->setToolTip(QStringLiteral("Automatic Gain Control (AGC)\n"
                                        "Controls receiver gain to produce a constant audio output level."));
    m_attBtn->setToolTip(QStringLiteral("Attenuator (ATT)\n"
                                        "Prevents a desired signal from becoming distorted in the presence of a very "
                                        "strong signal."));
    m_nbBtn->setToolTip(
        QStringLiteral("Noise Blanker (NB)\nEliminate pulse-type noise such as the noise from car ignitions."));
    m_notchBtn->setToolTip(QStringLiteral("Notch Filter\n"
                                          "Attenuates beat tones, tuning signals, and so on in the SSB, CW, RTTY, and "
                                          "AM modes."));
    m_nrBtn->setToolTip(
        QStringLiteral("Noise Reduction (NR)\nReduces random noise components and enhances signal audio."));
    m_preBtn->setToolTip(QStringLiteral("Preamplifier (PRE)\nAmplifies received signals in the receiver front end."));
    m_rfGainBtn->setToolTip(
        QStringLiteral("RF Gain\nIncrease/decrease the noise received from a nearby strong station."));
    m_ritBtn->setToolTip(
        QStringLiteral("Receive Increment Tuning (RIT)\nCompensate for differences in frequencies of other stations."));

    m_pttBtn = makeSelectorButton("PTT", QStringLiteral("OFF"), "PTT", "Hold to transmit.");
    m_pttBtn->setCheckable(false);

    m_compBtn = makeSelectorButton("COMP", QStringLiteral("OFF"), "Compressor", "Toggle speech compressor.");
    m_compBtn->setCheckable(true);
    m_compBtn->setProperty("toggleLabel", "COMP");
    m_offsetBtn =
        makeSelectorButton("OFFSET", QStringLiteral("SIMPLEX"), "Repeater offset", "Select repeater duplex offset.");
    m_offsetBtn->setToolTip("Select repeater duplex offset.");
    m_toneBtn = makeSelectorButton("TONE", QStringLiteral("OFF"), "Tone settings", "Select tone, CTCSS, or DTCS.");
    m_toneBtn->setToolTip("Select tone, CTCSS, or DTCS.");

    const ReceivePanel::Buttons receiveButtons{
        m_agcBtn, m_attBtn, m_nbBtn, m_notchBtn, m_nrBtn, m_preBtn, m_rfGainBtn, m_ritBtn,
    };
    const RepeaterPanel::Buttons repeaterButtons{
        m_offsetBtn,
        m_toneBtn,
    };
    const TransmitPanel::Buttons transmitButtons{
        m_compBtn,
    };
    auto* receiveGroup = new ReceivePanel(receiveButtons, strip);
    auto* repeaterGroup = new RepeaterPanel(repeaterButtons, strip);
    auto* transmitGroup = new TransmitPanel(transmitButtons, strip);
    auto* receiveStack = new QWidget(strip);
    auto* receiveStackLayout = new QVBoxLayout(receiveStack);
    receiveStackLayout->setContentsMargins(0, 0, 0, 0);
    receiveStackLayout->setSpacing(kControlGroupSpacing);
    auto* receiveBottomLayout = new QHBoxLayout;
    receiveBottomLayout->setContentsMargins(0, 0, 0, 0);
    receiveBottomLayout->setSpacing(kControlRowSpacing);
    receiveBottomLayout->addWidget(repeaterGroup, 1);
    receiveBottomLayout->addWidget(transmitGroup, 1);
    auto* pttGroup = new PttPanel(m_pttBtn, nullptr, strip);

    auto* receiveTopRow = new QHBoxLayout;
    receiveTopRow->setContentsMargins(0, 0, 0, 0);
    receiveTopRow->setSpacing(kControlRowSpacing);
    receiveTopRow->addWidget(receiveGroup, 1);
    receiveTopRow->addWidget(pttGroup);
    receiveStackLayout->addLayout(receiveTopRow);
    receiveStackLayout->addLayout(receiveBottomLayout);

    connect(m_agcBtn, &QPushButton::clicked, this, &MainWindow::showAgcMenu);
    connect(m_compBtn, &QPushButton::clicked, this,
            [this]()
            {
                if (m_vfo)
                {
                    m_vfo->setCompressor(!m_vfo->compressorOn());
                    setCommandButtonActive(m_compBtn, m_vfo->compressorOn());
                }
            });
    connect(m_notchBtn, &QPushButton::clicked, this, &MainWindow::showNotchMenu);
    connect(m_ritBtn, &QPushButton::clicked, this, &MainWindow::showRitMenu);
    connect(m_offsetBtn, &QPushButton::clicked, this, &MainWindow::showOffsetMenu);
    connect(m_toneBtn, &QPushButton::clicked, this, &MainWindow::showToneMenu);
    connect(m_memoryPanel, &MemoryPanel::memoryActivated, this,
            [this](const QString& memoryId) { selectMemoryById(memoryId, false); });

    controlRow->addWidget(m_vfoPanel);
    controlRow->addWidget(m_memoryPanel);
    controlRow->addWidget(receiveStack, 1);

    m_dtmfDialog = new DtmfDialog(this);
    m_metersDialog = new MetersDialog(this);
    m_dtmfDialog->hide();
    m_metersDialog->hide();

    m_dtmfPttOffTimer = new QTimer(this);
    m_dtmfPttOffTimer->setSingleShot(true);
    connect(m_dtmfPttOffTimer, &QTimer::timeout, this,
            [this]()
            {
                m_vfo->setPtt(false);
                m_dtmfSendActive = false;
                if (m_dtmfDialog)
                {
                    m_dtmfDialog->setSendInProgress(false);
                }
                // CI-V echo timing can cause the radio's TX-active acknowledgement to
                // arrive after the unkey command is queued, leaving pttChanged(true)
                // as the last state RadioModel sees. Reset the UI immediately; the
                // eventual pttChanged(false) from the radio is a harmless duplicate.
                onPttChanged(false);
            });

    root->addLayout(controlRow);
    vbox->addWidget(strip);
    strip->setFocus();

    connect(m_vfoPanel, &VfoPanel::frequencyReturnPressed, this, [this]() { commitFrequencyEdit(m_vfoPanel); });
    connect(m_pttBtn, &QPushButton::pressed, this, &MainWindow::onPttPressed);
    connect(m_pttBtn, &QPushButton::released, this, &MainWindow::onPttReleased);
    connect(m_dtmfDialog, &DtmfDialog::sendRequested, this, &MainWindow::onDtmfSendRequested);
    auto connectTxPowerSlider = [this](VfoPanel* widget)
    {
        connect(widget, &VfoPanel::txPowerChanged, this,
                [this](int value)
                {
                    if (m_applyingRadioSliderUpdate || !m_model->isReady() || m_controlsLocked)
                    {
                        return;
                    }
                    m_txPowerValue = qBound(0, value, 255);
                    if (auto* backend = m_model ? m_model->backend() : nullptr)
                    {
                        backend->setTxPower(m_txPowerValue);
                    }
                });
    };
    connectTxPowerSlider(m_vfoPanel);
    auto connectLanModSlider = [this](VfoPanel* widget)
    {
        connect(widget, &VfoPanel::lanModChanged, this,
                [this](int value)
                {
                    if (m_applyingRadioSliderUpdate || !m_model->isReady() || m_controlsLocked)
                    {
                        return;
                    }
                    m_lanModValue = qBound(0, value, 255);
                    AppSettings::instance().setValue(QStringLiteral("LanModLevel"), m_lanModValue);
                    m_model->setLanModLevel(m_lanModValue);
                });
    };
    connectLanModSlider(m_vfoPanel);
    auto connectSquelchSlider = [this](VfoPanel* widget)
    {
        connect(widget, &VfoPanel::squelchChanged, this,
                [this](int value)
                {
                    if (m_applyingRadioSliderUpdate || !m_model->isReady() || m_controlsLocked)
                    {
                        return;
                    }
                    if (auto* backend = m_model ? m_model->backend() : nullptr)
                    {
                        backend->setSquelch(value > 0, value);
                    }
                });
    };
    connectSquelchSlider(m_vfoPanel);
    connect(m_rfGainBtn, &QPushButton::clicked, this, &MainWindow::showRfGainMenu);
    auto showModeMenuFor = [this](const VfoPanel* widget)
    {
        if (!widget || !m_vfo)
        {
            return;
        }
        QMenu menu(this);
        styleCompactMenu(&menu);
        for (const QString& mode : m_vfo->availableModes())
        {
            menu.addAction(mode);
        }
        const QAction* chosen = menu.exec(widget->modeMenuPosition());
        if (chosen)
        {
            m_vfo->setMode(chosen->text());
        }
    };
    connect(m_vfoPanel, &VfoPanel::modeClicked, this, [showModeMenuFor, this]() { showModeMenuFor(m_vfoPanel); });
    auto showBandMenuFor = [this](const VfoPanel* widget)
    {
        if (!widget || !m_vfo)
        {
            return;
        }
        QMenu menu(this);
        styleCompactMenu(&menu);
        for (const availableBands band : sdr9700::kRadioUiBandOrder)
        {
            auto* action = menu.addAction(sdr9700::radioBandMenuLabel(band));
            action->setData(static_cast<int>(band));
        }
        const QAction* chosen = menu.exec(widget->bandMenuPosition());
        if (chosen)
        {
            const auto band = static_cast<availableBands>(chosen->data().toInt());
            const int bandIndex = sdr9700::radioBandUiIndex(band);
            if (bandIndex < 0)
            {
                return;
            }
            const quint64 defaultFrequency = sdr9700::radioBandDefaultFrequency(band);
            const quint64 hz =
                m_lastBandFrequencyHz[bandIndex] > 0 ? m_lastBandFrequencyHz[bandIndex] : defaultFrequency;
            if (hz == 0)
            {
                return;
            }
            qInfo(logGui()) << "VFO action: band selected" << sdr9700::radioBandShortLabel(band) << hz;
            m_vfo->setFrequencyHz(hz);
        }
    };
    connect(m_vfoPanel, &VfoPanel::bandClicked, this, [showBandMenuFor, this]() { showBandMenuFor(m_vfoPanel); });
    connect(m_vfoPanel, &VfoPanel::stepClicked, this,
            [this]()
            {
                if (!m_vfoPanel)
                {
                    return;
                }
                const int currentStep = tuningStepHz();
                QMenu menu(this);
                styleCompactMenu(&menu);
                for (const auto& preset : kStepPresets)
                {
                    auto* action = menu.addAction(QString::fromLatin1(preset.label));
                    action->setData(preset.hz);
                    action->setCheckable(true);
                    action->setChecked(preset.hz == currentStep);
                }
                const QAction* chosen = menu.exec(m_vfoPanel->stepMenuPosition());
                if (chosen)
                {
                    AppSettings::instance().setValue(QString::fromLatin1(kTuningStepHzSettingsKey),
                                                     chosen->data().toInt());
                    updateStepButton();
                    applyRadioTuningStep();
                }
            });
    updateStepButton();
    connect(m_nrBtn, &QPushButton::clicked, this,
            [this]()
            {
                if (m_vfo)
                {
                    m_vfo->setNrEnabled(!m_vfo->nrOn());
                    setCommandButtonActive(m_nrBtn, m_vfo->nrOn());
                }
            });
    connect(m_nbBtn, &QPushButton::clicked, this,
            [this]()
            {
                if (m_vfo)
                {
                    m_vfo->setNbEnabled(!m_vfo->nbOn());
                    setCommandButtonActive(m_nbBtn, m_vfo->nbOn());
                }
            });
    connect(m_preBtn, &QPushButton::clicked, this, &MainWindow::showPreampMenu);
    connect(m_attBtn, &QPushButton::clicked, this,
            [this]()
            {
                if (m_vfo)
                {
                    m_vfo->setAttenuatorEnabled(!m_vfo->attenuatorOn());
                    setCommandButtonActive(m_attBtn, m_vfo->attenuatorOn());
                }
            });

    m_currentAfGain = appVolumeSettingValue();
    m_savedAfGain = m_currentAfGain;
    resetRadioOwnedControlsForSync();
}

void MainWindow::updateTxIndicator(bool on)
{
    if (!m_txIndicator)
    {
        return;
    }
    if (m_txActive == on && !m_txIndicator->styleSheet().isEmpty())
    {
        if (!on && m_txDurationTimer && m_txDurationTimer->isActive())
        {
            m_txDurationTimer->stop();
            if (m_titleBar)
            {
                m_titleBar->setTxDurationActive(false);
            }
        }
        return;
    }
    m_txActive = on;
    updateIcomRC28Leds();
    if (m_vfoPanel)
    {
        m_vfoPanel->setAlcMode(on);
        m_vfoPanel->setSMeterValue(on ? 0 : qBound(0, static_cast<int>(m_lastSMeter * 100 / 255), 100));
    }
    if (on)
    {
        m_txIndicator->setStyleSheet(statusLabelStyle(UiTheme::Color::Danger, true));
        if (m_txSwrLabel)
        {
            m_txSwrLabel->setText(
                QStringLiteral("<span style='color:%1'>SWR --</span>").arg(UiTheme::Color::TextStatusSecondary));
        }
        m_txElapsed.start();
        updateTxDurationLabel();
        if (m_txDurationTimer)
        {
            m_txDurationTimer->start();
        }
    }
    else
    {
        m_txIndicator->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary, true));
        if (m_txDurationTimer)
        {
            m_txDurationTimer->stop();
        }
        if (m_txSwrLabel)
        {
            m_txSwrLabel->setText(
                QStringLiteral("<span style='color:%1'>SWR --</span>").arg(UiTheme::Color::TextStatusLabel));
        }
        if (m_titleBar)
        {
            m_titleBar->setTxDurationActive(false);
        }
        m_txPowerMeterWatts = 0.0;
        m_txSwr = 1.0;
        m_txAlc = 0.0;
        m_txCompressionDb = 0.0;
        m_txVoltageVolts = 0.0;
        m_txCurrentAmps = 0.0;
        m_txPowerMeterValid = false;
        m_txSwrValid = false;
        m_txAlcValid = false;
        m_txCompressionValid = false;
        m_txVoltageValid = false;
        m_txCurrentValid = false;
        if (m_metersDialog)
        {
            m_metersDialog->resetMeters();
            if (m_model && m_model->isReady())
            {
                m_metersDialog->setSMeter(m_lastSMeter);
            }
        }
        updateTxAudioMeter(0, 0);
    }
}

void MainWindow::updateTxDurationLabel()
{
    if (!m_titleBar)
    {
        return;
    }

    const qint64 secs = m_txElapsed.elapsed() / 1000;
    const int h = int(secs / 3600);
    const int m = int((secs % 3600) / 60);
    const int s = int(secs % 60);
    m_titleBar->setTxDuration(QStringLiteral("%1:%2:%3")
                                  .arg(h, 2, 10, QLatin1Char('0'))
                                  .arg(m, 2, 10, QLatin1Char('0'))
                                  .arg(s, 2, 10, QLatin1Char('0')),
                              m_txActive);
}

void MainWindow::updateStatusClock()
{
    if (!m_dateLabel || !m_timeLabel)
    {
        return;
    }

    const QDateTime now = m_statusClockUtc ? QDateTime::currentDateTimeUtc() : QDateTime::currentDateTime();
    m_dateLabel->setText(now.toString("yyyy-MM-dd"));
    m_timeLabel->setText(m_statusClockUtc ? now.toString("HH:mm:ss") + "Z" : now.toString("HH:mm:ss"));

    const QString tooltip = m_statusClockUtc ? QStringLiteral("UTC Time Mode\nClick to show local time.")
                                             : QStringLiteral("Local Time Mode\nClick to show UTC time.");
    m_dateLabel->setToolTip(tooltip);
    m_timeLabel->setToolTip(tooltip);
}

void MainWindow::toggleStatusClockMode()
{
    m_statusClockUtc = !m_statusClockUtc;
    AppSettings::instance().setValue("StatusClockUtc", m_statusClockUtc);
    updateStatusClock();
}

void MainWindow::updateSystemStats()
{
    if (!m_cpuLabel || !m_memLabel)
    {
        return;
    }

    // CPU usage from /proc/stat — delta between two calls
    auto cpuColor = [](int pct) -> const char*
    {
        if (pct < 50)
        {
            return UiTheme::Color::TextStatusSecondary;
        }
        if (pct < 80)
        {
            return UiTheme::Color::Warning;
        }
        return UiTheme::Color::Danger;
    };

    {
        QFile f(QStringLiteral("/proc/stat"));
        if (f.open(QIODevice::ReadOnly))
        {
            const QByteArray line = f.readLine();
            const QList<QByteArray> parts = line.split(' ');
            // Format: cpu  user nice system idle iowait irq softirq steal ...
            // Leading spaces mean parts[1] may be empty; filter empties
            QList<quint64> vals;
            for (const QByteArray& p : parts)
            {
                if (!p.isEmpty() && p != "cpu")
                {
                    vals.append(p.trimmed().toULongLong());
                }
            }

            if (vals.size() >= 4)
            {
                const quint64 idle = vals[3] + (vals.size() > 4 ? vals[4] : 0); // idle + iowait
                const quint64 total = std::accumulate(vals.cbegin(), vals.cend(), quint64{0});

                double cpuPct = 0.0;
                if (m_prevCpuTotal > 0 && total > m_prevCpuTotal)
                {
                    const quint64 dTotal = total - m_prevCpuTotal;
                    const quint64 dIdle = idle - m_prevCpuIdle;
                    cpuPct = 100.0 * static_cast<double>(dTotal - dIdle) / static_cast<double>(dTotal);
                    cpuPct = qBound(0.0, cpuPct, 100.0);
                }
                m_prevCpuTotal = total;
                m_prevCpuIdle = idle;

                const int cpuPctInt = static_cast<int>(cpuPct);
                m_cpuLabel->setText(QStringLiteral("<span style='color:%1'>%2%</span>")
                                        .arg(QLatin1String(cpuColor(cpuPctInt)), QString::number(cpuPct, 'f', 1)));
            }
        }
    }

    // Process RSS from /proc/self/status (VmRSS field)
    {
        QFile f(QStringLiteral("/proc/self/status"));
        if (f.open(QIODevice::ReadOnly))
        {
            const QString content = QString::fromLatin1(f.readAll());
            const QStringList lines = content.split('\n');
            const auto lineIt = std::find_if(lines.cbegin(), lines.cend(), [](const QString& line)
                                             { return line.startsWith(QLatin1String("VmRSS:")); });
            if (lineIt != lines.cend())
            {
                const QStringList parts = lineIt->simplified().split(' ', Qt::SkipEmptyParts);
                if (parts.size() >= 2)
                {
                    const double rssGb = parts[1].toDouble() / (1024.0 * 1024.0);
                    const QString rssStr = rssGb >= 1.0 ? QStringLiteral("%1G").arg(rssGb, 0, 'f', 1)
                                                        : QStringLiteral("%1M").arg(static_cast<int>(rssGb * 1024));
                    m_memLabel->setText(QStringLiteral("<span style='color:%1'>%2</span>")
                                            .arg(QLatin1String(UiTheme::Color::TextStatusSecondary), rssStr));
                }
            }
        }
    }
}

void MainWindow::buildStatusBar()
{
    statusBar()->setFixedHeight(46);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->setStyleSheet(QStringLiteral("QStatusBar { background: %1; border-top: 1px solid %2; }"
                                              "QStatusBar::item { border: none; }"
                                              "QLabel { background: transparent; }")
                                   .arg(UiTheme::Color::MenuBar, UiTheme::Color::StatusBorder));

    auto* container = new QWidget(this);
    auto* hbox = new QHBoxLayout(container);
    hbox->setContentsMargins(6, 0, 6, 0);
    hbox->setSpacing(0);

    auto makeSep = [this]() -> QLabel*
    {
        auto* s = new QLabel(QStringLiteral("·"), this);
        s->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 21px; }").arg("#7a8fa0"));
        s->setAlignment(Qt::AlignCenter);
        s->setFixedWidth(UiTheme::Size::StatusSeparatorWidth);
        return s;
    };

    // Measure all possible label strings at the label font size, then use the
    // widest result (+ padding) as the uniform width for every stack.
    const int uniformStackWidth = [&]()
    {
        QFont regular;
        regular.setPixelSize(12);
        QFont bold = regular;
        bold.setBold(true);
        const QFontMetrics fmR(regular);
        const QFontMetrics fmB(bold);

        int w = 0;
        // connection stack
        for (const char* s : {"Reconnecting", "Connected", "Disconnected"})
        {
            w = qMax(w, fmR.horizontalAdvance(QString::fromLatin1(s)));
        }
        w = qMax(w, fmB.horizontalAdvance(QStringLiteral("Radio")));
        // network stack
        for (const char* s : {"Excellent", "Good", "Fair", "Poor"})
        {
            w = qMax(w, fmR.horizontalAdvance(QString::fromLatin1(s)));
        }
        w = qMax(w, fmB.horizontalAdvance(QStringLiteral("Network")));
        // processor stack
        w = qMax(w, fmR.horizontalAdvance(QStringLiteral("100.0%")));
        w = qMax(w, fmB.horizontalAdvance(QStringLiteral("Processor")));
        // memory stack
        w = qMax(w, fmR.horizontalAdvance(QStringLiteral("999M")));
        w = qMax(w, fmR.horizontalAdvance(QStringLiteral("1.0G")));
        w = qMax(w, fmB.horizontalAdvance(QStringLiteral("Memory")));
        // TX stack
        w = qMax(w, fmR.horizontalAdvance(QStringLiteral("SWR 9.99")));
        w = qMax(w, fmB.horizontalAdvance(QStringLiteral("TX")));
        // time stack
        w = qMax(w, fmR.horizontalAdvance(QStringLiteral("0000-00-00")));
        w = qMax(w, fmR.horizontalAdvance(QStringLiteral("00:00:00Z")));

        return w + 16; // uniform padding buffer
    }();

    const int txStackWidth = [&]()
    {
        QFont regular;
        regular.setPixelSize(12);
        QFont bold = regular;
        bold.setBold(true);
        const QFontMetrics fmR(regular);
        const QFontMetrics fmB(bold);

        int w = fmR.horizontalAdvance(QStringLiteral("SWR 9.99"));
        w = qMax(w, fmB.horizontalAdvance(QStringLiteral("TX")));
        return w + 16;
    }();

    auto applyStatusContainerWidth = [](QWidget* widget, int width)
    {
        widget->setMinimumWidth(width);
        widget->setMaximumWidth(width);
    };

    auto* transmitStatusPanel = new QWidget(this);
    applyStatusContainerWidth(transmitStatusPanel, txStackWidth);
    transmitStatusPanel->setAccessibleName("Transmit status");
    transmitStatusPanel->setAccessibleDescription("Shows transmit state and SWR.");
    const QString txTooltip = QStringLiteral("Transmit status and SWR.");
    transmitStatusPanel->setToolTip(txTooltip);
    auto* transmitStatusLayout = new QVBoxLayout(transmitStatusPanel);
    transmitStatusLayout->setContentsMargins(0, 0, 0, 0);
    transmitStatusLayout->setSpacing(0);
    transmitStatusLayout->setAlignment(Qt::AlignVCenter);

    m_txIndicator = new QLabel("TX", this);
    m_txIndicator->setAlignment(Qt::AlignCenter);
    m_txIndicator->setToolTip(txTooltip);
    updateTxIndicator(false);

    m_txSwrLabel =
        new QLabel(QStringLiteral("<span style='color:%1'>SWR --</span>").arg(UiTheme::Color::TextStatusLabel), this);
    m_txSwrLabel->setTextFormat(Qt::RichText);
    m_txSwrLabel->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary));
    m_txSwrLabel->setAlignment(Qt::AlignCenter);
    m_txSwrLabel->setToolTip(QStringLiteral("Transmit SWR from the radio."));

    transmitStatusLayout->addWidget(m_txIndicator);
    transmitStatusLayout->addWidget(m_txSwrLabel);
    hbox->addWidget(transmitStatusPanel);
    hbox->addSpacing(16);

    m_txDurationTimer = new QTimer(this);
    m_txDurationTimer->setInterval(250);
    connect(m_txDurationTimer, &QTimer::timeout, this, &MainWindow::updateTxDurationLabel);

    m_toastLabel = new QLabel("", this);
    m_toastLabel->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusPrimary));
    m_toastLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    hbox->addWidget(m_toastLabel);
    m_statusLabel = m_toastLabel;

    hbox->addStretch(1);

    auto* connectionStatusPanel = new ClickableStatusPanel(this);
    applyStatusContainerWidth(connectionStatusPanel, uniformStackWidth);
    auto* connectionStatusLayout = new QVBoxLayout(connectionStatusPanel);
    connectionStatusLayout->setContentsMargins(0, 0, 0, 0);
    connectionStatusLayout->setSpacing(0);
    connectionStatusLayout->setAlignment(Qt::AlignVCenter);

    m_connDetailLabel = new QLabel("Radio", this);
    m_connDetailLabel->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary, true));
    m_connDetailLabel->setAlignment(Qt::AlignCenter);

    m_connStateName = QStringLiteral("Disconnected");
    m_connStateLabel = new QLabel(
        QStringLiteral("<span style='color:%1'>Disconnected</span>").arg(UiTheme::Color::TextStatusLabel), this);
    m_connStateLabel->setTextFormat(Qt::RichText);
    m_connStateLabel->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary));
    m_connStateLabel->setAlignment(Qt::AlignCenter);

    connectionStatusPanel->setCursor(Qt::PointingHandCursor);
    connectionStatusPanel->setAccessibleName("Radio connection");
    connectionStatusPanel->setAccessibleDescription("Click to open the radio chooser.");
    connectionStatusPanel->onClicked = [this]() { showRadioChooserDialog(); };

    connectionStatusLayout->addWidget(m_connDetailLabel);
    connectionStatusLayout->addWidget(m_connStateLabel);
    hbox->addWidget(connectionStatusPanel);
    updateConnectionTooltip();

    hbox->addWidget(makeSep());

    auto* networkStatusPanel = new QWidget(this);
    applyStatusContainerWidth(networkStatusPanel, uniformStackWidth);
    auto* networkStatusLayout = new QVBoxLayout(networkStatusPanel);
    networkStatusLayout->setContentsMargins(0, 0, 0, 0);
    networkStatusLayout->setSpacing(0);
    networkStatusLayout->setAlignment(Qt::AlignVCenter);
    m_netTitleLabel = new QLabel("Network", this);
    m_netTitleLabel->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary, true));
    m_netTitleLabel->setAlignment(Qt::AlignCenter);
    m_netQualLabel = new QLabel("—", this);
    m_netQualLabel->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary));
    m_netQualLabel->setAlignment(Qt::AlignCenter);
    m_netQualLabel->setTextFormat(Qt::RichText);
    networkStatusLayout->addWidget(m_netTitleLabel);
    networkStatusLayout->addWidget(m_netQualLabel);
    hbox->addWidget(networkStatusPanel);

    hbox->addWidget(makeSep());

    auto makeStatusTitle = [this](const QString& title) -> QLabel*
    {
        auto* label = new QLabel(title, this);
        label->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary, true));
        label->setAlignment(Qt::AlignCenter);
        return label;
    };

    auto makeStatusValue = [this]() -> QLabel*
    {
        auto* label = new QLabel(QStringLiteral("—"), this);
        label->setAlignment(Qt::AlignCenter);
        label->setTextFormat(Qt::RichText);
        label->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary));
        return label;
    };

    // Processor stack
    {
        auto* cpuStatusPanel = new QWidget(this);
        applyStatusContainerWidth(cpuStatusPanel, uniformStackWidth);
        auto* cpuStatusLayout = new QVBoxLayout(cpuStatusPanel);
        cpuStatusLayout->setContentsMargins(0, 0, 0, 0);
        cpuStatusLayout->setSpacing(0);
        cpuStatusLayout->setAlignment(Qt::AlignVCenter);

        auto* cpuTitleLabel = makeStatusTitle(QStringLiteral("Processor"));
        m_cpuLabel = makeStatusValue();
        cpuStatusLayout->addWidget(cpuTitleLabel);
        cpuStatusLayout->addWidget(m_cpuLabel);
        hbox->addWidget(cpuStatusPanel);
    }

    hbox->addWidget(makeSep());

    // Memory stack
    {
        auto* memoryStatusPanel = new QWidget(this);
        applyStatusContainerWidth(memoryStatusPanel, uniformStackWidth);
        auto* memoryStatusLayout = new QVBoxLayout(memoryStatusPanel);
        memoryStatusLayout->setContentsMargins(0, 0, 0, 0);
        memoryStatusLayout->setSpacing(0);
        memoryStatusLayout->setAlignment(Qt::AlignVCenter);

        auto* memoryTitleLabel = makeStatusTitle(QStringLiteral("Memory"));
        m_memLabel = makeStatusValue();
        memoryStatusLayout->addWidget(memoryTitleLabel);
        memoryStatusLayout->addWidget(m_memLabel);
        hbox->addWidget(memoryStatusPanel);
    }

    hbox->addWidget(makeSep());

    auto* clockStatusPanel = new ClickableStatusPanel(this);
    applyStatusContainerWidth(clockStatusPanel, uniformStackWidth);
    clockStatusPanel->setCursor(Qt::PointingHandCursor);
    clockStatusPanel->setAccessibleName("Status bar clock");
    clockStatusPanel->setAccessibleDescription("Click to switch between UTC and local time.");
    clockStatusPanel->onClicked = [this]() { toggleStatusClockMode(); };
    auto* clockStatusLayout = new QVBoxLayout(clockStatusPanel);
    clockStatusLayout->setContentsMargins(0, 0, 0, 0);
    clockStatusLayout->setSpacing(0);
    clockStatusLayout->setAlignment(Qt::AlignVCenter);

    m_dateLabel = new QLabel("", this);
    m_dateLabel->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary));
    m_dateLabel->setAlignment(Qt::AlignCenter);
    m_timeLabel = new QLabel("", this);
    m_timeLabel->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusSecondary));
    m_timeLabel->setAlignment(Qt::AlignCenter);

    clockStatusLayout->addWidget(m_dateLabel);
    clockStatusLayout->addWidget(m_timeLabel);
    hbox->addWidget(clockStatusPanel);

    // Never use showMessage(); it hides permanent widgets. All transient
    // messages go through showToast() which overlays m_statusLabel directly.
    statusBar()->addWidget(container, 1);

    // Toast timer restores connection status after a toast expires.
    m_toastTimer = new QTimer(this);
    m_toastTimer->setSingleShot(true);
    connect(m_toastTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_toastLabel)
                {
                    m_toastLabel->setText("");
                }
            });

    // AppSettings stores booleans as "True"/"False" strings per CONVENTIONS.md.
    m_statusClockUtc =
        AppSettings::instance().value("StatusClockUtc", "True").toString().compare("True", Qt::CaseInsensitive) == 0;
    updateStatusClock();
    updateNetworkQuality(0);
    auto* clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, this, &MainWindow::updateStatusClock);
    clockTimer->start(1000);

    auto* sysStatsTimer = new QTimer(this);
    connect(sysStatsTimer, &QTimer::timeout, this, &MainWindow::updateSystemStats);
    sysStatsTimer->start(2000);
    updateSystemStats();
}

void MainWindow::showRadioChooserDialog()
{
    if (m_radioChooserDialog)
    {
        bringDialogToFront(m_radioChooserDialog);
        return;
    }

    auto* dlg = new RadioChooserDialog(this);
    m_radioChooserDialog = dlg;
    connect(dlg, &QObject::destroyed, this,
            [this, dlg]()
            {
                if (m_radioChooserDialog == dlg)
                {
                    m_radioChooserDialog = nullptr;
                }
            });
    connect(dlg, &RadioChooserDialog::connectRequested, this,
            [this](const QUuid& id)
            {
                const RadioProfile* p = RadioProfileStore::instance().profileById(id);
                if (p)
                {
                    onConnectToProfile(*p);
                }
            });
    centerPopupWindow(dlg);
    QPointer<RadioChooserDialog> dlgGuard = dlg;
    dlg->exec();
    if (m_radioChooserDialog == dlgGuard)
    {
        m_radioChooserDialog = nullptr;
    }
    if (dlgGuard)
    {
        dlgGuard->deleteLater();
    }
}

void MainWindow::onConnectToProfile(const RadioProfile& profile)
{
    m_pendingProfileId = profile.id;
    m_radioHost = profile.host;
    m_radioPort = profile.port;
    m_radioUsername = profile.username;
    m_userDisconnected = false;
    m_model->connectToRadio(profile.host, profile.port, profile.username, profile.password);
    if (m_connStateLabel)
    {
        m_connStateName = QStringLiteral("Connecting");
        m_connStateLabel->setText(
            QStringLiteral("<span style='color:%1'>Connecting</span>").arg(UiTheme::Color::Accent));
    }
    updateConnectionTooltip();
}

void MainWindow::tryAutoConnect()
{
    RadioProfileStore& store = RadioProfileStore::instance();
    store.load();
    m_allowChooserOnDisconnect = false;

    const bool autoConnect = AppSettings::instance().value("AutoConnect", "True").toBool();
    if (autoConnect)
    {
        const QUuid lastId = store.lastProfileId();
        if (!lastId.isNull())
        {
            const RadioProfile* p = store.profileById(lastId);
            if (p)
            {
                onConnectToProfile(*p);
                return;
            }
        }
    }

    showRadioChooserDialog();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveWindowLayout();
#ifdef HAVE_HIDAPI
    if (m_icomRC28Manager)
    {
        m_icomRC28Manager->close();
    }
#endif
    m_userDisconnected = true;
    m_model->disconnectFromRadio();
    QMainWindow::closeEvent(event);
}

void MainWindow::restoreWindowLayout()
{
    const QSize fixedSize(UiTheme::Size::MainWindowMinWidth, UiTheme::Size::MainWindowMinHeight);
    const bool hasSavedPos = AppSettings::instance().contains(QStringLiteral("MainWindowX")) &&
                             AppSettings::instance().contains(QStringLiteral("MainWindowY"));
    if (hasSavedPos)
    {
        const QPoint savedTopLeft(AppSettings::instance().value(QStringLiteral("MainWindowX")).toInt(),
                                  AppSettings::instance().value(QStringLiteral("MainWindowY")).toInt());
        const QRect savedRect(savedTopLeft, fixedSize);
        const QPoint pos = availableScreenContains(savedRect)
                               ? savedTopLeft
                               : centeredRectInAvailableGeometry(fixedSize, availableGeometryFor(savedRect)).topLeft();
        move(pos);
    }
    else
    {
        const QString geometry = AppSettings::instance().value("MainWindowGeometry").toString();
        if (!geometry.isEmpty())
        {
            restoreGeometry(QByteArray::fromBase64(geometry.toLatin1()));
            setFixedSize(fixedSize);
            if (!availableScreenContains(frameGeometry()))
            {
                move(centeredRectInAvailableGeometry(fixedSize, availableGeometryFor(frameGeometry())).topLeft());
            }
        }
    }

    if (m_bandscopeDisplay)
    {
        int spectrumHeight = AppSettings::instance().value("BandscopeSpectrumHeight", -1).toInt();
        const QString migrationKey = QStringLiteral("BandscopeSpectrumHeight680Migrated");
        const QString height760MigrationKey = QStringLiteral("BandscopeSpectrumHeight760Migrated");
        const bool needsSpectrumHeightMigration = !AppSettings::instance().contains(migrationKey);
        const bool needsHeight760Migration = !AppSettings::instance().contains(height760MigrationKey);
        if (spectrumHeight > 0)
        {
            if (needsSpectrumHeightMigration)
            {
                spectrumHeight += kBandscopeSpectrumHeightIncrease;
            }
            if (needsHeight760Migration)
            {
                spectrumHeight += kBandscopeSpectrumHeight760Increase;
            }
            m_bandscopeDisplay->setSpectrumPaneHeight(spectrumHeight);
        }
        if (needsSpectrumHeightMigration)
        {
            AppSettings::instance().setValue(migrationKey, true);
        }
        if (needsHeight760Migration)
        {
            AppSettings::instance().setValue(height760MigrationKey, true);
        }
    }
}

void MainWindow::saveWindowLayout() const
{
    AppSettings::instance().setValue("MainWindowX", normalGeometry().x());
    AppSettings::instance().setValue("MainWindowY", normalGeometry().y());
    if (m_bandscopeDisplay)
    {
        AppSettings::instance().setValue("BandscopeSpectrumHeight", m_bandscopeDisplay->spectrumPaneHeight());
    }
}

void MainWindow::updateSpectrumVfoMarker()
{
    if (!m_bandscopeDisplay || !m_vfo)
    {
        return;
    }

    const quint64 displayedHz = m_displayBandscopeTuneHz > 0
                                    ? m_displayBandscopeTuneHz
                                    : (m_vfoFrequencyHz > 0 ? m_vfoFrequencyHz : m_vfo->frequencyHz());
    m_bandscopeDisplay->setVfoFrequency(displayedHz / 1e6);
}

void MainWindow::setRadioControlsEnabled(bool enabled)
{
    const bool controlsEnabled = enabled && !m_controlsLocked;
    if (m_vfoPanel)
    {
        m_vfoPanel->setEnabled(controlsEnabled);
        m_vfoPanel->setControlsEnabled(controlsEnabled);
    }
    if (m_memoryPanel)
    {
        m_memoryPanel->setEnabled(controlsEnabled);
    }
    if (m_titleBar)
    {
        m_titleBar->setVolumeEnabled(enabled);
    }

    for (auto* button : {m_agcBtn, m_nrBtn, m_nbBtn, m_notchBtn, m_preBtn, m_attBtn, m_ritBtn, m_compBtn, m_offsetBtn,
                         m_toneBtn, m_squelchBtn})
    {
        if (button)
        {
            button->setEnabled(controlsEnabled);
        }
    }
    if (m_muteBtn)
    {
        m_muteBtn->setEnabled(enabled);
    }
    if (m_pttBtn)
    {
        m_pttBtn->setEnabled(enabled);
    }
    if (m_rfGainBtn)
    {
        m_rfGainBtn->setEnabled(controlsEnabled);
    }

    if (m_bandscopeDisplay)
    {
        m_bandscopeDisplay->setInteractionLocked(m_controlsLocked);
    }
}

void MainWindow::resetRadioOwnedControlsForSync()
{
    m_vfoFrequencyHz = 0;
    m_lastSMeter = 0;
    m_txPowerValue = 0;
    m_rfGainValue = 0;
    m_squelchValue = 0;
    m_lanModValue = qBound(0, AppSettings::instance().value("LanModLevel", 128).toInt(), 255);
    m_duplexMode = dmSimplex;
    m_toneAccessMode = ratrNN;
    m_toneFrequency = 670;
    m_dtcsCode = 23;
    m_txPowerMeterWatts = 0.0;
    m_txSwr = 1.0;
    m_txAlc = 0.0;
    m_txCompressionDb = 0.0;
    m_txVoltageVolts = 0.0;
    m_txCurrentAmps = 0.0;
    m_txPowerMeterValid = false;
    m_txSwrValid = false;
    m_txAlcValid = false;
    m_txCompressionValid = false;
    m_txVoltageValid = false;
    m_txCurrentValid = false;

    if (m_vfoPanel)
    {
        if (!m_vfoPanel->frequencyHasFocus())
        {
            m_vfoPanel->setFrequencyText(QStringLiteral("---.---.---"));
        }
        m_vfoPanel->setBandText(QStringLiteral("--"));
        m_vfoPanel->setModeText(QStringLiteral("--"));
        m_vfoPanel->setMeterEnabled(false);
        m_vfoPanel->setAlcMode(false);
        m_vfoPanel->setSMeterValue(0);
        m_vfoPanel->setTxPower(0);
        m_vfoPanel->setLanMod(m_lanModValue);
        m_vfoPanel->setSquelch(0);
    }

    setCommandButtonActive(m_nrBtn, false);
    setCommandButtonActive(m_nbBtn, false);
    setSelectorButtonLines(m_notchBtn, QStringLiteral("NOTCH"), QStringLiteral("OFF"));
    setCommandButtonActive(m_notchBtn, false);
    setSelectorButtonLines(m_preBtn, QStringLiteral("PRE"), QStringLiteral("OFF"));
    setCommandButtonActive(m_preBtn, false);
    setCommandButtonActive(m_attBtn, false);
    setCommandButtonActive(m_compBtn, false);
    setSelectorButtonLines(m_agcBtn, QStringLiteral("AGC"), QStringLiteral("MID"));
    setSelectorButtonLines(m_ritBtn, QStringLiteral("RIT"), QStringLiteral("OFF"));
    setCommandButtonActive(m_ritBtn, false);
    updateOffsetButton();
    updateToneButton();
    updateSquelchButton();
    updateTxPowerButton();
    updateRfGainButton();
    updateTxIndicator(false);
    updateTxAudioMeter(0, 0);
    if (m_metersDialog)
    {
        m_metersDialog->resetMeters();
    }
}

void MainWindow::applyActiveVfoFromRadio()
{
    updateSpectrumVfoMarker();
}

void MainWindow::toggleControlLock()
{
    m_controlsLocked = !m_controlsLocked;
    updateControlLockIndicator();
    setRadioControlsEnabled(m_model && m_model->isConnected() && m_model->isReady());
    updateIcomRC28Leds();
}

void MainWindow::toggleMute()
{
    m_muted = !m_muted;
    if (m_muted)
    {
        m_savedAfGain = m_currentAfGain;
        m_currentAfGain = 0;
        if (m_titleBar)
        {
            m_titleBar->setVolume(0);
            m_titleBar->setMuted(true);
        }
        onAfGainChanged(0);
    }
    else
    {
        const int restored = qBound(0, m_savedAfGain, 255);
        m_currentAfGain = restored;
        if (m_titleBar)
        {
            m_titleBar->setVolume(restored);
            m_titleBar->setMuted(false);
        }
        onAfGainChanged(restored);
    }
    setCommandButtonActive(m_muteBtn, m_muted);
    updateIcomRC28Leds();
}

void MainWindow::cycleMode()
{
    if (!m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    const QStringList modes = m_vfo->availableModes();
    if (modes.isEmpty())
    {
        return;
    }

    const QString current = m_vfo->mode();
    const int index = modes.indexOf(current);
    const int nextIndex = index >= 0 ? (index + 1) % modes.size() : 0;
    m_vfo->setMode(modes.at(nextIndex));
}

void MainWindow::toggleRit()
{
    if (!m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    if (m_vfo->ritOn())
    {
        m_vfo->setRitEnabled(false);
    }
    else
    {
        m_vfo->setRitOffset(m_vfo->ritHz());
        m_vfo->setRitEnabled(true);
    }
    updateIcomRC28Leds();
}

#ifdef HAVE_HIDAPI
void MainWindow::dispatchIcomRC28Action(const QString& action)
{
    if (action.isEmpty() || action == QLatin1String("None"))
    {
        return;
    }

    if (action == QLatin1String("ToggleMute"))
    {
        toggleMute();
        return;
    }

    if (action == QLatin1String("ToggleLock"))
    {
        toggleControlLock();
        return;
    }

    if (!m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    if (action == QLatin1String("CycleStep"))
    {
        const int current = tuningStepHz();
        constexpr int nPresets = static_cast<int>(std::size(kStepPresets));
        int nextIdx = 0;
        for (int i = 0; i < nPresets; ++i)
        {
            if (kStepPresets[i].hz == current)
            {
                nextIdx = (i + 1) % nPresets;
                break;
            }
        }
        AppSettings::instance().setValue(QString::fromLatin1(kTuningStepHzSettingsKey), kStepPresets[nextIdx].hz);
        updateStepButton();
        applyRadioTuningStep();
    }
    else if (action == QLatin1String("ToggleRit"))
    {
        toggleRit();
    }
    else if (action == QLatin1String("CycleMode"))
    {
        cycleMode();
    }
}

void MainWindow::setIcomRC28Ptt(bool on)
{
    if (!m_vfo || !m_model->isReady())
    {
        return;
    }
    m_vfo->setPtt(on);
}

void MainWindow::updateIcomRC28Leds()
{
#ifdef HAVE_HIDAPI
    if (!m_icomRC28Manager || !m_icomRC28Manager->isOpen())
    {
        return;
    }

    // Active-low: clearing a bit turns the LED on.
    uint8_t b = IcomRC28Manager::kLedsAllOff;
    b &= ~IcomRC28Manager::kLedBitLink; // LINK always on while connected

    if (m_txActive)
    {
        b &= ~IcomRC28Manager::kLedBitTx;
    }

    // F-key LEDs reflect their hold action's active/toggled state
    auto holdActionActive = [this](const QString& actionId) -> bool
    {
        if (actionId == QLatin1String("ToggleMute"))
        {
            return m_muted;
        }
        if (actionId == QLatin1String("ToggleLock"))
        {
            return m_controlsLocked;
        }
        if (actionId == QLatin1String("ToggleRit"))
        {
            return m_vfo && m_vfo->ritOn();
        }
        return false;
    };

    if (holdActionActive(IcomRC28Manager::settingsField(QStringLiteral("f1Hold"), QStringLiteral("None"))))
    {
        b &= ~IcomRC28Manager::kLedBitF1;
    }
    if (holdActionActive(IcomRC28Manager::settingsField(QStringLiteral("f2Hold"), QStringLiteral("None"))))
    {
        b &= ~IcomRC28Manager::kLedBitF2;
    }

    m_icomRC28Manager->setIcomRC28Leds(b);
#endif
}

void MainWindow::handleIcomRC28Tune(int steps)
{
    qInfo(logIcomRC28()) << "Tune steps:" << steps;

    if (!m_vfo || !m_model->isReady())
    {
        return;
    }

    if (steps == 0)
    {
        return;
    }

    refreshIcomRC28EncoderSettings();
    if (m_icomRC28Sensitivity > 1)
    {
        if (m_icomRC28PulseAccum != 0 && ((steps > 0) != (m_icomRC28PulseAccum > 0)))
        {
            m_icomRC28PulseAccum = 0;
        }
        m_icomRC28PulseAccum += steps;
        const int dividedSteps = m_icomRC28PulseAccum / m_icomRC28Sensitivity;
        m_icomRC28PulseAccum -= dividedSteps * m_icomRC28Sensitivity;
        if (dividedSteps == 0)
        {
            if (m_icomRC28AutoSnap && m_icomRC28SnapTimer)
            {
                m_icomRC28SnapTimer->start();
            }
            return;
        }
        steps = dividedSteps;
    }
    if (m_icomRC28AutoSnap && m_icomRC28SnapTimer)
    {
        m_icomRC28SnapTimer->start();
    }

    const int stepHz = tuningStepHz();
    const qint64 currentHz =
        static_cast<qint64>(m_displayBandscopeTuneHz > 0 ? m_displayBandscopeTuneHz : m_vfo->frequencyHz());
    const qint64 targetHz = currentHz + static_cast<qint64>(steps) * stepHz;
    scheduleBandscopeTune(
        static_cast<quint64>(std::max<qint64>(static_cast<qint64>(kMinimumTuneFrequencyHz), targetHz)));
}

void MainWindow::refreshIcomRC28EncoderSettings()
{
    const int sensitivity =
        qBound(1, IcomRC28Manager::settingsField(QStringLiteral("sensitivity"), QStringLiteral("1")).toInt(), 10);
    if (sensitivity != m_icomRC28Sensitivity)
    {
        m_icomRC28PulseAccum = 0;
    }
    m_icomRC28Sensitivity = sensitivity;
    m_icomRC28AutoSnap =
        IcomRC28Manager::settingsField(QStringLiteral("autoSnap"), QStringLiteral("False")) == QLatin1String("True");
}

void MainWindow::snapIcomRC28FrequencyToKhz()
{
    if (!m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    const quint64 currentHz = m_displayBandscopeTuneHz > 0 ? m_displayBandscopeTuneHz : m_vfo->frequencyHz();
    const quint64 snappedHz =
        clampFrequencyHzToActiveBand(static_cast<quint64>(std::llround(currentHz / 1000.0)) * 1000ULL);
    if (snappedHz == currentHz)
    {
        return;
    }

    if (m_bandscopeTuneCommitTimer)
    {
        m_bandscopeTuneCommitTimer->stop();
    }
    if (m_bandscopeTuneReleaseTimer)
    {
        m_bandscopeTuneReleaseTimer->stop();
    }
    m_pendingBandscopeTuneHz = 0;
    m_displayBandscopeTuneHz = 0;
    m_bandscopeDisplayCenterHz = 0;
    m_bandscopeFixedPanStartHz = 0;
    m_bandscopeFixedPanEndHz = 0;
    if (m_bandscope)
    {
        m_bandscope->clearDisplayCenterHold();
    }
    clearActiveMemory();
    m_vfo->setFrequencyHz(snappedHz);
}

void MainWindow::handleIcomRC28Button(int button, int action)
{
    qInfo(logIcomRC28()) << "Button" << button << (action == 0 ? "press" : "release");

    if (!m_icomRC28Manager)
    {
        return;
    }

    if (button == 1 || button == 2)
    {
        const int index = button - 1;
        if (action == 0)
        {
            m_icomRC28ButtonDown[index] = true;
            m_icomRC28HoldConsumed[index] = false;
            if (m_icomRC28HoldTimers[index])
            {
                m_icomRC28HoldTimers[index]->start(600);
            }
            return;
        }

        m_icomRC28ButtonDown[index] = false;
        if (m_icomRC28HoldTimers[index] && m_icomRC28HoldTimers[index]->isActive() && !m_icomRC28HoldConsumed[index])
        {
            m_icomRC28HoldTimers[index]->stop();
            const QString field = index == 0 ? QStringLiteral("f1Press") : QStringLiteral("f2Press");
            dispatchIcomRC28Action(IcomRC28Manager::settingsField(field, QStringLiteral("None")));
        }
        m_icomRC28HoldConsumed[index] = false;
        return;
    }

    if (button != 3)
    {
        return;
    }

    const QString mode = IcomRC28Manager::settingsField(QStringLiteral("pttMode"), QStringLiteral("Disabled"));
    if (mode == QLatin1String("Disabled"))
    {
        return;
    }

    if (!m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    if (action == 0)
    {
        if (mode == QLatin1String("Latched"))
        {
            m_icomRC28PttLatched = !m_icomRC28PttLatched;
            setIcomRC28Ptt(m_icomRC28PttLatched);
        }
        else
        {
            m_icomRC28PttLatched = true;
            setIcomRC28Ptt(true);
        }
    }
    else if (mode == QLatin1String("Momentary"))
    {
        m_icomRC28PttLatched = false;
        setIcomRC28Ptt(false);
    }
}

#endif

void MainWindow::updateControlLockIndicator()
{
    if (m_titleBar)
    {
        m_titleBar->setLocked(m_controlsLocked);
    }

    if (!m_lockIndicator)
    {
        return;
    }

    m_lockIndicator->setText(QStringLiteral("LOCK"));
    m_lockIndicator->setStyleSheet(
        m_controlsLocked ? QStringLiteral("QLabel { color: %1; background: %2; font-weight: bold; font-size: 21px; "
                                          "border-radius: 4px; padding: 0px 1px; }")
                               .arg(UiTheme::Color::PanelDark, UiTheme::Color::Warning)
                         : QStringLiteral("QLabel { color: %1; font-weight: bold; font-size: 21px; }")
                               .arg(UiTheme::Color::TextStatusSecondary));
    const QString tooltip = m_controlsLocked ? QStringLiteral("Controls Locked\nClick to unlock.")
                                             : QStringLiteral("Controls Unlocked\nClick to lock.");
    m_lockIndicator->setToolTip(tooltip);
    if (m_lockWidget)
    {
        m_lockWidget->setToolTip(tooltip);
        m_lockWidget->setAccessibleName(m_controlsLocked ? QStringLiteral("Controls locked")
                                                         : QStringLiteral("Controls unlocked"));
    }
}

void MainWindow::updateBandscopeBandLimits(quint64 hz)
{
    if (!m_bandscope)
    {
        return;
    }

    quint64 startHz = 0;
    quint64 endHz = 0;
    const availableBands band = sdr9700::radioBandForFrequency(hz);
    if (band == bandUnknown || !sdr9700::radioBandEdges(band, &startHz, &endHz))
    {
        m_bandscope->clearFrequencyLimits();
        if (m_bandscopeDisplay)
        {
            m_bandscopeDisplay->clearFrequencyPanRange();
        }
        return;
    }

    m_bandscope->setFrequencyLimits(startHz / 1e6, endHz / 1e6);
    if (m_bandscopeDisplay)
    {
        m_bandscopeDisplay->setFrequencyPanRange(startHz / 1e6, endHz / 1e6);
    }
}

int MainWindow::tuningStepHz() const
{
    return qBound(
        1, AppSettings::instance().value(QString::fromLatin1(kTuningStepHzSettingsKey), kDefaultTuningStepHz).toInt(),
        10000000);
}

void MainWindow::applyRadioTuningStep()
{
    if (!m_model || !m_model->isReady())
    {
        return;
    }

    const int step = radioTuningStepForHz(tuningStepHz());
    if (step >= 0)
    {
        m_model->setTuningStep(step);
    }
}

void MainWindow::applyBandscopeSettings()
{
    if (!m_model || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    auto* backend = m_model->backend();
    if (!backend)
    {
        return;
    }

    const quint64 spanHz = AppSettings::instance()
                               .value(QString::fromLatin1(kBandscopeSpanHzSettingsKey),
                                      QVariant::fromValue<qulonglong>(kDefaultBandscopeSpanHz))
                               .toULongLong();

    backend->setScopeMode(0);
    backend->setScopeSpanHz(spanHz);
    m_bandscopeFixedPanStartHz = 0;
    m_bandscopeFixedPanEndHz = 0;
    if (m_bandscopeDisplay)
    {
        m_bandscopeDisplay->setCurrentSpanHz(spanHz);
    }
}

void MainWindow::updateStepButton()
{
    if (!m_vfoPanel)
    {
        return;
    }
    const int hz = tuningStepHz();
    const auto presetIt = std::find_if(std::begin(kStepPresets), std::end(kStepPresets),
                                       [hz](const StepPreset& preset) { return preset.hz == hz; });
    if (presetIt != std::end(kStepPresets))
    {
        m_vfoPanel->setStepText(QString::fromLatin1(presetIt->label));
        return;
    }
    // Custom value not in the preset list — format with units
    if (hz >= 1000000)
    {
        m_vfoPanel->setStepText(QStringLiteral("%1 MHz").arg(hz / 1000000));
    }
    else if (hz >= 1000)
    {
        m_vfoPanel->setStepText(QStringLiteral("%1 kHz").arg(hz / 1000.0, 0, 'f', hz % 1000 == 0 ? 0 : 3));
    }
    else
    {
        m_vfoPanel->setStepText(QStringLiteral("%1 Hz").arg(hz));
    }
}

quint64 MainWindow::roundFrequencyToStep(quint64 hz) const
{
    const quint64 stepHz = static_cast<quint64>(tuningStepHz());
    if (stepHz <= 1)
    {
        return hz;
    }
    return ((hz + stepHz / 2) / stepHz) * stepHz;
}

void MainWindow::panBandscopeToCenter(quint64 centerHz)
{
    if (!m_bandscopeDisplay || !m_bandscope || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    centerHz = clampBandscopeCenterHz(centerHz, m_bandscope->bandwidthMhz());
    const double bandwidthMhz = m_bandscope->bandwidthMhz();
    const quint64 bandwidthHz = static_cast<quint64>(std::llround(bandwidthMhz * 1e6));
    const quint64 startHz = centerHz - bandwidthHz / 2;
    const quint64 endHz = startHz + bandwidthHz;
    const double centerMhz = centerHz / 1e6;
    m_bandscopeDisplayCenterHz = centerHz;
    m_bandscopeDisplay->setFrequencyRange(centerMhz - bandwidthMhz / 2.0, centerMhz + bandwidthMhz / 2.0);
    if (auto* backend = m_model ? m_model->backend() : nullptr)
    {
        const auto changedEnough = [](quint64 current, quint64 previous)
        {
            return current > previous ? current - previous >= kBandscopeFixedPanMinDeltaHz
                                      : previous - current >= kBandscopeFixedPanMinDeltaHz;
        };
        if (m_bandscopeFixedPanStartHz == 0 || m_bandscopeFixedPanEndHz == 0 ||
            changedEnough(startHz, m_bandscopeFixedPanStartHz) || changedEnough(endHz, m_bandscopeFixedPanEndHz))
        {
            backend->setScopeFixedRangeHz(startHz, endHz);
            m_bandscopeFixedPanStartHz = startHz;
            m_bandscopeFixedPanEndHz = endHz;
        }
    }
    updateSpectrumVfoMarker();
}

quint64 MainWindow::clampBandscopeCenterHz(quint64 hz, double bandwidthMhz) const
{
    const quint64 referenceHz = m_vfoFrequencyHz > 0 ? m_vfoFrequencyHz : (m_vfo ? m_vfo->frequencyHz() : 0);
    availableBands band = sdr9700::radioBandForFrequency(referenceHz);
    if (band == bandUnknown)
    {
        band = sdr9700::radioBandForFrequency(hz);
    }

    quint64 startHz = 0;
    quint64 endHz = 0;
    if (band == bandUnknown || !sdr9700::radioBandEdges(band, &startHz, &endHz))
    {
        return hz;
    }

    const double startMhz = startHz / 1e6;
    const double endMhz = endHz / 1e6;
    const double halfBandwidthMhz = qMax(0.0, bandwidthMhz) / 2.0;
    const double minCenterMhz = startMhz + halfBandwidthMhz;
    const double maxCenterMhz = endMhz - halfBandwidthMhz;
    const double requestedMhz = hz / 1e6;
    const double clampedMhz =
        maxCenterMhz >= minCenterMhz ? qBound(minCenterMhz, requestedMhz, maxCenterMhz) : (startMhz + endMhz) / 2.0;
    return static_cast<quint64>(std::llround(clampedMhz * 1e6));
}

quint64 MainWindow::clampFrequencyHzToActiveBand(quint64 hz) const
{
    const quint64 referenceHz = m_vfoFrequencyHz > 0 ? m_vfoFrequencyHz : (m_vfo ? m_vfo->frequencyHz() : 0);
    availableBands band = sdr9700::radioBandForFrequency(referenceHz);
    if (band == bandUnknown)
    {
        band = sdr9700::radioBandForFrequency(hz);
    }

    quint64 startHz = 0;
    quint64 endHz = 0;
    if (band == bandUnknown || !sdr9700::radioBandEdges(band, &startHz, &endHz))
    {
        return hz;
    }

    return std::clamp(hz, startHz, endHz);
}

void MainWindow::scheduleBandscopeTune(quint64 hz)
{
    hz = clampFrequencyHzToActiveBand(roundFrequencyToStep(hz));
    const quint64 displayCenterHz = clampBandscopeCenterHz(hz, m_bandscope ? m_bandscope->bandwidthMhz() : 0.0);
    clearActiveMemory();
    m_pendingBandscopeTuneHz = hz;
    m_displayBandscopeTuneHz = hz;
    m_vfoFrequencyHz = hz;
    updateBandscopeBandLimits(hz);
    if (m_bandscopeFixedPanStartHz > 0 || m_bandscopeFixedPanEndHz > 0)
    {
        if (auto* backend = m_model ? m_model->backend() : nullptr)
        {
            const quint64 spanHz = AppSettings::instance()
                                       .value(QString::fromLatin1(kBandscopeSpanHzSettingsKey),
                                              QVariant::fromValue<qulonglong>(kDefaultBandscopeSpanHz))
                                       .toULongLong();
            backend->setScopeMode(0);
            backend->setScopeSpanHz(spanHz);
        }
        m_bandscopeFixedPanStartHz = 0;
        m_bandscopeFixedPanEndHz = 0;
    }
    if (m_bandscope)
    {
        m_bandscope->holdDisplayCenter(displayCenterHz / 1e6);
    }

    if (m_vfoPanel && !m_vfoPanel->frequencyHasFocus())
    {
        m_vfoPanel->setFrequencyText(formatFrequency(hz));
        m_vfoPanel->setBandText(bandLabelForHz(hz));
    }
    if (m_bandscope)
    {
        m_bandscope->centerOnFrequency(displayCenterHz / 1e6);
    }
    updateSpectrumVfoMarker();

    if (m_bandscopeTuneCommitTimer)
    {
        m_bandscopeTuneCommitTimer->start();
    }
    if (m_bandscopeTuneReleaseTimer)
    {
        m_bandscopeTuneReleaseTimer->start();
    }
}

void MainWindow::setActiveMemory(const QString& id, const QString& name, quint64 frequencyHz, int duplexMode,
                                 quint64 offsetHz, int toneMode, ushort toneValue)
{
    m_activeMemoryId = id;
    m_activeMemoryName = name.left(kMemoryNameMaxChars);
    m_activeMemoryFrequencyHz = frequencyHz;
    m_activeMemoryDuplexMode = static_cast<duplexMode_t>(duplexMode);
    m_activeMemoryOffsetHz = offsetHz;
    m_activeMemoryToneMode = static_cast<rptAccessTxRx_t>(toneMode);
    m_activeMemoryToneValue = toneValue;
    m_activeMemoryFrequencySettled = m_vfo && m_vfo->frequencyHz() == frequencyHz;
    m_activeMemoryDuplexSettled = m_duplexMode == m_activeMemoryDuplexMode;
    m_activeMemoryOffsetSettled = m_activeMemoryDuplexMode == dmSimplex || m_repeaterOffsetHz == offsetHz;
    m_activeMemoryToneModeSettled = m_toneAccessMode == m_activeMemoryToneMode;
    const bool isDtcs = isDtcsToneMode(m_activeMemoryToneMode);
    m_activeMemoryToneValueSettled = toneMode == ratrNN || (isDtcs && m_dtcsCode == toneValue) ||
                                     (!isDtcs && toneMode != ratrNN && m_toneFrequency == toneValue);
    updateMemoryNameLabel();
    if (m_memoryPanel)
    {
        m_memoryPanel->setActiveMemoryId(m_activeMemoryId);
    }
}

void MainWindow::clearActiveMemory()
{
    m_applyingMemorySelection = false;
    m_activeMemoryFrequencySettled = false;
    m_activeMemoryDuplexSettled = false;
    m_activeMemoryOffsetSettled = false;
    m_activeMemoryToneModeSettled = false;
    m_activeMemoryToneValueSettled = false;
    if (m_activeMemoryId.isEmpty())
    {
        return;
    }

    m_activeMemoryId.clear();
    m_activeMemoryName.clear();
    m_activeMemoryFrequencyHz = 0;
    m_activeMemoryDuplexMode = dmSimplex;
    m_activeMemoryOffsetHz = 0;
    m_activeMemoryToneMode = ratrNN;
    m_activeMemoryToneValue = 0;
    updateMemoryNameLabel();
    if (m_memoryPanel)
    {
        m_memoryPanel->setActiveMemoryId(QString());
    }
}

void MainWindow::checkIfMemorySelectionComplete()
{
    if (!m_applyingMemorySelection)
    {
        return;
    }
    if (m_activeMemoryFrequencySettled && m_activeMemoryDuplexSettled && m_activeMemoryOffsetSettled &&
        m_activeMemoryToneModeSettled && m_activeMemoryToneValueSettled)
    {
        m_applyingMemorySelection = false;
    }
}

void MainWindow::updateMemoryNameLabel()
{
    if (!m_vfoPanel)
    {
        return;
    }

    const QString text = m_activeMemoryId.isEmpty() ? QString::fromLatin1(kNoActiveMemoryLabel) : m_activeMemoryName;
    m_vfoPanel->setMemoryName(text, m_activeMemoryId.isEmpty() ? QStringLiteral("No active memory")
                                                               : QStringLiteral("Active memory: %1").arg(text));
}

void MainWindow::updateConnectionTooltip()
{
    if (!m_connStateLabel || !m_connDetailLabel)
    {
        return;
    }

    QStringList lines;
    lines << QStringLiteral("Radio Connection Status");
    lines << QStringLiteral("Radio ID: %1").arg(m_model ? m_model->radioName() : QStringLiteral("IC-9700"));
    lines << QStringLiteral("State: %1").arg(m_connStateName);
    if (!m_radioHost.isEmpty())
    {
        lines << QStringLiteral("Host: %1").arg(m_radioHost);
    }
    if (m_radioPort > 0)
    {
        lines << QStringLiteral("Port: %1").arg(m_radioPort);
    }
    if (!m_radioUsername.isEmpty())
    {
        lines << QStringLiteral("Username: %1").arg(m_radioUsername);
    }

    const QString tooltip = lines.join(QLatin1Char('\n'));
    m_connStateLabel->setToolTip(tooltip);
    m_connDetailLabel->setToolTip(tooltip);
}

void MainWindow::onConnectionChanged(bool connected)
{
    setRadioControlsEnabled(connected && m_model->isReady());
    resetRadioOwnedControlsForSync();

    if (connected)
    {
        m_reconnecting = false;
        m_lastErrorWasCredential = false;
        m_allowChooserOnDisconnect = true;
        if (m_reconnectTimer)
        {
            m_reconnectTimer->stop();
        }

        const int appVolume = appVolumeSettingValue();
        m_currentAfGain = appVolume;
        if (m_titleBar)
        {
            m_titleBar->setVolume(appVolume);
        }
        if (m_vfo)
        {
            m_vfo->setAfGain(appVolume);
        }

        if (m_connStateLabel)
        {
            m_connStateName = QStringLiteral("Syncing");
            m_connStateLabel->setText(
                QStringLiteral("<span style='color:%1'>Syncing</span>").arg(UiTheme::Color::Warning));
        }
        updateConnectionTooltip();
        if (!m_pendingProfileId.isNull())
        {
            if (!RadioProfileStore::instance().setLastProfileId(m_pendingProfileId))
            {
                showToast("Could not save last selected radio profile", 8000);
            }
        }
    }
    else
    {
        if (m_bandscopeTuneCommitTimer)
        {
            m_bandscopeTuneCommitTimer->stop();
        }
        if (m_bandscopeTuneReleaseTimer)
        {
            m_bandscopeTuneReleaseTimer->stop();
        }
        m_pendingBandscopeTuneHz = 0;
        m_displayBandscopeTuneHz = 0;
        m_bandscopeDisplayCenterHz = 0;
        m_bandscopeFixedPanStartHz = 0;
        m_bandscopeFixedPanEndHz = 0;
        if (m_bandscope)
        {
            m_bandscope->clearDisplayCenterHold();
        }
        clearActiveMemory();
        m_bandscopeDisplay->clearDisplay();
        m_bandscopeDisplay->clearFrequencyPanRange();
#ifdef HAVE_HIDAPI
        m_icomRC28PttLatched = false;
#endif
        updateNetworkQuality(0);

        const bool canReconnect = !m_userDisconnected && !m_lastErrorWasCredential && !m_pendingProfileId.isNull() &&
                                  RadioProfileStore::instance().profileById(m_pendingProfileId);

        if (canReconnect)
        {
            m_reconnecting = true;
            if (m_connStateLabel)
            {
                m_connStateName = QStringLiteral("Reconnecting");
                m_connStateLabel->setText(
                    QStringLiteral("<span style='color:%1'>Reconnecting</span>").arg(UiTheme::Color::Danger));
            }
            // Keep the IP in the detail label so the user knows which radio is reconnecting.
            updateConnectionTooltip();

            if (!m_reconnectTimer)
            {
                m_reconnectTimer = new QTimer(this);
                m_reconnectTimer->setSingleShot(true);
                connect(m_reconnectTimer, &QTimer::timeout, this,
                        [this]()
                        {
                            if (!m_reconnecting)
                            {
                                return;
                            }
                            const RadioProfile* p = RadioProfileStore::instance().profileById(m_pendingProfileId);
                            if (p)
                            {
                                onConnectToProfile(*p);
                            }
                            else
                            {
                                m_reconnecting = false;
                            }
                        });
            }
            m_reconnectTimer->start(5000);
        }
        else
        {
            const bool wasUserDisconnected = m_userDisconnected;
            m_reconnecting = false;
            m_userDisconnected = false;
            m_lastErrorWasCredential = false;
            if (m_connStateLabel)
            {
                m_connStateName = QStringLiteral("Disconnected");
                m_connStateLabel->setText(
                    QStringLiteral("<span style='color:%1'>Disconnected</span>").arg(UiTheme::Color::TextStatusLabel));
            }
            updateConnectionTooltip();
            if (!wasUserDisconnected && m_allowChooserOnDisconnect)
            {
                QTimer::singleShot(0, this, [this]() { showRadioChooserDialog(); });
            }
        }
    }
}

void MainWindow::onRadioReadyChanged(bool ready)
{
    const bool connected = m_model->isConnected();
    setRadioControlsEnabled(connected && ready);
    if (m_vfoPanel)
    {
        m_vfoPanel->setMeterEnabled(ready);
        if (!ready)
        {
            m_vfoPanel->setAlcMode(false);
            m_vfoPanel->setSMeterValue(0);
        }
    }
    if (!m_connStateLabel || !connected)
    {
        return;
    }

    if (ready)
    {
        applyRadioTuningStep();
        applyBandscopeSettings();
        m_connStateName = QStringLiteral("Connected");
        m_connStateLabel->setText(
            QStringLiteral("<span style='color:%1'>Connected</span>").arg(UiTheme::Color::Success));
    }
    else
    {
        m_connStateName = QStringLiteral("Syncing");
        m_connStateLabel->setText(QStringLiteral("<span style='color:%1'>Syncing</span>").arg(UiTheme::Color::Warning));
    }
    updateConnectionTooltip();
}

void MainWindow::onFrequencyChanged(quint64 hz)
{
    if (!m_activeMemoryId.isEmpty())
    {
        if (hz == m_activeMemoryFrequencyHz)
        {
            m_activeMemoryFrequencySettled = true;
            checkIfMemorySelectionComplete();
        }
        else if (m_activeMemoryFrequencySettled && !m_applyingMemorySelection)
        {
            clearActiveMemory();
        }
    }

    if (m_displayBandscopeTuneHz > 0 && m_bandscopeTuneReleaseTimer && m_bandscopeTuneReleaseTimer->isActive())
    {
        if (hz != m_displayBandscopeTuneHz)
        {
            qDebug(logBandscope()) << "Ignoring transient bandscope tune confirmation"
                                   << "confirmedHz=" << hz << "displayHz=" << m_displayBandscopeTuneHz;
            return;
        }
        m_pendingBandscopeTuneHz = 0;
        m_displayBandscopeTuneHz = 0;
        m_bandscopeDisplayCenterHz = 0;
        m_bandscopeFixedPanStartHz = 0;
        m_bandscopeFixedPanEndHz = 0;
        m_bandscopeTuneReleaseTimer->stop();
        if (m_bandscope)
        {
            m_bandscope->clearDisplayCenterHold();
        }
    }

    m_vfoFrequencyHz = hz;
    updateBandscopeBandLimits(hz);
    if (const int bandIndex = vfoBandIndexForHz(hz); bandIndex >= 0)
    {
        m_lastBandFrequencyHz[bandIndex] = hz;
    }
    qInfo(logGui()) << "VFO route: selected MAIN frequency" << hz;
    if (m_vfoPanel && !m_vfoPanel->frequencyHasFocus())
    {
        m_vfoPanel->setFrequencyText(formatFrequency(hz));
    }
    if (m_vfoPanel)
    {
        m_vfoPanel->setBandText(bandLabelForHz(hz));
    }
    updateSpectrumVfoMarker();
}

void MainWindow::onModeChanged(const QString& mode)
{
    if (m_vfoPanel)
    {
        m_vfoPanel->setModeText(mode);
    }
}

void MainWindow::onSmeterChanged(int s)
{
    if (!m_model->isReady())
    {
        return;
    }

    m_lastSMeter = qBound(0, s, 255);
    if (m_metersDialog)
    {
        m_metersDialog->setSMeter(m_lastSMeter);
    }
    if (!m_txActive)
    {
        if (m_vfoPanel)
        {
            m_vfoPanel->setSMeterValue(qBound(0, static_cast<int>(m_lastSMeter * 100 / 255), 100));
        }
    }
}

void MainWindow::onSwrChanged(double swr)
{
    m_txSwr = qBound(1.0, swr, 6.0);
    m_txSwrValid = true;
    if (m_metersDialog)
    {
        m_metersDialog->setSwr(m_txSwr);
    }
    if (!m_vfoPanel || !m_txActive)
    {
        return;
    }
    const char* swrColor = swr <= 1.7   ? UiTheme::Color::Success
                           : swr <= 2.7 ? UiTheme::Color::Warning
                                        : UiTheme::Color::Danger;
    if (m_txSwrLabel)
    {
        m_txSwrLabel->setText(QStringLiteral("<span style='color:%1'>SWR %2</span>")
                                  .arg(QString::fromLatin1(swrColor))
                                  .arg(swr, 0, 'f', 2));
    }
}

void MainWindow::onAlcChanged(double alc)
{
    m_txAlc = qBound(0.0, alc, 2.0);
    m_txAlcValid = true;
    if (m_metersDialog)
    {
        m_metersDialog->setAlc(m_txAlc);
    }
    if (!m_vfoPanel || !m_txActive)
    {
        return;
    }
    m_vfoPanel->setAlc(m_txAlc);
}

void MainWindow::updateTxAudioMeter(int peak, int rms)
{
    m_txAudioPeak = qBound(0, peak, 255);
    m_txAudioRms = qBound(0, rms, 255);
    if (m_metersDialog)
    {
        m_metersDialog->setTransmitAudioLevel(m_txAudioPeak, m_txAudioRms);
    }
}

void MainWindow::onSpectrumReady(const QVector<float>& levels, double start, double end, bool outOfRange)
{
    const quint64 referenceHz = m_vfoFrequencyHz > 0 ? m_vfoFrequencyHz : (m_vfo ? m_vfo->frequencyHz() : 0);
    if (referenceHz > 0)
    {
        updateBandscopeBandLimits(referenceHz);
    }
    m_bandscopeDisplay->setDataFrequencyRange(start, end);
    updateSpectrumVfoMarker();
    m_bandscopeDisplay->updateSpectrum(levels, outOfRange);
}

void MainWindow::showToast(const QString& msg, int durationMs)
{
    if (!m_toastLabel || !m_toastTimer)
    {
        return;
    }
    m_toastTimer->stop();
    m_toastLabel->setText(msg);
    m_toastLabel->setStyleSheet(statusLabelStyle(UiTheme::Color::TextStatusPrimary));
    m_toastTimer->start(durationMs);
}

void MainWindow::updateNetworkQuality(int rttMs)
{
    if (!m_netQualLabel)
    {
        return;
    }
    QString label, color;
    if (rttMs <= 0)
    {
        label = "—";
        color = UiTheme::Color::TextStatusLabel;
    }
    else if (rttMs < 20)
    {
        label = "Excellent";
        color = UiTheme::Color::Success;
    }
    else if (rttMs < 50)
    {
        label = "Good";
        color = UiTheme::Color::Accent;
    }
    else if (rttMs < 100)
    {
        label = "Fair";
        color = UiTheme::Color::Warning;
    }
    else
    {
        label = "Poor";
        color = UiTheme::Color::Danger;
    }

    const QString text = QString("<span style='color:%1'>%2</span>").arg(color, label);
    m_netQualLabel->setText(text);
    const QString tooltip = rttMs > 0 ? QStringLiteral("Network Performance\nRTT: %1 ms").arg(rttMs)
                                      : QStringLiteral("Network Performance\nRTT: unavailable");
    if (m_netTitleLabel)
    {
        m_netTitleLabel->setToolTip(tooltip);
    }
    m_netQualLabel->setToolTip(tooltip);
}

void MainWindow::onStatusMessage(const QString& msg)
{
    showToast(msg, 5000);
}

void MainWindow::onError(const QString& msg)
{
    showToast(QString("Error: %1").arg(msg), 8000);
    m_lastErrorWasCredential = msg.startsWith(QStringLiteral("Login denied"), Qt::CaseInsensitive);
}

void MainWindow::onAfGainChanged(int value)
{
    m_vfo->setAfGain(value);
}

void MainWindow::onRfGainChanged(int value)
{
    m_rfGainValue = qBound(0, value, 255);
    updateRfGainButton();
}

void MainWindow::onTxPowerChanged(int value)
{
    m_txPowerValue = qBound(0, value, 255);
    if (m_vfoPanel)
    {
        m_vfoPanel->setTxPower(m_txPowerValue);
    }
    updateTxPowerButton();
}

void MainWindow::showDtmfDialog()
{
    if (!m_dtmfDialog)
    {
        return;
    }

    bringDialogToFront(m_dtmfDialog);
}

void MainWindow::showMetersDialog()
{
    if (!m_metersDialog)
    {
        return;
    }

    if (m_model && m_model->isReady())
    {
        m_metersDialog->resetMeters();
        m_metersDialog->setSMeter(m_lastSMeter);
        if (m_txPowerMeterValid)
        {
            m_metersDialog->setPowerMeter(m_txPowerMeterWatts);
        }
        if (m_txSwrValid)
        {
            m_metersDialog->setSwr(m_txSwr);
        }
        if (m_txAlcValid)
        {
            m_metersDialog->setAlc(m_txAlc);
        }
        if (m_txCompressionValid)
        {
            m_metersDialog->setCompressionMeter(m_txCompressionDb);
        }
        if (m_txVoltageValid)
        {
            m_metersDialog->setVoltageMeter(m_txVoltageVolts);
        }
        if (m_txCurrentValid)
        {
            m_metersDialog->setCurrentMeter(m_txCurrentAmps);
        }
        m_metersDialog->setTransmitAudioLevel(m_txAudioPeak, m_txAudioRms);
    }
    else
    {
        m_metersDialog->resetMeters();
    }
    bringDialogToFront(m_metersDialog);
}

void MainWindow::onDtmfSendRequested(const QString& digits)
{
    if (m_dtmfSendActive || !m_vfo || digits.isEmpty())
    {
        return;
    }

    m_dtmfSendActive = true;
    if (m_dtmfDialog)
    {
        m_dtmfDialog->setSendInProgress(true);
    }

    m_vfo->setPtt(true);

    // The DTMF PCM buffer queued to UdpAudio is consumed only after the 1000 ms
    // TX gate expires. kTrailMs must cover the remaining gate window after the
    // lead-in (600 ms) plus some silence after the last tone.
    constexpr int kLeadInMs = 400;
    constexpr int kPerDigitMs = 400;
    constexpr int kTrailMs = 900;

    QTimer::singleShot(kLeadInMs, this,
                       [this, digits]()
                       {
                           if (m_dtmfSendActive && m_vfo)
                           {
                               m_vfo->sendDtmf(digits);
                           }
                       });

    m_dtmfPttOffTimer->start(kLeadInMs + digits.length() * kPerDigitMs + kTrailMs);
}

void MainWindow::onPttPressed()
{
    if (!m_vfo || !m_model->isReady())
    {
        return;
    }

    m_vfo->setPtt(true);
}

void MainWindow::onPttReleased()
{
    if (!m_vfo)
    {
        return;
    }
    m_vfo->setPtt(false);
}

void MainWindow::onPttChanged(bool on)
{
#ifdef HAVE_HIDAPI
    m_icomRC28PttLatched = on;
#endif
    if (!on && m_dtmfSendActive)
    {
        m_dtmfSendActive = false;
        if (m_dtmfPttOffTimer)
        {
            m_dtmfPttOffTimer->stop();
        }
        if (m_dtmfDialog)
        {
            m_dtmfDialog->setSendInProgress(false);
        }
    }
    updateTxIndicator(on);
    m_pttBtn->setProperty("pttActive", on);
    m_pttBtn->style()->unpolish(m_pttBtn);
    m_pttBtn->style()->polish(m_pttBtn);
    m_pttBtn->update();
    setSelectorButtonLines(m_pttBtn, QStringLiteral("PTT"), on ? QStringLiteral("ON") : QStringLiteral("OFF"));
}

void MainWindow::onDuplexModeChanged(duplexMode_t mode)
{
    m_duplexMode = mode;
    if (!m_activeMemoryId.isEmpty())
    {
        if (mode == m_activeMemoryDuplexMode)
        {
            m_activeMemoryDuplexSettled = true;
            checkIfMemorySelectionComplete();
        }
        else if (m_activeMemoryDuplexSettled && !m_applyingMemorySelection)
        {
            clearActiveMemory();
        }
    }
    updateOffsetButton();
}

void MainWindow::onRepeaterOffsetChanged(quint64 hz)
{
    m_repeaterOffsetHz = hz;
    if (!m_activeMemoryId.isEmpty() && m_activeMemoryDuplexMode != dmSimplex)
    {
        if (hz == m_activeMemoryOffsetHz)
        {
            m_activeMemoryOffsetSettled = true;
            checkIfMemorySelectionComplete();
        }
        else if (m_activeMemoryOffsetSettled && !m_applyingMemorySelection)
        {
            clearActiveMemory();
        }
    }
    updateOffsetButton();
}

void MainWindow::onToneAccessModeChanged(rptAccessTxRx_t mode)
{
    m_toneAccessMode = mode;
    if (!m_activeMemoryId.isEmpty())
    {
        if (mode == m_activeMemoryToneMode)
        {
            m_activeMemoryToneModeSettled = true;
            checkIfMemorySelectionComplete();
        }
        else if (m_activeMemoryToneModeSettled && !m_applyingMemorySelection)
        {
            clearActiveMemory();
        }
    }
    updateToneButton();
}

void MainWindow::onToneFrequencyChanged(ushort tone)
{
    m_toneFrequency = tone;
    if (!m_activeMemoryId.isEmpty() && m_activeMemoryToneMode != ratrNN && !isDtcsToneMode(m_activeMemoryToneMode))
    {
        if (tone == m_activeMemoryToneValue)
        {
            m_activeMemoryToneValueSettled = true;
            checkIfMemorySelectionComplete();
        }
        else if (m_activeMemoryToneValueSettled && !m_applyingMemorySelection)
        {
            clearActiveMemory();
        }
    }
    updateToneButton();
}

void MainWindow::onDtcsCodeChanged(ushort code)
{
    m_dtcsCode = code;
    if (!m_activeMemoryId.isEmpty() && isDtcsToneMode(m_activeMemoryToneMode))
    {
        if (code == m_activeMemoryToneValue)
        {
            m_activeMemoryToneValueSettled = true;
            checkIfMemorySelectionComplete();
        }
        else if (m_activeMemoryToneValueSettled && !m_applyingMemorySelection)
        {
            clearActiveMemory();
        }
    }
    updateToneButton();
}

void MainWindow::onSpectrumClicked(double freqMhz)
{
    if (!m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    clearActiveMemory();
    scheduleBandscopeTune(clampFrequencyHzToActiveBand(static_cast<quint64>(std::llround(freqMhz * 1e6))));
}

void MainWindow::commitFrequencyEdit(VfoPanel* panel)
{
    auto* backend = m_model ? m_model->backend() : nullptr;
    if (!panel || !m_vfo || !backend || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }
    if (panel != m_vfoPanel)
    {
        panel->setFrequencyText(QStringLiteral("---.---.---"));
        panel->clearFrequencyFocus();
        return;
    }

    const quint64 currentHz = m_vfoFrequencyHz;
    const auto restoreFrequencyText = [this, panel, currentHz]()
    {
        if (currentHz > 0)
        {
            panel->setFrequencyText(formatFrequency(currentHz));
        }
        else
        {
            panel->setFrequencyText(QStringLiteral("---.---.---"));
        }
    };

    quint64 hz = 0;
    if (!parseFrequencyText(panel->frequencyText(), &hz))
    {
        restoreFrequencyText();
        return;
    }

    clearActiveMemory();
    backend->setFrequencyHz(hz);
    panel->clearFrequencyFocus();
}

void MainWindow::showAgcMenu()
{
    if (!m_agcBtn || !m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }
    QMenu menu(this);
    styleCompactMenu(&menu);
    static const struct
    {
        const char* mode;
        const char* label;
    } kItems[] = {{"fast", "FAST"}, {"mid", "MID"}, {"slow", "SLOW"}};
    for (const auto& item : kItems)
    {
        auto* act = menu.addAction(QString::fromLatin1(item.label));
        const QString modeStr = QString::fromLatin1(item.mode);
        connect(act, &QAction::triggered, this, [this, modeStr]() { m_vfo->setAgcMode(modeStr); });
    }
    menu.exec(m_agcBtn->mapToGlobal(QPoint(0, m_agcBtn->height())));
}

void MainWindow::showPreampMenu()
{
    if (!m_preBtn || !m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    QMenu menu(this);
    styleCompactMenu(&menu);
    static const struct
    {
        const char* label;
        int level;
    } kItems[] = {{"OFF", 0}, {"INT", 1}, {"EXT", 2}, {"INT+EXT", 3}};
    for (const auto& item : kItems)
    {
        auto* act = menu.addAction(QString::fromLatin1(item.label));
        connect(act, &QAction::triggered, this, [this, item]() { m_vfo->setPreampLevel(item.level); });
    }
    menu.exec(m_preBtn->mapToGlobal(QPoint(0, m_preBtn->height())));
}

void MainWindow::updatePreampButton()
{
    if (!m_preBtn || !m_vfo)
    {
        return;
    }

    const int level = m_vfo->preampLevel();
    setSelectorButtonLines(m_preBtn, QStringLiteral("PRE"), preampLevelLabel(level));
    setCommandButtonActive(m_preBtn, level != 0);
}

void MainWindow::showNotchMenu()
{
    if (!m_notchBtn || !m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    QMenu menu(this);
    styleCompactMenu(&menu);
    const auto* offAction = menu.addAction(QStringLiteral("OFF"));
    const auto* autoAction = menu.addAction(QStringLiteral("AUTO"));
    const auto* manualAction = menu.addAction(QStringLiteral("MANUAL"));
    const auto* bothAction = menu.addAction(QStringLiteral("AUTO+MANUAL"));

    const QAction* selected = menu.exec(m_notchBtn->mapToGlobal(QPoint(0, m_notchBtn->height())));
    if (!selected)
    {
        return;
    }

    if (selected == offAction)
    {
        m_vfo->setAutoNotch(false);
        m_vfo->setManualNotch(false);
    }
    else if (selected == autoAction)
    {
        m_vfo->setManualNotch(false);
        m_vfo->setAutoNotch(true);
    }
    else if (selected == manualAction)
    {
        m_vfo->setAutoNotch(false);
        m_vfo->setManualNotch(true);
    }
    else if (selected == bothAction)
    {
        m_vfo->setAutoNotch(true);
        m_vfo->setManualNotch(true);
    }
}

void MainWindow::updateNotchButton()
{
    if (!m_notchBtn || !m_vfo)
    {
        return;
    }

    const bool autoOn = m_vfo->autoNotchOn();
    const bool manualOn = m_vfo->manualNotchOn();
    const QString secondary = autoOn && manualOn ? QStringLiteral("A/M")
                              : autoOn           ? QStringLiteral("AUTO")
                              : manualOn         ? QStringLiteral("MAN")
                                                 : QStringLiteral("OFF");
    setSelectorButtonLines(m_notchBtn, QStringLiteral("NOTCH"), secondary);
    setCommandButtonActive(m_notchBtn, autoOn || manualOn);
}

void MainWindow::updateRitButton()
{
    if (!m_ritBtn || !m_vfo)
    {
        return;
    }
    const bool on = m_vfo->ritOn();
    const short hz = m_vfo->ritHz();
    QString label;
    if (!on)
    {
        label = QStringLiteral("OFF");
    }
    else if (hz >= 0)
    {
        label = QStringLiteral("+%1").arg(hz);
    }
    else
    {
        label = QString::number(hz);
    }
    setSelectorButtonLines(m_ritBtn, QStringLiteral("RIT"), label);
    setCommandButtonActive(m_ritBtn, on);
}

void MainWindow::showRitMenu()
{
    if (!m_ritBtn || !m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }
    QMenu menu(this);
    styleCompactMenu(&menu);
    const auto* customAction = menu.addAction(QStringLiteral("CUSTOM"));
    menu.addSeparator();
    const auto* offAction = menu.addAction(QStringLiteral("OFF"));
    connect(customAction, &QAction::triggered, this, &MainWindow::showCustomRitDialog);
    connect(offAction, &QAction::triggered, this, [this]() { m_vfo->setRitEnabled(false); });
    menu.exec(m_ritBtn->mapToGlobal(QPoint(0, m_ritBtn->height())));
}

void MainWindow::showCustomRitDialog()
{
    if (!m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Custom RIT"));
    dialog.setModal(true);

    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* rit = new QSpinBox(&dialog);
    rit->setRange(-999, 999);
    rit->setSingleStep(10);
    rit->setSuffix(QStringLiteral(" Hz"));
    rit->setValue(m_vfo->ritOn() ? m_vfo->ritHz() : 0);
    form->addRow(QStringLiteral("RIT"), rit);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    centerPopupWindow(&dialog);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const short hz = static_cast<short>(rit->value());
    if (hz == 0)
    {
        m_vfo->setRitEnabled(false);
        return;
    }

    m_vfo->setRitOffset(hz);
    m_vfo->setRitEnabled(true);
}

void MainWindow::showOffsetMenu()
{
    if (!m_offsetBtn || !m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    QMenu menu(this);
    styleCompactMenu(&menu);

    const auto* simplexAction = menu.addAction(QStringLiteral("SIMPLEX"));
    menu.addSeparator();
    const QVector<OffsetPreset> presets = offsetPresetsForHz(m_vfo->frequencyHz());
    QVector<QAction*> presetActions;
    presetActions.reserve(presets.size());
    for (const OffsetPreset& preset : presets)
    {
        presetActions.append(menu.addAction(preset.label));
    }
    menu.addSeparator();
    const auto* customAction = menu.addAction(QStringLiteral("CUSTOM"));

    const QAction* selected = menu.exec(m_offsetBtn->mapToGlobal(QPoint(0, m_offsetBtn->height())));
    if (!selected)
    {
        return;
    }

    if (selected == simplexAction)
    {
        applyOffsetSelection(dmSimplex, m_repeaterOffsetHz);
        return;
    }

    for (int i = 0; i < presetActions.size(); ++i)
    {
        if (selected == presetActions.at(i))
        {
            const OffsetPreset& preset = presets.at(i);
            applyOffsetSelection(preset.mode, preset.hz);
            return;
        }
    }

    if (selected == customAction)
    {
        showCustomOffsetDialog();
    }
}

void MainWindow::showCustomOffsetDialog()
{
    if (m_controlsLocked)
    {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Custom Offset"));
    dialog.setModal(true);

    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* direction = new QComboBox(&dialog);
    direction->addItem(QStringLiteral("+"), QVariant::fromValue<int>(dmDupPlus));
    direction->addItem(QStringLiteral("-"), QVariant::fromValue<int>(dmDupMinus));
    direction->setCurrentIndex(m_duplexMode == dmDupMinus ? 1 : 0);

    auto* offset = new QDoubleSpinBox(&dialog);
    offset->setDecimals(3);
    offset->setRange(0.001, 99.999);
    offset->setSingleStep(0.005);
    offset->setSuffix(QStringLiteral(" MHz"));
    offset->setValue(qMax<quint64>(1, m_repeaterOffsetHz) / 1000000.0);

    form->addRow(QStringLiteral("Direction"), direction);
    form->addRow(QStringLiteral("Offset"), offset);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    centerPopupWindow(&dialog);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const auto mode = static_cast<duplexMode_t>(direction->currentData().toInt());
    const quint64 offsetHz = static_cast<quint64>(offset->value() * 1000000.0 + 0.5);
    applyOffsetSelection(mode, offsetHz);
}

void MainWindow::applyOffsetSelection(duplexMode_t mode, quint64 offsetHz)
{
    if (!m_vfo || m_controlsLocked)
    {
        return;
    }

    clearActiveMemory();
    m_duplexMode = mode;
    if (mode != dmSimplex)
    {
        m_repeaterOffsetHz = offsetHz;
    }
    updateOffsetButton();

    if (mode == dmSimplex)
    {
        m_vfo->setDuplexMode(dmSimplex);
        return;
    }

    m_vfo->setRepeaterOffsetHz(offsetHz);
    m_vfo->setDuplexMode(mode);
}

void MainWindow::updateOffsetButton()
{
    if (!m_offsetBtn)
    {
        return;
    }

    const bool active = m_duplexMode == dmDupMinus || m_duplexMode == dmDupPlus;
    setSelectorButtonLines(m_offsetBtn, QStringLiteral("OFFSET"), offsetModeLabel(m_duplexMode, m_repeaterOffsetHz));
    setCommandButtonActive(m_offsetBtn, active);
}

void MainWindow::showToneMenu()
{
    if (!m_toneBtn || !m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    QMenu menu(this);
    styleCompactMenu(&menu);

    auto styleToneGridButton = [](QPushButton* button)
    {
        button->setFixedSize(54, 24);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 3px; "
                                             "color: %3; font-size: 11px; }"
                                             "QPushButton:hover { background: %4; border-color: %5; color: %6; }")
                                  .arg(UiTheme::Color::Button, UiTheme::Color::BorderLight, UiTheme::Color::TextPrimary,
                                       UiTheme::Color::AccentDark, UiTheme::Color::Accent, UiTheme::Color::White));
    };

    auto addCtcssMenu = [this, &menu, styleToneGridButton](QMenu* parent, const QString& title, rptAccessTxRx_t mode)
    {
        auto* submenu = parent->addMenu(title);
        styleCompactMenu(submenu);
        auto* panel = new QWidget(submenu);
        auto* grid = new QGridLayout(panel);
        grid->setContentsMargins(6, 6, 6, 6);
        grid->setHorizontalSpacing(4);
        grid->setVerticalSpacing(4);
        static constexpr int kColumns = 4;
        int index = 0;
        for (const TonePreset& preset : kTonePresets)
        {
            auto* button = new QPushButton(QString::fromLatin1(preset.label), panel);
            styleToneGridButton(button);
            const ushort tone = preset.tone;
            connect(button, &QPushButton::clicked, this,
                    [this, &menu, submenu, mode, tone]()
                    {
                        applyToneSelection(mode, tone);
                        submenu->close();
                        menu.close();
                    });
            grid->addWidget(button, index / kColumns, index % kColumns);
            ++index;
        }
        auto* action = new QWidgetAction(submenu);
        action->setDefaultWidget(panel);
        submenu->addAction(action);
    };

    auto addDtcsMenu = [this, &menu, styleToneGridButton](QMenu* parent, const QString& title, rptAccessTxRx_t mode)
    {
        auto* submenu = parent->addMenu(title);
        styleCompactMenu(submenu);
        auto* panel = new QWidget(submenu);
        auto* grid = new QGridLayout(panel);
        grid->setContentsMargins(6, 6, 6, 6);
        grid->setHorizontalSpacing(4);
        grid->setVerticalSpacing(4);
        static constexpr int kColumns = 6;
        int index = 0;
        for (const ushort code : kDtcsCodes)
        {
            auto* button = new QPushButton(dtcsCodeLabel(code), panel);
            styleToneGridButton(button);
            connect(button, &QPushButton::clicked, this,
                    [this, &menu, submenu, mode, code]()
                    {
                        applyToneSelection(mode, code);
                        submenu->close();
                        menu.close();
                    });
            grid->addWidget(button, index / kColumns, index % kColumns);
            ++index;
        }
        auto* action = new QWidgetAction(submenu);
        action->setDefaultWidget(panel);
        submenu->addAction(action);
    };

    addCtcssMenu(&menu, QStringLiteral("TONE"), ratrTN);
    addCtcssMenu(&menu, QStringLiteral("CTCSS"), ratrNT);
    menu.addSeparator();
    addDtcsMenu(&menu, QStringLiteral("DCS"), ratrDN);
    addDtcsMenu(&menu, QStringLiteral("DTCS"), ratrDD);
    menu.addSeparator();
    const auto* offAction = menu.addAction(QStringLiteral("OFF"));

    const QAction* selected = menu.exec(m_toneBtn->mapToGlobal(QPoint(0, m_toneBtn->height())));
    if (!selected)
    {
        return;
    }

    if (selected == offAction)
    {
        applyToneSelection(ratrNN, 0);
    }
}

void MainWindow::applyToneSelection(rptAccessTxRx_t mode, ushort value)
{
    if (!m_vfo || m_controlsLocked)
    {
        return;
    }

    const bool dtcs = isDtcsToneMode(mode);
    clearActiveMemory();
    m_toneAccessMode = mode;
    if (dtcs)
    {
        m_dtcsCode = value;
    }
    else if (mode != ratrNN)
    {
        m_toneFrequency = value;
    }
    updateToneButton();

    if (mode == ratrNN)
    {
        m_vfo->setToneAccessMode(mode);
        return;
    }

    if (dtcs)
    {
        m_vfo->setDtcsCode(value);
    }
    else
    {
        m_vfo->setToneFrequency(value);
    }
    m_vfo->setToneAccessMode(mode);
}

void MainWindow::updateToneButton()
{
    if (!m_toneBtn)
    {
        return;
    }

    const bool active = m_toneAccessMode != ratrNN;
    const QString primary = active ? toneOptionLabel(m_toneAccessMode) : QStringLiteral("TONE");
    const ushort value = isDtcsToneMode(m_toneAccessMode) ? m_dtcsCode : m_toneFrequency;
    const QString secondary = active ? memoryToneFrequencyLabel(m_toneAccessMode, value) : QStringLiteral("OFF");
    setSelectorButtonLines(m_toneBtn, primary, secondary);
    setCommandButtonActive(m_toneBtn, active);
}

void MainWindow::updateSquelchButton()
{
    if (!m_squelchBtn)
    {
        return;
    }
    const bool active = m_squelchValue > 0;
    const QString pct = active ? QStringLiteral("%1%").arg(m_squelchValue * 100 / 255) : QStringLiteral("OFF");
    setSelectorButtonLines(m_squelchBtn, QStringLiteral("SQL"), pct);
    setCommandButtonActive(m_squelchBtn, active);
}

void MainWindow::updateTxPowerButton()
{
    if (!m_txPowerBtn)
    {
        return;
    }

    const bool active = m_txPowerValue > 0;
    const int pct = active ? qBound(1, qRound(m_txPowerValue * 100.0 / 255.0), 100) : 0;
    const QString secondary = active ? QStringLiteral("%1%").arg(pct) : QStringLiteral("OFF");
    setSelectorButtonLines(m_txPowerBtn, QStringLiteral("TX PWR"), secondary);
    setCommandButtonActive(m_txPowerBtn, active);
}

void MainWindow::updateRfGainButton()
{
    if (!m_rfGainBtn)
    {
        return;
    }

    const bool active = m_rfGainValue > 0;
    const int pct = active ? qBound(1, qRound(m_rfGainValue * 100.0 / 255.0), 100) : 0;
    const QString secondary = active ? QStringLiteral("%1%").arg(pct) : QStringLiteral("OFF");
    setSelectorButtonLines(m_rfGainBtn, QStringLiteral("RF GAIN"), secondary);
    setCommandButtonActive(m_rfGainBtn, active);
}

void MainWindow::showRfGainMenu()
{
    if (!m_rfGainBtn || !m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    QMenu menu(this);
    styleCompactMenu(&menu);

    auto* panel = new QWidget(&menu);
    panel->setFixedWidth(190);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(8, 6, 8, 6);
    panelLayout->setSpacing(4);

    auto rfGainPercentText = [](int value)
    {
        const int bounded = qBound(0, value, 255);
        if (bounded == 0)
        {
            return QStringLiteral("OFF");
        }
        return QStringLiteral("%1%").arg(qBound(1, qRound(bounded * 100.0 / 255.0), 100));
    };

    auto* valueLabel = new QLabel(rfGainPercentText(m_rfGainValue), panel);
    valueLabel->setAlignment(Qt::AlignCenter);
    valueLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 10px; font-weight: bold; }").arg(UiTheme::Color::TextMuted));

    auto applyRfGain = [this, valueLabel](int v)
    {
        m_rfGainValue = qBound(0, v, 255);
        const QString text = m_rfGainValue == 0
                                 ? QStringLiteral("OFF")
                                 : QStringLiteral("%1%").arg(qBound(1, qRound(m_rfGainValue * 100.0 / 255.0), 100));
        valueLabel->setText(text);
        updateRfGainButton();
        m_vfo->setRfGain(m_rfGainValue);
    };

    auto* slider = new QSlider(Qt::Horizontal, panel);
    slider->setRange(0, 255);
    slider->setValue(m_rfGainValue);
    connect(slider, &QSlider::valueChanged, this, [applyRfGain](int v) { applyRfGain(v); });

    panelLayout->addWidget(valueLabel);
    panelLayout->addWidget(slider);

    auto* panelAction = new QWidgetAction(&menu);
    panelAction->setDefaultWidget(panel);
    menu.addAction(panelAction);

    menu.exec(m_rfGainBtn->mapToGlobal(QPoint(0, m_rfGainBtn->height())));
}
