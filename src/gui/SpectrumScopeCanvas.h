// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QElapsedTimer>
#include <QColor>
#include <QPixmap>
#include <QPoint>
#include <QSize>
#include <QTimer>
#include <QVector>
#include <QWidget>

class SpectrumScopeCanvas : public QWidget
{
    Q_OBJECT

  public:
    explicit SpectrumScopeCanvas(QWidget* parent = nullptr);

    static int scaleHeight() { return 26; }

    void setFrequencyRange(double startMhz, double endMhz);
    void setDataFrequencyRange(double startMhz, double endMhz);
    void setVfoFrequency(double freqMhz);
    void setVfoMarkerColor(const QColor& color);
    void setBackgroundColor(const QColor& color);
    void setGridLineColor(const QColor& color);
    void setGridDensity(int density);
    void setInteractionLocked(bool locked);
    void setInvertMouseWheel(bool invert);
    void setFilterWidth(int lowHz, int highHz);
    void updateSpectrum(const QVector<float>& levels, bool outOfRange);
    void clearDisplay();

    int freqToX(double mhz) const;

  signals:
    void frequencyClicked(double freqMhz);
    void wheelStepRequested(int steps);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void wheelEvent(QWheelEvent* ev) override;

  private:
    int plotHeight() const;
    static int plotLeftX() { return 0; }
    int plotRightX() const;
    int plotWidthPx() const;
    double xToFreq(int x) const;
    int levelToY(float level, int topY, int h) const;
    int binForFrequency(double mhz, int binCount) const;
    int binForDisplayX(int x, int binCount) const;
    bool isSpectrumClickArea(const QPoint& pos) const;
    void invalidateStaticLayer();
    void ensureStaticLayer();
    void renderStaticLayer(QPainter* painter) const;
    void ensureDisplayBinMap(int binCount);
    void scheduleRepaint();

    double m_startMhz{144.0};
    double m_endMhz{146.0};
    double m_dataStartMhz{144.0};
    double m_dataEndMhz{146.0};
    double m_vfoMhz{145.0};
    QColor m_vfoMarkerColor{0xf5, 0xf7, 0xf8, 230};
    QColor m_backgroundColor{0x08, 0x12, 0x1b};
    QColor m_gridLineColor{0x6f, 0x89, 0x9e};
    float m_minLevel{0.0f};
    float m_maxLevel{160.0f};

    int m_filterLowHz{-1400};
    int m_filterHighHz{1400};
    int m_gridDensity{1};
    bool m_clickPressed{false};
    bool m_interactionLocked{false};
    bool m_invertMouseWheel{false};
    bool m_scopeOutOfRange{false};
    double m_wheelStepAccumulator{0.0};
    QPoint m_clickPressPos;

    QVector<float> m_spectrumBins;
    QVector<float> m_peakHold;
    QVector<int> m_displayBins;
    QPixmap m_staticLayer;
    QSize m_staticLayerSize;
    qreal m_staticLayerDevicePixelRatio{0.0};
    QSize m_displayBinMapSize;
    int m_displayBinMapBinCount{0};
    double m_displayBinMapStartMhz{0.0};
    double m_displayBinMapEndMhz{0.0};
    double m_displayBinMapDataStartMhz{0.0};
    double m_displayBinMapDataEndMhz{0.0};
    bool m_staticLayerDirty{true};

    QTimer m_peakDecayTimer;
    QTimer m_repaintTimer;
    QElapsedTimer m_lastFrameTimer;
};
