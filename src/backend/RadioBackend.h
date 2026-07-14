#pragma once

#include "IRadioBackend.h"
#include "Types.h"
#include "UdpBase.h"
#include "AudioConverter.h"
#include <QAudioDevice>
#include <QThread>
#include <QPointer>
#include <QTimer>
#include <atomic>
#include <functional>

class Commander;

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
    void setAttenuatorEnabled(bool on) override;
    void setAfGain(int level) override;
    void setRfGain(int level) override;
    void setSquelch(bool on, int level) override;
    void setAgcMode(const QString& mode) override;
    void setAutoNotch(bool on) override;
    void setCompressor(bool on) override;
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

    void setPtt(bool on) override;
    void setTxPower(int level) override;
    void setMicGain(int level) override;
    void pollFrequency() override;
    void setRxAudioDevice(const QAudioDevice& dev) override { m_rxDevice = dev; }
    void setTxAudioDevice(const QAudioDevice& dev) override { m_txDevice = dev; }
    void setLanModLevel(int level) override;
    void sendDtmf(const QString& digits) override;

  private slots:
    void onHaveAudioData(const audioPacket& pkt);
    void onLanReady();
    void onPortError(errorType err);
    void onNetworkStatus(networkStatus status);

  private:
    QString modeInfoToString(const ModeInfo& mi) const;
    void shutdownConnection();
    void requestInitialRadioState();
    void updateReadyState();
    void handleReportedFrequency(quint64 hz);
    void sendLanModLevel(int level);
    void startTxGainRamp(int targetLevel);
    void selectMainVfoForCommand(Commander* commandSession) const;
    bool isCurrentSession(quint64 session, const Commander* commandSession) const;
    void invokeOnCurrentCommander(const std::function<void(Commander*)>& command);
    void restartAfterSyncTimeout();

    QThread* m_workerThread{nullptr};
    Commander* m_commander{nullptr};
    std::atomic<quint64> m_sessionId{0};
    QString m_connectionHost;
    quint16 m_connectionPort{0};
    QString m_connectionUser;
    QString m_connectionPass;

    float m_scopeMinDbm{-130.0f};
    float m_scopeMaxDbm{-10.0f};
    quint32 m_rxSampleRate{48000};
    QAudioDevice m_rxDevice;
    QAudioDevice m_txDevice;
    int m_lanModLevel{128}; // 0-255, set via setLanModLevel()
    // True from the moment the user requests PTT until the radio is returned to
    // RX. The early true state covers the pre-ramp window where LAN modulation
    // is still muted but timers are preparing the radio-side TX audio path.
    bool m_pttActive{false};
    QTimer* m_pttStaleOnGuardTimer{nullptr};
    QTimer* m_txAudioEnableTimer{nullptr};
    QTimer* m_txGainRampTimer{nullptr};
    int m_txGainRampStart{0};
    int m_txGainRampTarget{0};
    int m_txGainRampStep{0};
    int m_txLanModApplied{0};
    QTimer* m_scopeRetryTimer{nullptr};
    QTimer* m_initialStateRetryTimer{nullptr};
    QTimer* m_syncWatchdogTimer{nullptr};
    bool m_scopeDataReceived{false};
    bool m_initialFrequencyReceived{false};
    bool m_initialModeReceived{false};
    bool m_initialMainFrequencyReceived{false};
    bool m_initialMainModeReceived{false};
    bool m_initialStateRequested{false};
    bool m_radioReady{false};
    QTimer* m_smeterPollTimer{nullptr};
    QTimer* m_bandStateRefreshTimer{nullptr};
    int m_currentBandKey{-1};
};
