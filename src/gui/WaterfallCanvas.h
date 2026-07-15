// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QImage>
#include <QVector>
#include <QWidget>

class WaterfallCanvas : public QWidget
{
    Q_OBJECT

  public:
    explicit WaterfallCanvas(QWidget* parent = nullptr);

    void setFrequencyRange(double startMhz, double endMhz);
    void setDataFrequencyRange(double startMhz, double endMhz);
    void setPaused(bool paused);
    void updateSpectrum(const QVector<float>& levels);
    void clearDisplay();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    double xToFreq(int x) const;
    int binForFrequency(double mhz, int binCount) const;
    int binForDisplayX(int x, int binCount) const;
    QRgb levelToColor(float level) const;
    void rebuildImage();

    double m_startMhz{144.0};
    double m_endMhz{146.0};
    double m_dataStartMhz{144.0};
    double m_dataEndMhz{146.0};
    float m_minLevel{0.0f};
    float m_maxLevel{160.0f};
    bool m_paused{false};
    QImage m_waterfall;
};
