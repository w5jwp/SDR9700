#include "MainWindowHelpers.h"

#include <QFontMetrics>
#include <QPainter>
#include <QStyle>
#include <QStringList>

namespace sdr9700::ui::main_window
{
void TwoLineButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QStyleOptionButton option;
    initStyleOption(&option);
    option.text.clear();

    QPainter painter(this);
    style()->drawControl(QStyle::CE_PushButton, &option, &painter, this);

    const QRect content = style()->subElementRect(QStyle::SE_PushButtonContents, &option, this).adjusted(2, 3, -2, -3);
    const QColor textColor =
        isEnabled() ? palette().color(QPalette::ButtonText) : palette().color(QPalette::Disabled, QPalette::ButtonText);

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

QString statusLabelStyle(const char* color, bool bold)
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

int spectrumScopeGridDensitySetting()
{
    return qBound(
        0,
        AppSettings::instance()
            .value(QString::fromLatin1(kSpectrumScopeGridDensitySettingsKey), kDefaultSpectrumScopeGridDensity)
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
    return qBound(0, AppSettings::instance().value(QStringLiteral("volumeLevel"), 128).toInt(), 255);
}

QString bandLabelForHz(quint64 hz)
{
    return sdr9700::radioBandShortLabel(sdr9700::radioBandForFrequency(hz));
}

int vfoBandIndexForHz(quint64 hz)
{
    return sdr9700::radioBandUiIndex(sdr9700::radioBandForFrequency(hz));
}

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
        return QStringLiteral("TONE");
    case ratrTT:
        return QStringLiteral("TONE");
    case ratrDN:
        return QStringLiteral("DTCS");
    case ratrDD:
        return QStringLiteral("DTCS");
    case ratrDT:
        return QStringLiteral("DTCS");
    default:
        return QStringLiteral("OFF");
    }
}

QString memoryToneFrequencyLabel(rptAccessTxRx_t mode, ushort value)
{
    switch (mode)
    {
    case ratrTN:
    case ratrNT:
    case ratrTT:
        return toneFrequencyLabel(value);
    case ratrDN:
    case ratrDD:
    case ratrDT:
        return dtcsCodeLabel(value);
    default:
        return QString();
    }
}

QString memoryModeLabel(int mode)
{
    switch (static_cast<radioMode_t>(mode))
    {
    case modeLSB:
        return QStringLiteral("LSB");
    case modeUSB:
        return QStringLiteral("USB");
    case modeAM:
        return QStringLiteral("AM");
    case modeCW:
        return QStringLiteral("CW");
    case modeRTTY:
        return QStringLiteral("RTTY");
    case modeFM:
        return QStringLiteral("FM");
    case modeCW_R:
        return QStringLiteral("CW-R");
    case modeRTTY_R:
        return QStringLiteral("RTTY-R");
    case modeDV:
        return QStringLiteral("DV");
    case modeDD:
        return QStringLiteral("DD");
    default:
        return QStringLiteral("0x%1").arg(mode, 2, 16, QLatin1Char('0')).toUpper();
    }
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
} // namespace sdr9700::ui::main_window
