// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QWidget>
#include <QVector>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>
#include <QPoint>
#include <cmath>

class QPushButton;

class SpectrumCanvas : public QWidget
{
    Q_OBJECT

  public:
    explicit SpectrumCanvas(QWidget* parent = nullptr);

    void setFrequencyRange(double startMhz, double endMhz);
    void setDataFrequencyRange(double startMhz, double endMhz);
    void setVfoFrequency(double freqMhz);
    void setInteractionLocked(bool locked);
    void setInvertMouseWheel(bool invert);
    int spectrumPaneHeight() const;
    void setSpectrumPaneHeight(int height);
    void updateSpectrum(const QVector<float>& binsDbm);
    void clearDisplay();

    void setFilterWidth(int lowHz, int highHz);

    int freqToX(double mhz) const;

    QSize sizeHint() const override { return {900, 340}; }

  signals:
    void frequencyClicked(double freqMhz);
    void tuneStepRequested(int steps);
    void tuneDragStarted();
    void tuneDragRequested(double deltaMhz);
    void zoomInRequested();
    void zoomOutRequested();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void wheelEvent(QWheelEvent* ev) override;
    void leaveEvent(QEvent*) override;

  private:
    int spectrumHeight() const;
    int waterfallTop() const;
    static int scaleHeight() { return 26; }
    static int splitterHeight() { return 2; }
    QRect splitterRect() const;

    double xToFreq(int x) const;
    int dbmToY(float dbm, int topY, int h) const;
    QRgb dbmToColor(float dbm) const;

    int defaultSpectrumHeight() const;
    int constrainedSpectrumHeight(int requested) const;
    bool applySpectrumPaneHeight(int requested);
    void updateBandscopeCursor(const QPoint& pos);
    void rebuildWaterfallImage();
    void appendWaterfallRow(const QVector<float>& binsDbm);
    int binForFrequency(double mhz, int binCount) const;
    int binForDisplayX(int x, int binCount) const;
    void repositionZoomButtons();
    void scheduleRepaint();

    double m_startMhz{144.0};
    double m_endMhz{146.0};
    double m_dataStartMhz{144.0};
    double m_dataEndMhz{146.0};
    double m_vfoMhz{145.0};
    float m_minDbm{-125.0f};
    float m_maxDbm{-15.0f};

    int m_filterLowHz{-1400};
    int m_filterHighHz{1400};
    int m_spectrumHeight{-1};
    bool m_draggingBandscope{false};
    bool m_bandscopeButtonPressed{false};
    bool m_interactionLocked{false};
    bool m_invertMouseWheel{false};
    double m_bandscopeDragAnchorFreqMhz{0.0};
    double m_wheelStepAccumulator{0.0};
    QPoint m_bandscopeDragStartPos;
    QPoint m_lastBandscopeDragPos;

    QVector<float> m_spectrumBins;
    QVector<float> m_peakHold;
    QImage m_waterfall;

    QTimer m_peakDecayTimer;
    QTimer m_repaintTimer;
    QElapsedTimer m_lastFrameTimer;
    QPushButton* m_zoomInButton{nullptr};
    QPushButton* m_zoomOutButton{nullptr};
};
