#include "SpectrumCanvas.h"
#include "LogCategories.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QFontMetrics>
#include <QCursor>
#include <algorithm>
#include <cmath>
#include <iterator>

static constexpr float kPeakDecayDbPerSec = 20.0f;
static constexpr float kPeakDecayPerTickDb = kPeakDecayDbPerSec * 0.05f;
static constexpr int kDbScalePanelWidth = 38;
static constexpr int kMinSpectrumHeight = 150;
static constexpr int kMinWaterfallHeight = 180;
static constexpr int kSpectrumVerticalPadding = 10;
static constexpr int kBandscopeDragThresholdPx = 6;
static constexpr double kWheelStepAngleDelta = 120.0;
static constexpr double kMinFrequencyRangeMhz = 0.001;

namespace
{
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

SpectrumCanvas::SpectrumCanvas(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(640, 320);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);

    m_zoomOutButton = new QPushButton(QStringLiteral("Zoom\nOut"), this);
    m_zoomInButton = new QPushButton(QStringLiteral("Zoom\nIn"), this);
    for (auto* button : {m_zoomOutButton, m_zoomInButton})
    {
        button->setFixedSize(56, 34);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setStyleSheet("QPushButton { background: rgba(16, 22, 30, 210); border: 1px solid #566576; "
                              "border-radius: 3px; color: #e8f2f8; font-size: 10px; font-weight: bold; "
                              "line-height: 11px; padding: 1px 3px; }"
                              "QPushButton:hover { background: rgba(32, 42, 55, 230); border-color: #7f96ad; }");
    }
    m_zoomOutButton->setAccessibleName("Zoom out");
    m_zoomInButton->setAccessibleName("Zoom in");
    connect(m_zoomOutButton, &QPushButton::clicked, this, &SpectrumCanvas::zoomOutRequested);
    connect(m_zoomInButton, &QPushButton::clicked, this, &SpectrumCanvas::zoomInRequested);
    repositionZoomButtons();

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
                        m_peakHold[i] -= kPeakDecayPerTickDb;
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
    connect(&m_repaintTimer, &QTimer::timeout, this, qOverload<>(&SpectrumCanvas::update));
    m_lastFrameTimer.start();
}

int SpectrumCanvas::spectrumHeight() const
{
    if (m_spectrumHeight < 0)
    {
        return defaultSpectrumHeight();
    }
    return constrainedSpectrumHeight(m_spectrumHeight);
}

int SpectrumCanvas::waterfallTop() const
{
    return spectrumHeight() + scaleHeight() + splitterHeight();
}

QRect SpectrumCanvas::splitterRect() const
{
    return QRect(0, spectrumHeight() + scaleHeight(), width(), splitterHeight());
}

int SpectrumCanvas::defaultSpectrumHeight() const
{
    return constrainedSpectrumHeight((height() - scaleHeight() - splitterHeight()) / 2);
}

int SpectrumCanvas::constrainedSpectrumHeight(int requested) const
{
    const int available = qMax(0, height() - scaleHeight() - splitterHeight());
    const int maxSpectrumHeight = qMax(kMinSpectrumHeight, available - kMinWaterfallHeight);
    return qBound(qMin(kMinSpectrumHeight, maxSpectrumHeight), requested, maxSpectrumHeight);
}

bool SpectrumCanvas::applySpectrumPaneHeight(int requested)
{
    const int constrained = constrainedSpectrumHeight(requested);
    if (m_spectrumHeight == constrained)
    {
        return false;
    }

    m_spectrumHeight = constrained;
    rebuildWaterfallImage();
    scheduleRepaint();
    return true;
}

void SpectrumCanvas::updateBandscopeCursor(const QPoint&)
{
    if (m_interactionLocked)
    {
        setCursor(Qt::ArrowCursor);
        return;
    }

    setCursor((m_bandscopeButtonPressed || m_draggingBandscope) ? Qt::ClosedHandCursor : Qt::ArrowCursor);
}

double SpectrumCanvas::xToFreq(int x) const
{
    const double startMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
    const double endMhz = highFrequencyMhz(m_startMhz, m_endMhz);
    if (width() <= 0 || endMhz <= startMhz)
    {
        return startMhz;
    }
    return startMhz + (double(x) / width()) * (endMhz - startMhz);
}

int SpectrumCanvas::freqToX(double mhz) const
{
    const double startMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
    const double endMhz = highFrequencyMhz(m_startMhz, m_endMhz);
    if (endMhz <= startMhz)
    {
        return 0;
    }
    return int((mhz - startMhz) / (endMhz - startMhz) * width());
}

int SpectrumCanvas::binForFrequency(double mhz, int binCount) const
{
    const double dataStartMhz = lowFrequencyMhz(m_dataStartMhz, m_dataEndMhz);
    const double dataEndMhz = highFrequencyMhz(m_dataStartMhz, m_dataEndMhz);
    if (binCount <= 0 || dataEndMhz <= dataStartMhz)
    {
        return 0;
    }

    const double normalized = (mhz - dataStartMhz) / (dataEndMhz - dataStartMhz);
    return qBound(0, int(normalized * binCount), binCount - 1);
}

int SpectrumCanvas::binForDisplayX(int x, int binCount) const
{
    return binForFrequency(xToFreq(x), binCount);
}

int SpectrumCanvas::dbmToY(float dbm, int topY, int h) const
{
    float norm = (dbm - m_minDbm) / (m_maxDbm - m_minDbm);
    norm = std::max(0.0f, std::min(1.0f, norm));
    const int pad = qMin(kSpectrumVerticalPadding, qMax(0, h / 4));
    const int plotH = qMax(1, h - pad * 2);
    return topY + pad + int((1.0f - norm) * plotH);
}

QRgb SpectrumCanvas::dbmToColor(float dbm) const
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

    float t = (dbm - m_minDbm) / (m_maxDbm - m_minDbm);
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

void SpectrumCanvas::setFrequencyRange(double startMhz, double endMhz)
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
    rebuildWaterfallImage();
    scheduleRepaint();
}

void SpectrumCanvas::setDataFrequencyRange(double startMhz, double endMhz)
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

void SpectrumCanvas::setVfoFrequency(double freqMhz)
{
    m_vfoMhz = freqMhz;
    scheduleRepaint();
}

void SpectrumCanvas::setFilterWidth(int lowHz, int highHz)
{
    m_filterLowHz = lowHz;
    m_filterHighHz = highHz;
    scheduleRepaint();
}

void SpectrumCanvas::setInteractionLocked(bool locked)
{
    if (m_interactionLocked == locked)
    {
        return;
    }

    m_interactionLocked = locked;
    m_draggingBandscope = false;
    m_bandscopeButtonPressed = false;
    if (m_zoomInButton)
    {
        m_zoomInButton->setEnabled(!locked);
    }
    if (m_zoomOutButton)
    {
        m_zoomOutButton->setEnabled(!locked);
    }
    updateBandscopeCursor(mapFromGlobal(QCursor::pos()));
    scheduleRepaint();
}

void SpectrumCanvas::setInvertMouseWheel(bool invert)
{
    m_invertMouseWheel = invert;
}

int SpectrumCanvas::spectrumPaneHeight() const
{
    return spectrumHeight();
}

void SpectrumCanvas::setSpectrumPaneHeight(int height)
{
    if (height <= 0 || m_spectrumHeight == height)
    {
        return;
    }

    m_spectrumHeight = height;
    rebuildWaterfallImage();
    scheduleRepaint();
}

void SpectrumCanvas::updateSpectrum(const QVector<float>& binsDbm)
{
    m_spectrumBins = binsDbm;

    if (m_peakHold.size() != binsDbm.size())
    {
        m_peakHold = binsDbm;
    }
    else
    {
        for (int i = 0; i < binsDbm.size(); ++i)
        {
            if (binsDbm[i] > m_peakHold[i])
            {
                m_peakHold[i] = binsDbm[i];
            }
        }
    }

    appendWaterfallRow(binsDbm);
    scheduleRepaint();
}

void SpectrumCanvas::clearDisplay()
{
    m_spectrumBins.clear();
    m_peakHold.clear();
    m_waterfall.fill(Qt::black);
    scheduleRepaint();
}

void SpectrumCanvas::scheduleRepaint()
{
    if (!m_repaintTimer.isActive())
    {
        m_repaintTimer.start();
    }
}

void SpectrumCanvas::rebuildWaterfallImage()
{
    const int wfH = height() - waterfallTop();
    if (wfH <= 0 || width() <= 0)
    {
        return;
    }
    m_waterfall = QImage(width(), wfH, QImage::Format_RGB32);
    m_waterfall.fill(Qt::black);
}

void SpectrumCanvas::appendWaterfallRow(const QVector<float>& binsDbm)
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
    if (binsDbm.isEmpty())
    {
        for (int x = 0; x < w; ++x)
        {
            row[x] = qRgb(0, 0, 0);
        }
        return;
    }
    for (int x = 0; x < w; ++x)
    {
        const int bin = binForDisplayX(x, binsDbm.size());
        row[x] = dbmToColor(binsDbm[bin]);
    }
}

void SpectrumCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    static const QColor kBgSpecTop(0x0b, 0x3f, 0x55);
    static const QColor kBgSpecMid(0x12, 0x63, 0x85);
    static const QColor kBgSpecBottom(0x06, 0x2b, 0x3c);
    static const QColor kBgScale(0x06, 0x11, 0x16);
    static const QColor kWaterfallBg(0x00, 0x24, 0xd8);
    static const QColor kGridMinor(0x9c, 0xd9, 0xe5, 46);
    static const QColor kGridMajor(0xc8, 0xf1, 0xf5, 86);
    static const QColor kGridText(0xc6, 0xe0, 0xe8);
    static const QColor kTrace(0xf2, 0xf7, 0xfa);
    static const QColor kPeak(0xae, 0xe8, 0xff, 95);
    static const QColor kScalePanel(0x00, 0x00, 0x00, 218);
    static const QColor kScalePanelBorder(0x52, 0x8f, 0x9e, 120);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int specH = spectrumHeight();
    const int wfTop = waterfallTop();
    const int wfH = height() - wfTop;
    const int w = width();

    QLinearGradient specBg(0, 0, 0, specH);
    specBg.setColorAt(0.00, kBgSpecTop);
    specBg.setColorAt(0.52, kBgSpecMid);
    specBg.setColorAt(1.00, kBgSpecBottom);
    p.fillRect(0, 0, w, specH, specBg);
    p.fillRect(0, wfTop, w, wfH, kWaterfallBg);

    const int specTop = 0;
    const int specDrawH = specH;
    const QRect spectrumPlotRect(kDbScalePanelWidth, specTop, qMax(0, w - kDbScalePanelWidth), specDrawH);

    {
        QFont f = p.font();
        f.setPointSize(8);
        p.setFont(f);

        const float range = m_maxDbm - m_minDbm;
        const float majorDbStep = range > 100.0f ? 20.0f : 10.0f;
        const float minorDbStep = majorDbStep / 4.0f;
        auto drawDbLines = [&](float step)
        {
            const int firstStep = int(std::ceil(m_minDbm / step));
            const int lastStep = int(std::floor(m_maxDbm / step));
            for (int i = firstStep; i <= lastStep; ++i)
            {
                const float db = float(i) * step;
                const int y = dbmToY(db, specTop, specDrawH);
                p.drawLine(kDbScalePanelWidth, y, w, y);
            }
        };

        p.setPen(QPen(kGridMinor, 1));
        drawDbLines(minorDbStep);

        p.setPen(QPen(kGridMajor, 1));
        drawDbLines(majorDbStep);

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
        p.drawText(QRect(0, specTop, w, specDrawH), Qt::AlignCenter, "No bandscope data — waiting for radio stream");
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

            int sy = dbmToY(m_spectrumBins[bin], specTop, specDrawH);
            if (specFirst)
            {
                specPath.moveTo(x, sy);
                specFirst = false;
            }
            else
            {
                specPath.lineTo(x, sy);
            }

            if (!m_peakHold.isEmpty() && bin < m_peakHold.size())
            {
                int py = dbmToY(m_peakHold[bin], specTop, specDrawH);
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

    if (!m_waterfall.isNull() && wfH > 0)
    {
        p.drawImage(QRect(0, wfTop, w, wfH), m_waterfall,
                    QRect(0, 0, m_waterfall.width(), std::min(wfH, m_waterfall.height())));
    }

    {
        const QRect split = splitterRect();
        p.fillRect(split, kBgScale);
        p.fillRect(split.left(), split.top(), split.width(), 1, QColor(0x9a, 0x24, 0x24));
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
        p.fillRect(0, specTop, kDbScalePanelWidth, specH, kScalePanel);
        p.setPen(kScalePanelBorder);
        p.drawLine(kDbScalePanelWidth - 1, specTop, kDbScalePanelWidth - 1, specH);

        QFont f = p.font();
        f.setPointSize(8);
        p.setFont(f);
        p.setPen(kGridText);

        const float range = m_maxDbm - m_minDbm;
        float dbStep = range > 100.0f ? 20.0f : 10.0f;
        for (float db = std::ceil(m_minDbm / dbStep) * dbStep; db <= m_maxDbm; db += dbStep)
        {
            int y = dbmToY(db, specTop, specDrawH);
            const int labelH = QFontMetrics(f).height();
            const QRect labelRect(3, y - labelH / 2, kDbScalePanelWidth - 7, labelH);
            if (labelRect.top() >= specTop && labelRect.bottom() <= specH)
            {
                p.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, QString("%1").arg(int(db)));
            }
        }
    }

    {
        int scaleY = spectrumHeight() - 1;
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
        const int scaleY = spectrumHeight() - 1;
        p.setPen(QPen(QColor(0xf5, 0xf7, 0xf8, 230), 1, Qt::SolidLine));
        p.drawLine(vx, 0, vx, scaleY - 1);
        p.drawLine(vx, waterfallTop(), vx, height());
    }
}

void SpectrumCanvas::resizeEvent(QResizeEvent*)
{
    rebuildWaterfallImage();
    repositionZoomButtons();
}

void SpectrumCanvas::repositionZoomButtons()
{
    if (!m_zoomInButton || !m_zoomOutButton)
    {
        return;
    }

    static constexpr int kMargin = 8;
    static constexpr int kGap = 4;
    const int y = kMargin;
    const int zoomInX = qMax(kMargin, width() - kMargin - m_zoomInButton->width());
    const int zoomOutX = qMax(kMargin, zoomInX - kGap - m_zoomOutButton->width());
    m_zoomOutButton->move(zoomOutX, y);
    m_zoomInButton->move(zoomInX, y);
}

void SpectrumCanvas::mousePressEvent(QMouseEvent* ev)
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
        setCursor(Qt::ClosedHandCursor);
    }
}

void SpectrumCanvas::mouseMoveEvent(QMouseEvent* ev)
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

void SpectrumCanvas::mouseReleaseEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton && m_draggingBandscope)
    {
        m_draggingBandscope = false;
        m_bandscopeButtonPressed = false;
        updateBandscopeCursor(ev->pos());
    }
    else if (ev->button() == Qt::LeftButton)
    {
        if (!m_interactionLocked)
        {
            Q_EMIT frequencyClicked(xToFreq(ev->pos().x()));
        }
        m_bandscopeButtonPressed = false;
        updateBandscopeCursor(ev->pos());
    }
}

void SpectrumCanvas::wheelEvent(QWheelEvent* ev)
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
    qDebug(logGui()) << "Bandscope wheel"
                     << "angle=" << angle << "pixel=" << ev->pixelDelta() << "qtInverted=" << ev->inverted()
                     << "physicalSteps=" << physicalSteps << "reversePref=" << m_invertMouseWheel
                     << "acceptedSteps=" << acceptedSteps << "accumulator=" << m_wheelStepAccumulator;

    Q_EMIT tuneStepRequested(acceptedSteps);
    ev->accept();
}

void SpectrumCanvas::leaveEvent(QEvent*)
{
    if (!m_draggingBandscope && !m_bandscopeButtonPressed)
    {
        unsetCursor();
    }
}
