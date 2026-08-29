#pragma once

#include "Types.h"
#include "RadioIdentities.h"
#include "CachingQueue.h"

#include <QObject>
#include <QVariant>
#include <QVector>
#include <array>
#include <atomic>
#include <mutex>

struct RadioRouterQueueDiagnostics
{
    qsizetype pendingItems{0};
    qsizetype highWaterMark{0};
    quint64 coalescedItems{0};
    quint64 drainEvents{0};
};

class RadioRouter : public QObject
{
    Q_OBJECT

  public:
    explicit RadioRouter(QObject* parent = nullptr);

    void route(const CacheItem& item);
    void routeBatch(const QVector<CacheItem>& items);
    quint64 beginQueueSession();
    void cancelQueueSession(quint64 session);
    void enqueueBatch(const QVector<CacheItem>& items, quint64 session = 0);
    RadioRouterQueueDiagnostics queueDiagnostics() const;

  signals:
    void radioValueUpdated(Funcs func, QVariant value, uchar receiver);
    void frequencyReported(quint64 hz);
    void modeReported(const QString& mode, int filter);
    void repeaterOffsetChanged(quint64 hz);
    void toneAccessModeChanged(rptAccessTxRx_t mode);
    void toneFrequencyChanged(ushort tone);
    void dtcsCodeChanged(ushort code);
    void smeterChanged(int value);
    void nrChanged(bool on);
    void nrLevelChanged(int level);
    void nbChanged(bool on);
    void nbLevelChanged(int level);
    void preampChanged(bool on);
    void preampLevelChanged(int level);
    void attenuatorChanged(bool on);
    void autoNotchChanged(bool on);
    void manualNotchChanged(bool on);
    void compressorChanged(bool on);
    void compressorLevelChanged(int level);
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
    void radioMemoryReceived(MemoryType memory);
    void scopeDataReady(const ScopeData& data);

  private:
    QString modeInfoToString(const ModeInfo& mi) const;
    bool toneRegisterIsDisplayed(Funcs command, uchar receiver) const;
    static bool isReplaceable(const CacheItem& item);
    void drainPendingBatch(quint64 session);

    std::array<rptAccessTxRx_t, 2> m_toneAccessModes{ratrNN, ratrNN};
    mutable std::mutex m_pendingMutex;
    QVector<CacheItem> m_pendingItems;
    bool m_drainScheduled{false};
    qsizetype m_pendingHighWaterMark{0};
    quint64 m_coalescedItems{0};
    quint64 m_drainEvents{0};
    quint64 m_queueSession{0};
    std::atomic<uchar> m_scopeReceiver{0};
};
