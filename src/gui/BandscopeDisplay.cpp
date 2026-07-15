#include "BandscopeDisplay.h"
#include "BandscopeCanvas.h"
#include "WaterfallCanvas.h"

#include <QResizeEvent>

namespace
{
constexpr int kMinSpectrumHeight = 150;
constexpr int kMinWaterfallHeight = 180;
constexpr int kDefaultBandscopeHeightBias = 0;
} // namespace

BandscopeDisplay::BandscopeDisplay(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(640, 320);

    m_bandscopeCanvas = new BandscopeCanvas(this);
    m_splitter = new QWidget(this);
    m_waterfallCanvas = new WaterfallCanvas(this);

    m_splitter->setStyleSheet(QStringLiteral("QWidget { background: #061116; border-top: 1px solid #9a2424; }"));
    m_splitter->setFixedHeight(splitterHeight());

    connect(m_bandscopeCanvas, &BandscopeCanvas::frequencyClicked, this, &BandscopeDisplay::frequencyClicked);
    connect(m_bandscopeCanvas, &BandscopeCanvas::tuneStepRequested, this, &BandscopeDisplay::tuneStepRequested);
    connect(m_bandscopeCanvas, &BandscopeCanvas::tuneDragStarted, this, &BandscopeDisplay::tuneDragStarted);
    connect(m_bandscopeCanvas, &BandscopeCanvas::tuneDragRequested, this, &BandscopeDisplay::tuneDragRequested);
}

int BandscopeDisplay::defaultSpectrumHeight() const
{
    return constrainedSpectrumHeight(((height() - BandscopeCanvas::scaleHeight() - splitterHeight()) / 2) +
                                     kDefaultBandscopeHeightBias);
}

int BandscopeDisplay::constrainedSpectrumHeight(int requested) const
{
    const int available = qMax(0, height() - BandscopeCanvas::scaleHeight() - splitterHeight());
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
    if (!m_bandscopeCanvas || !m_splitter || !m_waterfallCanvas)
    {
        return;
    }

    const int spectrumHeight = currentSpectrumHeight();
    const int bandscopeHeight = spectrumHeight + BandscopeCanvas::scaleHeight();
    const int splitTop = bandscopeHeight;
    const int waterfallTop = splitTop + splitterHeight();
    const int waterfallHeight = qMax(0, height() - waterfallTop);

    m_bandscopeCanvas->setGeometry(0, 0, width(), bandscopeHeight);
    m_splitter->setGeometry(0, splitTop, width(), splitterHeight());
    m_waterfallCanvas->setGeometry(0, waterfallTop, width(), waterfallHeight);
}

void BandscopeDisplay::setFrequencyRange(double startMhz, double endMhz)
{
    m_bandscopeCanvas->setFrequencyRange(startMhz, endMhz);
    m_waterfallCanvas->setFrequencyRange(startMhz, endMhz);
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

void BandscopeDisplay::setInteractionLocked(bool locked)
{
    m_bandscopeCanvas->setInteractionLocked(locked);
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
