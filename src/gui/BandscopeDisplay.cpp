#include "BandscopeDisplay.h"
#include "BandscopeCanvas.h"
#include "WaterfallCanvas.h"

#include <QComboBox>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr int kMinSpectrumHeight = 150;
constexpr int kMinWaterfallHeight = 180;
constexpr int kDefaultBandscopeHeightBias = 0;
constexpr int kSpanComboWidth = 94;
constexpr int kSpanComboHeight = 24;
constexpr int kSpanComboMargin = 8;
constexpr qint64 kPanScrollUnitHz = 100;
constexpr double kMinFrequencyRangeMhz = 0.001;

bool normalizeFrequencyRange(double* startMhz, double* endMhz)
{
    if (!startMhz || !endMhz || !std::isfinite(*startMhz) || !std::isfinite(*endMhz))
    {
        return false;
    }
    if (*endMhz < *startMhz)
    {
        std::swap(*startMhz, *endMhz);
    }
    return (*endMhz - *startMhz) >= kMinFrequencyRangeMhz;
}

int scrollUnitForMhz(double mhz)
{
    const qint64 unit = static_cast<qint64>(std::llround(mhz * 1e6 / kPanScrollUnitHz));
    return static_cast<int>(std::clamp<qint64>(unit, 0, std::numeric_limits<int>::max()));
}

int scrollUnitsForMhzDelta(double mhz)
{
    if (!std::isfinite(mhz) || mhz <= 0.0)
    {
        return 1;
    }

    const qint64 units = static_cast<qint64>(std::llround(mhz * 1e6 / kPanScrollUnitHz));
    return static_cast<int>(std::clamp<qint64>(units, 1, std::numeric_limits<int>::max()));
}

double mhzForScrollUnit(int unit)
{
    return (static_cast<double>(unit) * kPanScrollUnitHz) / 1e6;
}
} // namespace

BandscopeDisplay::BandscopeDisplay(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(640, 320);

    m_bandscopeCanvas = new BandscopeCanvas(this);
    m_panScrollBar = new QScrollBar(Qt::Horizontal, this);
    m_waterfallCanvas = new WaterfallCanvas(this);
    m_spanCombo = new QComboBox(this);

    m_panScrollBar->setFixedHeight(panScrollBarHeight());
    m_panScrollBar->setTracking(true);
    m_panScrollBar->setEnabled(false);
    m_panScrollBar->setToolTip(QStringLiteral("Pan bandscope"));
    m_panScrollBar->setAccessibleName(QStringLiteral("Bandscope pan"));
    m_panScrollBar->setStyleSheet(QStringLiteral(
        "QScrollBar:horizontal { background: #061116; border-top: 1px solid #0d2630; border-bottom: 1px solid #9a2424; "
        "height: 16px; margin: 0px; }"
        "QScrollBar::groove:horizontal { background: #061116; border: 1px solid #173542; border-radius: 4px; }"
        "QScrollBar::handle:horizontal { background: #2c8195; border: 1px solid #6fb5c8; border-radius: 4px; "
        "min-width: 34px; margin: 2px 1px; }"
        "QScrollBar::handle:horizontal:hover { background: #3b9eb5; border-color: #9bd8e9; }"
        "QScrollBar::handle:horizontal:pressed { background: #4bb9d2; border-color: #d6f5ff; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { background: transparent; border: none; "
        "width: 0px; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }"
        "QScrollBar:disabled { background: #061116; border-bottom-color: #061116; }"
        "QScrollBar::groove:disabled { background: #071116; border-color: #15242b; }"
        "QScrollBar::handle:disabled { background: #22313a; border-color: #33424a; }"));
    m_spanCombo->setFixedSize(kSpanComboWidth, kSpanComboHeight);
    m_spanCombo->setFocusPolicy(Qt::NoFocus);
    m_spanCombo->setToolTip(QStringLiteral("Bandscope span"));
    m_spanCombo->setStyleSheet(
        QStringLiteral("QComboBox { background: rgba(16, 22, 30, 220); "
                       "border: 1px solid #566576; border-radius: 3px; "
                       "color: #e8f2f8; font-size: 10px; font-weight: bold; "
                       "padding: 1px 18px 1px 6px; }"
                       "QComboBox:hover { background: rgba(32, 42, 55, 235); "
                       "border-color: #7f96ad; }"
                       "QComboBox::drop-down { border: none; width: 16px; }"
                       "QComboBox::down-arrow { image: none; width: 0px; height: 0px; "
                       "border-left: 4px solid transparent; border-right: 4px solid transparent; "
                       "border-top: 5px solid #e8f2f8; margin-right: 5px; }"
                       "QComboBox QAbstractItemView { background: #10161e; "
                       "border: 1px solid #566576; color: #e8f2f8; "
                       "selection-background-color: #2a82da; }"));
    m_spanCombo->raise();

    connect(m_bandscopeCanvas, &BandscopeCanvas::frequencyClicked, this, &BandscopeDisplay::frequencyClicked);
    connect(m_bandscopeCanvas, &BandscopeCanvas::wheelStepRequested, this, &BandscopeDisplay::panScrollBarBySteps);
    connect(m_panScrollBar, &QScrollBar::sliderPressed, m_waterfallCanvas,
            [this]() { m_waterfallCanvas->setPaused(true); });
    connect(m_panScrollBar, &QScrollBar::sliderReleased, m_waterfallCanvas,
            [this]() { m_waterfallCanvas->setPaused(false); });
    connect(m_panScrollBar, &QScrollBar::valueChanged, this,
            [this](int value)
            {
                if (!m_hasPanRange || m_interactionLocked || !m_panScrollBar->isEnabled())
                {
                    return;
                }
                Q_EMIT panCenterRequested(mhzForScrollUnit(value));
            });
    connect(m_spanCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index)
            {
                if (index < 0)
                {
                    return;
                }
                Q_EMIT spanSelected(m_spanCombo->itemData(index).toULongLong());
            });
}

void BandscopeDisplay::setSpanChoices(const QVector<SpanChoice>& choices)
{
    const quint64 previousHz = m_spanCombo->currentData().toULongLong();
    {
        const QSignalBlocker blocker(m_spanCombo);
        m_spanCombo->clear();
        for (const SpanChoice& choice : choices)
        {
            m_spanCombo->addItem(choice.label, QVariant::fromValue<qulonglong>(choice.hz));
        }
    }
    setCurrentSpanHz(previousHz);
    m_spanCombo->setVisible(!choices.isEmpty());
    updateSpanComboGeometry();
}

void BandscopeDisplay::setCurrentSpanHz(quint64 hz)
{
    const QSignalBlocker blocker(m_spanCombo);
    const int index = m_spanCombo->findData(QVariant::fromValue<qulonglong>(hz));
    if (index >= 0)
    {
        m_spanCombo->setCurrentIndex(index);
    }
}

void BandscopeDisplay::updateSpanComboGeometry()
{
    if (!m_spanCombo)
    {
        return;
    }
    const int x = qMax(kSpanComboMargin, width() - kSpanComboMargin - m_spanCombo->width());
    m_spanCombo->move(x, kSpanComboMargin);
    m_spanCombo->raise();
}

void BandscopeDisplay::updatePanScrollBar()
{
    if (!m_panScrollBar)
    {
        return;
    }

    double visibleStartMhz = m_visibleStartMhz;
    double visibleEndMhz = m_visibleEndMhz;
    double panStartMhz = m_panStartMhz;
    double panEndMhz = m_panEndMhz;
    const bool validRange = normalizeFrequencyRange(&visibleStartMhz, &visibleEndMhz) && m_hasPanRange &&
                            normalizeFrequencyRange(&panStartMhz, &panEndMhz);
    if (!validRange)
    {
        const QSignalBlocker blocker(m_panScrollBar);
        m_panScrollBar->setRange(0, 0);
        m_panScrollBar->setPageStep(1);
        m_panScrollBar->setSingleStep(1);
        m_panScrollBar->setValue(0);
        m_panScrollBar->setEnabled(false);
        return;
    }

    const double bandwidthMhz = visibleEndMhz - visibleStartMhz;
    const double halfBandwidthMhz = bandwidthMhz / 2.0;
    const double minCenterMhz = panStartMhz + halfBandwidthMhz;
    const double maxCenterMhz = panEndMhz - halfBandwidthMhz;
    const bool canPan = maxCenterMhz > minCenterMhz;
    const double centerMhz = qBound(qMin(minCenterMhz, maxCenterMhz), (visibleStartMhz + visibleEndMhz) / 2.0,
                                    qMax(minCenterMhz, maxCenterMhz));

    const QSignalBlocker blocker(m_panScrollBar);
    m_panScrollBar->setRange(scrollUnitForMhz(qMin(minCenterMhz, maxCenterMhz)),
                             scrollUnitForMhz(qMax(minCenterMhz, maxCenterMhz)));
    const int pageStep = scrollUnitsForMhzDelta(bandwidthMhz);
    m_panScrollBar->setPageStep(pageStep);
    m_panScrollBar->setSingleStep(qMax(1, pageStep / 100));
    m_panScrollBar->setValue(scrollUnitForMhz(centerMhz));
    m_panScrollBar->setEnabled(!m_interactionLocked && canPan);
}

void BandscopeDisplay::panScrollBarBySteps(int steps)
{
    if (steps == 0 || !m_panScrollBar || !m_panScrollBar->isEnabled())
    {
        return;
    }

    const int stepSize = qMax(1, m_panScrollBar->singleStep());
    m_panScrollBar->setValue(m_panScrollBar->value() + steps * stepSize);
}

int BandscopeDisplay::defaultSpectrumHeight() const
{
    return constrainedSpectrumHeight(((height() - BandscopeCanvas::scaleHeight() - panScrollBarHeight()) / 2) +
                                     kDefaultBandscopeHeightBias);
}

int BandscopeDisplay::constrainedSpectrumHeight(int requested) const
{
    const int available = qMax(0, height() - BandscopeCanvas::scaleHeight() - panScrollBarHeight());
    const int maxSpectrumHeight = qMax(kMinSpectrumHeight, available - kMinWaterfallHeight);
    return qBound(qMin(kMinSpectrumHeight, maxSpectrumHeight), requested, maxSpectrumHeight);
}

int BandscopeDisplay::currentSpectrumHeight() const
{
    if (m_spectrumHeight < 0)
    {
        return defaultSpectrumHeight();
    }
    return constrainedSpectrumHeight(m_spectrumHeight);
}

void BandscopeDisplay::updateChildGeometry()
{
    if (!m_bandscopeCanvas || !m_panScrollBar || !m_waterfallCanvas)
    {
        return;
    }

    const int spectrumHeight = currentSpectrumHeight();
    const int bandscopeHeight = spectrumHeight + BandscopeCanvas::scaleHeight();
    const int splitTop = bandscopeHeight;
    const int waterfallTop = splitTop + panScrollBarHeight();
    const int waterfallHeight = qMax(0, height() - waterfallTop);
    const int plotLeft = BandscopeCanvas::levelScalePanelWidth();

    m_bandscopeCanvas->setGeometry(0, 0, width(), bandscopeHeight);
    m_panScrollBar->setGeometry(plotLeft, splitTop, qMax(0, width() - plotLeft), panScrollBarHeight());
    m_waterfallCanvas->setGeometry(0, waterfallTop, width(), waterfallHeight);
    updateSpanComboGeometry();
}

void BandscopeDisplay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
}

void BandscopeDisplay::setFrequencyRange(double startMhz, double endMhz)
{
    m_visibleStartMhz = startMhz;
    m_visibleEndMhz = endMhz;
    m_bandscopeCanvas->setFrequencyRange(startMhz, endMhz);
    m_waterfallCanvas->setFrequencyRange(startMhz, endMhz);
    updatePanScrollBar();
}

void BandscopeDisplay::setFrequencyPanRange(double startMhz, double endMhz)
{
    if (!normalizeFrequencyRange(&startMhz, &endMhz))
    {
        clearFrequencyPanRange();
        return;
    }

    if (m_hasPanRange && m_panStartMhz == startMhz && m_panEndMhz == endMhz)
    {
        return;
    }

    m_panStartMhz = startMhz;
    m_panEndMhz = endMhz;
    m_hasPanRange = true;
    updatePanScrollBar();
}

void BandscopeDisplay::clearFrequencyPanRange()
{
    if (!m_hasPanRange)
    {
        return;
    }

    m_hasPanRange = false;
    m_panStartMhz = 0.0;
    m_panEndMhz = 0.0;
    updatePanScrollBar();
}

void BandscopeDisplay::setDataFrequencyRange(double startMhz, double endMhz)
{
    m_bandscopeCanvas->setDataFrequencyRange(startMhz, endMhz);
    m_waterfallCanvas->setDataFrequencyRange(startMhz, endMhz);
}

void BandscopeDisplay::setVfoFrequency(double freqMhz)
{
    m_bandscopeCanvas->setVfoFrequency(freqMhz);
}

void BandscopeDisplay::setVfoMarkerColor(const QColor& color)
{
    m_bandscopeCanvas->setVfoMarkerColor(color);
}

void BandscopeDisplay::setBackgroundColor(const QColor& color)
{
    m_bandscopeCanvas->setBackgroundColor(color);
}

void BandscopeDisplay::setGridLineColor(const QColor& color)
{
    m_bandscopeCanvas->setGridLineColor(color);
}

void BandscopeDisplay::setGridDensity(int density)
{
    m_bandscopeCanvas->setGridDensity(density);
}

void BandscopeDisplay::setInteractionLocked(bool locked)
{
    m_interactionLocked = locked;
    m_bandscopeCanvas->setInteractionLocked(locked);
    updatePanScrollBar();
}

void BandscopeDisplay::setInvertMouseWheel(bool invert)
{
    m_bandscopeCanvas->setInvertMouseWheel(invert);
}

int BandscopeDisplay::spectrumPaneHeight() const
{
    return currentSpectrumHeight();
}

void BandscopeDisplay::setSpectrumPaneHeight(int height)
{
    if (height <= 0 || m_spectrumHeight == height)
    {
        return;
    }

    m_spectrumHeight = height;
    updateChildGeometry();
}

void BandscopeDisplay::updateSpectrum(const QVector<float>& levels, bool outOfRange)
{
    m_bandscopeCanvas->updateSpectrum(levels, outOfRange);
    m_waterfallCanvas->updateSpectrum(levels);
}

void BandscopeDisplay::clearDisplay()
{
    m_bandscopeCanvas->clearDisplay();
    m_waterfallCanvas->clearDisplay();
}

void BandscopeDisplay::setFilterWidth(int lowHz, int highHz)
{
    m_bandscopeCanvas->setFilterWidth(lowHz, highHz);
}

int BandscopeDisplay::freqToX(double mhz) const
{
    return m_bandscopeCanvas->freqToX(mhz);
}

void BandscopeDisplay::resizeEvent(QResizeEvent* event)
{
    Q_UNUSED(event)
    updateChildGeometry();
}
