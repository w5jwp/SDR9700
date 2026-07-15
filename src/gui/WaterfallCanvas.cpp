#include "WaterfallCanvas.h"
#include "LogCategories.h"

#include <QElapsedTimer>
#include <QPainter>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>

namespace
{
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

WaterfallCanvas::WaterfallCanvas(QWidget* parent) : QWidget(parent)
{
    setAutoFillBackground(false);
}

double WaterfallCanvas::xToFreq(int x) const
{
    const double startMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
    const double endMhz = highFrequencyMhz(m_startMhz, m_endMhz);
    if (width() <= 0 || endMhz <= startMhz)
    {
        return startMhz;
    }
    return startMhz + (double(x) / width()) * (endMhz - startMhz);
}

int WaterfallCanvas::binForFrequency(double mhz, int binCount) const
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

int WaterfallCanvas::binForDisplayX(int x, int binCount) const
{
    return binForFrequency(xToFreq(x), binCount);
}

QRgb WaterfallCanvas::levelToColor(float level) const
{
    static const struct
    {
        float pos;
        int r, g, b;
    } stops[] = {
        {0.00f, 0, 20, 120},  {0.18f, 0, 58, 205},  {0.34f, 0, 150, 255}, {0.50f, 0, 220, 105},
        {0.66f, 165, 245, 0}, {0.78f, 255, 230, 0}, {0.90f, 255, 92, 0},  {1.00f, 255, 255, 210},
    };
    static constexpr int N = static_cast<int>(std::size(stops));

    float t = (level - m_minLevel) / (m_maxLevel - m_minLevel);
    t = std::max(0.0f, std::min(1.0f, t));

    for (int i = 1; i < N; ++i)
    {
        if (t <= stops[i].pos)
        {
            float f = (t - stops[i - 1].pos) / (stops[i].pos - stops[i - 1].pos);
            int r = int(stops[i - 1].r + f * (stops[i].r - stops[i - 1].r));
            int g = int(stops[i - 1].g + f * (stops[i].g - stops[i - 1].g));
            int b = int(stops[i - 1].b + f * (stops[i].b - stops[i - 1].b));
            return qRgb(r, g, b);
        }
    }
    Q_UNREACHABLE();
}

void WaterfallCanvas::setFrequencyRange(double startMhz, double endMhz)
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
    rebuildImage();
    update();
}

void WaterfallCanvas::setDataFrequencyRange(double startMhz, double endMhz)
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

void WaterfallCanvas::updateSpectrum(const QVector<float>& levels)
{
    if (m_waterfall.isNull() || m_waterfall.height() == 0)
    {
        return;
    }

    const int w = m_waterfall.width();
    const int h = m_waterfall.height();
    Q_ASSERT(m_waterfall.format() == QImage::Format_RGB32);
    if (h > 1)
    {
        memmove(m_waterfall.bits() + w * 4, m_waterfall.bits(), size_t(w * (h - 1) * 4));
    }

    QRgb* row = reinterpret_cast<QRgb*>(m_waterfall.bits());
    if (levels.isEmpty())
    {
        for (int x = 0; x < w; ++x)
        {
            row[x] = qRgb(0, 0, 0);
        }
        update();
        return;
    }

    if (logWaterfall().isDebugEnabled())
    {
        static QElapsedTimer waterfallLogTimer;
        if (!waterfallLogTimer.isValid() || waterfallLogTimer.elapsed() >= 1000)
        {
            float minLevel = std::numeric_limits<float>::max();
            float maxLevel = std::numeric_limits<float>::lowest();
            double totalLevel = 0.0;
            int zeroCount = 0;
            for (const float level : levels)
            {
                minLevel = std::min(minLevel, level);
                maxLevel = std::max(maxLevel, level);
                totalLevel += level;
                if (level <= m_minLevel)
                {
                    ++zeroCount;
                }
            }
            qDebug(logWaterfall()).nospace()
                << "Waterfall row: image=" << w << "x" << h << " bins=" << levels.size() << " levels[min=" << minLevel
                << " max=" << maxLevel << " avg=" << (totalLevel / double(levels.size())) << " floor=" << zeroCount
                << "/" << levels.size() << "]";
            waterfallLogTimer.restart();
        }
    }

    for (int x = 0; x < w; ++x)
    {
        const int bin = binForDisplayX(x, levels.size());
        row[x] = bin >= 0 ? levelToColor(levels[bin]) : qRgb(0x02, 0x0c, 0x14);
    }
    update();
}

void WaterfallCanvas::clearDisplay()
{
    if (!m_waterfall.isNull())
    {
        m_waterfall.fill(Qt::black);
    }
    update();
}

void WaterfallCanvas::rebuildImage()
{
    if (height() <= 0 || width() <= 0)
    {
        return;
    }
    m_waterfall = QImage(width(), height(), QImage::Format_RGB32);
    m_waterfall.fill(Qt::black);
}

void WaterfallCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    static const QColor kWaterfallBg(0x00, 0x24, 0xd8);

    QPainter p(this);
    p.fillRect(rect(), kWaterfallBg);
    if (!m_waterfall.isNull())
    {
        p.drawImage(rect(), m_waterfall, QRect(0, 0, m_waterfall.width(), qMin(height(), m_waterfall.height())));
    }
}

void WaterfallCanvas::resizeEvent(QResizeEvent* event)
{
    Q_UNUSED(event)
    rebuildImage();
}
