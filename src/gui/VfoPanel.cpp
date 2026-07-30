#include "VfoPanel.h"

#include "UiTheme.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
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
constexpr int kNoSpacing = 0;
constexpr int kGroupMargin = 8;
constexpr int kGroupSpacing = 6;
constexpr int kInlineSpacing = 4;
constexpr int kSliderRowSpacing = 2;
constexpr int kSliderWidth = 118;
constexpr QSize kVfoFieldSize(220, 64);
constexpr int kVfoFieldSideMargin = 6;
constexpr int kVfoFieldTopMargin = 5;
constexpr int kVfoFieldBottomMargin = 3;
constexpr int kFrequencyFontPixelSize = 28;
constexpr int kFrequencyEditHeight = 40;
constexpr int kMemoryNameHeight = 16;
constexpr int kMemoryNameTopMargin = 2;
constexpr int kMemoryNameRightMargin = kVfoFieldSideMargin + 1;
constexpr int kSignalMeterWidth = 220;
constexpr int kSignalMeterHeight = 10;
constexpr int kSignalScaleHeight = 11;
constexpr double kSignalScaleTextTopGap = 0.5;
constexpr QSize kSignalBoxSize(kSignalMeterWidth, kSignalMeterHeight + kSignalScaleHeight);
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

    auto* header = new QHBoxLayout;
    header->setContentsMargins(kNoMargins);
    header->setSpacing(kInlineSpacing);
    m_bandButton = makeSelectorButton(QStringLiteral("BAND"), QStringLiteral("--"), QStringLiteral("Band menu"),
                                      QStringLiteral("Open IC-9700 band presets."));
    m_modeButton = makeSelectorButton(QStringLiteral("MODE"), QStringLiteral("--"), QStringLiteral("Mode menu"),
                                      QStringLiteral("Open operating mode selection."));
    m_stepButton = makeSelectorButton(QStringLiteral("STEP"), QStringLiteral("100 Hz"), QStringLiteral("Step menu"),
                                      QStringLiteral("Select tuning step size."));

    header->addStretch();
    header->addWidget(m_stepButton);
    header->addSpacing(8);
    header->addWidget(m_bandButton);
    header->addSpacing(8);
    header->addWidget(m_modeButton);

    auto* frequencyField = new QWidget(this);
    frequencyField->setObjectName(QStringLiteral("vfoFrequencyField"));
    frequencyField->setFixedSize(kVfoFieldSize);
    frequencyField->setStyleSheet(
        QStringLiteral("QWidget#vfoFrequencyField { background: %1; border: 1px solid %2; border-radius: 3px; }")
            .arg(UiTheme::Color::Field, UiTheme::Color::BorderFocus));
    auto* frequencyLayout = new QVBoxLayout(frequencyField);
    frequencyLayout->setContentsMargins(kVfoFieldSideMargin, kVfoFieldTopMargin, kVfoFieldSideMargin,
                                        kVfoFieldBottomMargin);
    frequencyLayout->setSpacing(kNoSpacing);

    m_frequencyEdit = new QLineEdit(QStringLiteral("---.---.---"), frequencyField);
    m_frequencyEdit->setObjectName(QStringLiteral("vfoFrequencyEdit"));
    m_frequencyEdit->setFixedHeight(kFrequencyEditHeight);
    m_frequencyEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_frequencyEdit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_frequencyEdit->setTextMargins(0, 0, kVfoFieldSideMargin, 0);
    m_frequencyEdit->setFocusPolicy(Qt::ClickFocus);
    m_frequencyEdit->setAccessibleName(QStringLiteral("%1 frequency").arg(title));
    m_frequencyEdit->setAccessibleDescription(QStringLiteral("Enter frequency in MHz."));
    m_frequencyEdit->setToolTip(QStringLiteral("Enter frequency in MHz, then press Enter"));
    m_frequencyEdit->setStyleSheet(
        QStringLiteral("QLineEdit { background: transparent; border: none; padding: 0px; color: %1; }")
            .arg(UiTheme::Color::TextField));
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    QFont freqFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    // Tabular numerals keep the frequency display visually stable while
    // retaining the platform UI font's plain (non-slashed) zero.
    freqFont.setFeature(QFont::Tag("tnum"), 1);
#else
    // QFont OpenType feature selection was introduced in Qt 6.7. Preserve
    // equal-width digits on older supported Qt releases.
    QFont freqFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
#endif
    freqFont.setPixelSize(kFrequencyFontPixelSize);
    freqFont.setBold(true);
    m_frequencyEdit->setFont(freqFont);
    connect(m_frequencyEdit, &QLineEdit::returnPressed, this, &VfoPanel::frequencyReturnPressed);

    auto* memoryNameField = new QWidget(frequencyField);
    memoryNameField->setObjectName(QStringLiteral("vfoMemoryNameField"));
    memoryNameField->setFixedHeight(kMemoryNameHeight);
    memoryNameField->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* memoryNameLayout = new QHBoxLayout(memoryNameField);
    memoryNameLayout->setContentsMargins(kNoMargins);
    memoryNameLayout->setSpacing(kNoSpacing);

    m_memoryNameLabel = new QLineEdit(memoryNameField);
    m_memoryNameLabel->setObjectName(QStringLiteral("vfoMemoryNameLabel"));
    m_memoryNameLabel->setFixedHeight(kMemoryNameHeight);
    m_memoryNameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_memoryNameLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_memoryNameLabel->setTextMargins(0, kMemoryNameTopMargin, kMemoryNameRightMargin, 0);
    m_memoryNameLabel->setReadOnly(true);
    m_memoryNameLabel->setFocusPolicy(Qt::NoFocus);
    m_memoryNameLabel->setCursor(Qt::ArrowCursor);
    m_memoryNameLabel->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; border: none; color: %1; "
                                                    "font-size: 10px; font-weight: bold; padding: 0px; }")
                                         .arg(UiTheme::Color::TextMuted));
    memoryNameLayout->addWidget(m_memoryNameLabel);

    frequencyLayout->addWidget(memoryNameField);
    frequencyLayout->addWidget(m_frequencyEdit);

    auto* signalBox = new QWidget(this);
    signalBox->setFixedSize(kSignalBoxSize);
    m_signalMeter = new SMeter(signalBox);
    m_signalMeter->setRange(0, 100);
    m_signalMeter->setValue(0);
    m_signalMeter->setFixedWidth(kSignalMeterWidth);
    m_signalMeter->setFixedHeight(kSignalMeterHeight);
    m_signalMeter->setTextVisible(false);
    m_signalMeter->setAccessibleName(QStringLiteral("%1 signal meter").arg(title));
    m_signalMeter->setAccessibleDescription(QStringLiteral("Received signal strength meter."));
    m_signalMeter->move(0, 0);
    m_signalScale = new SMeterScaleCanvas(signalBox);
    static_cast<SMeterScaleCanvas*>(m_signalScale)->setFixedWidth(kSignalMeterWidth);
    m_signalScale->move(0, kSignalMeterHeight);

    auto* sliders = new QWidget(this);
    auto* slidersLayout = new QVBoxLayout(sliders);
    slidersLayout->setContentsMargins(kNoMargins);
    slidersLayout->setSpacing(kSliderRowSpacing);
    slidersLayout->addWidget(makeSliderRow(QStringLiteral("TX PWR"), 0, &m_txPowerSlider, &m_txPowerValueLabel));
    slidersLayout->addWidget(makeSliderRow(QStringLiteral("SQL"), 0, &m_squelchSlider, &m_squelchValueLabel));
    slidersLayout->addWidget(makeSliderRow(QStringLiteral("LAN MOD"), 0, &m_lanModSlider, &m_lanModValueLabel));

    layout->addLayout(header);
    layout->addWidget(frequencyField);
    layout->addWidget(signalBox);
    layout->addWidget(sliders);

    connect(m_bandButton, &QPushButton::clicked, this, &VfoPanel::bandClicked);
    connect(m_modeButton, &QPushButton::clicked, this, &VfoPanel::modeClicked);
    connect(m_stepButton, &QPushButton::clicked, this, &VfoPanel::stepClicked);
    connect(m_txPowerSlider, &QSlider::valueChanged, this, &VfoPanel::txPowerChanged);
    connect(m_lanModSlider, &QSlider::valueChanged, this, &VfoPanel::lanModChanged);
    connect(m_squelchSlider, &QSlider::valueChanged, this, &VfoPanel::squelchChanged);

    setMeterEnabled(false);
}

QString VfoPanel::frequencyText() const
{
    return m_frequencyEdit ? m_frequencyEdit->text() : QString();
}

bool VfoPanel::frequencyHasFocus() const
{
    return m_frequencyEdit && m_frequencyEdit->hasFocus();
}

void VfoPanel::clearFrequencyFocus()
{
    if (m_frequencyEdit)
    {
        m_frequencyEdit->clearFocus();
    }
}

void VfoPanel::setFrequencyText(const QString& text)
{
    if (m_frequencyEdit)
    {
        m_frequencyEdit->setText(text);
    }
}

void VfoPanel::setFrequencyReadOnly(bool readOnly)
{
    if (m_frequencyEdit)
    {
        m_frequencyEdit->setReadOnly(readOnly);
        m_frequencyEdit->setFocusPolicy(readOnly ? Qt::NoFocus : Qt::ClickFocus);
    }
}

void VfoPanel::setMemoryName(const QString& text, const QString& tooltip)
{
    if (m_memoryNameLabel)
    {
        m_memoryNameLabel->setText(text);
        m_memoryNameLabel->setToolTip(tooltip);
    }
}

void VfoPanel::setBandText(const QString& text)
{
    if (m_bandButton)
    {
        m_bandButton->setText(text);
        m_bandButton->setAccessibleName(QStringLiteral("Band %1").arg(text));
    }
}

void VfoPanel::setModeText(const QString& text)
{
    if (m_modeButton)
    {
        m_modeButton->setText(text);
        m_modeButton->setAccessibleName(QStringLiteral("Mode %1").arg(text));
    }
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
    if (m_frequencyEdit)
    {
        m_frequencyEdit->setEnabled(enabled);
    }
    if (m_bandButton)
    {
        m_bandButton->setEnabled(enabled);
    }
    if (m_modeButton)
    {
        m_modeButton->setEnabled(enabled);
    }
    if (m_stepButton)
    {
        m_stepButton->setEnabled(enabled);
    }
    if (m_txPowerSlider)
    {
        m_txPowerSlider->setEnabled(enabled);
    }
    if (m_lanModSlider)
    {
        m_lanModSlider->setEnabled(enabled);
    }
    if (m_squelchSlider)
    {
        m_squelchSlider->setEnabled(enabled);
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

void VfoPanel::setTxPower(int value)
{
    setSliderValue(m_txPowerSlider, m_txPowerValueLabel, value);
}

void VfoPanel::setLanMod(int value)
{
    setSliderValue(m_lanModSlider, m_lanModValueLabel, value);
}

void VfoPanel::setSquelch(int value)
{
    setSliderValue(m_squelchSlider, m_squelchValueLabel, value);
}

QPoint VfoPanel::bandMenuPosition() const
{
    return m_bandButton ? m_bandButton->mapToGlobal(QPoint(0, m_bandButton->height())) : mapToGlobal(QPoint());
}

QPoint VfoPanel::modeMenuPosition() const
{
    return m_modeButton ? m_modeButton->mapToGlobal(QPoint(0, m_modeButton->height())) : mapToGlobal(QPoint());
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
