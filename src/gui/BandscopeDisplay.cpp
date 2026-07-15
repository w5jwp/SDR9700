#include "BandscopeDisplay.h"
#include "BandscopeCanvas.h"
#include "WaterfallCanvas.h"

#include <QComboBox>
#include <QResizeEvent>
#include <QSignalBlocker>

namespace
{
constexpr int kMinSpectrumHeight = 150;
constexpr int kMinWaterfallHeight = 180;
constexpr int kDefaultBandscopeHeightBias = 0;
constexpr int kSpanComboWidth = 94;
constexpr int kSpanComboHeight = 24;
constexpr int kSpanComboMargin = 8;
} // namespace

BandscopeDisplay::BandscopeDisplay(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(640, 320);

    m_bandscopeCanvas = new BandscopeCanvas(this);
    m_splitter = new QWidget(this);
    m_waterfallCanvas = new WaterfallCanvas(this);
    m_spanCombo = new QComboBox(this);

    m_splitter->setStyleSheet(QStringLiteral("QWidget { background: #061116; border-top: 1px solid #9a2424; }"));
    m_splitter->setFixedHeight(splitterHeight());
    m_spanCombo->setFixedSize(kSpanComboWidth, kSpanComboHeight);
    m_spanCombo->setFocusPolicy(Qt::NoFocus);
    m_spanCombo->setToolTip(QStringLiteral("Bandscope span"));
    m_spanCombo->setStyleSheet(
        QStringLiteral("QComboBox { background: rgba(16, 22, 30, 220); "
                       "border: 1px solid #566576; border-radius: 3px; "
                       "color: #e8f2f8; font-size: 10px; font-weight: bold; "
                       "padding: 1px 18px 1px 6px; }"
                       "QComboBox:hover { background: rgba(32, 42, 55, 235); "
                       "border-color: #7f96ad; }"
                       "QComboBox::drop-down { border: none; width: 16px; }"
                       "QComboBox::down-arrow { image: none; width: 0px; height: 0px; "
                       "border-left: 4px solid transparent; border-right: 4px solid transparent; "
                       "border-top: 5px solid #e8f2f8; margin-right: 5px; }"
                       "QComboBox QAbstractItemView { background: #10161e; "
                       "border: 1px solid #566576; color: #e8f2f8; "
                       "selection-background-color: #2a82da; }"));
    m_spanCombo->raise();

    connect(m_bandscopeCanvas, &BandscopeCanvas::frequencyClicked, this, &BandscopeDisplay::frequencyClicked);
    connect(m_bandscopeCanvas, &BandscopeCanvas::tuneStepRequested, this, &BandscopeDisplay::tuneStepRequested);
    connect(m_bandscopeCanvas, &BandscopeCanvas::tuneDragStarted, this, &BandscopeDisplay::tuneDragStarted);
    connect(m_bandscopeCanvas, &BandscopeCanvas::tuneDragRequested, this, &BandscopeDisplay::tuneDragRequested);
    connect(m_bandscopeCanvas, &BandscopeCanvas::pointerInteractionStarted, m_waterfallCanvas,
            [this]() { m_waterfallCanvas->setPaused(true); });
    connect(m_bandscopeCanvas, &BandscopeCanvas::pointerInteractionFinished, m_waterfallCanvas,
            [this]() { m_waterfallCanvas->setPaused(false); });
    connect(m_spanCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index)
            {
                if (index < 0)
                {
                    return;
                }
                Q_EMIT spanSelected(m_spanCombo->itemData(index).toULongLong());
            });
}

void BandscopeDisplay::setSpanChoices(const QVector<SpanChoice>& choices)
{
    const quint64 previousHz = m_spanCombo->currentData().toULongLong();
    {
        const QSignalBlocker blocker(m_spanCombo);
        m_spanCombo->clear();
        for (const SpanChoice& choice : choices)
        {
            m_spanCombo->addItem(choice.label, QVariant::fromValue<qulonglong>(choice.hz));
        }
    }
    setCurrentSpanHz(previousHz);
    m_spanCombo->setVisible(!choices.isEmpty());
    updateSpanComboGeometry();
}

void BandscopeDisplay::setCurrentSpanHz(quint64 hz)
{
    const QSignalBlocker blocker(m_spanCombo);
    const int index = m_spanCombo->findData(QVariant::fromValue<qulonglong>(hz));
    if (index >= 0)
    {
        m_spanCombo->setCurrentIndex(index);
    }
}

void BandscopeDisplay::updateSpanComboGeometry()
{
    if (!m_spanCombo)
    {
        return;
    }
    const int x = qMax(kSpanComboMargin, width() - kSpanComboMargin - m_spanCombo->width());
    m_spanCombo->move(x, kSpanComboMargin);
    m_spanCombo->raise();
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
    updateSpanComboGeometry();
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
