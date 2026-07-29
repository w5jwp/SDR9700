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
    void parsesFrequencyReplyFamily();
    void parsesModeReplyFamily();
    void rejectsMalformedFrequencyAndModeReplies();
    void parsesLevelAndMeterReplyFamily();
    void rejectsMalformedLevelAndMeterReplies();
    void parsesFeatureAndScopeReplyFamilies();
    void rejectsMalformedFeatureAndScopeReplies();
    void parsesMemoryFields();
    void serializesOutboundCommandValues();
    void rejectsUnknownOutboundValueTypes();
    void pttAcknowledgementDoesNotSynthesizeTransmitState();
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

void CommanderCodecTest::parsesFrequencyReplyFamily()
{
    Frequency expected;
    expected.Hz = 145825000;
    m_commander.payloadIn = QByteArray(1, '\x01') + m_commander.makeFreqPayload(expected);
    Funcs func = funcFreq;
    QVariant value;
    uchar receiver = 0;

    QCOMPARE(m_commander.parseFrequencyReply(func, value, receiver), Commander::ReplyParseResult::Parsed);
    QCOMPARE(func, funcFreq);
    QCOMPARE(receiver, uchar(1));
    QCOMPARE(value.value<Frequency>().Hz, expected.Hz);
}

void CommanderCodecTest::parsesModeReplyFamily()
{
    m_commander.payloadIn = QByteArray::fromHex("01050102");
    Funcs func = funcMode;
    QVariant value;
    uchar receiver = 0;

    QCOMPARE(m_commander.parseModeReply(func, value, receiver), Commander::ReplyParseResult::Parsed);
    QCOMPARE(func, funcMode);
    QCOMPARE(receiver, uchar(1));
    const ModeInfo mode = value.value<ModeInfo>();
    QCOMPARE(mode.mk, modeFM);
    QCOMPARE(mode.data, uchar(1));
    QCOMPARE(mode.filter, uchar(2));
    QCOMPARE(mode.VFO, selVFO_t(1));
}

void CommanderCodecTest::rejectsMalformedFrequencyAndModeReplies()
{
    QVariant value;
    uchar receiver = 0;

    Funcs frequencyFunc = funcFreq;
    m_commander.payloadIn.clear();
    QCOMPARE(m_commander.parseFrequencyReply(frequencyFunc, value, receiver), Commander::ReplyParseResult::Malformed);
    QVERIFY(!value.isValid());

    Funcs modeFunc = funcMode;
    m_commander.payloadIn.clear();
    QCOMPARE(m_commander.parseModeReply(modeFunc, value, receiver), Commander::ReplyParseResult::Malformed);
    QVERIFY(!value.isValid());

    modeFunc = funcDataModeWithFilter;
    m_commander.payloadIn = QByteArray(1, '\x01');
    QCOMPARE(m_commander.parseModeReply(modeFunc, value, receiver), Commander::ReplyParseResult::Malformed);
    QVERIFY(!value.isValid());
}

void CommanderCodecTest::parsesLevelAndMeterReplyFamily()
{
    QVariant value;

    m_commander.payloadIn = QByteArray::fromHex("0123");
    QCOMPARE(m_commander.parseLevelMeterReply(funcAfGain, value), Commander::ReplyParseResult::Parsed);
    QCOMPARE(value.toUInt(), 123U);

    m_commander.payloadIn = QByteArray::fromHex("01230000");
    QCOMPARE(m_commander.parseLevelMeterReply(funcAbsoluteMeter, value), Commander::ReplyParseResult::Parsed);
    const MeterKind meter = value.value<MeterKind>();
    QCOMPARE(meter.value, 12.3);
    QCOMPARE(meter.type, meterdBu);

    QCOMPARE(m_commander.parseLevelMeterReply(funcToneFreq, value), Commander::ReplyParseResult::NotHandled);
}

void CommanderCodecTest::rejectsMalformedLevelAndMeterReplies()
{
    QVariant value;
    m_commander.payloadIn = QByteArray(1, '\0');
    QCOMPARE(m_commander.parseLevelMeterReply(funcAfGain, value), Commander::ReplyParseResult::Malformed);
    QVERIFY(!value.isValid());

    m_commander.payloadIn = QByteArray(3, '\0');
    QCOMPARE(m_commander.parseLevelMeterReply(funcAbsoluteMeter, value), Commander::ReplyParseResult::Malformed);
    QVERIFY(!value.isValid());
}

void CommanderCodecTest::parsesFeatureAndScopeReplyFamilies()
{
    QVariant value;

    m_commander.payloadIn = QByteArray(1, '\x01');
    QCOMPARE(m_commander.parseFeatureReply(funcNoiseBlanker, value, 0), Commander::ReplyParseResult::Parsed);
    QVERIFY(value.toBool());

    uchar receiver = 0;
    m_commander.payloadIn = QByteArray::fromHex("0102");
    QCOMPARE(m_commander.parseScopeReply(funcScopeMode, value, receiver), Commander::ReplyParseResult::Parsed);
    QCOMPARE(receiver, uchar(1));
    QCOMPARE(value.value<uchar>(), uchar(2));

    QCOMPARE(m_commander.parseScopeReply(funcToneFreq, value, receiver), Commander::ReplyParseResult::NotHandled);
}

void CommanderCodecTest::rejectsMalformedFeatureAndScopeReplies()
{
    QVariant value;
    m_commander.payloadIn.clear();
    QCOMPARE(m_commander.parseFeatureReply(funcNoiseBlanker, value, 0), Commander::ReplyParseResult::Malformed);
    QVERIFY(!value.isValid());

    uchar receiver = 0;
    m_commander.payloadIn = QByteArray(1, '\0');
    QCOMPARE(m_commander.parseScopeReply(funcScopeMode, value, receiver), Commander::ReplyParseResult::Malformed);
    QVERIFY(!value.isValid());
}

void CommanderCodecTest::parsesMemoryFields()
{
    MemoryType memory;
    m_commander.initializeMemoryForParsing(memory);

    m_commander.parseMemoryField(MemParserFormat('a', 0, 1), QByteArray::fromHex("02"), memory);
    m_commander.parseMemoryField(MemParserFormat('b', 0, 2), QByteArray::fromHex("0042"), memory);
    Frequency frequency;
    frequency.Hz = 145825000;
    m_commander.parseMemoryField(MemParserFormat('f', 0, 5), m_commander.makeFreqPayload(frequency), memory);
    m_commander.parseMemoryField(MemParserFormat('z', 0, 8), QByteArray("SAT TEST"), memory);

    QCOMPARE(memory.group, quint16(2));
    QCOMPARE(memory.channel, quint16(42));
    QCOMPARE(memory.frequency.Hz, quint64(145825000));
    QCOMPARE(QByteArray(memory.name, 8), QByteArray("SAT TEST"));
}

void CommanderCodecTest::serializesOutboundCommandValues()
{
    QByteArray payload;
    const FuncType boolCommand = m_commander.radioCaps.commands.value(funcNoiseBlanker);
    QVERIFY(m_commander.appendSetCommandValue(funcNoiseBlanker, QVariant::fromValue(true), 0, boolCommand, payload));
    QCOMPARE(payload, QByteArray(1, '\x01'));

    Frequency frequency;
    frequency.Hz = 145825000;
    payload.clear();
    const FuncType frequencyCommand = m_commander.radioCaps.commands.value(funcFreqSet);
    QVERIFY(
        m_commander.appendSetCommandValue(funcFreqSet, QVariant::fromValue(frequency), 0, frequencyCommand, payload));
    QCOMPARE(m_commander.parseFreqDataToInt(payload), frequency.Hz);
}

void CommanderCodecTest::rejectsUnknownOutboundValueTypes()
{
    QByteArray payload;
    const FuncType command = m_commander.radioCaps.commands.value(funcNoiseBlanker);
    QVERIFY(!m_commander.appendSetCommandValue(funcNoiseBlanker, QVariant::fromValue(QRect(1, 2, 3, 4)), 0, command,
                                               payload));
    QVERIFY(payload.isEmpty());
}

void CommanderCodecTest::pttAcknowledgementDoesNotSynthesizeTransmitState()
{
    m_commander.queue->resetSessionState();
    m_commander.radioPoweredOn = true;
    QSignalSpy cacheSpy(m_commander.queue, &CachingQueue::cacheUpdated);

    const FuncType command = m_commander.radioCaps.commands.value(funcTransceiverStatus);
    m_commander.rememberPendingSetCommand(funcTransceiverStatus, QByteArray::fromHex("1c0001"),
                                          QVariant::fromValue(true), 0, command);
    m_commander.handleNewData(QByteArray::fromHex("fefee1a2fbfd"));
    QCOMPARE(cacheSpy.count(), 0);

    m_commander.handleNewData(QByteArray::fromHex("fefee1a21c0000fd"));
    cacheSpy.clear();
    m_commander.handleNewData(QByteArray::fromHex("fefee1a21c0001fd"));
    QTRY_COMPARE(cacheSpy.count(), 1);
    const CacheItem update = qvariant_cast<CacheItem>(cacheSpy.takeFirst().at(0));
    QCOMPARE(update.command, funcTransceiverStatus);
    QVERIFY(update.value.toBool());
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
