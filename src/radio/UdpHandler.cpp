#include "UdpHandler.h"
#include "AppInfo.h"
#include "LogCategories.h"

#include <QNetworkInterface>
#include <QRandomGenerator>
#include <algorithm>
#include <iterator>

namespace
{
constexpr quint32 kLoginErrorInvalidCredentials = 0xfeffffff;

template <size_t N> void copyPacketField(char (&destination)[N], const QByteArray& source)
{
    const int length = qMin<int>(static_cast<int>(N), source.size());
    if (length > 0)
    {
        memcpy(destination, source.constData(), length);
    }
}

template <size_t N> void copyPacketField(char (&destination)[N], const QString& source)
{
    copyPacketField(destination, source.toLatin1());
}

QString boundedLatin1(const char* s, int maxLen)
{
    return QString::fromLatin1(parseNullTerminatedString(QByteArray::fromRawData(s, maxLen), 0));
}
} // namespace

UdpHandler::UdpHandler(UdpConnectionSettings settings, audioSetup rxAudio, audioSetup txAudio)
    : controlPort(settings.controlLANPort),
      civPort(0),
      audioPort(0),
      civLocalPort(0),
      audioLocalPort(0),
      rxSetup(rxAudio),
      txSetup(txAudio)
{
    this->port = this->controlPort;
    this->username = settings.username;
    passcode(settings.username, usernameEncoded);
    passwordEncoded = settings.passwordEncoded;
    this->compName = settings.clientName.isEmpty() ? APP_NAME : (settings.clientName.mid(0, 8) + "-" + APP_NAME);
    std::fill(std::begin(audioLevelsTxPeak), std::end(audioLevelsTxPeak), 0);
    std::fill(std::begin(audioLevelsRxPeak), std::end(audioLevelsRxPeak), 0);
    std::fill(std::begin(audioLevelsTxRMS), std::end(audioLevelsTxRMS), 0);
    std::fill(std::begin(audioLevelsRxRMS), std::end(audioLevelsRxRMS), 0);

    qInfo(logUdp()) << "Starting UdpHandler user:" << username << " rx latency:" << rxSetup.latency
                    << " tx latency:" << txSetup.latency << " rx sample rate: " << rxSetup.sampleRate
                    << " rx codec: " << rxSetup.codec << " tx sample rate: " << txSetup.sampleRate
                    << " tx codec: " << txSetup.codec;

    // Use numeric IPv4 addresses only here. Synchronous DNS lookup can stall
    // connection setup; if hostname support is needed later, resolve it before
    // constructing UdpHandler with QHostInfo::lookupHost().
    if (!radioIP.setAddress(settings.ipAddress))
    {
        qWarning(logUdp()) << "Invalid radio IPv4 address:" << settings.ipAddress;
        return;
    }

    // The IC-9700 LAN handshake needs the local IPv4 address encoded in its
    // session ID. Prefer an address on the same subnet as the radio, then fall
    // back to the first active non-loopback IPv4 address.
    QHostAddress fallbackLocalIp;
    const quint32 radioIpv4 = radioIP.toIPv4Address();
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces())
    {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning) ||
            flags.testFlag(QNetworkInterface::IsLoopBack))
        {
            continue;
        }

        for (const QNetworkAddressEntry& entry : iface.addressEntries())
        {
            const QHostAddress address = entry.ip();
            if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isLoopback())
            {
                continue;
            }

            if (fallbackLocalIp.isNull())
            {
                fallbackLocalIp = address;
            }

            const quint32 mask = entry.netmask().toIPv4Address();
            if (mask != 0 && (address.toIPv4Address() & mask) == (radioIpv4 & mask))
            {
                localIP = address;
                break;
            }
        }

        if (!localIP.isNull())
        {
            break;
        }
    }

    if (localIP.isNull())
    {
        localIP = fallbackLocalIp;
    }
}

void UdpHandler::init()
{
    UdpBase::init(0);

    QUdpSocket::connect(udp, &QUdpSocket::readyRead, this, &UdpHandler::dataReceived);

    tokenTimer = new QTimer(this);
    areYouThereTimer = new QTimer(this);
    pingTimer = new QTimer(this);
    idleTimer = new QTimer(this);

    connect(tokenTimer, &QTimer::timeout, this, std::bind(&UdpHandler::sendToken, this, 0x05));
    connect(areYouThereTimer, &QTimer::timeout, this, &UdpHandler::sendAreYouThere);
    connect(pingTimer, &QTimer::timeout, this, &UdpBase::sendPing);
    connect(idleTimer, &QTimer::timeout, this, std::bind(&UdpBase::sendControl, this, true, 0, 0));

    // Probe until the IC-9700 replies with "I am here".
    areYouThereTimer->start(AREYOUTHERE_PERIOD);
}

void UdpHandler::shutdown()
{
    qDebug(logUdp()) << "[SHUTDOWN] UdpHandler::shutdown() enter";

    // Stop all timers before deleting the UDP socket. Any timer that fires
    // after udp is deleted would call sendPing()/sendControl() on a null
    // pointer. pingTimer and idleTimer connect to UdpBase methods that
    // dereference udp unconditionally.
    if (pingTimer)
    {
        pingTimer->stop();
    }
    if (idleTimer)
    {
        idleTimer->stop();
    }
    if (watchdogTimer)
    {
        watchdogTimer->stop();
    }
    if (retransmitTimer)
    {
        retransmitTimer->stop();
    }
    if (tokenTimer)
    {
        tokenTimer->stop();
    }
    if (areYouThereTimer)
    {
        areYouThereTimer->stop();
    }

    m_shuttingDown = true;
    m_disconnectStatusReceived = false;

    // Delete audio/civ sub-objects; their destructors close their own
    // sockets, dispose audio handlers, and wait on audio threads.
    if (streamOpened)
    {
        if (civ != nullptr)
        {
            civ->closeStream();
        }
        if (audio != nullptr)
        {
            qDebug(logUdp()) << "[SHUTDOWN] deleting audio ...";
            delete audio;
            audio = nullptr;
            qDebug(logUdp()) << "[SHUTDOWN] audio deleted";
        }
        if (civ != nullptr)
        {
            qDebug(logUdp()) << "[SHUTDOWN] deleting civ ...";
            delete civ;
            civ = nullptr;
            qDebug(logUdp()) << "[SHUTDOWN] civ deleted";
        }
        qInfo(logUdp()) << "Sending token removal packet";
        sendToken(0x01);
        waitForDisconnectStatus(500);
        streamOpened = false;
    }

    if (civPortReservation)
    {
        civPortReservation->close();
        delete civPortReservation;
        civPortReservation = nullptr;
    }
    if (audioPortReservation)
    {
        audioPortReservation->close();
        delete audioPortReservation;
        audioPortReservation = nullptr;
    }

    // Close this handler's UDP socket so readyRead signals stop before shutdown returns.
    if (udp != nullptr)
    {
        qDebug(logUdp()) << "[SHUTDOWN] closing handler UDP socket";
        udp->close();
        delete udp;
        udp = nullptr;
    }
    usernameEncoded.fill('\0');
    usernameEncoded.clear();
    passwordEncoded.fill('\0');
    passwordEncoded.clear();
    m_shuttingDown = false;

    qDebug(logUdp()) << "[SHUTDOWN] UdpHandler::shutdown() complete";
    emit shutdownFinished();
}

UdpHandler::~UdpHandler()
{
    // Ensure the control socket is closed even if shutdown() was skipped.
    if (streamOpened)
    {
        if (audio != nullptr)
        {
            delete audio;
            audio = nullptr;
        }

        if (civ != nullptr)
        {
            civ->closeStream();
            delete civ;
            civ = nullptr;
        }

        qInfo(logUdp()) << "Sending token removal packet";
        sendToken(0x01);
    }
    if (civPortReservation)
    {
        delete civPortReservation;
        civPortReservation = nullptr;
    }
    if (audioPortReservation)
    {
        delete audioPortReservation;
        audioPortReservation = nullptr;
    }
    usernameEncoded.fill('\0');
    passwordEncoded.fill('\0');
}

void UdpHandler::changeLatency(quint16 value)
{
    emit haveChangeLatency(value);
}

void UdpHandler::setVolume(quint8 value)
{
    emit haveSetVolume(value);
}

void UdpHandler::enableAudio()
{
    if (audio != nullptr)
    {
        audio->enableAudio();
    }
}

void UdpHandler::setPttActive(bool active)
{
    // UdpHandler and UdpAudio live on udpHandlerThread, so the handoff to
    // UdpAudio is direct once this slot is invoked by Commander.
    if (audio != nullptr)
    {
        audio->setTxActive(active);
    }
}

void UdpHandler::queueDtmfPcm(const QByteArray& pcm)
{
    if (audio != nullptr)
    {
        audio->queueDtmfPcm(pcm);
    }
}

void UdpHandler::receiveFromCivStream(const QByteArray& data)
{
    emit haveDataFromPort(data);
}

void UdpHandler::receiveAudioData(const audioPacket& data)
{
    emit haveAudioData(data);
}

void UdpHandler::receiveDataFromUserToRadio(QByteArray data)
{
    if (civ != nullptr)
    {
        qDebug(logUdp()) << "receiveDataFromUserToRadio: civ non-null, civPort=" << civPort
                         << "data=" << data.toHex(' ');
        civ->send(data);
    }
    else
    {
        qDebug(logUdp()) << "receiveDataFromUserToRadio: civ IS NULL, dropping data=" << data.toHex(' ');
    }
}

void UdpHandler::getRxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under,
                             bool over)
{
    status.rxAudioLevel = amplitudePeak;
    status.rxLatency = latency;
    status.rxCurrentLatency = qint32(current);
    status.rxUnderrun = under;
    status.rxOverrun = over;
    audioLevelsRxPeak[(audioLevelsRxPosition) % audioLevelBufferSize] = amplitudePeak;
    audioLevelsRxRMS[(audioLevelsRxPosition) % audioLevelBufferSize] = amplitudeRMS;

    if ((audioLevelsRxPosition) % 4 == 0)
    {
        // Emit a short rolling summary instead of every audio level sample.
        quint8 meanPeak = findMax(audioLevelsRxPeak);
        quint8 meanRMS = findMean(audioLevelsRxRMS);
        networkAudioLevels l;
        l.haveRxLevels = true;
        l.rxAudioPeak = meanPeak;
        l.rxAudioRMS = meanRMS;
        emit haveNetworkAudioLevels(l);
    }
    audioLevelsRxPosition++;
}

void UdpHandler::getTxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under,
                             bool over)
{
    status.txAudioLevel = amplitudePeak;
    status.txLatency = latency;
    status.txCurrentLatency = qint32(current);
    status.txUnderrun = under;
    status.txOverrun = over;
    audioLevelsTxPeak[(audioLevelsTxPosition) % audioLevelBufferSize] = amplitudePeak;
    audioLevelsTxRMS[(audioLevelsTxPosition) % audioLevelBufferSize] = amplitudeRMS;

    if ((audioLevelsTxPosition) % 4 == 0)
    {
        // Emit a short rolling summary instead of every audio level sample.
        quint8 meanPeak = findMax(audioLevelsTxPeak);
        quint8 meanRMS = findMean(audioLevelsTxRMS);
        networkAudioLevels l;
        l.haveTxLevels = true;
        l.txAudioPeak = meanPeak;
        l.txAudioRMS = meanRMS;
        emit haveNetworkAudioLevels(l);
    }
    audioLevelsTxPosition++;
}

quint8 UdpHandler::findMean(quint8* d)
{
    unsigned int sum = 0;
    for (int p = 0; p < audioLevelBufferSize; p++)
    {
        sum += d[p];
    }
    return sum / audioLevelBufferSize;
}

quint8 UdpHandler::findMax(quint8* d)
{
    unsigned int max = 0;
    for (int p = 0; p < audioLevelBufferSize; p++)
    {
        if (d[p] > max)
        {
            max = d[p];
        }
    }
    return max;
}

void UdpHandler::dataReceived()
{
    while (udp->hasPendingDatagrams())
    {
        markPacketReceived();
        QNetworkDatagram datagram = udp->receiveDatagram();
        QByteArray r = datagram.data();

        switch (r.length())
        {
        case (CONTROL_SIZE):
        {
            const control_packet* in = reinterpret_cast<const control_packet*>(r.constData());
            if (in->type == 0x04)
            {
                qInfo(logUdp()) << this->metaObject()->className()
                                << ": Received I am here from: " << datagram.senderAddress().toString();

                if (areYouThereTimer->isActive())
                {
                    areYouThereTimer->stop();
                    pingTimer->start(PING_PERIOD);
                    idleTimer->start(IDLE_PERIOD);
                }
            }
            else if (in->type == 0x06)
            {
                qInfo(logUdp()) << this->metaObject()->className() << ": Received I am ready";
                sendLogin();
            }
            break;
        }
        case (PING_SIZE):
        {
            const ping_packet* in = reinterpret_cast<const ping_packet*>(r.constData());
            if (in->type == 0x07 && in->reply == 0x01 && streamOpened)
            {
                status.networkLatency += elapsedMs() - lastPingSentMs;
                status.networkLatency /= 2;
                status.packetsSent = packetsSent;
                status.packetsLost = packetsLost;
                if (audio != nullptr)
                {
                    status.packetsSent = status.packetsSent + audio->packetsSentCount();
                    status.packetsLost = status.packetsLost + audio->packetsLostCount();
                }
                if (civ != nullptr)
                {
                    status.packetsSent = status.packetsSent + civ->packetsSentCount();
                    status.packetsLost = status.packetsLost + civ->packetsLostCount();
                }

                if (status.rxCurrentLatency <= qint32(status.rxLatency) && !status.rxUnderrun && !status.rxOverrun)
                {
                    status.rxLatencyClass = QStringLiteral("normal");
                }
                else if (status.rxUnderrun)
                {
                    status.rxLatencyClass = QStringLiteral("underrun");
                }
                else if (status.rxOverrun)
                {
                    status.rxLatencyClass = QStringLiteral("overrun");
                }
                else
                {
                    status.rxLatencyClass = QStringLiteral("buffering");
                }
                QString txString;
                if (txSetup.codec == 0)
                {
                    txString = "(no tx)";
                }
                status.message = QString("%1 rx latency: %2 ms / rtt: %3 ms / loss: %4/%5")
                                     .arg(txString)
                                     .arg(status.rxCurrentLatency, 3)
                                     .arg(status.networkLatency, 3)
                                     .arg(status.packetsLost, 3)
                                     .arg(status.packetsSent, 3);
                status.userVisibleMessage = false;
                status.timeDifference = audio != nullptr ? audio->getTimeDifference() : 0;
                emit haveNetworkStatus(status);
            }
            break;
        }
        case (TOKEN_SIZE):
        {
            const token_packet* in = reinterpret_cast<const token_packet*>(r.constData());
            if (in->requesttype == 0x05 && in->requestreply == 0x02 && in->type != 0x01)
            {
                if (in->response == 0x0000)
                {
                    qDebug(logUdp()) << this->metaObject()->className() << ": Token renewal successful";
                    tokenTimer->start(TOKEN_RENEWAL);
                    gotAuthOK = true;
                    if (!streamOpened)
                    {
                        sendRequestStream();
                    }
                }
                else if (in->response == 0xffffffff)
                {
                    qWarning(logUdp()) << this->metaObject()->className()
                                       << ": Radio rejected token renewal, performing login";
                    remoteId = in->sentid;
                    tokRequest = in->tokrequest;
                    token = in->token;
                    streamOpened = false;
                    sendLogin();
                }
                else
                {
                    qWarning(logUdp()) << this->metaObject()->className() << ": Unknown response to token renewal? "
                                       << in->response;
                }
            }
            else if (in->requesttype == 0x01 && in->requestreply == 0x02 && in->type != 0x01)
            {
                m_disconnectStatusReceived = true;
                qInfo(logUdp()) << this->metaObject()->className()
                                << ": Token removal acknowledged, response=" << Qt::hex << in->response;
            }
            break;
        }
        case (STATUS_SIZE):
        {
            const status_packet* in = reinterpret_cast<const status_packet*>(r.constData());
            if (in->type != 0x01)
            {
                if (in->error == 0xffffffff && !streamOpened)
                {
                    emit haveNetworkError(errorType(true, radioIP.toString(),
                                                    "Connection failed\ntry rebooting the radio.",
                                                    ErrorCode::ConnectionFailed));
                    qInfo(logUdp()) << this->metaObject()->className()
                                    << ": Connection failed, wait a few minutes or reboot the radio.";
                }
                else if (in->error == 0x00000000 && in->disc == 0x01)
                {
                    m_disconnectStatusReceived = true;
                    if (!m_shuttingDown)
                    {
                        emit haveNetworkError(
                            errorType(false, radioIP.toString(), "Got radio disconnected.", ErrorCode::Disconnected));
                    }
                    qInfo(logUdp()) << this->metaObject()->className() << ": Got radio disconnected.";
                    if (streamOpened)
                    {
                        // Close stream sockets while keeping control login state alive.
                        if (audio != nullptr)
                        {
                            delete audio;
                            audio = nullptr;
                        }

                        if (civ != nullptr)
                        {
                            delete civ;
                            civ = nullptr;
                        }

                        streamOpened = false;
                    }
                }
                else
                {
                    civPort = qFromBigEndian(in->civport);
                    audioPort = qFromBigEndian(in->audioport);
                    qInfo(logUdp()) << "Connection: error="
                                    << QString("0x%1").arg(qFromBigEndian(in->error), 8, 16, QChar('0'))
                                    << "disc=" << quint8(in->disc) << "civPort=" << civPort << "audioPort=" << audioPort
                                    << "rx codec=" << rxSetup.codec << "tx codec=" << txSetup.codec;
                    if (civPort == 0 || audioPort == 0)
                    {
                        qWarning(logUdp()) << "Radio returned invalid stream ports; refusing to open CIV/audio streams";
                        emit haveNetworkError(errorType(true, radioIP.toString(),
                                                        "Connection failed: radio returned invalid UDP stream ports.",
                                                        ErrorCode::ConnectionFailed));
                        if (civPortReservation)
                        {
                            civPortReservation->close();
                            delete civPortReservation;
                            civPortReservation = nullptr;
                        }
                        if (audioPortReservation)
                        {
                            audioPortReservation->close();
                            delete audioPortReservation;
                            audioPortReservation = nullptr;
                        }
                        streamOpened = false;
                        break;
                    }
                    if (!streamOpened)
                    {

                        if (civPortReservation)
                        {
                            civPortReservation->close();
                            delete civPortReservation;
                            civPortReservation = nullptr;
                        }
                        civ = new UdpCivData(localIP, radioIP, civPort, civLocalPort);
                        QObject::connect(civ, &UdpCivData::receive, this, &UdpHandler::receiveFromCivStream);
                        streamOpened = true;
                        emit streamReady();

                        if (txSampleRates < 2)
                        {
                            txSetup.sampleRate = 0;
                            txSetup.codec = 0;
                        }
                    }
                    if (audio == nullptr)
                    {
                        if (audioPortReservation)
                        {
                            audioPortReservation->close();
                            delete audioPortReservation;
                            audioPortReservation = nullptr;
                        }
                        audio = new UdpAudio(localIP, radioIP, audioPort, audioLocalPort, rxSetup, txSetup);

                        QObject::connect(audio, &UdpAudio::haveAudioData, this, &UdpHandler::receiveAudioData);
                        QObject::connect(this, &UdpHandler::haveChangeLatency, audio, &UdpAudio::changeLatency);
                        QObject::connect(this, &UdpHandler::haveSetVolume, audio, &UdpAudio::setVolume);
                        QObject::connect(audio, &UdpAudio::haveRxLevels, this, &UdpHandler::getRxLevels);
                        QObject::connect(audio, &UdpAudio::haveTxLevels, this, &UdpHandler::getTxLevels);
                    }

                    qInfo(logUdp()) << this->metaObject()->className()
                                    << "Got serial and audio request success, device name: " << devName;
                }
            }
            break;
        }
        case (LOGIN_RESPONSE_SIZE):
        {
            const login_response_packet* in = reinterpret_cast<const login_response_packet*>(r.constData());
            if (in->type != 0x01)
            {

                m_connectionType = in->connection;
                qInfo(logUdp()) << "Got connection type:" << m_connectionType;
                // IC-9700 accepts mono LPCM16 for LAN audio; Qt audio handlers
                // convert to/from the local device channel layout.
                static constexpr quint8 kLpcmMono16 = 0x04;
                if (rxSetup.codec >= 0x40 || txSetup.codec >= 0x40)
                {
                    emit haveNetworkError(
                        errorType(QString("UDP"), QString("Opus codec not supported, forcing LPCM16")));
                    if (rxSetup.codec >= 0x40)
                    {
                        rxSetup.codec = kLpcmMono16;
                    }
                    if (txSetup.codec >= 0x40)
                    {
                        txSetup.codec = kLpcmMono16;
                    }
                }

                if (in->error == kLoginErrorInvalidCredentials)
                {
                    emit haveNetworkError(
                        errorType(true, radioIP.toString(), "Invalid Username/Password", ErrorCode::AuthFailure));
                    qInfo(logUdp()) << this->metaObject()->className() << ": Invalid Username/Password";
                }
                else if (!isAuthenticated)
                {

                    if (in->tokrequest == tokRequest)
                    {
                        status.message = "Radio Login OK!";
                        qInfo(logUdp()) << this->metaObject()->className() << ": Received matching token response";
                        token = in->token;
                        sendToken(0x02);
                        tokenTimer->start(TOKEN_RENEWAL);
                        isAuthenticated = true;
                    }
                    else
                    {
                        qInfo(logUdp()) << this->metaObject()->className()
                                        << ": Token response did not match, sent:" << tokRequest << " got "
                                        << in->tokrequest;
                    }
                }

                qInfo(logUdp()) << this->metaObject()->className() << ": Detected connection speed " << in->connection;
            }
            break;
        }
        case (CONNINFO_SIZE):
        {
            // Connection status for one radio advertised by the IC-9700 LAN server.

            const conninfo_packet* in = reinterpret_cast<const conninfo_packet*>(r.constData());
            QHostAddress ip = QHostAddress(qToBigEndian(in->ipaddress));

            qInfo(logUdp()) << "Got Connection status for:" << in->name << "Busy:" << in->busy << "Computer"
                            << in->computer << "IP" << ip.toString();

            // Match the status packet to a previously advertised radio.
            for (quint8 f = 0; f < radios.size(); f++)
            {

                if ((radios[f].commoncap == 0x8010 && radios[f].macaddress[0] == in->macaddress[0] &&
                     radios[f].macaddress[1] == in->macaddress[1] && radios[f].macaddress[2] == in->macaddress[2] &&
                     radios[f].macaddress[3] == in->macaddress[3] && radios[f].macaddress[4] == in->macaddress[4] &&
                     radios[f].macaddress[5] == in->macaddress[5]) ||
                    !memcmp(radios[f].guid, in->guid, GUIDLEN))
                {

                    bool admin = false;
                    if (in->busy && in->computer[0] != '\x0')
                    {
                        admin = true;
                    }

                    qDebug(logUdp()) << "Is the user an admin? " << admin;
                    emit setRadioUsage(f, admin, in->busy, boundedLatin1(in->computer, sizeof(in->computer)),
                                       ip.toString());
                    qDebug(logUdp()) << "Set radio usage num:" << f << boundedLatin1(in->name, sizeof(in->name))
                                     << "Busy:" << in->busy << "Computer"
                                     << boundedLatin1(in->computer, sizeof(in->computer)) << "IP" << ip.toString();
                }
            }

            if (!streamOpened && radios.size() == 1)
            {

                qDebug(logUdp()) << "Single radio available, can I connect to it?";

                if (in->busy)
                {
                    const QString inComputer = boundedLatin1(in->computer, sizeof(in->computer));
                    if (in->ipaddress != 0x00 && inComputer != compName)
                    {
                        networkStatus busyStatus = status;
                        busyStatus.message =
                            "Waiting for " + devName + "; in use by " + inComputer + " (" + ip.toString() + ")";
                        busyStatus.userVisibleMessage = true;
                        emit haveNetworkStatus(busyStatus);
                        sendControl(false, 0x00, in->seq); // Respond with an idle
                    }
                    else if (inComputer != compName)
                    {
                        networkStatus busyStatus = status;
                        busyStatus.message = "Waiting for " + devName + "; in use by another station";
                        busyStatus.userVisibleMessage = true;
                        emit haveNetworkStatus(busyStatus);
                        sendControl(false, 0x00, in->seq); // Respond with an idle
                    }
                    else
                    {
                        // Already connected as this SDR9700 client; no stream
                        // request is needed for the duplicate status packet.
                    }
                }
                else
                {
                    qDebug(logUdp()) << "Attempting to connect to radio";
                    networkStatus availableStatus = status;
                    availableStatus.message = devName + " available; connecting";
                    availableStatus.userVisibleMessage = true;
                    emit haveNetworkStatus(availableStatus);

                    setCurrentRadio(0);
                }
            }
            else if (streamOpened)
            {
                // Status refresh while the stream is already open.
            }
            break;
        }

        default:
        {
            if (r.length() < CAPABILITIES_SIZE)
            {
                qWarning(logUdp()) << "Ignoring short UDP datagram that is not a known fixed packet:" << r.length();
                break;
            }

            const capabilities_packet* in = reinterpret_cast<const capabilities_packet*>(r.constData());
            const int capabilityBytes = r.length() - CAPABILITIES_SIZE;
            if (capabilityBytes % RADIO_CAP_SIZE != 0)
            {
                qWarning(logUdp()) << "Ignoring malformed capabilities packet length:" << r.length();
                break;
            }

            const int availableRadios = capabilityBytes / RADIO_CAP_SIZE;
            const int advertisedRadios = qFromBigEndian(in->numradios);
            const int radioCount = qMin(advertisedRadios, availableRadios);
            if (advertisedRadios != availableRadios)
            {
                qWarning(logUdp()) << "Capabilities radio count mismatch, advertised" << advertisedRadios << "contains"
                                   << availableRadios;
            }

            radios.clear();
            numRadios = static_cast<quint8>(qMin(radioCount, 255));

            for (int i = 0; i < radioCount; ++i)
            {
                radio_cap_packet rad;
                const char* tmpRad = r.constData();
                memcpy(&rad, tmpRad + CAPABILITIES_SIZE + i * RADIO_CAP_SIZE, RADIO_CAP_SIZE);
                radios.append(rad);
                qInfo(logUdp()) << this->metaObject()->className()
                                << QString("Received radio capabilities, Name: %1, Audio: %2, CIV: %3, MAC: "
                                           "%4:%5:%6:%7:%8:%9 CAPF: %10")
                                       .arg(boundedLatin1(rad.name, sizeof(rad.name)))
                                       .arg(boundedLatin1(rad.audio, sizeof(rad.audio)))
                                       .arg((quint8)rad.civ, 2, 16, QChar('0'))
                                       .arg(rad.macaddress[0], 2, 16, QChar('0'))
                                       .arg(rad.macaddress[1], 2, 16, QChar('0'))
                                       .arg(rad.macaddress[2], 2, 16, QChar('0'))
                                       .arg(rad.macaddress[3], 2, 16, QChar('0'))
                                       .arg(rad.macaddress[4], 2, 16, QChar('0'))
                                       .arg(rad.macaddress[5], 2, 16, QChar('0'))
                                       .arg(rad.capf, 4, 16, QChar('0'));
            }

            emit requestRadioSelection(radios);

            break;
        }
        }
        UdpBase::dataReceived(r);
        r.clear();
        datagram.clear();
    }
    return;
}

void UdpHandler::setCurrentRadio(quint8 radio)
{
    if (radio >= radios.size())
    {
        qWarning(logUdp()) << "Ignoring invalid radio selection" << radio << "available radios" << radios.size();
        emit haveNetworkError(
            errorType(false, radioIP.toString(), "Invalid radio selection.", ErrorCode::InvalidRadio));
        return;
    }

    // Tear down any previous stream before selecting a radio.
    if (audio != nullptr)
    {
        delete audio;
        audio = nullptr;
    }

    if (civ != nullptr)
    {
        delete civ;
        civ = nullptr;
    }

    streamOpened = false;

    qInfo(logUdp()) << "Got Radio" << radio;
    qInfo(logUdp()) << "Find available local ports";

    // Reserve local CIV/audio ports before requesting the remote stream ports.
    if (civLocalPort == 0 || audioLocalPort == 0)
    {
        civPortReservation = new QUdpSocket(this);
        audioPortReservation = new QUdpSocket(this);
        if (!civPortReservation->bind() || !audioPortReservation->bind())
        {
            qWarning(logUdp()) << "Unable to reserve local CIV/audio UDP ports";
            delete civPortReservation;
            civPortReservation = nullptr;
            delete audioPortReservation;
            audioPortReservation = nullptr;
            emit haveNetworkError(errorType(false, radioIP.toString(), "Unable to reserve local UDP ports.",
                                            ErrorCode::PortReservationFailed));
            return;
        }
        civLocalPort = civPortReservation->localPort();
        audioLocalPort = audioPortReservation->localPort();
    }
    int baudrate = qFromBigEndian(radios[radio].baudrate);
    emit haveBaudRate(baudrate);

    if (radios[radio].commoncap == 0x8010)
    {
        memcpy(&macaddress, radios[radio].macaddress, sizeof(macaddress));
        useGuid = false;
    }
    else
    {
        useGuid = true;
        memcpy(&guid, radios[radio].guid, GUIDLEN);
    }

    devName = radios[radio].name;
    audioType = radios[radio].audio;
    civId = radios[radio].civ;
    rxSampleRates = radios[radio].rxsample;
    txSampleRates = radios[radio].txsample;
    if (txSampleRates < 2)
    {
        txSetup.sampleRate = 0;
        txSetup.codec = 0;
    }

    sendRequestStream();
}

void UdpHandler::sendRequestStream()
{
    conninfo_packet p;
    memset(p.packet, 0x0, sizeof(p));
    p.len = sizeof(p);
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.payloadsize = qToBigEndian((quint32)(sizeof(p) - 0x10));
    p.requesttype = 0x03;
    p.requestreply = 0x01;

    if (!useGuid)
    {
        p.commoncap = 0x8010;
        memcpy(p.macaddress, macaddress, sizeof(p.macaddress));
    }
    else
    {
        memcpy(p.guid, guid, sizeof(p.guid));
    }
    p.innerseq = qToBigEndian(authSeq++);
    p.tokrequest = tokRequest;
    p.token = token;
    copyPacketField(p.name, devName);
    p.rxenable = 1;
    if (this->txSampleRates > 1 && txSetup.sampleRate > 0 && txSetup.codec != 0)
    {
        p.txenable = 1;
        p.txcodec = txSetup.codec;
        p.txsample = qToBigEndian((quint32)txSetup.sampleRate);
        p.txbuffer = qToBigEndian((quint32)txSetup.latency);
    }
    p.rxcodec = rxSetup.codec;
    copyPacketField(p.username, usernameEncoded);
    p.rxsample = qToBigEndian((quint32)rxSetup.sampleRate);
    p.civport = qToBigEndian((quint32)civLocalPort);
    p.audioport = qToBigEndian((quint32)audioLocalPort);
    p.convert = 1;
    QByteArray request = QByteArray::fromRawData(reinterpret_cast<const char*>(p.packet), sizeof(p));
    qInfo(logUdp()) << "Requesting stream: rx codec=" << quint8(p.rxcodec) << "rx sample=" << qFromBigEndian(p.rxsample)
                    << "tx enabled=" << quint8(p.txenable) << "tx codec=" << quint8(p.txcodec)
                    << "tx sample=" << qFromBigEndian(p.txsample) << "civ local port=" << civLocalPort
                    << "audio local port=" << audioLocalPort;
    sendTrackedPacket(request);
    return;
}

void UdpHandler::sendAreYouThere()
{
    if (areYouThereCounter == 20)
    {
        qInfo(logUdp()) << this->metaObject()->className() << ": Radio not responding.";
        status.message = "Radio not responding!";
        emit haveNetworkError(errorType(true, radioIP.toString(), "Connection failed: radio not responding.",
                                        ErrorCode::ConnectionFailed));
        areYouThereTimer->stop();
        return;
    }
    qInfo(logUdp()) << this->metaObject()->className() << ": Sending Are You There...";

    areYouThereCounter++;
    UdpBase::sendControl(false, 0x03, 0x00);
}

void UdpHandler::sendLogin()
{

    qInfo(logUdp()) << this->metaObject()->className() << ": Sending login packet";

    tokRequest = static_cast<quint16>(QRandomGenerator::global()->generate());

    login_packet p;
    memset(p.packet, 0x0, sizeof(p));
    p.len = sizeof(p);
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.payloadsize = qToBigEndian((quint32)(sizeof(p) - 0x10));
    p.requesttype = 0x00;
    p.requestreply = 0x01;

    p.innerseq = qToBigEndian(authSeq++);
    p.tokrequest = tokRequest;
    copyPacketField(p.username, usernameEncoded);
    copyPacketField(p.password, passwordEncoded);
    copyPacketField(p.name, compName);

    sendTrackedPacket(QByteArray::fromRawData(reinterpret_cast<const char*>(p.packet), sizeof(p)));
    return;
}

void UdpHandler::sendToken(uint8_t magic)
{
    qDebug(logUdp()) << this->metaObject()->className() << "Sending Token request: " << magic;

    token_packet p;
    memset(p.packet, 0x0, sizeof(p));
    p.len = sizeof(p);
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.payloadsize = qToBigEndian((quint32)(sizeof(p) - 0x10));
    p.requesttype = magic;
    p.requestreply = 0x01;
    p.innerseq = qToBigEndian(authSeq++);
    p.tokrequest = tokRequest;
    p.resetcap = qToBigEndian((quint16)0x0798);
    p.token = token;

    sendTrackedPacket(QByteArray::fromRawData(reinterpret_cast<const char*>(p.packet), sizeof(p)));
    return;
}

void UdpHandler::waitForDisconnectStatus(int timeoutMs)
{
    if (!udp)
    {
        return;
    }

    QElapsedTimer timer;
    timer.start();
    while (!m_disconnectStatusReceived && timer.elapsed() < timeoutMs)
    {
        if (!udp->waitForReadyRead(qMin(50, timeoutMs - static_cast<int>(timer.elapsed()))))
        {
            continue;
        }
        dataReceived();
    }

    if (!m_disconnectStatusReceived)
    {
        qWarning(logUdp()) << "Timed out waiting for radio disconnect acknowledgement after token removal";
    }
}
