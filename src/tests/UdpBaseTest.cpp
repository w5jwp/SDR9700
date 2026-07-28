// QtTest invokes private slots through the generated meta-object.
#include "UdpBase.h"

#include <QtTest>

class TestUdpBase : public UdpBase
{
  public:
    quint16 boundPort() const { return udp ? udp->localPort() : 0; }
    bool hasPendingDatagrams() const { return udp && udp->hasPendingDatagrams(); }
    QNetworkDatagram receiveDatagram() { return udp ? udp->receiveDatagram() : QNetworkDatagram(); }

    void setExpectedPeer(const QHostAddress& address, quint16 peerPort)
    {
        radioIP = address;
        port = peerPort;
    }

    bool accepts(const QNetworkDatagram& datagram) const { return acceptDatagramFrom(datagram); }
};

class UdpBaseTest : public QObject
{
    Q_OBJECT

  private slots:
    void bindsEphemeralLoopbackSocket();
    void receivesLoopbackDatagram();
    void validatesDatagramEndpoint();
    void encodesLoginTextDeterministically();
    void parsesNullTerminatedPacketText();
};

void UdpBaseTest::bindsEphemeralLoopbackSocket()
{
    TestUdpBase stream;

    QVERIFY(stream.init(0));
    QVERIFY(stream.isSocketBound());
    QVERIFY(stream.boundPort() > 0);
}

void UdpBaseTest::receivesLoopbackDatagram()
{
    TestUdpBase stream;
    QUdpSocket peer;
    QVERIFY(stream.init(0));
    QVERIFY(peer.bind(QHostAddress::LocalHost, 0));
    stream.setExpectedPeer(QHostAddress::LocalHost, peer.localPort());

    QCOMPARE(peer.writeDatagram(QByteArrayLiteral("loopback"), QHostAddress::LocalHost, stream.boundPort()), qint64(8));
    QTRY_VERIFY(stream.hasPendingDatagrams());
    const QNetworkDatagram received = stream.receiveDatagram();
    QCOMPARE(received.data(), QByteArrayLiteral("loopback"));
    QVERIFY(stream.accepts(received));
}

void UdpBaseTest::validatesDatagramEndpoint()
{
    TestUdpBase stream;
    stream.setExpectedPeer(QHostAddress::LocalHost, 50001);

    QNetworkDatagram expected(QByteArrayLiteral("data"));
    expected.setSender(QHostAddress::LocalHost, 50001);
    QVERIFY(stream.accepts(expected));

    QNetworkDatagram wrongPort(QByteArrayLiteral("data"));
    wrongPort.setSender(QHostAddress::LocalHost, 50002);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Ignoring UDP datagram.*")));
    QVERIFY(!stream.accepts(wrongPort));
}

void UdpBaseTest::encodesLoginTextDeterministically()
{
    QByteArray first;
    QByteArray second;
    passcode(QStringLiteral("operator-password-longer-than-16"), first);
    passcode(QStringLiteral("operator-password-longer-than-16"), second);

    QCOMPARE(first, second);
    QCOMPARE(first.size(), 16);
    QVERIFY(!first.contains(QByteArrayLiteral("operator")));
}

void UdpBaseTest::parsesNullTerminatedPacketText()
{
    QCOMPARE(parseNullTerminatedString(QByteArray("prefix\0suffix", 13), 0), QByteArrayLiteral("prefix"));
    QCOMPARE(parseNullTerminatedString(QByteArrayLiteral("prefix"), 3), QByteArrayLiteral("fix"));
    QCOMPARE(parseNullTerminatedString(QByteArrayLiteral("short"), 20), QByteArray());
}

QTEST_GUILESS_MAIN(UdpBaseTest)
#include "UdpBaseTest.moc"
