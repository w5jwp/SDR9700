#include "VfoPanel.h"

#include "UiTheme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <algorithm>

namespace
{
constexpr QMargins kNoMargins(0, 0, 0, 0);
constexpr int kGroupMargin = 8;
constexpr int kGroupSpacing = 6;
constexpr int kInlineSpacing = 4;
constexpr int kSliderRowSpacing = 2;
constexpr int kSliderWidth = 118;
constexpr int kSignalMeterWidth = 220;
constexpr int kSignalScaleHeight = 11;
constexpr double kSignalScaleTextTopGap = 0.5;
constexpr QSize kHeaderBadgeSize(54, 18);
constexpr double kRfPowerMeterMaxWatts = 100.0;

enum class SignalMeterMode
{
    Signal,
    RfPower,
};

struct ScaleMark
{
    QString label;
    double fraction{0.0};
};

double rfPowerMeterFraction(double watts)
{
    static const ScaleMark kPowerMarks[] = {{QStringLiteral("1"), 0.060},  {QStringLiteral("5"), 0.180},
                                            {QStringLiteral("25"), 0.400}, {QStringLiteral("50"), 0.620},
                                            {QStringLiteral("75"), 0.820}, {QStringLiteral("100W"), 1.000}};

    const double bounded = qBound(0.0, watts, kRfPowerMeterMaxWatts);
    if (bounded <= 0.0)
    {
        return 0.0;
    }

    double previousWatts = 0.0;
    double previousFraction = 0.0;
    for (const ScaleMark& mark : kPowerMarks)
    {
        const QString numericLabel = mark.label;
        bool ok = false;
        const double markWatts = numericLabel.endsWith(QLatin1Char('W'))
                                     ? numericLabel.left(numericLabel.size() - 1).toDouble(&ok)
                                     : numericLabel.toDouble(&ok);
        if (!ok)
        {
            continue;
        }
        if (bounded <= markWatts)
        {
            const double spanWatts = qMax(0.001, markWatts - previousWatts);
            const double fraction =
                previousFraction + ((bounded - previousWatts) / spanWatts) * (mark.fraction - previousFraction);
            return qBound(0.0, fraction, 1.0);
        }
        previousWatts = markWatts;
        previousFraction = mark.fraction;
    }

    return 1.0;
}

QString commandButtonStyle()
{
    return QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 3px; color: %3; "
                          "font-size: 9px; font-weight: bold; }"
                          "QPushButton:hover { background: %4; border-color: %5; color: %6; }")
        .arg(UiTheme::Color::Button, UiTheme::Color::BorderLight, UiTheme::Color::TextPrimary,
             UiTheme::Color::ButtonHover, UiTheme::Color::ButtonHoverBorder, UiTheme::Color::TextBright);
}

class SMeter : public QProgressBar
{
  public:
    explicit SMeter(QWidget* parent = nullptr) : QProgressBar(parent) {}

    void setMeterMode(SignalMeterMode mode)
    {
        if (m_meterMode == mode)
        {
            return;
        }
        m_meterMode = mode;
        resetMeter();
        update();
    }

    void resetMeter() { setValue(0); }

    void setMeterValue(int newValue)
    {
        const int bounded = qBound(minimum(), newValue, maximum());
        setValue(bounded);
        update();
    }

  protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event)

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRect barRect = rect().adjusted(0, 0, -1, -1);
        painter.setPen(UiTheme::Color::MeterFrame);
        painter.setBrush(UiTheme::Color::MeterTroughQColor);
        painter.drawRoundedRect(barRect, 3, 3);

        const int span = maximum() - minimum();
        if (span <= 0)
        {
            return;
        }

        const double fraction = qBound(0.0, double(value() - minimum()) / double(span), 1.0);
        const int fillWidth = static_cast<int>((barRect.width() - 2) * fraction + 0.5);
        if (fillWidth <= 0)
        {
            return;
        }

        const QRect fillRect = barRect.adjusted(1, 1, -1, -1);
        painter.setPen(Qt::NoPen);
        if (m_meterMode == SignalMeterMode::RfPower)
        {
            QLinearGradient gradient(fillRect.left(), 0, fillRect.right(), 0);
            gradient.setColorAt(0.00, UiTheme::Color::MeterBlue);
            gradient.setColorAt(0.20, UiTheme::Color::MeterCyan);
            gradient.setColorAt(0.48, UiTheme::Color::MeterGreen);
            gradient.setColorAt(0.76, UiTheme::Color::MeterAmber);
            gradient.setColorAt(1.00, UiTheme::Color::MeterRed);
            painter.setBrush(gradient);
        }
        else
        {
            QLinearGradient gradient(fillRect.left(), 0, fillRect.right(), 0);
            gradient.setColorAt(0.00, UiTheme::Color::MeterBlue);
            gradient.setColorAt(0.35, UiTheme::Color::MeterCyan);
            gradient.setColorAt(0.60, UiTheme::Color::MeterGreen);
            gradient.setColorAt(0.80, UiTheme::Color::MeterAmber);
            gradient.setColorAt(1.00, UiTheme::Color::MeterRed);
            painter.setBrush(gradient);
        }
        painter.drawRoundedRect(QRect(fillRect.left(), fillRect.top(), fillWidth, fillRect.height()), 2, 2);
    }

  private:
    SignalMeterMode m_meterMode{SignalMeterMode::Signal};
};

class SMeterScaleCanvas : public QWidget
{
  public:
    explicit SMeterScaleCanvas(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(kSignalScaleHeight);
        setMinimumWidth(kSignalMeterWidth);
    }

    void setMeterMode(SignalMeterMode mode)
    {
        if (m_meterMode == mode)
        {
            return;
        }
        m_meterMode = mode;
        update();
    }

  protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event)

        static const ScaleMark kSMarks[] = {{QStringLiteral("1"), 0.0635},   {QStringLiteral("3"), 0.1905},
                                            {QStringLiteral("5"), 0.3175},   {QStringLiteral("7"), 0.4444},
                                            {QStringLiteral("9"), 0.5714},   {QStringLiteral("+20"), 0.7143},
                                            {QStringLiteral("+40"), 0.8571}, {QStringLiteral("+60"), 1.0000}};

        static const ScaleMark kPowerMarks[] = {{QStringLiteral("1"), 0.060},  {QStringLiteral("5"), 0.180},
                                                {QStringLiteral("25"), 0.400}, {QStringLiteral("50"), 0.620},
                                                {QStringLiteral("75"), 0.820}, {QStringLiteral("100W"), 1.000}};

        const ScaleMark* marks = m_meterMode == SignalMeterMode::RfPower ? kPowerMarks : kSMarks;
        const int maxIndex = (m_meterMode == SignalMeterMode::RfPower ? static_cast<int>(std::size(kPowerMarks))
                                                                      : static_cast<int>(std::size(kSMarks))) -
                             1;

        QPainter painter(this);
        QFont scaleFont = font();
        scaleFont.setPixelSize(9);
        scaleFont.setBold(true);
        painter.setFont(scaleFont);
        painter.setPen(UiTheme::Color::MeterScaleText);

        const QFontMetrics metrics(scaleFont);
        int maxTextWidth = 0;
        for (int i = 0; i <= maxIndex; ++i)
        {
            maxTextWidth = qMax(maxTextWidth, metrics.horizontalAdvance(marks[i].label));
        }

        const int scaleInset = maxTextWidth / 2 + 3;
        const int scaleLeft = scaleInset;
        const int scaleRight = qMax(scaleLeft, width() - 1 - scaleInset);
        const int scaleWidth = qMax(1, scaleRight - scaleLeft);
        const double baselineY = qMin<double>(height() - 1, metrics.ascent() + kSignalScaleTextTopGap);
        for (int i = 0; i <= maxIndex; ++i)
        {
            const QString& mark = marks[i].label;
            const int textWidth = metrics.horizontalAdvance(mark);
            const int anchorX = scaleLeft + static_cast<int>(marks[i].fraction * scaleWidth + 0.5);
            const int x = qBound(0, anchorX - textWidth / 2, std::max(0, width() - textWidth));
            painter.drawText(QPointF(x, baselineY), mark);
        }
    }

  private:
    SignalMeterMode m_meterMode{SignalMeterMode::Signal};
};
} // namespace

VfoPanel::VfoPanel(const QString& title, QWidget* parent) : QGroupBox(parent)
{
    setTitle(title);
    setAccessibleName(title);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kGroupMargin, kGroupMargin, kGroupMargin, kGroupMargin);
    layout->setSpacing(kGroupSpacing);

    m_lanModControl = new QWidget(this);
    auto* slidersLayout = new QVBoxLayout(m_lanModControl);
    slidersLayout->setContentsMargins(kNoMargins);
    slidersLayout->setSpacing(kSliderRowSpacing);
    slidersLayout->addWidget(makeSliderRow(QStringLiteral("LAN MOD"), 0, &m_lanModSlider, &m_lanModValueLabel));

    layout->addWidget(m_lanModControl);

    connect(m_lanModSlider, &QSlider::valueChanged, this, &VfoPanel::lanModChanged);

    setMeterEnabled(false);
}

void VfoPanel::setStepText(const QString& text)
{
    if (m_stepButton)
    {
        m_stepButton->setText(text);
    }
}

void VfoPanel::setControlsEnabled(bool enabled)
{
    if (m_stepButton)
    {
        m_stepButton->setEnabled(enabled);
    }
    if (m_lanModSlider)
    {
        m_lanModSlider->setEnabled(enabled);
    }
}

void VfoPanel::setMeterEnabled(bool enabled)
{
    m_meterEnabled = enabled;
    if (m_signalMeter)
    {
        m_signalMeter->setEnabled(enabled);
        if (!enabled)
        {
            if (auto* meter = dynamic_cast<SMeter*>(m_signalMeter))
            {
                meter->resetMeter();
            }
            else
            {
                m_signalMeter->setValue(0);
            }
            m_signalMeter->setAccessibleDescription(QStringLiteral("Signal meter disabled while syncing."));
        }
    }
    if (m_signalScale)
    {
        m_signalScale->setEnabled(enabled);
    }
}

void VfoPanel::setSMeterValue(int value)
{
    if (m_signalMeter)
    {
        if (!m_meterEnabled)
        {
            if (auto* meter = dynamic_cast<SMeter*>(m_signalMeter))
            {
                meter->resetMeter();
            }
            else
            {
                m_signalMeter->setValue(0);
            }
            return;
        }
        if (auto* meter = dynamic_cast<SMeter*>(m_signalMeter))
        {
            meter->setMeterValue(qBound(0, value, 100));
        }
        else
        {
            m_signalMeter->setValue(qBound(0, value, 100));
        }
        m_signalMeter->setAccessibleDescription(QStringLiteral("Received signal strength meter."));
    }
}

void VfoPanel::setTransmitPowerMode(bool on)
{
    if (auto* meter = dynamic_cast<SMeter*>(m_signalMeter))
    {
        meter->setMeterMode(on ? SignalMeterMode::RfPower : SignalMeterMode::Signal);
        m_signalMeter->setAccessibleDescription(on ? QStringLiteral("RF power meter.")
                                                   : QStringLiteral("Received signal strength meter."));
    }
    if (auto* scale = dynamic_cast<SMeterScaleCanvas*>(m_signalScale))
    {
        scale->setMeterMode(on ? SignalMeterMode::RfPower : SignalMeterMode::Signal);
    }
}

void VfoPanel::setTransmitPowerMeter(double watts)
{
    if (!m_signalMeter || !m_meterEnabled)
    {
        return;
    }
    const double boundedWatts = qBound(0.0, watts, kRfPowerMeterMaxWatts);
    const int barValue = qBound(0, static_cast<int>(rfPowerMeterFraction(boundedWatts) * 100.0 + 0.5), 100);
    if (auto* meter = dynamic_cast<SMeter*>(m_signalMeter))
    {
        meter->setMeterValue(barValue);
    }
    else
    {
        m_signalMeter->setValue(barValue);
    }
    m_signalMeter->setAccessibleDescription(QStringLiteral("RF power meter: %1 watts").arg(boundedWatts, 0, 'f', 1));
}

void VfoPanel::setLanMod(int value)
{
    setSliderValue(m_lanModSlider, m_lanModValueLabel, value);
}

QPoint VfoPanel::stepMenuPosition() const
{
    return m_stepButton ? m_stepButton->mapToGlobal(QPoint(0, m_stepButton->height())) : mapToGlobal(QPoint());
}

QPushButton* VfoPanel::makeSelectorButton(const QString& primary, const QString& secondary, const QString& name,
                                          const QString& description)
{
    Q_UNUSED(primary)

    auto* button = new QPushButton(secondary, this);
    button->setCheckable(false);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(kHeaderBadgeSize);
    button->setAccessibleName(name);
    button->setAccessibleDescription(description);
    button->setStyleSheet(commandButtonStyle());
    return button;
}

QWidget* VfoPanel::makeSliderRow(const QString& labelText, int value, QSlider** sliderOut, QLabel** valueLabelOut)
{
    auto* row = new QWidget(this);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(kNoMargins);
    rowLayout->setSpacing(kInlineSpacing);

    auto* label = new QLabel(labelText, row);
    label->setFixedWidth(54);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 10px; font-weight: bold; }").arg(UiTheme::Color::TextMuted));

    auto* slider = new QSlider(Qt::Horizontal, row);
    slider->setRange(0, 255);
    slider->setValue(value);
    slider->setFixedWidth(kSliderWidth);
    slider->setFixedHeight(UiTheme::Size::ControlSliderHeight);

    auto* pct = new QLabel(row);
    pct->setFixedWidth(36);
    pct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    pct->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 10px; font-weight: bold; }").arg(UiTheme::Color::TextMuted));
    updateSliderValueLabel(pct, value);
    connect(slider, &QSlider::valueChanged, pct, [this, pct](int v) { updateSliderValueLabel(pct, v); });

    rowLayout->addWidget(label);
    rowLayout->addWidget(slider, 1);
    rowLayout->addWidget(pct);

    if (sliderOut)
    {
        *sliderOut = slider;
    }
    if (valueLabelOut)
    {
        *valueLabelOut = pct;
    }
    return row;
}

void VfoPanel::setSliderValue(QSlider* slider, QLabel* valueLabel, int value)
{
    if (!slider)
    {
        return;
    }
    const int bounded = qBound(0, value, 255);
    if (slider->value() == bounded)
    {
        updateSliderValueLabel(valueLabel, bounded);
        return;
    }
    const QSignalBlocker blocker(slider);
    slider->setValue(bounded);
    updateSliderValueLabel(valueLabel, bounded);
}

void VfoPanel::updateSliderValueLabel(QLabel* label, int value)
{
    if (label)
    {
        label->setText(QStringLiteral("%1%").arg(qBound(0, value, 255) * 100 / 255));
    }
}
