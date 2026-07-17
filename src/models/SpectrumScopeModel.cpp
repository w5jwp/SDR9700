#include "SpectrumScopeModel.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace
{
constexpr double kMinSourceBandwidthMhz = 0.001;
constexpr double kScopeRangeToleranceMhz = 0.000001;

bool normalizeRange(double* startMhz, double* endMhz)
{
    if (!startMhz || !endMhz || !std::isfinite(*startMhz) || !std::isfinite(*endMhz))
    {
        return false;
    }
    if (*endMhz < *startMhz)
    {
        std::swap(*startMhz, *endMhz);
    }
    return (*endMhz - *startMhz) >= kMinSourceBandwidthMhz;
}
} // namespace

SpectrumScopeModel::SpectrumScopeModel(QObject* parent) : QObject(parent) {}

double SpectrumScopeModel::constrainedBandwidth(double bandwidthMhz) const
{
    double displayStartMhz = sourceStartMhz();
    double displayEndMhz = sourceEndMhz();
    if (m_hasFrequencyLimits)
    {
        displayStartMhz = qMax(displayStartMhz, m_limitStartMhz);
        displayEndMhz = qMin(displayEndMhz, m_limitEndMhz);
    }

    const double maximumBandwidth = qMax(kMinSourceBandwidthMhz, displayEndMhz - displayStartMhz);
    return qBound(kMinSourceBandwidthMhz, bandwidthMhz, maximumBandwidth);
}

double SpectrumScopeModel::constrainedCenter(double centerMhz, double bandwidthMhz) const
{
    double displayStartMhz = sourceStartMhz();
    double displayEndMhz = sourceEndMhz();
    if (m_hasFrequencyLimits)
    {
        displayStartMhz = qMax(displayStartMhz, m_limitStartMhz);
        displayEndMhz = qMin(displayEndMhz, m_limitEndMhz);
    }

    if (displayEndMhz <= displayStartMhz)
    {
        return centerMhz;
    }

    const double halfBandwidth = bandwidthMhz / 2.0;
    const double minCenter = displayStartMhz + halfBandwidth;
    const double maxCenter = displayEndMhz - halfBandwidth;
    return qBound(qMin(minCenter, maxCenter), centerMhz, qMax(minCenter, maxCenter));
}

void SpectrumScopeModel::constrainDisplayRange()
{
    const double nextBandwidth = constrainedBandwidth(m_bandwidthMhz);
    const double nextCenter = constrainedCenter(m_centerMhz, nextBandwidth);
    if (qAbs(nextCenter - m_centerMhz) < 1e-9 && qAbs(nextBandwidth - m_bandwidthMhz) < 1e-9)
    {
        return;
    }

    m_centerMhz = nextCenter;
    m_bandwidthMhz = nextBandwidth;
    emit rangeChanged(m_centerMhz, m_bandwidthMhz);
}

void SpectrumScopeModel::centerOnFrequency(double freqMhz)
{
    const double nextBandwidth = constrainedBandwidth(m_bandwidthMhz);
    const double nextCenter = constrainedCenter(freqMhz, nextBandwidth);
    if (qAbs(nextCenter - m_centerMhz) < 1e-9 && qAbs(nextBandwidth - m_bandwidthMhz) < 1e-9)
    {
        return;
    }

    m_centerMhz = nextCenter;
    m_bandwidthMhz = nextBandwidth;
    emit rangeChanged(m_centerMhz, m_bandwidthMhz);
}

void SpectrumScopeModel::holdDisplayCenter(double centerMhz)
{
    if (!std::isfinite(centerMhz))
    {
        return;
    }

    m_heldCenterMhz = centerMhz;
    m_hasDisplayCenterHold = true;
}

void SpectrumScopeModel::clearDisplayCenterHold()
{
    m_hasDisplayCenterHold = false;
    m_heldCenterMhz = 0.0;
}

void SpectrumScopeModel::setFrequencyLimits(double startMhz, double endMhz)
{
    if (!normalizeRange(&startMhz, &endMhz))
    {
        clearFrequencyLimits();
        return;
    }
    if (m_hasFrequencyLimits && qAbs(m_limitStartMhz - startMhz) < 1e-9 && qAbs(m_limitEndMhz - endMhz) < 1e-9)
    {
        return;
    }

    m_limitStartMhz = startMhz;
    m_limitEndMhz = endMhz;
    m_hasFrequencyLimits = true;
    constrainDisplayRange();
}

void SpectrumScopeModel::clearFrequencyLimits()
{
    if (!m_hasFrequencyLimits)
    {
        return;
    }

    m_hasFrequencyLimits = false;
    m_limitStartMhz = 0.0;
    m_limitEndMhz = 0.0;
    constrainDisplayRange();
}

void SpectrumScopeModel::ingestSpectrum(const QVector<float>& levels, double startMhz, double endMhz, bool outOfRange)
{
    // Minimum change in MHz that warrants a rangeChanged emission.
    static constexpr double kRangeChangeTolerance = 0.0001;

    const bool reversedRange = std::isfinite(startMhz) && std::isfinite(endMhz) && endMhz < startMhz;
    if (!normalizeRange(&startMhz, &endMhz))
    {
        return;
    }
    if (m_hasDisplayCenterHold &&
        (m_heldCenterMhz < startMhz - kScopeRangeToleranceMhz || m_heldCenterMhz > endMhz + kScopeRangeToleranceMhz))
    {
        return;
    }

    // Update range tracking from the radio-provided spectrum bounds.
    const double incomingCenter = (startMhz + endMhz) / 2.0;
    const double incomingBw = endMhz - startMhz;
    m_sourceCenterMhz = incomingCenter;
    m_sourceBandwidthMhz = incomingBw;

    double displayCenter = m_hasDisplayCenterHold ? m_heldCenterMhz : incomingCenter;
    double displayBandwidth = incomingBw;
    displayBandwidth = constrainedBandwidth(displayBandwidth);
    displayCenter = constrainedCenter(displayCenter, displayBandwidth);

    if (qAbs(displayCenter - m_centerMhz) > kRangeChangeTolerance ||
        qAbs(displayBandwidth - m_bandwidthMhz) > kRangeChangeTolerance)
    {
        m_centerMhz = displayCenter;
        m_bandwidthMhz = displayBandwidth;
        emit rangeChanged(m_centerMhz, m_bandwidthMhz);
    }

    if (reversedRange)
    {
        QVector<float> normalizedLevels = levels;
        std::reverse(normalizedLevels.begin(), normalizedLevels.end());
        emit spectrumReady(normalizedLevels, startMhz, endMhz, outOfRange);
        return;
    }

    emit spectrumReady(levels, startMhz, endMhz, outOfRange);
}
