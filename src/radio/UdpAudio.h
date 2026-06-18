#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QHostInfo>
#include <QTimer>
#include <QMutex>
#include <QByteArray>
#include <atomic>
#include <QVector>
#include <QMap>
#include <QUuid>

#include <QtEndian>

#include <QBuffer>
#include <QThread>
#include <QElapsedTimer>

#include <QDebug>

#include "PacketTypes.h"

#include "UdpBase.h"

#include "AudioHandler.h"

class UdpAudio : public UdpBase
{
    Q_OBJECT

  public:
    UdpAudio(QHostAddress local, QHostAddress ip, quint16 audioPort, quint16 lport, audioSetup rxSetup,
             audioSetup txSetup);
    ~UdpAudio();

    int audioLatency = 0;

  signals:
    void haveAudioData(audioPacket data);

    void setupTxAudio(audioSetup setup);
    void setupRxAudio(audioSetup setup);

    void haveChangeLatency(quint16 value);
    void haveSetVolume(quint8 value);
    void haveRxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under,
                      bool over);
    void haveTxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under,
                      bool over);

  public slots:
    void enableAudio();
    void changeLatency(quint16 value);
    void setVolume(quint8 value);
    void setTxActive(bool active);
    void getRxLevels(quint16 amplitude, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);
    void getTxLevels(quint16 amplitude, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);
    void receiveAudioData(audioPacket audio);
    void queueDtmfPcm(const QByteArray& pcm);

  private slots:
    void onRxAudioInitFailed();
    void onTxAudioInitFailed();
    void sendNextDtmfFrame();

  private:
    void sendAudioBuffer(const QByteArray& data);
    void sendTxSilenceFrames(int frameCount);
    void dataReceived();
    void watchdog();
    void startAudio();
    void stopAudioWorker(AudioHandlerBase*& handler, QThread*& workerThread, const char* name);
    audioSetup rxSetup;
    audioSetup txSetup;

    uint16_t sendAudioSeq = 0;

    AudioHandlerBase* rxaudio = nullptr;
    QThread* rxAudioThread = nullptr;

    AudioHandlerBase* txaudio = nullptr;
    QThread* txAudioThread = nullptr;

    QTimer* txAudioTimer = nullptr;
    bool enableTx = true;

    QMutex audioMutex;

    std::atomic_bool m_txActive{false};
    QByteArray m_dtmfPcm;
    int m_dtmfPcmOffset{0};
    QTimer* m_dtmfTimer{nullptr};
    bool m_dtmfTimerActive{false};
    bool m_watchdogAlerted = false;
    QElapsedTimer m_txGateTimer;
    int m_txGateMs = 0;
    int m_txSilencePacketBytes = 1920; // 20 ms, 48 kHz, mono 16-bit PCM.
    int m_txFadeSamplesRemaining = 0;
    int m_txFadeSamplesTotal = 0;

    bool m_audioReady = false;

    QElapsedTimer audioClock;
    bool audioHaveBase = false;
    quint32 audioBaseSeq = 0;
    qint64 audioBaseNs = 0;
    int audioPktMs = 20; // IC-9700 LAN audio frames are currently handled as 20 ms packets.
    int latencyCounter = 0;
    int rxPacketDiagnosticsCount = 0;
};
