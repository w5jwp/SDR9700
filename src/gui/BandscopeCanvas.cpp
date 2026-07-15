#include "BandscopeCanvas.h"
#include "LogCategories.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace
{
constexpr float kPeakDecayLevelPerSec = 25.0f;
constexpr float kPeakDecayPerTickLevel = kPeakDecayLevelPerSec * 0.05f;
constexpr int kClickMoveTolerancePx = 6;
constexpr int kLevelScaleTopInsetPx = 6;
constexpr int kLevelScaleBottomInsetPx = 9;
constexpr int kGridDensityFewer = 0;
constexpr int kGridDensityNormal = 1;
constexpr int kGridDensityMore = 2;
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

int normalizedGridDensity(int density)
{
    return qBound(kGridDensityFewer, density, kGridDensityMore);
}
} // namespace

BandscopeCanvas::BandscopeCanvas(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);

    m_peakDecayTimer.setInterval(50);
    connect(&m_peakDecayTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_peakHold.isEmpty())
                {
                    return;
                }
                bool changed = false;
                for (int i = 0; i < m_peakHold.size(); ++i)
                {
                    if (!m_spectrumBins.isEmpty() && i < m_spectrumBins.size() && m_peakHold[i] > m_spectrumBins[i])
                    {
                        m_peakHold[i] -= kPeakDecayPerTickLevel;
                        changed = true;
                    }
                }
                if (changed)
                {
                    scheduleRepaint();
                }
            });
    m_peakDecayTimer.start();
    m_repaintTimer.setSingleShot(true);
    m_repaintTimer.setInterval(16);
    connect(&m_repaintTimer, &QTimer::timeout, this, qOverload<>(&BandscopeCanvas::update));
    m_lastFrameTimer.start();
}

int BandscopeCanvas::plotHeight() const
{
    return qMax(1, height() - scaleHeight());
}

int BandscopeCanvas::spectrumPaneHeight() const
{
    return plotHeight();
}

double BandscopeCanvas::xToFreq(int x) const
{
    const double startMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
    const double endMhz = highFrequencyMhz(m_startMhz, m_endMhz);
    const int plotW = width() - levelScalePanelWidth();
    if (plotW <= 0 || endMhz <= startMhz)
    {
        return startMhz;
    }
    const int plotX = qBound(levelScalePanelWidth(), x, width());
    return startMhz + (double(plotX - levelScalePanelWidth()) / plotW) * (endMhz - startMhz);
}

int BandscopeCanvas::freqToX(double mhz) const
{
    const double startMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
    const double endMhz = highFrequencyMhz(m_startMhz, m_endMhz);
    const int plotW = width() - levelScalePanelWidth();
    if (plotW <= 0 || endMhz <= startMhz)
    {
        return levelScalePanelWidth();
    }
    return levelScalePanelWidth() + int((mhz - startMhz) / (endMhz - startMhz) * plotW);
}

int BandscopeCanvas::levelToY(float level, int topY, int h) const
{
    float norm = (level - m_minLevel) / (m_maxLevel - m_minLevel);
    norm = std::max(0.0f, std::min(1.0f, norm));
    const int topInset = qMin(kLevelScaleTopInsetPx, qMax(0, h - 1));
    const int bottomInset = qMin(kLevelScaleBottomInsetPx, qMax(0, h - 1 - topInset));
    return topY + topInset + int((1.0f - norm) * qMax(1, h - 1 - topInset - bottomInset));
}

int BandscopeCanvas::binForFrequency(double mhz, int binCount) const
{
    const double dataStartMhz = lowFrequencyMhz(m_dataStartMhz, m_dataEndMhz);
    const double dataEndMhz = highFrequencyMhz(m_dataStartMhz, m_dataEndMhz);
    if (binCount <= 0 || dataEndMhz <= dataStartMhz)
    {
        return -1;
    }
    if (mhz < dataStartMhz || mhz > dataEndMhz)
    {
        return -1;
    }

    const double normalized = (mhz - dataStartMhz) / (dataEndMhz - dataStartMhz);
    return qBound(0, int(normalized * binCount), binCount - 1);
}

int BandscopeCanvas::binForDisplayX(int x, int binCount) const
{
    return binForFrequency(xToFreq(x), binCount);
}

void BandscopeCanvas::invalidateStaticLayer()
{
    m_staticLayerDirty = true;
    m_displayBins.clear();
}

void BandscopeCanvas::ensureStaticLayer()
{
    const QSize currentSize = size();
    if (!currentSize.isValid())
    {
        return;
    }

    if (!m_staticLayerDirty && !m_staticLayer.isNull() && m_staticLayerSize == currentSize)
    {
        return;
    }

    m_staticLayer = QPixmap(currentSize);
    m_staticLayerSize = currentSize;
    m_staticLayer.fill(Qt::transparent);

    QPainter painter(&m_staticLayer);
    painter.setRenderHint(QPainter::Antialiasing, false);
    renderStaticLayer(&painter);

    m_staticLayerDirty = false;
}

void BandscopeCanvas::renderStaticLayer(QPainter* painter) const
{
    if (!painter)
    {
        return;
    }

    static const QColor kBgScale(0x06, 0x11, 0x16);
    static const QColor kGridText(0xc6, 0xe0, 0xe8);
    static const QColor kScalePanelBorder(0x52, 0x8f, 0x9e, 120);
    static const QColor kScaleAccentLine(0x9a, 0x24, 0x24);

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
        float majorLevelStep = range > 100.0f ? 20.0f : 10.0f;
        float levelMinorDivisions = 4.0f;
        if (m_gridDensity == kGridDensityFewer)
        {
            majorLevelStep *= 2.0f;
            levelMinorDivisions = 2.0f;
        }
        else if (m_gridDensity == kGridDensityMore)
        {
            majorLevelStep /= 2.0f;
            levelMinorDivisions = 5.0f;
        }
        const float minorLevelStep = majorLevelStep / levelMinorDivisions;
        auto drawLevelLines = [&](float step)
        {
            const int firstStep = int(std::ceil(m_minLevel / step));
            const int lastStep = int(std::floor(m_maxLevel / step));
            for (int i = firstStep; i <= lastStep; ++i)
            {
                const float level = float(i) * step;
                const int y = levelToY(level, specTop, specDrawH);
                painter->drawLine(levelScalePanelWidth(), y, w, y);
            }
        };

        painter->setPen(QPen(colorWithAlpha(m_gridLineColor, 46), 1));
        drawLevelLines(minorLevelStep);

        painter->setPen(QPen(colorWithAlpha(m_gridLineColor, 86), 1));
        drawLevelLines(majorLevelStep);

        const double scaleStartMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
        const double scaleEndMhz = highFrequencyMhz(m_startMhz, m_endMhz);
        const int plotW = qMax(1, w - levelScalePanelWidth());
        const double mhzPerPx = (scaleEndMhz > scaleStartMhz) ? (scaleEndMhz - scaleStartMhz) / plotW : 1.0;
        double tickStep = 0.5;
        double minMajorGridPx = 80.0;
        double frequencyMinorDivisions = 5.0;
        if (m_gridDensity == kGridDensityFewer)
        {
            minMajorGridPx = 130.0;
            frequencyMinorDivisions = 2.0;
        }
        else if (m_gridDensity == kGridDensityMore)
        {
            minMajorGridPx = 45.0;
            frequencyMinorDivisions = 10.0;
        }
        while (tickStep / mhzPerPx < minMajorGridPx && tickStep < 100)
        {
            tickStep *= 2;
        }
        const double minorTickStep = tickStep / frequencyMinorDivisions;
        auto drawMhzLines = [&](double step)
        {
            const qint64 firstStep = qint64(std::ceil(scaleStartMhz / step));
            const qint64 lastStep = qint64(std::floor(scaleEndMhz / step));
            for (qint64 i = firstStep; i <= lastStep; ++i)
            {
                const double mhz = double(i) * step;
                const int x = freqToX(mhz);
                painter->drawLine(x, specTop, x, specH);
            }
        };

        painter->setPen(QPen(colorWithAlpha(m_gridLineColor, 46), 1));
        drawMhzLines(minorTickStep);

        painter->setPen(QPen(colorWithAlpha(m_gridLineColor, 86), 1));
        drawMhzLines(tickStep);
    }

    {
        painter->fillRect(0, specTop, levelScalePanelWidth(), qMax(0, specH - specTop), kBgScale);
        painter->fillRect(0, specH - 1, levelScalePanelWidth(), scaleHeight(), kBgScale);
        painter->setPen(kScalePanelBorder);
        painter->drawLine(levelScalePanelWidth() - 1, specTop, levelScalePanelWidth() - 1, qMax(specTop, specH - 1));

        QFont f = painter->font();
        f.setPointSize(8);
        painter->setFont(f);
        painter->setPen(kGridText);
        const QFontMetrics fontMetrics(f);

        const float range = m_maxLevel - m_minLevel;
        const float levelStep = range > 100.0f ? 20.0f : 10.0f;
        for (float level = std::ceil(m_minLevel / levelStep) * levelStep; level <= m_maxLevel; level += levelStep)
        {
            const int y = levelToY(level, specTop, specDrawH);
            const int labelH = fontMetrics.height();
            const int labelY = y <= specTop ? specTop : y - labelH / 2;
            const QRect labelRect(1, labelY, levelScalePanelWidth() - 4, labelH);
            if (labelRect.intersects(QRect(0, specTop, levelScalePanelWidth(), specH + scaleHeight())))
            {
                const int relativeLevel = int(std::lround(level - m_maxLevel));
                painter->drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, QString("%1").arg(relativeLevel));
            }
        }
    }

    {
        const int scaleY = specH - 1;
        painter->fillRect(0, scaleY, w, scaleHeight(), kBgScale);
        painter->fillRect(levelScalePanelWidth(), scaleY, qMax(0, w - levelScalePanelWidth()), 1, kScaleAccentLine);
        painter->setPen(kGridText);

        QFont f = painter->font();
        f.setPointSize(8);
        painter->setFont(f);
        const QFontMetrics fontMetrics(f);

        const double scaleStartMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
        const double scaleEndMhz = highFrequencyMhz(m_startMhz, m_endMhz);
        const int plotW = qMax(1, w - levelScalePanelWidth());
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
        const double first = std::ceil(scaleStartMhz / tickStep) * tickStep;
        for (double mhz = first; mhz <= scaleEndMhz; mhz += tickStep)
        {
            const int x = freqToX(mhz);
            const QString label = QString::number(mhz, 'f', decimals);
            const int labelW = fontMetrics.horizontalAdvance(label);
            const int labelX =
                qBound(levelScalePanelWidth() + 2, x - labelW / 2, qMax(levelScalePanelWidth() + 2, w - labelW - 2));
            painter->setPen(QPen(kGridText, 1));
            painter->drawLine(x, tickTop, x, tickTop + tickH);
            painter->setPen(kGridText);
            painter->drawText(labelX, textY, label);
        }
    }
}

void BandscopeCanvas::ensureDisplayBinMap(int binCount)
{
    const QSize currentSize = size();
    if (binCount <= 0 || !currentSize.isValid())
    {
        m_displayBins.clear();
        return;
    }

    if (!m_displayBins.isEmpty() && m_displayBinMapSize == currentSize && m_displayBinMapBinCount == binCount &&
        m_displayBinMapStartMhz == m_startMhz && m_displayBinMapEndMhz == m_endMhz &&
        m_displayBinMapDataStartMhz == m_dataStartMhz && m_displayBinMapDataEndMhz == m_dataEndMhz)
    {
        return;
    }

    m_displayBins.resize(width());
    for (int x = 0; x < m_displayBins.size(); ++x)
    {
        m_displayBins[x] = binForDisplayX(x, binCount);
    }

    m_displayBinMapSize = currentSize;
    m_displayBinMapBinCount = binCount;
    m_displayBinMapStartMhz = m_startMhz;
    m_displayBinMapEndMhz = m_endMhz;
    m_displayBinMapDataStartMhz = m_dataStartMhz;
    m_displayBinMapDataEndMhz = m_dataEndMhz;
}

void BandscopeCanvas::setFrequencyRange(double startMhz, double endMhz)
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
    invalidateStaticLayer();
    scheduleRepaint();
}

void BandscopeCanvas::setDataFrequencyRange(double startMhz, double endMhz)
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
    m_displayBins.clear();
}

void BandscopeCanvas::setVfoFrequency(double freqMhz)
{
    m_vfoMhz = freqMhz;
    scheduleRepaint();
}

void BandscopeCanvas::setVfoMarkerColor(const QColor& color)
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

void BandscopeCanvas::setBackgroundColor(const QColor& color)
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

void BandscopeCanvas::setGridLineColor(const QColor& color)
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

void BandscopeCanvas::setGridDensity(int density)
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

void BandscopeCanvas::setFilterWidth(int lowHz, int highHz)
{
    m_filterLowHz = lowHz;
    m_filterHighHz = highHz;
    scheduleRepaint();
}

void BandscopeCanvas::setInteractionLocked(bool locked)
{
    if (m_interactionLocked == locked)
    {
        return;
    }

    m_interactionLocked = locked;
    m_clickPressed = false;
    scheduleRepaint();
}

void BandscopeCanvas::setInvertMouseWheel(bool invert)
{
    m_invertMouseWheel = invert;
}

void BandscopeCanvas::updateSpectrum(const QVector<float>& levels, bool outOfRange)
{
    m_spectrumBins = levels;
    m_scopeOutOfRange = outOfRange;

    if (m_peakHold.size() != levels.size())
    {
        m_peakHold = levels;
    }
    else
    {
        for (int i = 0; i < levels.size(); ++i)
        {
            if (levels[i] > m_peakHold[i])
            {
                m_peakHold[i] = levels[i];
            }
        }
    }

    scheduleRepaint();
}

void BandscopeCanvas::clearDisplay()
{
    m_spectrumBins.clear();
    m_peakHold.clear();
    m_scopeOutOfRange = false;
    scheduleRepaint();
}

void BandscopeCanvas::scheduleRepaint()
{
    if (!m_repaintTimer.isActive())
    {
        m_repaintTimer.start();
    }
}

void BandscopeCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    static const QColor kTrace(0xf2, 0xf7, 0xfa);
    static const QColor kPeak(0xae, 0xe8, 0xff, 95);

    ensureStaticLayer();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int specH = plotHeight();
    const int w = width();
    const int specTop = 0;
    const int specDrawH = specH;
    const QRect spectrumPlotRect(levelScalePanelWidth(), specTop, qMax(0, w - levelScalePanelWidth()),
                                 qMax(0, specDrawH - 1));
    p.drawPixmap(0, 0, m_staticLayer);

    if (m_spectrumBins.isEmpty())
    {
        p.setPen(QColor(0x4a, 0x60, 0x78));
        QFont f = p.font();
        f.setPointSize(9);
        p.setFont(f);
        p.drawText(QRect(0, specTop, w, specDrawH), Qt::AlignCenter, "No bandscope data - waiting for radio stream");
    }

    if (!m_spectrumBins.isEmpty())
    {
        ensureDisplayBinMap(m_spectrumBins.size());
        p.setRenderHint(QPainter::Antialiasing, true);
        p.save();
        p.setClipRect(spectrumPlotRect);
        QPainterPath specPath, peakPath;
        bool specFirst = true, peakFirst = true;

        for (int x = levelScalePanelWidth(); x < w; ++x)
        {
            const int bin = (x >= 0 && x < m_displayBins.size()) ? m_displayBins[x] : -1;
            const float level = bin >= 0 ? m_spectrumBins[bin] : m_minLevel;

            const int sy = levelToY(level, specTop, specDrawH);
            if (specFirst)
            {
                specPath.moveTo(x, sy);
                specFirst = false;
            }
            else
            {
                specPath.lineTo(x, sy);
            }

            if (!m_peakHold.isEmpty() && bin >= 0 && bin < m_peakHold.size())
            {
                const int py = levelToY(m_peakHold[bin], specTop, specDrawH);
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

        QPainterPath fillPath(specPath);
        fillPath.lineTo(w, specH);
        fillPath.lineTo(levelScalePanelWidth(), specH);
        fillPath.closeSubpath();

        QLinearGradient fillGrad(0, specTop, 0, specH);
        fillGrad.setColorAt(0.00, QColor(0xff, 0xf8, 0x00, 230));
        fillGrad.setColorAt(0.18, QColor(0x72, 0xff, 0x00, 220));
        fillGrad.setColorAt(0.42, QColor(0x00, 0xf0, 0xff, 205));
        fillGrad.setColorAt(0.72, QColor(0x43, 0xb8, 0xff, 205));
        fillGrad.setColorAt(1.00, QColor(0x35, 0x8f, 0xff, 190));
        p.setPen(Qt::NoPen);
        p.fillPath(fillPath, fillGrad);

        p.setPen(QPen(kPeak, 1));
        p.drawPath(peakPath);

        p.setPen(QPen(kTrace, 1));
        p.drawPath(specPath);

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
}

void BandscopeCanvas::mousePressEvent(QMouseEvent* ev)
{
    if (m_interactionLocked)
    {
        return;
    }
    if (ev->button() == Qt::LeftButton)
    {
        m_clickPressed = true;
        m_clickPressPos = ev->pos();
        ev->accept();
    }
}

void BandscopeCanvas::mouseReleaseEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton)
    {
        const bool isClick = m_clickPressed && rect().contains(ev->pos()) &&
                             (ev->pos() - m_clickPressPos).manhattanLength() <= kClickMoveTolerancePx;
        m_clickPressed = false;
        if (!m_interactionLocked && isClick)
        {
            Q_EMIT frequencyClicked(xToFreq(ev->pos().x()));
        }
        ev->accept();
    }
}

void BandscopeCanvas::wheelEvent(QWheelEvent* ev)
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
    qDebug(logBandscope()) << "Bandscope wheel"
                           << "angle=" << angle << "pixel=" << ev->pixelDelta() << "qtInverted=" << ev->inverted()
                           << "physicalSteps=" << physicalSteps << "reversePref=" << m_invertMouseWheel
                           << "acceptedSteps=" << acceptedSteps << "accumulator=" << m_wheelStepAccumulator;

    Q_EMIT wheelStepRequested(acceptedSteps);
    ev->accept();
}
