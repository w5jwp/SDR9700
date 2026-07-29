#include "Commander.h"

#include "RadioCapabilities.h"

#include <QSignalSpy>
#include <QTest>
#include <limits>

class CommanderCodecTest : public QObject
{
    Q_OBJECT

  private slots:
    void init();
    void encodesAndDecodesPackedBcd();
    void frequencyPayloadRoundTrips_data();
    void frequencyPayloadRoundTrips();
    void tonePayloadRoundTrips();
    void parsesModesAndUnknownMode();
    void rejectsShortSpectrumFrames();
    void assemblesMultiPacketSpectrum();
    void parserToleratesDeterministicArbitraryInput();

  private:
    Commander m_commander;
};

void CommanderCodecTest::init()
{
    sdr9700::populateRadioCapabilities(m_commander.radioCaps);
    m_commander.haveRadioCaps = true;
    m_commander.setCIVAddr(0xA2);
}

void CommanderCodecTest::encodesAndDecodesPackedBcd()
{
    QCOMPARE(m_commander.bcdEncodeInt(quint16(4123)), QByteArray::fromHex("4123"));
    QCOMPARE(Commander::bcdHexToUInt(0x41, 0x23), 4123U);
    QCOMPARE(m_commander.bcdEncodeInt(654321U), QByteArray::fromHex("654321"));
    QCOMPARE(Commander::bcdHexToUInt(0x65, 0x43, 0x21), 654321U);
    QVERIFY(m_commander.bcdEncodeInt(quint16(10000)).isEmpty());
    QVERIFY(m_commander.bcdEncodeInt(1000000U).isEmpty());
}

void CommanderCodecTest::frequencyPayloadRoundTrips_data()
{
    QTest::addColumn<quint64>("hz");
    QTest::newRow("2m") << quint64(146520000);
    QTest::newRow("70cm") << quint64(440000000);
    QTest::newRow("23cm") << quint64(1296000000);
    QTest::newRow("six-byte") << quint64(10000000000ULL);
}

void CommanderCodecTest::frequencyPayloadRoundTrips()
{
    QFETCH(quint64, hz);
    Frequency frequency;
    frequency.Hz = hz;
    const QByteArray encoded = m_commander.makeFreqPayload(frequency);
    QCOMPARE(encoded.size(), hz >= 10000000000ULL ? 6 : 5);
    QCOMPARE(m_commander.parseFreqDataToInt(encoded), hz);
}

void CommanderCodecTest::tonePayloadRoundTrips()
{
    for (const quint16 tone : {quint16(670), quint16(1273), quint16(2541)})
    {
        const ToneInfo decoded = m_commander.decodeTone(m_commander.encodeTone(tone, true, true));
        QCOMPARE(decoded.tone, tone);
        QVERIFY(decoded.tinv);
        QVERIFY(decoded.rinv);
    }
    const ToneInfo shortTone = m_commander.decodeTone(QByteArray::fromHex("01"));
    QCOMPARE(shortTone.tone, quint16(670));
}

void CommanderCodecTest::parsesModesAndUnknownMode()
{
    QCOMPARE(m_commander.parseMode(5, 0, 2).mk, modeFM);
    QCOMPARE(m_commander.parseMode(17, 1, 1).mk, modeDV);
    const ModeInfo unknown = m_commander.parseMode(0x7f, 0, 1);
    QCOMPARE(unknown.mk, modeUnknown);
}

void CommanderCodecTest::rejectsShortSpectrumFrames()
{
    m_commander.radioCaps.spectSeqMax = 11;
    ScopeData scope;
    for (int length = 0; length < 14; ++length)
    {
        m_commander.payloadIn = QByteArray(length, '\0');
        QVERIFY(!m_commander.parseSpectrum(scope, 0));
    }
}

void CommanderCodecTest::assemblesMultiPacketSpectrum()
{
    m_commander.radioCaps.spectSeqMax = 3;
    m_commander.radioCaps.spectLenMax = 8;

    Frequency start;
    start.Hz = 144000000;
    Frequency end;
    end.Hz = 148000000;
    m_commander.payloadIn = QByteArray::fromHex("010301") + m_commander.makeFreqPayload(start) +
                            m_commander.makeFreqPayload(end) + QByteArray::fromHex("0011");
    ScopeData scope;
    QVERIFY(!m_commander.parseSpectrum(scope, 0));

    m_commander.payloadIn = QByteArray::fromHex("0203aabb");
    QVERIFY(!m_commander.parseSpectrum(scope, 0));
    m_commander.payloadIn = QByteArray::fromHex("0303ccdd");
    QVERIFY(m_commander.parseSpectrum(scope, 0));
    QCOMPARE(scope.startFreq, 144.0);
    QCOMPARE(scope.endFreq, 148.0);
    QCOMPARE(scope.data, QByteArray::fromHex("aabbccdd"));
    QVERIFY(scope.valid);
}

void CommanderCodecTest::parserToleratesDeterministicArbitraryInput()
{
    quint32 state = 0x9700;
    for (int iteration = 0; iteration < 500; ++iteration)
    {
        state = state * 1664525U + 1013904223U;
        const int length = int(state % 128U);
        QByteArray input(length, Qt::Uninitialized);
        for (int i = 0; i < length; ++i)
        {
            state = state * 1664525U + 1013904223U;
            input[i] = char(state >> 24);
        }
        m_commander.parseData(input);
    }
    QVERIFY(true);
}

QTEST_GUILESS_MAIN(CommanderCodecTest)

#include "CommanderCodecTest.moc"
