#pragma once

#include "IRadioBackend.h"
#include "Types.h"
#include "UdpBase.h"
#include "AudioConverter.h"
#include <QAudioDevice>
#include <QThread>
#include <QPointer>
#include <QTimer>
#include <QVector>
#include "TransmitSafetyPolicy.h"
#include "PttConfirmationPolicy.h"
#include "TransmitConfigurationPolicy.h"
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

struct CacheItem;
class Commander;
class ScopeController;
class RadioRouter;

class RadioBackend : public IRadioBackend
{
    Q_OBJECT

  public:
    explicit RadioBackend(QObject* parent = nullptr);
    ~RadioBackend() override;

  public slots:
    void connectToRadio(const QString& host, quint16 port, const QString& user, const QString& pass) override;
    void disconnectFromRadio() override;

    void setFrequencyHz(quint64 hz) override;
    void setMode(const QString& mode) override;
    void setFilterWidth(int lowHz, int highHz) override;

    void setNrEnabled(bool on) override;
    void setNrLevel(int level) override;
    void setNbEnabled(bool on) override;
    void setNbLevel(int level) override;
    void setPreampEnabled(bool on) override;
    void setPreampLevel(int level) override;
    void setAttenuatorEnabled(bool on) override;
    void setAfGain(int level) override;
    void setRfGain(int level) override;
    void setSquelch(bool on, int level) override;
    void setAgcMode(const QString& mode) override;
    void setAutoNotch(bool on) override;
    void setManualNotch(bool on) override;
    void setCompressor(bool on) override;
    void setCompressorLevel(int level) override;
    void setXfcEnabled(bool on) override;
    void setRitEnabled(bool on) override;
    void setRitOffset(short hz) override;
    void setDuplexMode(duplexMode_t mode) override;
    void setRepeaterOffsetHz(quint64 hz) override;
    void setToneAccessMode(rptAccessTxRx_t mode) override;
    void setToneFrequency(ushort tone) override;
    void setDtcsCode(ushort code) override;

    void setScopeEnabled(bool on) override;
    void setScopeSpanHz(quint64 hz) override;
    void setScopeMode(int mode) override;
    void setScopeVfo(Vfo vfo) override;
    void setScopeFixedRangeHz(quint64 startHz, quint64 endHz) override;

    bool setPtt(bool on) override;
    void setTxPower(int level) override;
    void setTuningStep(int step) override;
    void selectVfo(Vfo vfo) override;
    void exchangeMainSub() override;
    void setVfoFrequencyHz(Vfo vfo, quint64 hz) override;
    void setVfoMode(Vfo vfo, const QString& mode) override;
    void requestVfoState(Vfo vfo) override;
    void setVfoAgcMode(Vfo vfo, const QString& mode) override;
    void setVfoAttenuatorEnabled(Vfo vfo, bool on) override;
    void setVfoNbEnabled(Vfo vfo, bool on) override;
    void setVfoNotch(Vfo vfo, VfoNotch notch) override;
    void setVfoNrEnabled(Vfo vfo, bool on) override;
    void setVfoPreampLevel(Vfo vfo, int level) override;
    void setVfoRfGain(Vfo vfo, int level) override;
    void setVfoSquelch(Vfo vfo, int level) override;
    void setDualWatchEnabled(bool on) override;
    void pollFrequency() override;
    void selectVfoMode() override;
    void selectRadioMemory(quint16 group, quint16 channel) override;
    void requestRadioMemory(quint16 group, quint16 channel) override;
    void writeRadioMemory(MemoryType memory) override;
    void setRxAudioDevice(const QAudioDevice& dev) override;
    void setTxAudioDevice(const QAudioDevice& dev) override;
    void stopLocalAudio() override;
    void setLanModLevel(int level) override;
    void sendDtmf(const QString& digits) override;

  private slots:
    void onHaveAudioData(const audioPacket& pkt);
    void onLanReady();
    void onPortError(errorType err);
    void onNetworkStatus(networkStatus status);

  private:
    void shutdownConnection(bool emitDisconnectedSignal = true, bool emitDisconnectedStage = true);
    void requestInitialRadioState();
    void requestPostReadyRadioState();
    void updateReadyState();
    void setScopeSyncDegraded(bool degraded);
    void handleReportedFrequency(quint64 hz);
    void sendLanModLevel(int level);
    void sendPttOffNow();
    void armTransmitSafety();
    void disarmTransmitSafety();
    void forcePttOffForSafety(const QString& message);
    void handleTransmitSwr(double swr);
    static void selectMainVfoForCommand(Commander* commandSession);
    static void requestSubVfoStateForCommand(Commander* commandSession);
    static void selectMemoryBandForCommand(Commander* commandSession, quint16 group);
    static void selectMemoryForCommand(Commander* commandSession, quint16 group, quint16 channel,
                                       bool prepareBand = true);
    void resetScopeController();
    bool isCurrentSession(quint64 session, const Commander* commandSession) const;
    void invokeOnCurrentCommander(const std::function<void(Commander*)>& command);
    void routeVfoReceiverCommand(Vfo vfo, const std::function<void(Commander*, uchar)>& command);
    void restartAfterSyncTimeout();

    QThread* m_workerThread{nullptr};
    QThread* m_radioDataThread{nullptr};
    Commander* m_commander{nullptr};
    ScopeController* m_scopeController{nullptr};
    RadioRouter* m_radioRouter{nullptr};
    QMetaObject::Connection m_queueSendValuesConnection;
    std::atomic<quint64> m_sessionId{0};
    // Worker-thread callbacks capture this token instead of RadioBackend. It is
    // invalidated before teardown, so delayed work cannot dereference a backend
    // that was destroyed while a radio thread was stopping.
    std::shared_ptr<std::atomic_bool> m_sessionActive;
    QString m_connectionHost;
    quint16 m_connectionPort{0};
    QString m_connectionUser;
    QString m_connectionPass;

    quint32 m_rxSampleRate{48000};
    int m_rxChannelCount{2};
    QAudioDevice m_rxDevice;
    QAudioDevice m_txDevice;
    int m_lanModLevel{128}; // 0-255, set via setLanModLevel()
    std::optional<int> m_originalDataOffMod;
    std::optional<int> m_originalData1Mod;
    QString m_lastUserVisibleNetworkMessage;
    // Tracks requested and confirmed state separately so the UI can release
    // immediately while TX safety remains armed until radio confirmation.
    sdr9700::PttConfirmationPolicy m_pttState;
    QTimer* m_pttReleaseDelayTimer{nullptr};
    QTimer* m_pttOffConfirmationTimer{nullptr};
    QTimer* m_pttMaxDurationTimer{nullptr};
    QTimer* m_scopeRetryTimer{nullptr};
    QTimer* m_initialStateRetryTimer{nullptr};
    QTimer* m_syncWatchdogTimer{nullptr};
    bool m_scopeDataReceived{false};
    bool m_scopeSyncDegraded{false};
    bool m_initialFrequencyReceived{false};
    bool m_initialModeReceived{false};
    bool m_initialMainFrequencyReceived{false};
    bool m_initialMainModeReceived{false};
    bool m_initialStateRequested{false};
    bool m_radioReady{false};
    int m_syncReconnectAttempts{0};
    bool m_syncReconnectPending{false};
    std::optional<std::pair<quint16, quint16>> m_selectedRadioMemory;
    QTimer* m_smeterPollTimer{nullptr};
    bool m_smeterPollSubNext{false};
    bool m_smeterPollPending{false};
    bool m_smeterPollSettlingSample{false};
    uchar m_smeterPollPendingReceiver{0};
    int m_smeterPollPendingTicks{0};
    Vfo m_smeterRestoreVfo{Vfo::Main};
    QTimer* m_bandStateRefreshTimer{nullptr};
    QTimer* m_mainSubExchangeRetryTimer{nullptr};
    int m_currentBandKey{-1};
    quint64 m_currentMainFrequencyHz{0};
    Vfo m_activeVfo{Vfo::Main};
    bool m_dualWatchEnabled{false};
    bool m_mainSubExchangePending{false};
    quint8 m_mainSubExchangeConfirmations{0};
    duplexMode_t m_currentDuplexMode{dmSimplex};
    quint64 m_currentRepeaterOffsetHz{0};
    sdr9700::TransmitConfigurationPolicy m_transmitConfiguration;
    int m_currentMainFilter{1};
    sdr9700::TransmitSafetyPolicy m_transmitSafetyPolicy;
    int m_txMeterPollTick{0};
};
