#pragma once

#include "UtilityWindow.h"

#include <QString>

class QLabel;
class QGridLayout;
class QProgressBar;

class MetersDialog : public sdr9700::ui::UtilityWindow
{
    Q_OBJECT

  public:
    explicit MetersDialog(QWidget* parent = nullptr);

  public slots:
    void resetMeters();
    void setSMeter(int value);
    void setPowerMeter(double watts);
    void setSwr(double swr);
    void setAlc(double alc);
    void setCompressionMeter(double db);
    void setVoltageMeter(double volts);
    void setCurrentMeter(double amps);
    void setTransmitAudioLevel(int peak, int rms);

  private:
    struct MeterRow
    {
        QProgressBar* bar{nullptr};
        QLabel* valueLabel{nullptr};
    };

    MeterRow addMeterRow(QGridLayout* layout, int row, const QString& label, const QString& description);
    void setMeterRow(const MeterRow& row, int value, const QString& text);
    void setMeterFillColor(const MeterRow& row, const char* color);

    MeterRow m_sMeter;
    MeterRow m_powerMeter;
    MeterRow m_swrMeter;
    MeterRow m_alcMeter;
    MeterRow m_compressionMeter;
    MeterRow m_voltageMeter;
    MeterRow m_currentMeter;
    MeterRow m_txAudioAverageMeter;
    MeterRow m_txAudioPeakMeter;
};
