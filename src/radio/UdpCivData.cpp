#include "UdpCivData.h"
#include "LogCategories.h"

UdpCivData::UdpCivData(QHostAddress local, QHostAddress ip, quint16 civPort, quint16 localPort)
{
    qInfo(logUdp()) << "Starting UdpCivData";
    localIP = local;
    port = civPort;
    radioIP = ip;

    UdpBase::init(localPort);

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
    connect(startCivDataTimer, &QTimer::timeout, this, std::bind(&UdpCivData::sendOpenClose, this, false));
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
            qInfo(logUdp()) << " CIV Watchdog: no CIV data received for 2s, requesting data start.";
            if (startCivDataTimer != nullptr)
            {
                startCivDataTimer->start(100);
            }
            m_watchdogAlerted = true;
        }
    }
    else
    {
        m_watchdogAlerted = false;
    }
}

void UdpCivData::send(QByteArray d)
{
    qDebug(logUdp()) << "UdpCivData::send() port=" << port << "radioIP=" << radioIP.toString() << "len=" << d.length()
                     << "data=" << d.toHex(' ');
    data_packet p;
    memset(p.packet, 0x0, sizeof(p));
    p.len = (quint32)sizeof(p) + d.length();
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.reply = (char)0xc1;
    p.datalen = d.length();
    p.sendseq = qToBigEndian(sendSeqB);

    QByteArray t = QByteArray::fromRawData(reinterpret_cast<const char*>(p.packet), sizeof(p));
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

    openclose_packet p;
    memset(p.packet, 0x0, sizeof(p));
    p.len = sizeof(p);
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.data = 0x01c0;
    p.sendseq = qToBigEndian(sendSeqB);
    p.magic = magic;

    sendSeqB++;

    sendTrackedPacket(QByteArray::fromRawData(reinterpret_cast<const char*>(p.packet), sizeof(p)));
    return;
}

void UdpCivData::dataReceived()
{
    while (udp->hasPendingDatagrams())
    {
        QNetworkDatagram datagram = udp->receiveDatagram();
        QByteArray r = datagram.data();

        qDebug(logUdp()) << "UdpCivData: rx len=" << r.length() << "hex=" << r.left(16).toHex(' ');
        switch (r.length())
        {
        case (CONTROL_SIZE):
        {
            const control_packet* in = reinterpret_cast<const control_packet*>(r.constData());
            qDebug(logUdp()) << "UdpCivData: control type=0x" << Qt::hex << (int)in->type;
            if (in->type == 0x04)
            {
                areYouThereTimer->stop();
            }
            else if (in->type == 0x06)
            {
                remoteId = in->sentid;
                // Request the CI-V data stream until the radio starts sending frames.
                sendOpenClose(false);
                if (startCivDataTimer != nullptr)
                {
                    startCivDataTimer->start(100);
                }
            }
            break;
        }
        default:
        {
            if (r.length() > 21)
            {
                const ping_packet* in = reinterpret_cast<const ping_packet*>(r.constData());
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
                    markPacketReceived();
                    const int marker = r.indexOf(QByteArrayLiteral("\x27\x00\x00"));
                    const int pos = marker >= 0 ? marker + 2 : -1;
                    const int len = pos >= 0 ? r.mid(pos).indexOf(QByteArrayLiteral("\xfd")) : -1;
                    if (pos >= 0)
                    {
                        qInfo(logUdp()) << "CIV datagram: totalLen=" << r.length() << "scopeOffset=" << pos
                                        << "scopePayloadLen=" << len;
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
