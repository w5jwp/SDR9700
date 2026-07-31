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
    UdpCivData(QHostAddress local, QHostAddress ip, quint16 civPort, quint16 localPort);
    ~UdpCivData();

    void closeStream();
    void requestDataRestart();

  signals:
    void receive(QByteArray);
    void ready();

  public slots:
    void send(QByteArray d);

  private:
    void dataReceived();
    void requestDataStart();
    void sendOpenClose(bool close);

    QTimer* startCivDataTimer = nullptr;
    bool m_closeSent = false;
    bool m_readyEmitted = false;
    int m_openStartRequestCount = 0;
};
