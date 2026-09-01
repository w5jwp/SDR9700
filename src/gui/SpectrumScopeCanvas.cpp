#include "SpectrumScopeCanvas.h"
#include "UiTheme.h"
#include "LogCategories.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QSizeF>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace
{
constexpr int kClickMoveTolerancePx = 6;
constexpr int kLevelScaleTopInsetPx = 6;
// Map the raw scope minimum to the clipped bottom edge. This suppresses the
// artificial full-width baseline while leaving signal peaks visible.
constexpr int kLevelScaleBottomInsetPx = 0;
constexpr int kFrequencyLabelHorizontalPaddingPx = 6;
constexpr int kGridDensityFewer = 0;
constexpr int kGridDensityMore = 2;
constexpr float kSpectrumSmoothingAlpha = 0.35f;
constexpr float kMaximumSpatialSmoothBlend = 0.75f;
constexpr int kTraceSamplesPerPixel = 2;
constexpr int kTraceBottomClipInsetPx = 2;
constexpr double kScopeDisplayExponent = 0.58;
constexpr double kScopeDisplayCeilingFraction = 0.98;
constexpr double kWheelStepAngleDelta = 120.0;
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

double lowFrequencyMhz(double startMhz, double endMhz)
{
    return qMin(startMhz, endMhz);
}

double highFrequencyMhz(double startMhz, double endMhz)
{
    return qMax(startMhz, endMhz);
}

QColor colorWithAlpha(const QColor& color, int alpha)
{
    return QColor(color.red(), color.green(), color.blue(), alpha);
}

QColor spectrumHeatColor(float level)
{
    const double linearFraction = std::clamp(double(level) / 160.0, 0.0, 1.0);
    return UiTheme::spectrumSignalColor(std::pow(linearFraction, kScopeDisplayExponent));
}

int normalizedGridDensity(int density)
{
    return qBound(kGridDensityFewer, density, kGridDensityMore);
}
} // namespace

SpectrumScopeCanvas::SpectrumScopeCanvas(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);

    m_peakDecayTimer.setInterval(50);
    connect(&m_peakDecayTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_peakHoldDurationMs <= 0 || m_peakHold.isEmpty() ||
                    m_peakHoldTimestampsMs.size() != m_peakHold.size())
                {
                    return;
                }
                const qint64 nowMs = m_peakClock.elapsed();
                bool changed = false;
                for (int i = 0; i < m_peakHold.size(); ++i)
                {
                    if (nowMs - m_peakHoldTimestampsMs[i] >= m_peakHoldDurationMs)
                    {
                        if (!m_spectrumBins.isEmpty() && i < m_spectrumBins.size() &&
                            !qFuzzyCompare(m_peakHold[i], m_spectrumBins[i]))
                        {
                            m_peakHold[i] = m_spectrumBins[i];
                            changed = true;
                        }
                        m_peakHoldTimestampsMs[i] = nowMs;
                    }
                }
                if (changed)
                {
                    rebuildDisplayBins();
                    scheduleRepaint();
                }
            });
    m_peakDecayTimer.start();
    m_repaintTimer.setSingleShot(true);
    m_repaintTimer.setInterval(16);
    connect(&m_repaintTimer, &QTimer::timeout, this, qOverload<>(&SpectrumScopeCanvas::update));
    m_peakClock.start();
}

int SpectrumScopeCanvas::plotHeight() const
{
    return qMax(1, height() - scaleHeight());
}

int SpectrumScopeCanvas::plotRightX() const
{
    return qMax(plotLeftX(), width() - 1);
}

int SpectrumScopeCanvas::plotWidthPx() const
{
    return plotRightX() - plotLeftX();
}

double SpectrumScopeCanvas::xToFreq(int x) const
{
    const double startMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
    const double endMhz = highFrequencyMhz(m_startMhz, m_endMhz);
    const int plotLeft = plotLeftX();
    const int plotRight = plotRightX();
    const int plotW = plotWidthPx();
    if (plotW <= 0 || endMhz <= startMhz)
    {
        return startMhz;
    }
    // Map the right edge to the last drawable pixel, not one pixel past the
    // widget. Click-to-tune and trace drawing must share the same closed pixel
    // range or signals near the edge appear slightly displaced after tuning.
    const int plotX = qBound(plotLeft, x, plotRight);
    return startMhz + (double(plotX - plotLeft) / plotW) * (endMhz - startMhz);
}

int SpectrumScopeCanvas::freqToX(double mhz) const
{
    const double startMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
    const double endMhz = highFrequencyMhz(m_startMhz, m_endMhz);
    const int plotLeft = plotLeftX();
    const int plotW = plotWidthPx();
    if (plotW <= 0 || endMhz <= startMhz)
    {
        return plotLeft;
    }
    return plotLeft + int((mhz - startMhz) / (endMhz - startMhz) * plotW);
}

double SpectrumScopeCanvas::levelToY(float level, int topY, int h) const
{
    const double linearFraction = std::clamp(double(level - m_minLevel) / double(m_maxLevel - m_minLevel), 0.0, 1.0);
    // IC-9700 scope bytes are vertical raster intensities, not S-meter units.
    // A linear 0..160 projection substantially understates ordinary received
    // signals: a simultaneously observed S8 signal produced a scope peak near
    // 35 while CI-V 15 02 reported 103..105. The exponent maps that observation
    // to about 41% of the full S0..S9+60 meter range, while the ceiling fraction
    // reserves two percent of headroom for a maximum 160-byte scope sample.
    const double norm = std::pow(linearFraction, kScopeDisplayExponent) * kScopeDisplayCeilingFraction;
    const int topInset = qMin(kLevelScaleTopInsetPx, qMax(0, h - 1));
    const int bottomInset = qMin(kLevelScaleBottomInsetPx, qMax(0, h - 1 - topInset));
    return topY + topInset + (1.0 - norm) * qMax(1, h - 1 - topInset - bottomInset);
}

double SpectrumScopeCanvas::gridLevelToY(float level, int topY, int h) const
{
    const double norm = std::clamp(double(level - m_minLevel) / double(m_maxLevel - m_minLevel), 0.0, 1.0);
    const int topInset = qMin(kLevelScaleTopInsetPx, qMax(0, h - 1));
    const int bottomInset = qMin(kLevelScaleBottomInsetPx, qMax(0, h - 1 - topInset));
    return topY + topInset + (1.0 - norm) * qMax(1, h - 1 - topInset - bottomInset);
}

double SpectrumScopeCanvas::sourcePositionForDisplayX(double x, int binCount) const
{
    const double displayStartMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
    const double displayEndMhz = highFrequencyMhz(m_startMhz, m_endMhz);
    const double dataStartMhz = lowFrequencyMhz(m_dataStartMhz, m_dataEndMhz);
    const double dataEndMhz = highFrequencyMhz(m_dataStartMhz, m_dataEndMhz);
    const int plotW = plotWidthPx();
    if (binCount <= 0 || plotW <= 0 || displayEndMhz <= displayStartMhz || dataEndMhz <= dataStartMhz)
    {
        return -1.0;
    }
    const double boundedX = qBound(double(plotLeftX()), x, double(plotRightX()));
    const double mhz = displayStartMhz + ((boundedX - plotLeftX()) / plotW) * (displayEndMhz - displayStartMhz);
    if (mhz < dataStartMhz || mhz > dataEndMhz)
    {
        return -1.0;
    }
    if (binCount == 1)
    {
        return 0.0;
    }

    const double normalized = (mhz - dataStartMhz) / (dataEndMhz - dataStartMhz);
    return qBound(0.0, normalized * double(binCount - 1), double(binCount - 1));
}

float SpectrumScopeCanvas::interpolatedLevel(const QVector<float>& levels, double sourcePosition)
{
    if (levels.isEmpty() || sourcePosition < 0.0)
    {
        return 0.0f;
    }
    if (levels.size() == 1 || sourcePosition >= levels.size() - 1)
    {
        return levels.constLast();
    }

    const int i1 = qBound(0, int(std::floor(sourcePosition)), levels.size() - 1);
    const int i2 = qMin(i1 + 1, levels.size() - 1);
    const float t = float(sourcePosition - i1);
    const float p0 = levels[qMax(0, i1 - 1)];
    const float p1 = levels[i1];
    const float p2 = levels[i2];
    const float p3 = levels[qMin(levels.size() - 1, i2 + 1)];
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float interpolated = 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                                       (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);

    // Catmull-Rom can overshoot around a sharp transition. The scope trace is
    // measured data, so interpolation may round the path between adjacent bins
    // but must never fabricate a value outside those bins' actual range.
    return qBound(qMin(p1, p2), interpolated, qMax(p1, p2));
}

QVector<float> SpectrumScopeCanvas::spatiallySmoothedBins(const QVector<float>& bins)
{
    if (bins.size() < 3)
    {
        return bins;
    }

    int plateauPairs = 0;
    for (int i = 1; i < bins.size(); ++i)
    {
        if (qAbs(bins[i] - bins[i - 1]) < 0.01f)
        {
            ++plateauPairs;
        }
    }
    const float plateauFraction = float(plateauPairs) / float(bins.size() - 1);
    const float blend = kMaximumSpatialSmoothBlend * qBound(0.0f, (plateauFraction - 0.35f) / 0.30f, 1.0f);
    if (blend <= 0.0f)
    {
        return bins;
    }

    QVector<float> smoothedBins(bins.size());
    smoothedBins[0] = bins[0];
    smoothedBins.last() = bins.constLast();
    for (int i = 1; i < bins.size() - 1; ++i)
    {
        const float smoothed =
            (i >= 2 && i + 2 < bins.size())
                ? (bins[i - 2] + 4.0f * bins[i - 1] + 6.0f * bins[i] + 4.0f * bins[i + 1] + bins[i + 2]) / 16.0f
                : (bins[i - 1] + 2.0f * bins[i] + bins[i + 1]) / 4.0f;
        smoothedBins[i] = bins[i] * (1.0f - blend) + smoothed * blend;
    }
    return smoothedBins;
}

void SpectrumScopeCanvas::rebuildDisplayBins()
{
    // Peak hold intentionally skips temporal smoothing so it retains the
    // strongest captured sample. It does use the same spatial presentation as
    // the live trace, preventing held peaks from exposing the radio's coarse
    // source-bin stair steps.
    m_displaySpectrumBins = spatiallySmoothedBins(m_spectrumBins);
    m_displayPeakHold = spatiallySmoothedBins(m_peakHold);
}

bool SpectrumScopeCanvas::isSpectrumClickArea(const QPoint& pos) const
{
    const QRect plotRect(plotLeftX(), 0, qMax(0, width() - plotLeftX()), qMax(0, plotHeight() - 1));
    return plotRect.contains(pos);
}

void SpectrumScopeCanvas::invalidateStaticLayer()
{
    m_staticLayerDirty = true;
}

void SpectrumScopeCanvas::ensureStaticLayer()
{
    const QSize currentSize = size();
    if (!currentSize.isValid())
    {
        return;
    }

    const qreal devicePixelRatio = devicePixelRatioF();
    if (!m_staticLayerDirty && !m_staticLayer.isNull() && m_staticLayerSize == currentSize &&
        qFuzzyCompare(m_staticLayerDevicePixelRatio, devicePixelRatio))
    {
        return;
    }

    m_staticLayer = QPixmap((QSizeF(currentSize) * devicePixelRatio).toSize());
    m_staticLayer.setDevicePixelRatio(devicePixelRatio);
    m_staticLayerSize = currentSize;
    m_staticLayerDevicePixelRatio = devicePixelRatio;
    m_staticLayer.fill(Qt::transparent);

    QPainter painter(&m_staticLayer);
    painter.setRenderHint(QPainter::Antialiasing, false);
    renderStaticLayer(&painter);

    m_staticLayerDirty = false;
}

void SpectrumScopeCanvas::renderStaticLayer(QPainter* painter) const
{
    if (!painter)
    {
        return;
    }

    static const QColor kBgScale(0x06, 0x11, 0x16);
    static const QColor kGridText(0xc6, 0xe0, 0xe8);

    const int specH = plotHeight();
    const int w = width();
    const int specTop = 0;
    const int specDrawH = specH;

    QLinearGradient specBg(0, 0, 0, specH);
    specBg.setColorAt(0.00, m_backgroundColor);
    specBg.setColorAt(0.52, m_backgroundColor.lighter(145));
    specBg.setColorAt(1.00, m_backgroundColor.darker(135));
    painter->fillRect(0, 0, w, specH, specBg);

    {
        QFont f = painter->font();
        f.setPointSize(8);
        painter->setFont(f);

        const float range = m_maxLevel - m_minLevel;
        // Keep the level grid open enough that the spectrum trace is not boxed in by
        // closely spaced horizontal rules. Density preferences still scale this base.
        float majorLevelStep = range > 100.0f ? 20.0f : 10.0f;
        if (m_gridDensity == kGridDensityFewer)
        {
            majorLevelStep *= 2.0f;
        }
        else if (m_gridDensity == kGridDensityMore)
        {
            majorLevelStep /= 2.0f;
        }
        auto drawLevelLines = [&](float step)
        {
            const int firstStep = int(std::ceil(m_minLevel / step));
            const int lastStep = int(std::floor(m_maxLevel / step));
            for (int i = firstStep; i <= lastStep; ++i)
            {
                const float level = float(i) * step;
                if (qFuzzyIsNull(level - m_minLevel))
                {
                    // The opaque red scope boundary owns the minimum-level
                    // row. Drawing the blue grid floor there leaves a second
                    // device-pixel row beside it and makes the pair appear
                    // purple on high-DPI displays.
                    continue;
                }
                // Grid geometry is a visual ruler, not an amplitude transfer
                // curve. Keep its divisions linear even though received trace
                // samples use the calibrated non-linear projection.
                const int y = gridLevelToY(level, specTop, specDrawH);
                painter->drawLine(plotLeftX(), y, w, y);
            }
        };

        painter->setPen(QPen(colorWithAlpha(m_gridLineColor, 86), 1));
        drawLevelLines(majorLevelStep);

        const double scaleStartMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
        const double scaleEndMhz = highFrequencyMhz(m_startMhz, m_endMhz);
        const int plotW = qMax(1, plotWidthPx());
        const double mhzPerPx = (scaleEndMhz > scaleStartMhz) ? (scaleEndMhz - scaleStartMhz) / plotW : 1.0;
        // The reference presentation uses closely spaced major divisions. Start
        // at 100 kHz and still coarsen the step for wider spans or small canvases.
        double tickStep = 0.1;
        double minMajorGridPx = 60.0;
        if (m_gridDensity == kGridDensityFewer)
        {
            minMajorGridPx = 105.0;
        }
        else if (m_gridDensity == kGridDensityMore)
        {
            minMajorGridPx = 35.0;
        }
        while (tickStep / mhzPerPx < minMajorGridPx && tickStep < 100)
        {
            tickStep *= 2;
        }
        auto drawMhzLines = [&](double step)
        {
            const qint64 firstStep = qint64(std::ceil(scaleStartMhz / step - 1e-9));
            const qint64 lastStep = qint64(std::floor(scaleEndMhz / step + 1e-9));
            for (qint64 i = firstStep; i <= lastStep; ++i)
            {
                const double mhz = double(i) * step;
                const int x = freqToX(mhz);
                painter->drawLine(x, specTop, x, specH);
            }
        };

        painter->setPen(QPen(colorWithAlpha(m_gridLineColor, 86), 1));
        drawMhzLines(tickStep);
    }

    {
        const int scaleY = specH - 1;
        painter->fillRect(0, scaleY, w, scaleHeight(), kBgScale);
        painter->setPen(kGridText);

        QFont f = painter->font();
        f.setPointSize(8);
        painter->setFont(f);
        const QFontMetrics fontMetrics(f);

        const double scaleStartMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
        const double scaleEndMhz = highFrequencyMhz(m_startMhz, m_endMhz);
        const int plotW = qMax(1, plotWidthPx());
        const double mhzPerPx = (scaleEndMhz > scaleStartMhz) ? (scaleEndMhz - scaleStartMhz) / plotW : 1.0;
        static constexpr double kNiceSteps[] = {100.0, 50.0, 25.0, 10.0, 5.0,   2.5,  1.0,
                                                0.5,   0.25, 0.1,  0.05, 0.025, 0.01, 0.005};
        double tickStep = kNiceSteps[0];
        for (double s : kNiceSteps)
        {
            if (s / mhzPerPx >= 70.0)
            {
                tickStep = s;
            }
            else
            {
                break;
            }
        }

        const int decimals = (tickStep >= 1.0) ? 1 : (tickStep >= 0.1) ? 2 : 3;
        const int tickTop = scaleY + 2;
        const int tickH = 6;
        const int textY = scaleY + 21;
        const qint64 firstStep = qint64(std::ceil(scaleStartMhz / tickStep - 1e-9));
        const qint64 lastStep = qint64(std::floor(scaleEndMhz / tickStep + 1e-9));
        for (qint64 i = firstStep; i <= lastStep; ++i)
        {
            const double mhz = double(i) * tickStep;
            const int x = freqToX(mhz);
            const QString label = QString::number(mhz, 'f', decimals);
            const int labelW = fontMetrics.horizontalAdvance(label);
            const int labelX = qBound(plotLeftX() + kFrequencyLabelHorizontalPaddingPx, x - labelW / 2,
                                      qMax(plotLeftX() + kFrequencyLabelHorizontalPaddingPx,
                                           w - labelW - kFrequencyLabelHorizontalPaddingPx));
            painter->setPen(QPen(kGridText, 1));
            painter->drawLine(x, tickTop, x, tickTop + tickH);
            painter->setPen(kGridText);
            painter->drawText(labelX, textY, label);
        }
    }
}

void SpectrumScopeCanvas::setFrequencyRange(double startMhz, double endMhz)
{
    if (!normalizeFrequencyRange(&startMhz, &endMhz))
    {
        return;
    }
    if (m_startMhz == startMhz && m_endMhz == endMhz)
    {
        return;
    }
    m_startMhz = startMhz;
    m_endMhz = endMhz;
    m_resetSpectrumSmoothing = true;
    invalidateStaticLayer();
    scheduleRepaint();
}

void SpectrumScopeCanvas::setDataFrequencyRange(double startMhz, double endMhz)
{
    if (!normalizeFrequencyRange(&startMhz, &endMhz))
    {
        return;
    }
    if (m_dataStartMhz == startMhz && m_dataEndMhz == endMhz)
    {
        return;
    }
    m_dataStartMhz = startMhz;
    m_dataEndMhz = endMhz;
    m_resetSpectrumSmoothing = true;
}

void SpectrumScopeCanvas::setVfoFrequency(double freqMhz)
{
    m_vfoMhz = freqMhz;
    scheduleRepaint();
}

void SpectrumScopeCanvas::setVfoMarkerColor(const QColor& color)
{
    if (!color.isValid())
    {
        return;
    }

    QColor markerColor = color;
    markerColor.setAlpha(230);
    if (m_vfoMarkerColor == markerColor)
    {
        return;
    }

    m_vfoMarkerColor = markerColor;
    scheduleRepaint();
}

void SpectrumScopeCanvas::setBackgroundColor(const QColor& color)
{
    if (!color.isValid())
    {
        return;
    }

    const QColor normalized(color.red(), color.green(), color.blue());
    if (m_backgroundColor == normalized)
    {
        return;
    }

    m_backgroundColor = normalized;
    invalidateStaticLayer();
    scheduleRepaint();
}

void SpectrumScopeCanvas::setGridLineColor(const QColor& color)
{
    if (!color.isValid())
    {
        return;
    }

    const QColor normalized(color.red(), color.green(), color.blue());
    if (m_gridLineColor == normalized)
    {
        return;
    }

    m_gridLineColor = normalized;
    invalidateStaticLayer();
    scheduleRepaint();
}

void SpectrumScopeCanvas::setGridDensity(int density)
{
    const int normalized = normalizedGridDensity(density);
    if (m_gridDensity == normalized)
    {
        return;
    }

    m_gridDensity = normalized;
    invalidateStaticLayer();
    scheduleRepaint();
}

void SpectrumScopeCanvas::setFilterWidth(int lowHz, int highHz)
{
    m_filterLowHz = lowHz;
    m_filterHighHz = highHz;
    scheduleRepaint();
}

void SpectrumScopeCanvas::setInteractionLocked(bool locked)
{
    if (m_interactionLocked == locked)
    {
        return;
    }

    m_interactionLocked = locked;
    m_clickPressed = false;
    scheduleRepaint();
}

void SpectrumScopeCanvas::setInvertMouseWheel(bool invert)
{
    m_invertMouseWheel = invert;
}

void SpectrumScopeCanvas::setPeakHoldDurationMs(int durationMs)
{
    durationMs = qMax(0, durationMs);
    if (m_peakHoldDurationMs == durationMs)
    {
        return;
    }
    m_peakHoldDurationMs = durationMs;
    m_peakHold.clear();
    m_peakHoldTimestampsMs.clear();
    if (durationMs > 0 && !m_spectrumBins.isEmpty())
    {
        m_peakHold = m_spectrumBins;
        m_peakHoldTimestampsMs.fill(m_peakClock.elapsed(), m_spectrumBins.size());
    }
    rebuildDisplayBins();
    scheduleRepaint();
}

void SpectrumScopeCanvas::updateSpectrum(const QVector<float>& levels, bool outOfRange)
{
    // Keep an owned copy for painting because the incoming QVector belongs to
    // the model signal delivery path and may be superseded before paintEvent().
    // Backout/optimization point: a future double-buffered SpectrumScopeModel
    // could own this storage and let the canvas paint a shared immutable frame.
    // Reuse the existing backing store where possible. Spectrum frames arrive
    // continuously, so avoiding a fresh QVector allocation per repaint keeps
    // click tuning and waterfall painting from competing with allocator churn.
    if (m_resetSpectrumSmoothing || m_spectrumBins.size() != levels.size())
    {
        m_spectrumBins = levels;
        m_resetSpectrumSmoothing = false;
    }
    else
    {
        for (int i = 0; i < levels.size(); ++i)
        {
            m_spectrumBins[i] =
                kSpectrumSmoothingAlpha * levels[i] + (1.0f - kSpectrumSmoothingAlpha) * m_spectrumBins[i];
        }
    }
    m_scopeOutOfRange = outOfRange;
    if (m_peakHoldDurationMs <= 0)
    {
        m_peakHold.clear();
        m_peakHoldTimestampsMs.clear();
    }
    else if (m_peakHold.size() != levels.size())
    {
        m_peakHold = levels;
        m_peakHoldTimestampsMs.fill(m_peakClock.elapsed(), levels.size());
    }
    else
    {
        const qint64 nowMs = m_peakClock.elapsed();
        for (int i = 0; i < levels.size(); ++i)
        {
            if (levels[i] > m_peakHold[i])
            {
                m_peakHold[i] = levels[i];
                m_peakHoldTimestampsMs[i] = nowMs;
            }
        }
    }

    rebuildDisplayBins();

    scheduleRepaint();
}

void SpectrumScopeCanvas::clearDisplay()
{
    m_spectrumBins.clear();
    m_displaySpectrumBins.clear();
    m_peakHold.clear();
    m_displayPeakHold.clear();
    m_peakHoldTimestampsMs.clear();
    m_scopeOutOfRange = false;
    m_resetSpectrumSmoothing = true;
    scheduleRepaint();
}

void SpectrumScopeCanvas::scheduleRepaint()
{
    if (!m_repaintTimer.isActive())
    {
        m_repaintTimer.start();
    }
}

void SpectrumScopeCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    static const QColor kPeak(0x8b, 0xa0, 0xb0, 95);
    ensureStaticLayer();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int specH = plotHeight();
    const int w = width();
    const int specTop = 0;
    const int specDrawH = specH;
    // Keep the rounded trace feather away from the scale boundary. A zero-level
    // trace lies on that boundary; without this separate inset its blue outer
    // stroke blends with the red separator and makes the line look purple.
    const QRect spectrumPlotRect(plotLeftX(), specTop, qMax(0, w - plotLeftX()),
                                 qMax(0, specDrawH - kTraceBottomClipInsetPx));
    p.drawPixmap(0, 0, m_staticLayer);

    if (!m_displaySpectrumBins.isEmpty())
    {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.save();
        p.setClipRect(spectrumPlotRect);
        QPainterPath peakPath;
        QVector<QPointF> tracePoints;
        QVector<float> traceLevels;
        bool peakFirst = true;

        const int sampleCount = qMax(1, plotWidthPx() * kTraceSamplesPerPixel);
        tracePoints.reserve(sampleCount + 1);
        traceLevels.reserve(sampleCount + 1);
        for (int sample = 0; sample <= sampleCount; ++sample)
        {
            const double x = plotLeftX() + (double(sample) / sampleCount) * plotWidthPx();
            const double sourcePosition = sourcePositionForDisplayX(x, m_displaySpectrumBins.size());
            const float level =
                sourcePosition >= 0.0 ? interpolatedLevel(m_displaySpectrumBins, sourcePosition) : m_minLevel;

            const double sy = levelToY(level, specTop, specDrawH);
            tracePoints.append(QPointF(x, sy));
            traceLevels.append(level);

            if (!m_displayPeakHold.isEmpty() && sourcePosition >= 0.0)
            {
                const double py = levelToY(interpolatedLevel(m_displayPeakHold, sourcePosition), specTop, specDrawH);
                if (peakFirst)
                {
                    peakPath.moveTo(x, py);
                    peakFirst = false;
                }
                else
                {
                    peakPath.lineTo(x, py);
                }
            }
        }

        p.setPen(Qt::NoPen);
        for (int i = 0; i + 1 < tracePoints.size(); ++i)
        {
            const QPointF& first = tracePoints[i];
            const QPointF& second = tracePoints[i + 1];
            const float averageLevel = (traceLevels[i] + traceLevels[i + 1]) * 0.5f;
            QColor topColor = spectrumHeatColor(averageLevel);
            topColor.setAlpha(54);
            const QColor bottomColor(0x00, 0x00, 0x4d, 178);
            QLinearGradient fillGradient(0.0, qMin(first.y(), second.y()), 0.0, specH);
            fillGradient.setColorAt(0.0, topColor);
            fillGradient.setColorAt(1.0, bottomColor);
            QPolygonF segment;
            segment << first << second << QPointF(second.x(), specH) << QPointF(first.x(), specH);
            p.setBrush(fillGradient);
            p.drawPolygon(segment);
        }

        p.setPen(QPen(kPeak, 1));
        p.drawPath(peakPath);

        for (int i = 0; i + 1 < tracePoints.size(); ++i)
        {
            QColor featherColor = spectrumHeatColor((traceLevels[i] + traceLevels[i + 1]) * 0.5f);
            featherColor.setAlpha(56);
            p.setPen(QPen(featherColor, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawLine(tracePoints[i], tracePoints[i + 1]);
        }
        for (int i = 0; i + 1 < tracePoints.size(); ++i)
        {
            const QColor traceColor = spectrumHeatColor((traceLevels[i] + traceLevels[i + 1]) * 0.5f);
            p.setPen(QPen(traceColor, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawLine(tracePoints[i], tracePoints[i + 1]);
        }

        p.restore();
        p.setRenderHint(QPainter::Antialiasing, false);
    }

    if (m_scopeOutOfRange)
    {
        p.setPen(QColor(0xff, 0x7a, 0x7a));
        QFont f = p.font();
        f.setPointSize(10);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(0, specTop, w, specDrawH), Qt::AlignCenter, QStringLiteral("OUT OF RANGE"));
    }

    const double visibleStartMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
    const double visibleEndMhz = highFrequencyMhz(m_startMhz, m_endMhz);
    if (m_vfoMhz >= visibleStartMhz && m_vfoMhz <= visibleEndMhz && m_filterHighHz > m_filterLowHz)
    {
        const double loMhz = m_vfoMhz + m_filterLowHz / 1e6;
        const double hiMhz = m_vfoMhz + m_filterHighHz / 1e6;
        const int fx1 = freqToX(loMhz);
        const int fx2 = freqToX(hiMhz);
        const QColor filterFill(0x00, 0xb4, 0xd8, 22);
        p.fillRect(fx1, 0, fx2 - fx1, specH, filterFill);
    }

    if (m_vfoMhz >= visibleStartMhz && m_vfoMhz <= visibleEndMhz)
    {
        const int vx = freqToX(m_vfoMhz);
        const int scaleY = specH - 1;
        p.setPen(QPen(m_vfoMarkerColor, 1, Qt::SolidLine));
        p.drawLine(vx, 0, vx, scaleY - 1);
    }

    // Draw the scale boundary independently and last. Raw scope level zero is
    // deliberately clipped at this edge, but must not obscure the red border.
    const int scaleY = specH - 1;
    // Use an opaque rectangle rather than a one-pixel pen. A pen is centered
    // on its coordinate and can straddle the adjacent blue trace-floor row at
    // high device-pixel ratios, making the nominally red boundary look purple.
    p.fillRect(plotLeftX(), scaleY, qMax(0, w - plotLeftX()), 1, UiTheme::Color::SpectrumBoundary);
}

void SpectrumScopeCanvas::mousePressEvent(QMouseEvent* ev)
{
    if (m_interactionLocked)
    {
        return;
    }
    if (ev->button() == Qt::LeftButton)
    {
        m_clickPressed = isSpectrumClickArea(ev->pos());
        m_clickPressPos = ev->pos();
        ev->accept();
    }
}

void SpectrumScopeCanvas::mouseReleaseEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton)
    {
        const bool isClick = m_clickPressed && isSpectrumClickArea(ev->pos()) &&
                             (ev->pos() - m_clickPressPos).manhattanLength() <= kClickMoveTolerancePx;
        m_clickPressed = false;
        if (!m_interactionLocked && isClick)
        {
            Q_EMIT frequencyClicked(xToFreq(ev->pos().x()));
        }
        ev->accept();
    }
}

void SpectrumScopeCanvas::wheelEvent(QWheelEvent* ev)
{
    if (m_interactionLocked)
    {
        ev->ignore();
        return;
    }

    const QPoint angle = ev->angleDelta();
    const int rawDelta = angle.y() != 0 ? angle.y() : angle.x();
    if (rawDelta == 0 || m_endMhz <= m_startMhz)
    {
        ev->ignore();
        return;
    }

    double physicalSteps = rawDelta / kWheelStepAngleDelta;
    if (ev->inverted())
    {
        physicalSteps = -physicalSteps;
    }
    if (m_invertMouseWheel)
    {
        physicalSteps = -physicalSteps;
    }

    if ((m_wheelStepAccumulator > 0.0 && physicalSteps > 0.0) || (m_wheelStepAccumulator < 0.0 && physicalSteps < 0.0))
    {
        m_wheelStepAccumulator += physicalSteps;
    }
    else
    {
        m_wheelStepAccumulator = physicalSteps;
    }

    const int acceptedSteps = static_cast<int>(m_wheelStepAccumulator);
    if (acceptedSteps == 0)
    {
        ev->accept();
        return;
    }

    m_wheelStepAccumulator -= acceptedSteps;
    qDebug(logSpectrumScope()).noquote().nospace()
        << "Spectrum scope wheel angle=" << angle << " pixel=" << ev->pixelDelta() << " qtInverted=" << ev->inverted()
        << " physicalSteps=" << physicalSteps << " reversePreference=" << m_invertMouseWheel
        << " acceptedSteps=" << acceptedSteps << " accumulator=" << m_wheelStepAccumulator;

    Q_EMIT wheelStepRequested(acceptedSteps);
    ev->accept();
}
