#include "UdpAudio.h"
#include "LogCategories.h"
#include <algorithm>
#include <cstring>

namespace
{
constexpr int kAudioThreadShutdownWaitMs = 500;
constexpr quint8 kLpcmMono16Codec = 0x04;
} // namespace

UdpAudio::UdpAudio(QHostAddress local, QHostAddress ip, quint16 audioPort, quint16 lport, audioSetup rxSetup,
                   audioSetup txSetup)
{
    qInfo(logUdp()) << "Starting UdpAudio";
    this->localIP = local;
    this->port = audioPort;
    this->radioIP = ip;
    this->rxSetup = rxSetup;
    this->txSetup = txSetup;
    if (this->txSetup.sampleRate > 0 && this->txSetup.codec == kLpcmMono16Codec)
    {
        m_txSilencePacketBytes = int(this->txSetup.sampleRate / 50) * int(sizeof(qint16));
    }

    if (txSetup.sampleRate == 0)
    {
        enableTx = false;
    }

    init(lport);

    QUdpSocket::connect(udp, &QUdpSocket::readyRead, this, &UdpAudio::dataReceived);

    // Audio workers are not started here; enableAudio() is called once the
    // radio reaches the ready (synced) state so playback begins with known
    // frequency and mode. areYouThereTimer (below) keeps the audio stream
    // alive during the sync window.

    watchdogTimer = new QTimer(this);
    connect(watchdogTimer, &QTimer::timeout, this, &UdpAudio::watchdog);
    watchdogTimer->start(WATCHDOG_PERIOD);

    areYouThereTimer = new QTimer(this);
    connect(areYouThereTimer, &QTimer::timeout, this, std::bind(&UdpBase::sendControl, this, false, 0x03, 0));
    areYouThereTimer->start(AREYOUTHERE_PERIOD);

    m_dtmfTimer = new QTimer(this);
    m_dtmfTimer->setInterval(20);
    m_dtmfTimer->setSingleShot(false);
    connect(m_dtmfTimer, &QTimer::timeout, this, &UdpAudio::sendNextDtmfFrame);
}

UdpAudio::~UdpAudio()
{
    qDebug(logUdp()) << "[SHUTDOWN] ~UdpAudio enter";
    stopAudioWorker(rxaudio, rxAudioThread, "rxAudioThread");
    stopAudioWorker(txaudio, txAudioThread, "txAudioThread");
    qDebug(logUdp()) << "[SHUTDOWN] ~UdpAudio complete";
}

void UdpAudio::stopAudioWorker(AudioHandlerBase*& handler, QThread*& workerThread, const char* name)
{
    if (handler)
    {
        qDebug(logUdp()) << "[SHUTDOWN]" << name << "handler dispose ...";
        handler->dispose();
        handler->deleteLater();
        handler = nullptr;
    }

    if (workerThread == nullptr)
    {
        return;
    }

    qDebug(logUdp()) << "[SHUTDOWN]" << name << "quit";
    workerThread->quit();
    if (!workerThread->wait(kAudioThreadShutdownWaitMs))
    {
        qWarning(logUdp()) << "[SHUTDOWN]" << name << "did not stop within" << kAudioThreadShutdownWaitMs
                           << "ms; requesting interruption";
        workerThread->requestInterruption();
        workerThread->quit();
        if (!workerThread->wait(kAudioThreadShutdownWaitMs))
        {
            qCritical(logUdp()) << "[SHUTDOWN]" << name
                                << "did not stop after bounded shutdown; leaving thread detached";
            workerThread->setParent(nullptr);
            connect(workerThread, &QThread::finished, workerThread, &QObject::deleteLater);
            workerThread = nullptr;
            return;
        }
    }
    delete workerThread;
    workerThread = nullptr;
    qDebug(logUdp()) << "[SHUTDOWN]" << name << "done";
}

void UdpAudio::watchdog()
{
    if (msSinceLastReceived() > 30000)
    {
        if (!m_watchdogAlerted)
        {
            // Audio watchdog is intentionally conservative: it tears down the
            // local audio workers and lets the user reconnect instead of
            // issuing unsolicited recovery traffic on the control channel.
            qInfo(logUdp()) << " Audio Watchdog: no audio data received for 30s, restart required; last packet was"
                            << msSinceLastReceived() << "ms ago";
            m_watchdogAlerted = true;
            stopAudioWorker(rxaudio, rxAudioThread, "rxAudioThread");
            stopAudioWorker(txaudio, txAudioThread, "txAudioThread");
        }
    }
    else
    {
        m_watchdogAlerted = false;
    }
}

void UdpAudio::sendAudioBuffer(const QByteArray& data)
{
    int len = 0;
    while (len < data.length())
    {
        QByteArray partial = data.mid(len, 1364);
        len += partial.length();
        audio_packet p;
        memset(p.packet, 0x0, sizeof(p));
        p.len = (quint32)sizeof(p) + partial.length();
        p.sentid = myId;
        p.rcvdid = remoteId;
        // The IC-9700 expects normal TX audio fragments to use 0x0080.
        // Marking intermediate fragments as 0x0081 can produce short
        // decode artifacts at the start of transmit.
        p.ident = 0x0080;
        p.datalen = (quint16)qToBigEndian((quint16)partial.length());
        p.sendseq = (quint16)qToBigEndian((quint16)sendAudioSeq);
        QByteArray tx = QByteArray::fromRawData(reinterpret_cast<const char*>(p.packet), sizeof(p));
        tx.append(partial);
        sendTrackedPacket(tx);
        sendAudioSeq++;
    }
}

void UdpAudio::receiveAudioData(audioPacket audio)
{
    if (txaudio == nullptr)
    {
        qDebug(logUdp()) << "TX: receiveAudioData called but txaudio is null";
        return;
    }
    if (audio.data.length() > 0)
    {
        m_txSilencePacketBytes = audio.data.length();

        // DTMF timer owns the audio path; mic frames are suppressed entirely.
        if (m_dtmfTimerActive)
        {
            return;
        }

        sendAudioBuffer(audio.data);
    }
}

void UdpAudio::queueDtmfPcm(const QByteArray& pcm)
{
    m_dtmfPcm = pcm;
    m_dtmfPcmOffset = 0;
    if (!pcm.isEmpty())
    {
        m_dtmfTimerActive = true;
        m_dtmfTimer->start();
    }
}

void UdpAudio::sendNextDtmfFrame()
{
    if (!m_txActive.load())
    {
        m_dtmfTimerActive = false;
        m_dtmfTimer->stop();
        return;
    }

    if (m_dtmfPcmOffset >= m_dtmfPcm.size())
    {
        m_dtmfTimerActive = false;
        m_dtmfTimer->stop();
        return;
    }

    const int take = qMin(m_txSilencePacketBytes, m_dtmfPcm.size() - m_dtmfPcmOffset);
    QByteArray frame(m_txSilencePacketBytes, '\0');
    memcpy(frame.data(), m_dtmfPcm.constData() + m_dtmfPcmOffset, take);
    m_dtmfPcmOffset += take;
    sendAudioBuffer(frame);

    if (m_dtmfPcmOffset >= m_dtmfPcm.size())
    {
        m_dtmfTimerActive = false;
        m_dtmfTimer->stop();
    }
}

void UdpAudio::changeLatency(quint16 value)
{
    emit haveChangeLatency(value);
}

void UdpAudio::setVolume(quint8 value)
{
    emit haveSetVolume(value);
}

void UdpAudio::getRxLevels(quint16 amplitude, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under,
                           bool over)
{

    emit haveRxLevels(amplitude, amplitudeRMS, latency, current, under, over);
}

void UdpAudio::getTxLevels(quint16 amplitude, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under,
                           bool over)
{
    emit haveTxLevels(amplitude, amplitudeRMS, latency, current, under, over);
}

void UdpAudio::dataReceived()
{

    while (udp->hasPendingDatagrams())
    {
        QNetworkDatagram datagram = udp->receiveDatagram();
        QByteArray r = datagram.data();

        switch (r.length())
        {
        case (16):
        {
            break;
        }
        default:
        {
            if (r.length() < AUDIO_SIZE)
            {
                break;
            }

            const control_packet* in = reinterpret_cast<const control_packet*>(r.constData());

            if (in->type != 0x01 && in->len >= 0x20)
            {
                const quint32 packetLength = in->len;
                if (packetLength != quint32(r.length()))
                {
                    qWarning(logUdp()) << "Dropping audio datagram with mismatched length: header" << packetLength
                                       << "actual" << r.length();
                    break;
                }

                const audio_packet* audioIn = reinterpret_cast<const audio_packet*>(r.constData());
                const int payloadLength = r.length() - AUDIO_SIZE;
                const quint16 declaredPayloadLength = qFromBigEndian(audioIn->datalen);
                if (declaredPayloadLength != payloadLength)
                {
                    qWarning(logUdp()) << "Dropping audio datagram with mismatched payload length: header"
                                       << declaredPayloadLength << "actual" << payloadLength;
                    break;
                }

                if (in->seq == 0)
                {
                    seqPrefix++;
                }

                // Log the first few RX audio packet headers per stream instance
                // so reconnects get fresh diagnostics.
                if (++rxPacketDiagnosticsCount <= 3)
                {
                    qDebug(logUdp()) << "RX audio hdr: type=" << Qt::hex << in->type << "len=" << in->len
                                     << "seq=" << in->seq << "data_len=" << (r.length() - 0x18)
                                     << "hdr=" << r.left(0x18).toHex(' ');
                }

                if (rxAudioThread == nullptr && m_audioReady)
                {
                    startAudio();
                }

                const int excess = pingLatenessMs - (pingBaselineMs + rxSetup.latency);

                if (excess > 0)
                {
                    qDebug(logUdp()) << "Audio latency high:"
                                     << "lateness" << pingLatenessMs << "baseline" << pingBaselineMs << "excess"
                                     << excess;

                    if (++latencyCounter > 5)
                    {
                        qInfo(logUdp()) << "Latency sustained -> flushing audio";
                        latencyCounter = 0;
                        break;
                    }
                }
                else
                {
                    latencyCounter = 0;
                }
                audioPacket tempAudio;
                tempAudio.seq = (quint32(seqPrefix) << 16) | quint32(in->seq);
                tempAudio.time = QTime::currentTime();
                tempAudio.sent = 0;
                tempAudio.data = r.mid(0x18);

                emit haveAudioData(tempAudio);
                markPacketReceived();
            }
            break;
        }
        }

        UdpBase::dataReceived(r);
        r.clear();
        datagram.clear();
    }
}

void UdpAudio::enableAudio()
{
    m_audioReady = true;
    if (rxAudioThread == nullptr)
    {
        startAudio();
    }
}

void UdpAudio::startAudio()
{

    if (rxSetup.type == qtAudio)
    {
        rxaudio = new AudioHandlerQtOutput();
    }
    else
    {
        qCritical(logAudio()) << "Unsupported Receive Audio Handler selected! Only qtAudio is supported.";
        return;
    }

    rxAudioThread = new QThread(this);
    rxAudioThread->setObjectName("rxAudio()");

    rxaudio->moveToThread(rxAudioThread);

    rxAudioThread->start(QThread::TimeCriticalPriority);

    connect(this, &UdpAudio::setupRxAudio, rxaudio, &AudioHandlerBase::init);

    connect(this, &UdpAudio::haveAudioData, rxaudio, &AudioHandlerBase::incomingAudio);
    connect(this, &UdpAudio::haveChangeLatency, rxaudio, &AudioHandlerBase::changeLatency);
    connect(this, &UdpAudio::haveSetVolume, rxaudio, &AudioHandlerBase::setVolume);
    connect(rxaudio, &AudioHandlerBase::haveLevels, this, &UdpAudio::getRxLevels);
    connect(rxAudioThread, &QThread::finished, rxaudio, &QObject::deleteLater);
    connect(rxaudio, &AudioHandlerBase::initFailed, this, &UdpAudio::onRxAudioInitFailed);

    sendControl(false, 0x03, 0x00);

    if (pingTimer != nullptr)
    {
        pingTimer->stop();
        pingTimer->deleteLater();
    }
    pingTimer = new QTimer(this);
    connect(pingTimer, &QTimer::timeout, this, &UdpBase::sendPing);
    pingTimer->start(PING_PERIOD);

    if (enableTx)
    {
        if (txSetup.type == qtAudio)
        {
            txaudio = new AudioHandlerQtInput();
        }
        else
        {
            qCritical(logAudio()) << "Unsupported Transmit Audio Handler selected! Only qtAudio is supported.";
            return;
        }

        txAudioThread = new QThread(this);
        txAudioThread->setObjectName("txAudio()");

        txaudio->moveToThread(txAudioThread);

        txAudioThread->start(QThread::TimeCriticalPriority);

        connect(this, &UdpAudio::setupTxAudio, txaudio, &AudioHandlerBase::init);
        connect(txaudio, &AudioHandlerBase::haveAudioData, this, &UdpAudio::receiveAudioData);
        connect(txaudio, &AudioHandlerBase::haveLevels, this, &UdpAudio::getTxLevels);

        connect(txAudioThread, &QThread::finished, txaudio, &QObject::deleteLater);
        connect(txaudio, &AudioHandlerBase::initFailed, this, &UdpAudio::onTxAudioInitFailed);

        emit setupTxAudio(txSetup);
    }

    emit setupRxAudio(rxSetup);
}

void UdpAudio::setTxActive(bool active)
{
    m_txActive.store(active);
    if (!active)
    {
        m_dtmfTimer->stop();
        m_dtmfTimerActive = false;
        m_dtmfPcm.clear();
        m_dtmfPcmOffset = 0;
    }
    qDebug(logUdp()) << "UdpAudio: TX audio" << (active ? "ENABLED (PTT on)" : "DISABLED (PTT off)");
}

void UdpAudio::onRxAudioInitFailed()
{
    qWarning(logAudio()) << "RX Audio Initialization failed. Cleaning up.";
    if (rxAudioThread)
    {
        rxAudioThread->quit();
        // Detach from parent and wire self-delete so the thread cleans up
        // asynchronously. Null the member so dataReceived() can call startAudio()
        // again if a new stream arrives.
        rxAudioThread->setParent(nullptr);
        connect(rxAudioThread, &QThread::finished, rxAudioThread, &QObject::deleteLater);
        rxAudioThread = nullptr;
    }
    rxaudio = nullptr;
}

void UdpAudio::onTxAudioInitFailed()
{
    qWarning(logAudio()) << "TX Audio Initialization failed. Cleaning up.";
    if (txAudioThread)
    {
        txAudioThread->quit();
        txAudioThread->setParent(nullptr);
        connect(txAudioThread, &QThread::finished, txAudioThread, &QObject::deleteLater);
        txAudioThread = nullptr;
    }
    txaudio = nullptr;
}
