#include "UdpCivData.h"
#include "LogCategories.h"

namespace
{
constexpr int kOpenStartRetryIntervalMs = 250;
constexpr int kOpenStartMaxAttempts = 12;

bool isScopeDataDatagram(const QByteArray& datagram)
{
    return datagram.size() > DATA_SIZE + 5 && static_cast<uchar>(datagram.at(DATA_SIZE + 4)) == 0x27 &&
           static_cast<uchar>(datagram.at(DATA_SIZE + 5)) == 0x00;
}
} // namespace

UdpCivData::UdpCivData(QHostAddress local, QHostAddress ip, quint16 civPort, quint16 localPort)
{
    qInfo(logUdp()) << "Starting UdpCivData";
    localIP = local;
    port = civPort;
    radioIP = ip;

    if (!UdpBase::init(localPort))
    {
        return;
    }

    QUdpSocket::connect(udp, &QUdpSocket::readyRead, this, &UdpCivData::dataReceived);

    sendControl(false, 0x03, 0x00);

    pingTimer = new QTimer(this);
    idleTimer = new QTimer(this);
    areYouThereTimer = new QTimer(this);
    startCivDataTimer = new QTimer(this);
    watchdogTimer = new QTimer(this);

    connect(pingTimer, &QTimer::timeout, this, &UdpBase::sendPing);
    connect(watchdogTimer, &QTimer::timeout, this, &UdpCivData::watchdog);
    connect(idleTimer, &QTimer::timeout, this, std::bind(&UdpBase::sendControl, this, true, 0, 0));
    connect(startCivDataTimer, &QTimer::timeout, this, &UdpCivData::requestDataStart);
    connect(areYouThereTimer, &QTimer::timeout, this, std::bind(&UdpBase::sendControl, this, false, 0x03, 0));
    watchdogTimer->start(WATCHDOG_PERIOD);
    // Ping and idle intervals use the LAN protocol constants from PacketTypes.h.
    // The are-you-there timer stops after the radio replies "I am here".
    pingTimer->start(PING_PERIOD);
    idleTimer->start(IDLE_PERIOD);
    areYouThereTimer->start(AREYOUTHERE_PERIOD);
}

UdpCivData::~UdpCivData()
{
    closeStream();
}

void UdpCivData::closeStream()
{
    if (m_closeSent)
    {
        return;
    }
    m_closeSent = true;
    if (startCivDataTimer)
    {
        startCivDataTimer->stop();
    }
    sendOpenClose(true);
}

void UdpCivData::watchdog()
{
    if (msSinceLastReceived() > 2000)
    {
        if (!m_watchdogAlerted)
        {
            if (m_readyEmitted)
            {
                // Once the CI-V stream has delivered its 0x06 ready handshake,
                // the radio should answer command traffic. During stale-session
                // recovery the first data-start packet can be lost or ignored;
                // retry it a bounded number of times instead of restarting the
                // old 100 ms forever-loop that could starve memory polling.
                qInfo(logUdp())
                    << " CIV Watchdog: no CI-V data received for 2s after stream ready; retrying data start.";
                if (startCivDataTimer != nullptr && !startCivDataTimer->isActive() &&
                    m_openStartRequestCount < kOpenStartMaxAttempts)
                {
                    startCivDataTimer->start(kOpenStartRetryIntervalMs);
                }
            }
            else
            {
                qInfo(logUdp()) << " CIV Watchdog: no CI-V data received for 2s, requesting data start.";
                if (startCivDataTimer != nullptr)
                {
                    startCivDataTimer->start(kOpenStartRetryIntervalMs);
                }
            }
            m_watchdogAlerted = true;
        }
    }
    else
    {
        m_watchdogAlerted = false;
        if (startCivDataTimer != nullptr)
        {
            startCivDataTimer->stop();
        }
    }
}

void UdpCivData::requestDataStart()
{
    if (m_closeSent)
    {
        if (startCivDataTimer != nullptr)
        {
            startCivDataTimer->stop();
        }
        return;
    }

    if (m_openStartRequestCount >= kOpenStartMaxAttempts)
    {
        qWarning(logUdp()) << "CI-V data-start request did not produce data after" << m_openStartRequestCount
                           << "attempts";
        if (startCivDataTimer != nullptr)
        {
            startCivDataTimer->stop();
        }
        return;
    }

    ++m_openStartRequestCount;
    sendOpenClose(false);
}

void UdpCivData::send(QByteArray d)
{
    qDebug(logUdp()) << "UdpCivData::send() port=" << port << "radioIP=" << radioIP.toString() << "len=" << d.length()
                     << "data=" << d.toHex(' ');
    data_packet p{};
    p.len = (quint32)sizeof(p) + d.length();
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.reply = (char)0xc1;
    p.datalen = d.length();
    p.sendseq = qToBigEndian(sendSeqB);

    QByteArray t = encodePacket(p);
    t.append(d);
    sendTrackedPacket(t);
    sendSeqB++;
    return;
}

void UdpCivData::sendOpenClose(bool close)
{
    uint8_t magic = 0x04;

    if (close)
    {
        magic = 0x00;
    }
    qDebug(logUdp()) << "UdpCivData::sendOpenClose close=" << close << "remoteId=0x" << Qt::hex << remoteId;

    openclose_packet p{};
    p.len = sizeof(p);
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.data = 0x01c0;
    p.sendseq = qToBigEndian(sendSeqB);
    p.magic = magic;

    sendSeqB++;

    sendTrackedPacket(encodePacket(p));
    return;
}

void UdpCivData::dataReceived()
{
    while (udp->hasPendingDatagrams())
    {
        QNetworkDatagram datagram = udp->receiveDatagram();
        if (!acceptDatagramFrom(datagram))
        {
            continue;
        }
        QByteArray r = datagram.data();

        const bool scopeDataDatagram = isScopeDataDatagram(r);
        switch (r.length())
        {
        case (CONTROL_SIZE):
        {
            const auto decoded = decodePacket<control_packet>(r);
            const control_packet* in = &*decoded;
            // Type 0 control packets are the steady-state LAN keepalive/ack
            // stream. Logging every one can overwhelm stderr and hide the
            // startup/memory-sync evidence we actually need in field logs.
            if (in->type != 0x00)
            {
                qDebug(logUdp()) << "UdpCivData: control type=0x" << Qt::hex << (int)in->type;
            }
            if (in->type == 0x04)
            {
                areYouThereTimer->stop();
            }
            else if (in->type == 0x06)
            {
                remoteId = in->sentid;
                // Request the CI-V data stream until the radio starts sending
                // frames. Packet captures from stale-session recovery showed
                // the radio can acknowledge 0x06 but ignore the first open
                // packet, so keep a bounded retry alive until real data arrives.
                m_openStartRequestCount = 0;
                requestDataStart();
                if (startCivDataTimer != nullptr && !startCivDataTimer->isActive())
                {
                    startCivDataTimer->start(kOpenStartRetryIntervalMs);
                }
                if (!m_readyEmitted)
                {
                    // UdpHandler must not announce streamReady before this
                    // point. CI-V command packets include remoteId; packets sent
                    // earlier are tracked with rcvdid=0 and can leave the radio
                    // repeatedly requesting retransmits instead of answering
                    // startup 03/04 frequency and mode probes.
                    m_readyEmitted = true;
                    emit ready();
                }
            }
            break;
        }
        default:
        {
            if (r.length() > 21)
            {
                const auto decoded = decodePacket<ping_packet>(r);
                const ping_packet* in = &*decoded;
                if (in->type != 0x01)
                {
                    if (in->len != quint32(r.length()))
                    {
                        qWarning(logUdp()) << "Dropping CI-V datagram with mismatched length: header" << in->len
                                           << "actual" << r.length();
                        break;
                    }
                    if (quint32(in->datalen) + DATA_SIZE != in->len)
                    {
                        qWarning(logUdp()) << "Dropping CI-V datagram with mismatched payload length: header"
                                           << in->datalen << "packet length" << in->len;
                        break;
                    }
                    // Stop start requests once valid CI-V data arrives.
                    if (startCivDataTimer != nullptr)
                    {
                        startCivDataTimer->stop();
                    }
                    m_openStartRequestCount = 0;
                    markPacketReceived();
                    // Large 0x27 scope data datagrams arrive continuously once
                    // the Spectrum Scope is enabled. Do not log them here; the
                    // scope parser and model are the right places to diagnose
                    // display behavior, and raw UDP logging can hide connection
                    // and memory-sync state.
                    if (!scopeDataDatagram)
                    {
                        qDebug(logUdp()) << "UdpCivData: rx len=" << r.length() << "hex=" << r.left(16).toHex(' ');
                    }
                    emit receive(r.mid(0x15));
                }
            }
            break;
        }
        }
        UdpBase::dataReceived(r);

        r.clear();
        datagram.clear();
    }
}
