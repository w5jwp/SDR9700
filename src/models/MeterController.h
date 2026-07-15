#pragma once

#include <QObject>

struct MeterSnapshot
{
    int sMeter{0};
    bool sMeterValid{false};
    double powerWatts{0.0};
    bool powerValid{false};
    double swr{1.0};
    bool swrValid{false};
    double alc{0.0};
    bool alcValid{false};
    double compressionDb{0.0};
    bool compressionValid{false};
    double voltageVolts{0.0};
    bool voltageValid{false};
    double currentAmps{0.0};
    bool currentValid{false};
    int txAudioPeak{0};
    int txAudioRms{0};
};

class QTimer;

class MeterController : public QObject
{
    Q_OBJECT

  public:
    explicit MeterController(QObject* parent = nullptr);

  public slots:
    void reset();
    void resetTransmitMeters();
    void setSMeter(int value);
    void setPowerMeter(double watts);
    void setSwr(double swr);
    void setAlc(double alc);
    void setCompressionMeter(double db);
    void setVoltageMeter(double volts);
    void setCurrentMeter(double amps);
    void setTransmitAudioLevel(int peak, int rms);

  signals:
    void snapshotChanged(const MeterSnapshot& snapshot);

  private:
    void scheduleFlush();
    void flush();

    MeterSnapshot m_snapshot;
    QTimer* m_flushTimer{nullptr};
    bool m_dirty{false};
};

Q_DECLARE_METATYPE(MeterSnapshot)
