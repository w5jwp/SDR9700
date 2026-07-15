#pragma once

#include "Types.h"
#include "RadioIdentities.h"

#include <QObject>
#include <QVariant>
#include <QVector>

struct CacheItem;

class RadioRouter : public QObject
{
    Q_OBJECT

  public:
    explicit RadioRouter(QObject* parent = nullptr);

    void route(const CacheItem& item);
    void routeBatch(const QVector<CacheItem>& items);

  signals:
    void radioValueUpdated(Funcs func, QVariant value, uchar receiver);
    void frequencyReported(quint64 hz);
    void modeReported(const QString& mode, int filter);
    void vfoBandMSRequested();
    void repeaterOffsetChanged(quint64 hz);
    void toneAccessModeChanged(rptAccessTxRx_t mode);
    void toneFrequencyChanged(ushort tone);
    void dtcsCodeChanged(ushort code);
    void smeterChanged(int value);
    void nrChanged(bool on);
    void nbChanged(bool on);
    void preampChanged(bool on);
    void preampLevelChanged(int level);
    void attenuatorChanged(bool on);
    void autoNotchChanged(bool on);
    void manualNotchChanged(bool on);
    void compressorChanged(bool on);
    void xfcChanged(bool on);
    void ritEnabledChanged(bool on);
    void ritOffsetChanged(short hz);
    void agcModeChanged(const QString& mode);
    void rfGainChanged(int level);
    void txPowerChanged(int level);
    void squelchChanged(bool on, int level);
    void swrMeterChanged(double swr);
    void powerMeterChanged(double watts);
    void alcChanged(double alc);
    void compressionMeterChanged(double db);
    void voltageMeterChanged(double volts);
    void currentMeterChanged(double amps);
    void pttChanged(bool on);
    void duplexModeChanged(duplexMode_t mode);
    void dataOffModChanged(const radioInput& input);
    void data1ModChanged(const radioInput& input);
    void scopeDataReady(const ScopeData& data);

  private:
    QString modeInfoToString(const ModeInfo& mi) const;
};
