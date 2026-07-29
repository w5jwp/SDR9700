#include "PacketTypes.h"

#include <QTest>
#include <cstddef>
#include <cstring>

class PacketLayoutTest : public QObject
{
    Q_OBJECT

  private slots:
    void packetSizesMatchProtocolConstants();
    void commonHeaderOffsetsAreStable();
    void variableHeaderOffsetsAreStable();
    void endianConversionsRoundTrip();
    void rtpBitFieldsOccupyExpectedBytes();
    void packetDecoderRejectsShortInputAndCopiesAlignedData();
    void packetEncoderCopiesWireBytes();
};

void PacketLayoutTest::packetSizesMatchProtocolConstants()
{
    QCOMPARE(sizeof(control_packet), size_t(CONTROL_SIZE));
    QCOMPARE(sizeof(watchdog_packet), size_t(WATCHDOG_SIZE));
    QCOMPARE(sizeof(ping_packet), size_t(PING_SIZE));
    QCOMPARE(sizeof(audio_packet), size_t(AUDIO_SIZE));
    QCOMPARE(sizeof(token_packet), size_t(TOKEN_SIZE));
    QCOMPARE(sizeof(status_packet), size_t(STATUS_SIZE));
    QCOMPARE(sizeof(login_packet), size_t(LOGIN_SIZE));
    QCOMPARE(sizeof(conninfo_packet), size_t(CONNINFO_SIZE));
}

void PacketLayoutTest::commonHeaderOffsetsAreStable()
{
    QCOMPARE(offsetof(control_packet, len), size_t(0x00));
    QCOMPARE(offsetof(control_packet, type), size_t(0x04));
    QCOMPARE(offsetof(control_packet, seq), size_t(0x06));
    QCOMPARE(offsetof(control_packet, sentid), size_t(0x08));
    QCOMPARE(offsetof(control_packet, rcvdid), size_t(0x0c));
}

void PacketLayoutTest::variableHeaderOffsetsAreStable()
{
    QCOMPARE(offsetof(audio_packet, ident), size_t(0x10));
    QCOMPARE(offsetof(audio_packet, sendseq), size_t(0x12));
    QCOMPARE(offsetof(audio_packet, datalen), size_t(0x16));
    QCOMPARE(offsetof(login_packet, username), size_t(0x40));
    QCOMPARE(offsetof(login_packet, password), size_t(0x50));
    QCOMPARE(offsetof(conninfo_packet, rxsample), size_t(0x74));
    QCOMPARE(offsetof(conninfo_packet, audioport), size_t(0x80));
}

void PacketLayoutTest::endianConversionsRoundTrip()
{
    control_packet packet{};
    packet.len = qToLittleEndian(quint32(CONTROL_SIZE));
    packet.seq = qToLittleEndian(quint16(0xfffe));
    packet.sentid = qToLittleEndian(quint32(0x12345678));
    QCOMPARE(qFromLittleEndian(packet.len), quint32(CONTROL_SIZE));
    QCOMPARE(qFromLittleEndian(packet.seq), quint16(0xfffe));
    QCOMPARE(qFromLittleEndian(packet.sentid), quint32(0x12345678));
}

void PacketLayoutTest::rtpBitFieldsOccupyExpectedBytes()
{
    rtp_header header{};
    header.version = 2;
    header.payloadType = 0x60;
    header.marker = 1;
    QCOMPARE(header.packet[0], quint8(0x80));
    QCOMPARE(header.packet[1], quint8(0xe0));
}

void PacketLayoutTest::packetDecoderRejectsShortInputAndCopiesAlignedData()
{
    QByteArray bytes(CONTROL_SIZE, '\0');
    const quint32 length = qToLittleEndian(quint32(CONTROL_SIZE));
    const quint16 sequence = qToLittleEndian(quint16(0x4321));
    std::memcpy(bytes.data(), &length, sizeof(length));
    std::memcpy(bytes.data() + offsetof(control_packet, seq), &sequence, sizeof(sequence));

    QVERIFY(!decodePacket<control_packet>(QByteArrayView(bytes).first(CONTROL_SIZE - 1)).has_value());
    const auto decoded = decodePacket<control_packet>(bytes);
    QVERIFY(decoded.has_value());
    QCOMPARE(qFromLittleEndian(decoded->len), quint32(CONTROL_SIZE));
    QCOMPARE(qFromLittleEndian(decoded->seq), quint16(0x4321));
}

void PacketLayoutTest::packetEncoderCopiesWireBytes()
{
    control_packet packet{};
    packet.len = qToLittleEndian(quint32(CONTROL_SIZE));
    packet.type = 0x04;
    packet.seq = qToLittleEndian(quint16(0x1234));

    const QByteArray encoded = encodePacket(packet);
    QCOMPARE(encoded.size(), CONTROL_SIZE);

    const auto decoded = decodePacket<control_packet>(encoded);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->type, quint8(0x04));
    QCOMPARE(qFromLittleEndian(decoded->seq), quint16(0x1234));
}

QTEST_GUILESS_MAIN(PacketLayoutTest)

#include "PacketLayoutTest.moc"
