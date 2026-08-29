#include "VfoSMeter.h"

#include "SMeterScale.h"
#include "UiTheme.h"

#include <QPainter>

namespace
{
constexpr int kMeterHeight = 31;
constexpr int kReadoutWidth = 48;
constexpr int kReadoutGap = 8;
constexpr int kMeterEndInset = 20;
constexpr int kSegmentGap = 2;
constexpr int kSegmentWidth = 6;
constexpr int kSegmentHeight = 14;
constexpr int kScaleFontSize = 7;
constexpr int kReadoutFontSize = 12;
constexpr int kLegendRightInset = 4;

struct MeterMark
{
    const char* label;
    int raw;
    bool overS9;
};

constexpr MeterMark kMarks[] = {{"1", 13, false},  {"3", 40, false},   {"5", 67, false},   {"7", 93, false},
                                {"9", 120, false}, {"+20", 160, true}, {"+40", 201, true}, {"+60", 241, true}};

struct PowerMark
{
    const char* label;
    double watts;
    double fraction;
};

constexpr PowerMark kPowerMarks[] = {{"1", 1.0, 0.060},   {"5", 5.0, 0.180},   {"25", 25.0, 0.400},
                                     {"50", 50.0, 0.620}, {"75", 75.0, 0.820}, {"100W", 100.0, 1.000}};

double powerFraction(double watts)
{
    const double bounded = qBound(0.0, watts, 100.0);
    double previousWatts = 0.0;
    double previousFraction = 0.0;
    for (const PowerMark& mark : kPowerMarks)
    {
        if (bounded <= mark.watts)
        {
            return previousFraction +
                   ((bounded - previousWatts) / (mark.watts - previousWatts)) * (mark.fraction - previousFraction);
        }
        previousWatts = mark.watts;
        previousFraction = mark.fraction;
    }
    return 1.0;
}

QString signalText(int rawValue)
{
    const int bounded = qBound(0, rawValue, 255);
    if (bounded <= 120)
    {
        return QStringLiteral("S%1").arg(qBound(0, qRound(bounded * 9.0 / 120.0), 9));
    }

    // Icom defines raw 0241 as S9+60. Preserve values through 0255 in
    // the model, but do not extrapolate the presentation beyond full scale.
    const int plusDb = qRound(qMin(bounded - 120, 121) * 60.0 / 121.0);
    return QStringLiteral("S9+%1").arg(plusDb, 2, 10, QLatin1Char('0'));
}
} // namespace

VfoSMeter::VfoSMeter(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(kMeterHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(QStringLiteral("Signal strength meter"));
}

void VfoSMeter::setRawValue(int value)
{
    const int bounded = qBound(0, value, 255);
    if (m_rawValue == bounded)
    {
        return;
    }
    m_rawValue = bounded;
    setAccessibleDescription(QStringLiteral("Signal strength %1").arg(signalText(m_rawValue)));
    update();
}

void VfoSMeter::setTransmitPowerMode(bool enabled)
{
    if (m_transmitPowerMode == enabled)
    {
        return;
    }
    m_transmitPowerMode = enabled;
    setAccessibleName(enabled ? QStringLiteral("RF power meter") : QStringLiteral("Signal strength meter"));
    update();
}

void VfoSMeter::setPowerWatts(double watts)
{
    const double bounded = qBound(0.0, watts, 100.0);
    if (qFuzzyCompare(m_powerWatts + 1.0, bounded + 1.0))
    {
        return;
    }
    m_powerWatts = bounded;
    if (m_transmitPowerMode)
    {
        setAccessibleDescription(QStringLiteral("RF power %1 watts").arg(m_powerWatts, 0, 'f', 1));
        update();
    }
}

void VfoSMeter::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const int meterRight = qMax(0, width() - kReadoutWidth - kReadoutGap - kMeterEndInset);
    const QRect meterRect(0, height() - kSegmentHeight - 2, meterRight, kSegmentHeight);
    painter.fillRect(meterRect, QColor(4, 9, 13));

    QFont scaleFont = font();
    scaleFont.setPixelSize(kScaleFontSize);
    scaleFont.setBold(true);
    painter.setFont(scaleFont);
    const QFontMetrics scaleMetrics(scaleFont);
    if (m_transmitPowerMode)
    {
        for (const PowerMark& mark : kPowerMarks)
        {
            const int x = qRound(mark.fraction * qMax(0, meterRect.width() - 1));
            painter.setPen(QColor(QString::fromLatin1(UiTheme::Color::TextStatusSecondary)));
            const QString label = QString::fromLatin1(mark.label);
            const int labelWidth = scaleMetrics.horizontalAdvance(label);
            painter.drawText(qBound(0, x - labelWidth / 2, qMax(0, meterRect.width() - labelWidth - kLegendRightInset)),
                             meterRect.top() - 6, label);
        }
    }
    else
    {
        for (const MeterMark& mark : kMarks)
        {
            const int x = qRound(mark.raw / 241.0 * qMax(0, meterRect.width() - 1));
            painter.setPen(QColor(
                QString::fromLatin1(mark.overS9 ? UiTheme::Color::Danger : UiTheme::Color::TextStatusSecondary)));
            const QString label = QString::fromLatin1(mark.label);
            const int labelWidth = scaleMetrics.horizontalAdvance(label);
            painter.drawText(qBound(0, x - labelWidth / 2, qMax(0, meterRect.width() - labelWidth - kLegendRightInset)),
                             meterRect.top() - 6, label);
        }
    }

    const double meterFraction = m_transmitPowerMode ? powerFraction(m_powerWatts) : qMin(m_rawValue, 241) / 241.0;
    const int activeWidth = qRound(meterFraction * meterRect.width());
    const int s9X = qRound(120.0 / 241.0 * meterRect.width());
    for (int x = meterRect.left(); x + kSegmentWidth <= meterRect.right() + 1; x += kSegmentWidth + kSegmentGap)
    {
        const bool active = x - meterRect.left() < activeWidth;
        const bool overS9 = !m_transmitPowerMode && x - meterRect.left() >= s9X;
        const bool highPower = m_transmitPowerMode && x - meterRect.left() >= meterRect.width() * 0.82;
        painter.fillRect(QRect(x, meterRect.top(), kSegmentWidth, meterRect.height()),
                         QColor(QString::fromLatin1(!active               ? UiTheme::Color::BorderLight
                                                    : overS9 || highPower ? UiTheme::Color::Danger
                                                                          : UiTheme::Color::AccentBright)));
    }

    const QRect readoutRect(width() - kReadoutWidth - 10, meterRect.top(), kReadoutWidth, kSegmentHeight);
    QFont readoutFont = font();
    readoutFont.setPixelSize(kReadoutFontSize);
    readoutFont.setBold(true);
    painter.setFont(readoutFont);
    painter.setPen(QColor(QString::fromLatin1(UiTheme::Color::TextPrimary)));
    const QString readout =
        m_transmitPowerMode ? QStringLiteral("%1W").arg(qRound(m_powerWatts)) : signalText(m_rawValue);
    painter.drawText(readoutRect, Qt::AlignLeft | Qt::AlignVCenter, readout);
}
