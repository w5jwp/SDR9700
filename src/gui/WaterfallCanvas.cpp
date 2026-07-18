#include "WaterfallCanvas.h"

#include <QPainter>

namespace
{
const QColor kWaterfallBg(0x00, 0x24, 0xd8);
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
}
