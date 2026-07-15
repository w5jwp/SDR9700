#include "MetersDialog.h"
#include "DialogPlacement.h"
#include "UiTheme.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

namespace
{
constexpr int kMeterScale = 1000;
constexpr int kSMeterMax = 255;
constexpr int kS9MeterValue = 146;
constexpr double kPowerMeterMaxWatts = 120.0;
constexpr double kSwrMeterMin = 1.0;
constexpr double kSwrMeterMax = 6.0;
constexpr double kAlcMeterMax = 2.0;
constexpr double kCompressionMeterMaxDb = 25.5;
constexpr double kVoltageMeterMax = 16.0;
constexpr double kCurrentMeterMax = 20.0;
constexpr int kAudioMax = 255;

QString meterStyle(const QString& fill)
{
    return QStringLiteral("QProgressBar {"
                          "  background: %1; border: 1px solid %2; border-radius: 3px;"
                          "}"
                          "QProgressBar::chunk {"
                          "  background: %3;"
                          "  border-radius: 2px;"
                          "}")
        .arg(QLatin1String(UiTheme::Color::MeterTrough), QLatin1String(UiTheme::Color::BorderMedium), fill);
}

QString standardMeterFill()
{
    return QStringLiteral("qlineargradient(x1:0, y1:0, x2:1, y2:0,"
                          " stop:0 %1, stop:0.72 %2, stop:1 %3)")
        .arg(QLatin1String(UiTheme::Color::Accent), QLatin1String(UiTheme::Color::Success),
             QLatin1String(UiTheme::Color::Warning));
}

int scaledValue(double value, double maximum)
{
    if (maximum <= 0.0)
    {
        return 0;
    }
    return qBound(0, qRound(value / maximum * kMeterScale), kMeterScale);
}

QString sMeterText(int value)
{
    const int bounded = qBound(0, value, kSMeterMax);
    if (bounded <= kS9MeterValue)
    {
        const int sUnits = qBound(0, qRound(static_cast<double>(bounded) / kS9MeterValue * 9.0), 9);
        return QStringLiteral("S%1").arg(sUnits);
    }

    const int plusDb = qBound(
        0,
        qRound(static_cast<double>(bounded - kS9MeterValue) / static_cast<double>(kSMeterMax - kS9MeterValue) * 60.0),
        60);
    return QStringLiteral("S9+%1").arg(plusDb);
}

class MetersTitleBar : public QWidget
{
  public:
    explicit MetersTitleBar(QWidget* parent = nullptr) : QWidget(parent) {}

  protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            QWidget* panel = parentWidget();
            if (panel && panel->isWindow())
            {
                if (QWindow* win = panel->windowHandle())
                {
                    win->startSystemMove();
                }
            }
            else if (panel)
            {
                m_dragging = true;
                m_dragOffset = panel->mapFromGlobal(event->globalPosition().toPoint());
            }
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!m_dragging)
        {
            QWidget::mouseMoveEvent(event);
            return;
        }

        QWidget* panel = parentWidget();
        QWidget* parent = panel ? panel->parentWidget() : nullptr;
        if (!panel || !parent)
        {
            return;
        }

        QPoint target = parent->mapFromGlobal(event->globalPosition().toPoint() - m_dragOffset);
        target.setX(qBound(0, target.x(), qMax(0, parent->width() - panel->width())));
        target.setY(qBound(0, target.y(), qMax(0, parent->height() - panel->height())));
        panel->move(target);
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        m_dragging = false;
        QWidget::mouseReleaseEvent(event);
    }

  private:
    bool m_dragging{false};
    QPoint m_dragOffset;
};
} // namespace

MetersDialog::MetersDialog(QWidget* parent) : QDialog(parent), m_centerHost(parent)
{
    setWindowTitle(QStringLiteral("Meters"));
    setWindowModality(Qt::NonModal);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowFlags(Qt::FramelessWindowHint);
    setFixedWidth(500);
    setStyleSheet(QStringLiteral("MetersDialog { background: %1; border: 1px solid %2; }")
                      .arg(QLatin1String(UiTheme::Color::Panel), QLatin1String(UiTheme::Color::Border)));

    auto* root = new QVBoxLayout(this);
    root->setSpacing(0);
    root->setContentsMargins(0, 0, 0, 0);

    auto* titleBar = new MetersTitleBar(this);
    titleBar->setFixedHeight(28);
    titleBar->setStyleSheet(QStringLiteral("background: %1;").arg(UiTheme::Color::MenuBar));
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(10, 0, 0, 0);
    titleLayout->setSpacing(0);

    auto* titleLabel = new QLabel(QStringLiteral("Meters"), titleBar);
    titleLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 12px; font-weight: bold; background: transparent; }")
            .arg(UiTheme::Color::TextMuted));
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();

    auto* closeBtn = new QPushButton(QStringLiteral("X"), titleBar);
    closeBtn->setFixedSize(28, 28);
    closeBtn->setStyleSheet(
        QStringLiteral("QPushButton { background: transparent; border: none; color: %1; font-size: 13px; }"
                       "QPushButton:hover { background: %2; color: %3; }")
            .arg(QLatin1String(UiTheme::Color::TextMuted), QLatin1String(UiTheme::Color::Danger),
                 QLatin1String(UiTheme::Color::White)));
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    titleLayout->addWidget(closeBtn);
    root->addWidget(titleBar);

    auto* content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(12, 10, 12, 12);
    contentLayout->setSpacing(8);
    root->addWidget(content);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(7);
    grid->setColumnStretch(1, 1);

    m_alcMeter = addMeterRow(grid, 0, QStringLiteral("ALC"), QStringLiteral("Automatic level control"));
    m_txAudioAverageMeter = addMeterRow(grid, 1, QStringLiteral("Audio Average"),
                                        QStringLiteral("Local microphone input average level; high at 60% or above"));
    m_txAudioPeakMeter = addMeterRow(grid, 2, QStringLiteral("Audio Peak"),
                                     QStringLiteral("Local microphone input peak level; high at 85%, clipping at 95%"));
    m_compressionMeter = addMeterRow(grid, 3, QStringLiteral("Compression"), QStringLiteral("Transmit compression"));
    m_currentMeter = addMeterRow(grid, 4, QStringLiteral("Drain Current (I<sub>d</sub>)"),
                                 QStringLiteral("Final amplifier drain current"));
    m_voltageMeter = addMeterRow(grid, 5, QStringLiteral("Drain Voltage (V<sub>d</sub>)"),
                                 QStringLiteral("Final amplifier drain voltage"));
    m_powerMeter = addMeterRow(grid, 6, QStringLiteral("RF Power"), QStringLiteral("Transmit output power"));
    m_sMeter = addMeterRow(grid, 7, QStringLiteral("S-Meter"), QStringLiteral("Receive signal strength"));
    m_swrMeter = addMeterRow(grid, 8, QStringLiteral("SWR"), QStringLiteral("Standing wave ratio"));

    contentLayout->addLayout(grid);
    resetMeters();
}

void MetersDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);

    sdr9700::ui::centerWindowOn(this, m_centerHost);
    QTimer::singleShot(0, this, [this]() { sdr9700::ui::centerWindowOn(this, m_centerHost); });
    QTimer::singleShot(50, this, [this]() { sdr9700::ui::centerWindowOn(this, m_centerHost); });
}

MetersDialog::MeterRow MetersDialog::addMeterRow(QGridLayout* layout, int row, const QString& label,
                                                 const QString& description)
{
    auto* labelWidget = new QLabel(label, this);
    labelWidget->setFixedWidth(190);
    labelWidget->setTextFormat(Qt::RichText);
    labelWidget->setToolTip(description);
    labelWidget->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 12px; }").arg(UiTheme::Color::TextMuted));

    auto* bar = new QProgressBar(this);
    bar->setRange(0, kMeterScale);
    bar->setTextVisible(false);
    bar->setFixedHeight(12);
    bar->setToolTip(description);
    bar->setStyleSheet(meterStyle(standardMeterFill()));

    auto* valueLabel = new QLabel(QStringLiteral("--"), this);
    valueLabel->setFixedWidth(76);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valueLabel->setToolTip(description);
    valueLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 12px; font-family: monospace; }")
                                  .arg(UiTheme::Color::TextBright));

    layout->addWidget(labelWidget, row, 0);
    layout->addWidget(bar, row, 1);
    layout->addWidget(valueLabel, row, 2);

    return MeterRow{bar, valueLabel};
}

void MetersDialog::setMeterRow(const MeterRow& row, int value, const QString& text)
{
    if (row.bar)
    {
        row.bar->setValue(qBound(0, value, kMeterScale));
    }
    if (row.valueLabel)
    {
        row.valueLabel->setText(text);
    }
}

void MetersDialog::setMeterFillColor(const MeterRow& row, const char* color)
{
    if (!row.bar)
    {
        return;
    }
    row.bar->setStyleSheet(meterStyle(QString::fromLatin1(color)));
}

void MetersDialog::resetMeters()
{
    setMeterRow(m_sMeter, 0, QStringLiteral("--"));
    setMeterRow(m_powerMeter, 0, QStringLiteral("-- W"));
    setMeterRow(m_swrMeter, 0, QStringLiteral("--"));
    setMeterRow(m_alcMeter, 0, QStringLiteral("--"));
    setMeterRow(m_compressionMeter, 0, QStringLiteral("-- dB"));
    setMeterRow(m_voltageMeter, 0, QStringLiteral("-- V"));
    setMeterRow(m_currentMeter, 0, QStringLiteral("-- A"));
    setMeterRow(m_txAudioAverageMeter, 0, QStringLiteral("--"));
    setMeterRow(m_txAudioPeakMeter, 0, QStringLiteral("--"));
    setMeterFillColor(m_txAudioAverageMeter, UiTheme::Color::TextStatusLabel);
    setMeterFillColor(m_txAudioPeakMeter, UiTheme::Color::TextStatusLabel);
}

void MetersDialog::setSMeter(int value)
{
    const int bounded = qBound(0, value, kSMeterMax);
    setMeterRow(m_sMeter, scaledValue(bounded, kSMeterMax), sMeterText(bounded));
}

void MetersDialog::setPowerMeter(double watts)
{
    const double bounded = qBound(0.0, watts, kPowerMeterMaxWatts);
    setMeterRow(m_powerMeter, scaledValue(bounded, kPowerMeterMaxWatts),
                QStringLiteral("%1 W").arg(bounded, 0, 'f', 1));
}

void MetersDialog::setSwr(double swr)
{
    const double bounded = qBound(kSwrMeterMin, swr, kSwrMeterMax);
    setMeterRow(m_swrMeter, scaledValue(bounded - kSwrMeterMin, kSwrMeterMax - kSwrMeterMin),
                QStringLiteral("%1").arg(bounded, 0, 'f', 2));
}

void MetersDialog::setAlc(double alc)
{
    const double bounded = qBound(0.0, alc, kAlcMeterMax);
    setMeterRow(m_alcMeter, scaledValue(bounded, kAlcMeterMax), QStringLiteral("%1").arg(bounded, 0, 'f', 2));
}

void MetersDialog::setCompressionMeter(double db)
{
    const double bounded = qBound(0.0, db, kCompressionMeterMaxDb);
    setMeterRow(m_compressionMeter, scaledValue(bounded, kCompressionMeterMaxDb),
                QStringLiteral("%1 dB").arg(bounded, 0, 'f', 1));
}

void MetersDialog::setVoltageMeter(double volts)
{
    const double bounded = qBound(0.0, volts, kVoltageMeterMax);
    setMeterRow(m_voltageMeter, scaledValue(bounded, kVoltageMeterMax), QStringLiteral("%1 V").arg(bounded, 0, 'f', 1));
}

void MetersDialog::setCurrentMeter(double amps)
{
    const double bounded = qBound(0.0, amps, kCurrentMeterMax);
    setMeterRow(m_currentMeter, scaledValue(bounded, kCurrentMeterMax), QStringLiteral("%1 A").arg(bounded, 0, 'f', 1));
}

void MetersDialog::setTransmitAudioLevel(int peak, int rms)
{
    const int peakPct = qBound(0, qRound(qBound(0, peak, kAudioMax) * 100.0 / kAudioMax), 100);
    const int averagePct = qBound(0, qRound(qBound(0, rms, kAudioMax) * 100.0 / kAudioMax), 100);
    const bool inactive = peakPct == 0 && averagePct == 0;

    setMeterRow(m_txAudioAverageMeter, scaledValue(averagePct, 100),
                inactive ? QStringLiteral("--") : QStringLiteral("%1%").arg(averagePct));
    setMeterRow(m_txAudioPeakMeter, scaledValue(peakPct, 100),
                inactive ? QStringLiteral("--") : QStringLiteral("%1%").arg(peakPct));

    const char* averageColor = UiTheme::Color::TextStatusLabel;
    if (averagePct >= 60)
    {
        averageColor = UiTheme::Color::Warning;
    }
    else if (averagePct >= 5)
    {
        averageColor = UiTheme::Color::Success;
    }

    const char* peakColor = UiTheme::Color::TextStatusLabel;
    if (peakPct >= 95)
    {
        peakColor = UiTheme::Color::Danger;
    }
    else if (peakPct >= 85)
    {
        peakColor = UiTheme::Color::Warning;
    }
    else if (peakPct > 0)
    {
        peakColor = UiTheme::Color::Success;
    }

    setMeterFillColor(m_txAudioAverageMeter, averageColor);
    setMeterFillColor(m_txAudioPeakMeter, peakColor);
}
