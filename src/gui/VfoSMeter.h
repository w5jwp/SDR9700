#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class VfoSMeter : public QWidget
{
    Q_OBJECT

  public:
    explicit VfoSMeter(QWidget* parent = nullptr);

    void setRawValue(int value);
    void setTransmitPowerMode(bool enabled);
    void setPowerWatts(double watts);
    void setMaxPowerWatts(double watts);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    void advanceSignalDisplay();

    int m_rawValue{0};
    double m_displayRawValue{0.0};
    double m_powerWatts{0.0};
    double m_maxPowerWatts{100.0};
    bool m_transmitPowerMode{false};
    QTimer m_signalAnimationTimer;
    QElapsedTimer m_signalAnimationElapsed;
};
