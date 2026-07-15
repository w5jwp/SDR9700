#include "BandscopeCanvas.h"
#include "LogCategories.h"

#include <QCursor>
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
constexpr int kLevelScalePanelWidth = 30;
constexpr int kSpectrumVerticalPadding = 10;
constexpr int kBandscopeDragThresholdPx = 6;
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
    if (width() <= 0 || endMhz <= startMhz)
    {
        return startMhz;
    }
    return startMhz + (double(x) / width()) * (endMhz - startMhz);
}

int BandscopeCanvas::freqToX(double mhz) const
{
    const double startMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
    const double endMhz = highFrequencyMhz(m_startMhz, m_endMhz);
    if (endMhz <= startMhz)
    {
        return 0;
    }
    return int((mhz - startMhz) / (endMhz - startMhz) * width());
}

int BandscopeCanvas::levelToY(float level, int topY, int h) const
{
    float norm = (level - m_minLevel) / (m_maxLevel - m_minLevel);
    norm = std::max(0.0f, std::min(1.0f, norm));
    const int pad = qMin(kSpectrumVerticalPadding, qMax(0, h / 4));
    const int plotH = qMax(1, h - pad * 2);
    return topY + pad + int((1.0f - norm) * plotH);
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
}

void BandscopeCanvas::setVfoFrequency(double freqMhz)
{
    m_vfoMhz = freqMhz;
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
    const bool wasInteracting = m_draggingBandscope || m_bandscopeButtonPressed;
    m_draggingBandscope = false;
    m_bandscopeButtonPressed = false;
    if (wasInteracting)
    {
        Q_EMIT pointerInteractionFinished();
    }
    updateBandscopeCursor(mapFromGlobal(QCursor::pos()));
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

void BandscopeCanvas::updateBandscopeCursor(const QPoint&)
{
    if (m_interactionLocked)
    {
        setCursor(Qt::ArrowCursor);
        return;
    }

    setCursor((m_bandscopeButtonPressed || m_draggingBandscope) ? Qt::ClosedHandCursor : Qt::ArrowCursor);
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

    static const QColor kBgSpecTop(0x0b, 0x3f, 0x55);
    static const QColor kBgSpecMid(0x12, 0x63, 0x85);
    static const QColor kBgSpecBottom(0x06, 0x2b, 0x3c);
    static const QColor kBgScale(0x06, 0x11, 0x16);
    static const QColor kGridMinor(0x9c, 0xd9, 0xe5, 46);
    static const QColor kGridMajor(0xc8, 0xf1, 0xf5, 86);
    static const QColor kGridText(0xc6, 0xe0, 0xe8);
    static const QColor kTrace(0xf2, 0xf7, 0xfa);
    static const QColor kPeak(0xae, 0xe8, 0xff, 95);
    static const QColor kScalePanel(0x00, 0x00, 0x00, 218);
    static const QColor kScalePanelBorder(0x52, 0x8f, 0x9e, 120);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int specH = plotHeight();
    const int w = width();

    QLinearGradient specBg(0, 0, 0, specH);
    specBg.setColorAt(0.00, kBgSpecTop);
    specBg.setColorAt(0.52, kBgSpecMid);
    specBg.setColorAt(1.00, kBgSpecBottom);
    p.fillRect(0, 0, w, specH, specBg);

    const int specTop = 0;
    const int specDrawH = specH;
    const QRect spectrumPlotRect(kLevelScalePanelWidth, specTop, qMax(0, w - kLevelScalePanelWidth), specDrawH);

    {
        QFont f = p.font();
        f.setPointSize(8);
        p.setFont(f);

        const float range = m_maxLevel - m_minLevel;
        const float majorLevelStep = range > 100.0f ? 20.0f : 10.0f;
        const float minorLevelStep = majorLevelStep / 4.0f;
        auto drawLevelLines = [&](float step)
        {
            const int firstStep = int(std::ceil(m_minLevel / step));
            const int lastStep = int(std::floor(m_maxLevel / step));
            for (int i = firstStep; i <= lastStep; ++i)
            {
                const float level = float(i) * step;
                const int y = levelToY(level, specTop, specDrawH);
                p.drawLine(kLevelScalePanelWidth, y, w, y);
            }
        };

        p.setPen(QPen(kGridMinor, 1));
        drawLevelLines(minorLevelStep);

        p.setPen(QPen(kGridMajor, 1));
        drawLevelLines(majorLevelStep);

        const double scaleStartMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
        const double scaleEndMhz = highFrequencyMhz(m_startMhz, m_endMhz);
        double mhzPerPx = (scaleEndMhz > scaleStartMhz && w > 0) ? (scaleEndMhz - scaleStartMhz) / w : 1.0;
        double tickStep = 0.5;
        while (tickStep / mhzPerPx < 80 && tickStep < 100)
        {
            tickStep *= 2;
        }
        const double minorTickStep = tickStep / 5.0;
        auto drawMhzLines = [&](double step)
        {
            const qint64 firstStep = qint64(std::ceil(scaleStartMhz / step));
            const qint64 lastStep = qint64(std::floor(scaleEndMhz / step));
            for (qint64 i = firstStep; i <= lastStep; ++i)
            {
                const double mhz = double(i) * step;
                const int x = freqToX(mhz);
                p.drawLine(x, specTop, x, specH);
            }
        };

        p.setPen(QPen(kGridMinor, 1));
        drawMhzLines(minorTickStep);

        p.setPen(QPen(kGridMajor, 1));
        drawMhzLines(tickStep);
    }

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
        p.setRenderHint(QPainter::Antialiasing, true);
        p.save();
        p.setClipRect(spectrumPlotRect);
        QPainterPath specPath, peakPath;
        bool specFirst = true, peakFirst = true;
        const int n = m_spectrumBins.size();

        for (int x = 0; x < w; ++x)
        {
            const int bin = binForDisplayX(x, n);
            const float level = bin >= 0 ? m_spectrumBins[bin] : m_minLevel;

            int sy = levelToY(level, specTop, specDrawH);
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
                int py = levelToY(m_peakHold[bin], specTop, specDrawH);
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
        fillPath.lineTo(0, specH);
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
        double loMhz = m_vfoMhz + m_filterLowHz / 1e6;
        double hiMhz = m_vfoMhz + m_filterHighHz / 1e6;
        int fx1 = freqToX(loMhz);
        int fx2 = freqToX(hiMhz);
        QColor filterFill(0x00, 0xb4, 0xd8, 22);
        p.fillRect(fx1, 0, fx2 - fx1, specH, filterFill);
    }

    {
        p.fillRect(0, specTop, kLevelScalePanelWidth, specH, kScalePanel);
        p.setPen(kScalePanelBorder);
        p.drawLine(kLevelScalePanelWidth - 1, specTop, kLevelScalePanelWidth - 1, specH);

        QFont f = p.font();
        f.setPointSize(8);
        p.setFont(f);
        p.setPen(kGridText);

        const float range = m_maxLevel - m_minLevel;
        float levelStep = range > 100.0f ? 20.0f : 10.0f;
        for (float level = std::ceil(m_minLevel / levelStep) * levelStep; level <= m_maxLevel; level += levelStep)
        {
            int y = levelToY(level, specTop, specDrawH);
            const int labelH = QFontMetrics(f).height();
            const QRect labelRect(1, y - labelH / 2, kLevelScalePanelWidth - 4, labelH);
            if (labelRect.top() >= specTop && labelRect.bottom() <= specH)
            {
                const int relativeLevel = int(std::lround(level - m_maxLevel));
                p.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, QString("%1").arg(relativeLevel));
            }
        }
    }

    {
        const int scaleY = specH - 1;
        p.fillRect(0, scaleY, w, scaleHeight(), kBgScale);
        p.fillRect(0, scaleY, w, 1, QColor(0x9a, 0x24, 0x24));
        p.setPen(kGridText);

        QFont f = p.font();
        f.setPointSize(8);
        p.setFont(f);

        const double scaleStartMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
        const double scaleEndMhz = highFrequencyMhz(m_startMhz, m_endMhz);
        const double mhzPerPx = (scaleEndMhz > scaleStartMhz && w > 0) ? (scaleEndMhz - scaleStartMhz) / w : 1.0;
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

        int decimals = (tickStep >= 1.0) ? 1 : (tickStep >= 0.1) ? 2 : 3;

        const int tickTop = scaleY + 2;
        const int tickH = 6;
        const int textY = scaleY + 21;
        double first = std::ceil(scaleStartMhz / tickStep) * tickStep;
        for (double mhz = first; mhz <= scaleEndMhz; mhz += tickStep)
        {
            int x = freqToX(mhz);
            QString label = QString::number(mhz, 'f', decimals);
            const int labelW = QFontMetrics(f).horizontalAdvance(label);
            const int labelX = qBound(2, x - labelW / 2, qMax(2, w - labelW - 2));
            p.setPen(QPen(kGridText, 1));
            p.drawLine(x, tickTop, x, tickTop + tickH);
            p.setPen(kGridText);
            p.drawText(labelX, textY, label);
        }
    }

    if (m_vfoMhz >= visibleStartMhz && m_vfoMhz <= visibleEndMhz)
    {
        const int vx = freqToX(m_vfoMhz);
        const int scaleY = specH - 1;
        p.setPen(QPen(QColor(0xf5, 0xf7, 0xf8, 230), 1, Qt::SolidLine));
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
        m_bandscopeDragStartPos = ev->pos();
        m_lastBandscopeDragPos = ev->pos();
        m_bandscopeDragAnchorFreqMhz = xToFreq(ev->pos().x());
        m_bandscopeButtonPressed = true;
        m_draggingBandscope = false;
        Q_EMIT pointerInteractionStarted();
        setCursor(Qt::ClosedHandCursor);
    }
}

void BandscopeCanvas::mouseMoveEvent(QMouseEvent* ev)
{
    updateBandscopeCursor(ev->pos());

    if (m_interactionLocked)
    {
        return;
    }

    if (ev->buttons() & Qt::LeftButton)
    {
        if (!m_draggingBandscope)
        {
            if ((ev->pos() - m_bandscopeDragStartPos).manhattanLength() < kBandscopeDragThresholdPx)
            {
                return;
            }
            m_draggingBandscope = true;
            m_lastBandscopeDragPos = m_bandscopeDragStartPos;
            setCursor(Qt::ClosedHandCursor);
            Q_EMIT tuneDragStarted();
        }

        if (width() <= 0)
        {
            return;
        }

        const double deltaMhz = xToFreq(ev->pos().x()) - m_bandscopeDragAnchorFreqMhz;
        Q_EMIT tuneDragRequested(deltaMhz);
        m_lastBandscopeDragPos = ev->pos();
    }
}

void BandscopeCanvas::mouseReleaseEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton && m_draggingBandscope)
    {
        m_draggingBandscope = false;
        m_bandscopeButtonPressed = false;
        Q_EMIT pointerInteractionFinished();
        updateBandscopeCursor(ev->pos());
    }
    else if (ev->button() == Qt::LeftButton)
    {
        if (!m_interactionLocked)
        {
            Q_EMIT frequencyClicked(xToFreq(ev->pos().x()));
        }
        m_bandscopeButtonPressed = false;
        Q_EMIT pointerInteractionFinished();
        updateBandscopeCursor(ev->pos());
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

    Q_EMIT tuneStepRequested(acceptedSteps);
    ev->accept();
}

void BandscopeCanvas::leaveEvent(QEvent*)
{
    if (!m_draggingBandscope && !m_bandscopeButtonPressed)
    {
        unsetCursor();
    }
}
