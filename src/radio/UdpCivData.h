#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QHostInfo>
#include <QTimer>
#include <QMutex>
#include <QByteArray>
#include <QVector>
#include <QMap>
#include <QUuid>

#include <QtEndian>

#include <QBuffer>
#include <QThread>

#include <QDebug>

#include "PacketTypes.h"

#include "UdpBase.h"

class UdpCivData : public UdpBase
{
    Q_OBJECT

  public:
    UdpCivData(QHostAddress local, QHostAddress ip, quint16 civPort, bool splitWf, quint16 localPort);
    ~UdpCivData();

    void closeStream();

  signals:
    void receive(QByteArray);

  public slots:
    void send(QByteArray d);

  private:
    void watchdog();
    void dataReceived();
    void sendOpenClose(bool close);

    QTimer* startCivDataTimer = nullptr;
    bool splitWaterfall = false;
    bool m_watchdogAlerted = false;
    bool m_closeSent = false;
};
