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
#include "CivSequenceGate.h"

#include "UdpBase.h"

class UdpCivData : public UdpBase
{
    Q_OBJECT

  public:
    UdpCivData(QHostAddress local, QHostAddress ip, quint16 civPort, quint16 localPort,
               QUdpSocket* boundSocket = nullptr);
    ~UdpCivData();

    void closeStream();
    void requestDataRestart();

  signals:
    void receive(QByteArray);
    // The transport handshake is complete. UdpHandler opens the CI-V pipe
    // only after the companion audio transport reaches the same state.
    void ready();

  public slots:
    void send(QByteArray d);

  private:
    void dataReceived();
    void requestDataStart();
    void sendOpenClose(bool close);
    void deliverSequencedPayloads(const CivSequenceGateResult& result);

    QTimer* startCivDataTimer = nullptr;
    bool m_closeSent = false;
    bool m_readyEmitted = false;
    int m_openStartRequestCount = 0;
    CivSequenceGate m_sequenceGate;
};
