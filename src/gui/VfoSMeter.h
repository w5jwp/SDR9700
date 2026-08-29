#pragma once

#include <QWidget>

class VfoSMeter : public QWidget
{
    Q_OBJECT

  public:
    explicit VfoSMeter(QWidget* parent = nullptr);

    void setRawValue(int value);
    void setTransmitPowerMode(bool enabled);
    void setPowerWatts(double watts);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    int m_rawValue{0};
    double m_powerWatts{0.0};
    bool m_transmitPowerMode{false};
};
