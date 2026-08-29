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
    void acknowledgementsAreDiagnosticOnly();
    void tracksPendingReplyPressure();
    void unsolicitedUpdateDoesNotConsumePendingReply();
    void malformedReplyDoesNotConsumePendingReply();
    void correlatesEquivalentFrequencyAndModeReplyCommands();
    void discardsPendingRepliesByCanonicalFamily();
    void serializesReceiverlessReadsByCanonicalFamily();
    void discardsLateReplyDuringFamilyDrain();
    void adaptsReplyDrainWindowsToMeasuredRtt();
    void rejectsShortSpectrumFrames();
    void assemblesMultiPacketSpectrum();
    void rejectsBrokenAndExpiredSpectrumAssemblies();
    void parserToleratesDeterministicArbitraryInput();
    void schedulerCoalescesAndBoundsReads();
    void schedulerMakesStartupProgressUnderMeterPressure();

  private:
    Commander m_commander;
};

void CommanderCodecTest::init()
{
    m_commander.m_pendingReplies.clear();
    m_commander.m_deferredReplyReads.clear();
    m_commander.m_replyFamilyDrains.clear();
    m_commander.m_replyDrainTimer->stop();
    m_commander.m_correlationDiagnostics = {};
    m_commander.resetScheduledCommands();
    m_commander.m_schedulerDiagnostics = {};
    m_commander.m_shutdownComplete = false;
    sdr9700::populateRadioCapabilities(m_commander.radioCaps);
    m_commander.haveRadioCaps = true;
    m_commander.setCIVAddr(0xA2);
}

void CommanderCodecTest::schedulerCoalescesAndBoundsReads()
{
    for (int i = 0; i < 100; ++i)
    {
        m_commander.scheduleMeterRead(funcSMeter, 0);
    }
    QCOMPARE(m_commander.m_scheduledCommands.size(), 1);
    QCOMPARE(m_commander.schedulerDiagnostics().coalescedCommands, quint64(99));

    for (int i = 0; i < 100; ++i)
    {
        m_commander.scheduleStartupRead(funcRfGain, static_cast<uchar>(i));
    }
    QCOMPARE(m_commander.m_scheduledCommands.size(), qsizetype(64));
    QVERIFY(m_commander.schedulerDiagnostics().droppedCommands > 0);
    QCOMPARE(m_commander.schedulerDiagnostics().highWaterMark, qsizetype(64));
}

void CommanderCodecTest::schedulerMakesStartupProgressUnderMeterPressure()
{
    m_commander.scheduleMeterRead(funcSMeter, 0);
    m_commander.scheduleMeterRead(funcSWRMeter, 0);
    m_commander.scheduleMeterRead(funcPowerMeter, 0);
    m_commander.scheduleMeterRead(funcALCMeter, 0);
    m_commander.scheduleStartupRead(funcRfGain, 0);

    m_commander.m_consecutiveMeterDispatches = 3;
    m_commander.dispatchNextScheduledCommand();

    QCOMPARE(m_commander.m_scheduledCommands.size(), 4);
    QVERIFY(std::none_of(m_commander.m_scheduledCommands.cbegin(), m_commander.m_scheduledCommands.cend(),
                         [](const Commander::ScheduledCommand& command) { return command.func == funcRfGain; }));
    QCOMPARE(m_commander.m_consecutiveMeterDispatches, 0);
}

void CommanderCodecTest::correlatesEquivalentFrequencyAndModeReplyCommands()
{
    m_commander.rememberPendingReply(funcFreqGet, 1);
    m_commander.rememberPendingReply(funcModeGet, 1);

    uchar receiver = 0;
    QVERIFY(m_commander.takePendingReplyReceiver(funcSelectedFreq, &receiver));
    QCOMPARE(receiver, uchar(1));
    receiver = 0;
    QVERIFY(m_commander.takePendingReplyReceiver(funcSelectedMode, &receiver));
    QCOMPARE(receiver, uchar(1));
}

void CommanderCodecTest::discardsPendingRepliesByCanonicalFamily()
{
    m_commander.rememberPendingReply(funcFreqGet, 1);
    m_commander.rememberPendingReply(funcModeGet, 0);

    m_commander.discardPendingReplies(funcSelectedFreq);

    uchar receiver = 0;
    QVERIFY(!m_commander.takePendingReplyReceiver(funcFreqTR, &receiver));
    QVERIFY(m_commander.replyFamilyDraining(funcFreqGet));
    QVERIFY(m_commander.takePendingReplyReceiver(funcSelectedMode, &receiver));
    QCOMPARE(receiver, uchar(0));
}

void CommanderCodecTest::serializesReceiverlessReadsByCanonicalFamily()
{
    m_commander.m_pendingCommandClock.restart();
    m_commander.rememberPendingReply(funcFreqGet, 0);
    QSignalSpy wireSpy(&m_commander, &Commander::dataForComm);

    QVERIFY(m_commander.deferReplyReadIfBlocked(funcFreqGet, 1));
    QVERIFY(m_commander.deferReplyReadIfBlocked(funcFreqGet, 1));
    QCOMPARE(m_commander.m_deferredReplyReads.size(), 1);
    QCOMPARE(m_commander.correlationDiagnostics().deferredReplyReads, quint64(1));
    QCOMPARE(m_commander.correlationDiagnostics().coalescedReplyReads, quint64(1));

    uchar receiver = 0xff;
    QVERIFY(m_commander.takePendingReplyReceiver(funcSelectedFreq, &receiver));
    QCOMPARE(receiver, uchar(0));
    QVERIFY(m_commander.replyFamilyBlocked(funcFreq));

    QTRY_COMPARE_WITH_TIMEOUT(m_commander.m_pendingReplies.size(), 1, 250);
    QCOMPARE(m_commander.m_pendingReplies.constFirst().receiver, uchar(1));
    QCOMPARE(m_commander.m_deferredReplyReads.size(), 0);
    QCOMPARE(wireSpy.count(), 3);
    QVERIFY(wireSpy.at(0).at(0).toByteArray().contains(QByteArray::fromHex("07d1")));
    QVERIFY(wireSpy.at(1).at(0).toByteArray().contains(QByteArray::fromHex("03")));
    QVERIFY(wireSpy.at(2).at(0).toByteArray().contains(QByteArray::fromHex("07d0")));

    m_commander.m_pendingReplies.clear();
    m_commander.m_replyFamilyDrains.clear();
    m_commander.rememberPendingReply(funcModeGet, 0);
    for (int receiverIndex = 0; receiverIndex < 70; ++receiverIndex)
    {
        if (receiverIndex >= 64)
        {
            QTest::ignoreMessage(QtWarningMsg,
                                 QRegularExpression(QStringLiteral("CI-V deferred reply-read queue full.*")));
        }
        m_commander.deferReplyReadIfBlocked(funcModeGet, static_cast<uchar>(receiverIndex));
    }
    QCOMPARE(m_commander.m_deferredReplyReads.size(), qsizetype(64));
    QCOMPARE(m_commander.correlationDiagnostics().droppedReplyReads, quint64(6));
}

void CommanderCodecTest::discardsLateReplyDuringFamilyDrain()
{
    m_commander.m_pendingCommandClock.restart();
    m_commander.queue->resetSessionState();
    m_commander.rememberPendingReply(funcFreqGet, 0);

    m_commander.handleNewData(QByteArray::fromHex("fefee1a2030052140600fd"));
    QCOMPARE(m_commander.correlationDiagnostics().pendingReplies, qsizetype(0));

    QSignalSpy cacheSpy(m_commander.queue, &CachingQueue::cacheUpdated);
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("Discarding unattributed receiver-less CI-V reply.*")));
    m_commander.handleNewData(QByteArray::fromHex("fefee1a2030052140600fd"));
    QCOMPARE(cacheSpy.count(), 0);
    QCOMPARE(m_commander.correlationDiagnostics().drainedReplyFrames, quint64(1));
}

void CommanderCodecTest::adaptsReplyDrainWindowsToMeasuredRtt()
{
    CivRttEstimator estimator;
    QCOMPARE(estimator.resolvedDrainMs(), qint64(50));
    QCOMPARE(estimator.abandonedDrainMs(), qint64(500));

    estimator.observe(40);
    QCOMPARE(estimator.sampleCount(), quint64(1));
    QCOMPARE(estimator.resolvedDrainMs(), qint64(60));
    QCOMPARE(estimator.abandonedDrainMs(), qint64(120));

    for (int sample = 0; sample < 20; ++sample)
    {
        estimator.observe(40);
    }
    const qint64 settledResolved = estimator.resolvedDrainMs();
    const qint64 settledAbandoned = estimator.abandonedDrainMs();
    QVERIFY(settledResolved < 60);
    QCOMPARE(settledAbandoned, qint64(100));

    estimator.observe(400);
    QVERIFY(estimator.resolvedDrainMs() > settledResolved);
    QVERIFY(estimator.abandonedDrainMs() > settledAbandoned);
    QVERIFY(estimator.resolvedDrainMs() <= 250);
    QVERIFY(estimator.abandonedDrainMs() <= 2000);

    m_commander.m_rttEstimator = estimator;
    const CommanderCorrelationDiagnostics diagnostics = m_commander.correlationDiagnostics();
    QCOMPARE(diagnostics.rttSampleCount, estimator.sampleCount());
    QCOMPARE(diagnostics.resolvedReplyDrainMs, estimator.resolvedDrainMs());
    QCOMPARE(diagnostics.abandonedReplyDrainMs, estimator.abandonedDrainMs());
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

    m_commander.payloadIn = QByteArray::fromHex("0192");
    QCOMPARE(m_commander.parseLevelMeterReply(funcCompressorLevel, value), Commander::ReplyParseResult::Parsed);
    QCOMPARE(value.toUInt(), 192U);

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

    payload.clear();
    const FuncType agcCommand = m_commander.radioCaps.commands.value(funcAGCTimeConstant);
    QVERIFY(
        m_commander.appendSetCommandValue(funcAGCTimeConstant, QVariant::fromValue<uchar>(3), 0, agcCommand, payload));
    QCOMPARE(payload, QByteArray(1, '\x03'));

    payload.clear();
    const FuncType compressorLevelCommand = m_commander.radioCaps.commands.value(funcCompressorLevel);
    QVERIFY(m_commander.appendSetCommandValue(funcCompressorLevel, QVariant::fromValue<ushort>(192), 0,
                                              compressorLevelCommand, payload));
    QCOMPARE(payload, QByteArray::fromHex("0192"));

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

void CommanderCodecTest::acknowledgementsAreDiagnosticOnly()
{
    m_commander.queue->resetSessionState();
    m_commander.radioPoweredOn = true;
    QSignalSpy cacheSpy(m_commander.queue, &CachingQueue::cacheUpdated);

    m_commander.handleNewData(QByteArray::fromHex("fefee1a2fbfd"));
    m_commander.handleNewData(QByteArray::fromHex("fefee1a2fafd"));
    QCOMPARE(cacheSpy.count(), 0);
    QCOMPARE(m_commander.correlationDiagnostics().acceptedAcknowledgements, quint64(1));
    QCOMPARE(m_commander.correlationDiagnostics().rejectedAcknowledgements, quint64(1));

    m_commander.handleNewData(QByteArray::fromHex("fefee1a21c0000fd"));
    cacheSpy.clear();
    m_commander.handleNewData(QByteArray::fromHex("fefee1a21c0001fd"));
    QTRY_COMPARE(cacheSpy.count(), 1);
    const CacheItem update = qvariant_cast<CacheItem>(cacheSpy.takeFirst().at(0));
    QCOMPARE(update.command, funcTransceiverStatus);
    QVERIFY(update.value.toBool());
}

void CommanderCodecTest::tracksPendingReplyPressure()
{
    m_commander.m_pendingReplies.clear();
    m_commander.m_correlationDiagnostics = {};
    m_commander.m_pendingCommandClock.restart();

    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("CI-V pending reply overflow.*")));
    for (int i = 0; i < 65; ++i)
    {
        m_commander.rememberPendingReply(funcFreqGet, static_cast<uchar>(i % 2));
    }

    const CommanderCorrelationDiagnostics diagnostics = m_commander.correlationDiagnostics();
    QCOMPARE(diagnostics.pendingReplies, qsizetype(64));
    QCOMPARE(diagnostics.pendingReplyHighWaterMark, qsizetype(65));
    QCOMPARE(diagnostics.pendingReplyOverflows, quint64(1));
}

void CommanderCodecTest::unsolicitedUpdateDoesNotConsumePendingReply()
{
    m_commander.m_pendingReplies.clear();
    m_commander.m_correlationDiagnostics = {};
    m_commander.m_pendingCommandClock.restart();
    m_commander.queue->resetSessionState();
    m_commander.rememberPendingReply(funcFreqGet, 1);

    m_commander.handleNewData(QByteArray::fromHex("fefe00a2030052140600fd"));

    QCOMPARE(m_commander.correlationDiagnostics().pendingReplies, qsizetype(1));
    QCOMPARE(m_commander.correlationDiagnostics().ambiguousUnsolicitedFrames, quint64(1));
    uchar receiver = 0;
    QVERIFY(m_commander.takePendingReplyReceiver(funcFreqGet, &receiver));
    QCOMPARE(receiver, uchar(1));
    QVERIFY(!m_commander.queue->getCache(funcFreq, 0).value.isValid());
    QVERIFY(!m_commander.queue->getCache(funcFreq, 1).value.isValid());
}

void CommanderCodecTest::malformedReplyDoesNotConsumePendingReply()
{
    m_commander.m_pendingReplies.clear();
    m_commander.m_correlationDiagnostics = {};
    m_commander.m_pendingCommandClock.restart();
    m_commander.rememberPendingReply(funcModeGet, 1);

    m_commander.handleNewData(QByteArray::fromHex("fefee1a204fd"));

    QCOMPARE(m_commander.correlationDiagnostics().pendingReplies, qsizetype(1));
    uchar receiver = 0;
    QVERIFY(m_commander.takePendingReplyReceiver(funcModeGet, &receiver));
    QCOMPARE(receiver, uchar(1));
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

void CommanderCodecTest::rejectsBrokenAndExpiredSpectrumAssemblies()
{
    m_commander.radioCaps.spectSeqMax = 3;
    m_commander.radioCaps.spectLenMax = 8;

    Frequency start;
    start.Hz = 144000000;
    Frequency end;
    end.Hz = 148000000;
    const QByteArray first = QByteArray::fromHex("010301") + m_commander.makeFreqPayload(start) +
                             m_commander.makeFreqPayload(end) + QByteArray::fromHex("0011");

    ScopeData scope;
    m_commander.payloadIn = first;
    QVERIFY(!m_commander.parseSpectrum(scope, 0));
    m_commander.payloadIn = QByteArray::fromHex("0303ccdd");
    QVERIFY(!m_commander.parseSpectrum(scope, 0));
    QVERIFY(scope.data.isEmpty());

    m_commander.payloadIn = first;
    QVERIFY(!m_commander.parseSpectrum(scope, 0));
    QTest::qWait(275);
    m_commander.payloadIn = QByteArray::fromHex("0203aabb");
    QVERIFY(!m_commander.parseSpectrum(scope, 0));
    QVERIFY(scope.data.isEmpty());
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
