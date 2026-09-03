#include "WaterfallCanvas.h"
#include <QLinearGradient>
#include <QPainter>

namespace
{
const QColor kWaterfallBg(0x00, 0x24, 0xd8);
constexpr int kControlShelfShadowHeightPx = 8;
}

WaterfallCanvas::WaterfallCanvas(QWidget* parent) : QWidget(parent)
{
    setAutoFillBackground(false);
}

void WaterfallCanvas::setWaterfallImageSource(const QImage* image)
{
    m_waterfall = image;
    update();
}

void WaterfallCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter p(this);
    p.fillRect(rect(), kWaterfallBg);
    if (m_waterfall && !m_waterfall->isNull())
    {
        p.drawImage(rect(), *m_waterfall, QRect(0, 0, m_waterfall->width(), qMin(height(), m_waterfall->height())));
    }

    // Continue the frequency-control shelf treatment below the strip. The
    // neutral edge defines the shelf while the shadow fades into the waterfall.
    const int shadowHeight = qMin(height(), kControlShelfShadowHeightPx);
    QLinearGradient shelfShadow(0, 0, 0, shadowHeight);
    shelfShadow.setColorAt(0.0, QColor(0x00, 0x04, 0x08, 220));
    shelfShadow.setColorAt(1.0, QColor(0x00, 0x08, 0x0f, 0));
    p.fillRect(0, 0, width(), shadowHeight, shelfShadow);
    p.fillRect(0, 0, width(), 1, QColor(0x2a, 0x40, 0x4f));
}
