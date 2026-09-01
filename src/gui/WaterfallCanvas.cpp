#include "WaterfallCanvas.h"
#include "UiTheme.h"

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

    // Paint the lower scope boundary after the waterfall. The neighboring
    // dark-blue pixels otherwise make a style-sheet border appear purple even
    // when its nominal color is the same red as the upper boundary.
    p.fillRect(0, 0, width(), 1, UiTheme::Color::SpectrumBoundary);
}
