#include "ConnectionRetryPolicy.h"
#include "DualWatchTransitionPolicy.h"
#include "MemorySyncPolicy.h"
#include "MainSubExchangePolicy.h"
#include "PttConfirmationPolicy.h"
#include "SpectrumTuningPolicy.h"
#include "TransmitSafetyPolicy.h"
#include "TransmitFrequencyPolicy.h"
#include "TransmitConfigurationPolicy.h"

#include <QTest>
#include <array>
#include <limits>

class OfflinePoliciesTest : public QObject
{
    Q_OBJECT

  private slots:
    void clampsMemoryPollingInterval();
    void tracksMemorySynchronization();
    void retriesIncompleteOperationSynchronization();
    void advancesMemorySynchronizationSlots();
    void identifiesExpectedMemoryWriteReadback();
    void retriesRecoverableRadioConnectionFailures();
    void roundsTuningFrequency();
    void clampsFrequencyAndScopeCenterToBand();
    void requiresConsecutiveHighSwrReadings();
    void resetsTransmitSafetyWhenNotTransmitting();
    void keepsPttActiveUntilRadioConfirmsUnkey();
    void validatesDuplexTransmitFrequency();
    void blocksPttUntilTransmitConfigurationIsConfirmed();
    void serializesRepeatedMainSubExchanges();
    void requiresDualWatchStateAndSubIdentity();
};

void OfflinePoliciesTest::requiresDualWatchStateAndSubIdentity()
{
    // Alternate ten thousand complete transitions so every enable must wait
    // for fresh SUB identity and every disable must remain state-confirmed.
    constexpr int kTransitionCount = 10000;
    sdr9700::DualWatchTransitionPolicy policy;

    for (int transition = 0; transition < kTransitionCount; ++transition)
    {
        const bool enable = transition % 2 == 0;
        QVERIFY(policy.request(enable));
        QVERIFY(!policy.request(!enable));
        QVERIFY(!policy.observeState(!enable));
        QVERIFY(!policy.complete());
        QVERIFY(policy.observeState(enable));

        if (enable)
        {
            QCOMPARE(policy.missingConfirmations(), quint8(sdr9700::DualWatchTransitionPolicy::kSubFrequencyConfirmed |
                                                           sdr9700::DualWatchTransitionPolicy::kSubModeConfirmed));
            policy.observeSubMode();
            QVERIFY(!policy.complete());
            policy.observeSubFrequency();
        }

        QVERIFY(policy.complete());
        policy.reset();
        QVERIFY(!policy.pending());
    }
}

void OfflinePoliciesTest::serializesRepeatedMainSubExchanges()
{
    constexpr int kExchangeCount = 500;
    constexpr int kDuplicateClicksPerExchange = 10;
    // Five thousand rejected duplicate clicks intentionally pressure hundreds
    // of complete exchange cycles.
    sdr9700::MainSubExchangePolicy policy;
    bool mainContainsVhf = true;

    for (int exchange = 0; exchange < kExchangeCount; ++exchange)
    {
        QVERIFY(policy.request());
        QCOMPARE(policy.state(), sdr9700::MainSubExchangePolicy::State::AwaitingRadio);
        for (int duplicateClick = 0; duplicateClick < kDuplicateClicksPerExchange; ++duplicateClick)
        {
            QVERIFY(!policy.request());
        }
        QVERIFY(policy.confirmRadio());
        QCOMPARE(policy.state(), sdr9700::MainSubExchangePolicy::State::AwaitingScope);
        QVERIFY(!policy.request());
        QVERIFY(!policy.confirmRadio());
        QVERIFY(policy.confirmScope());
        mainContainsVhf = !mainContainsVhf;
        QVERIFY(!policy.pending());
    }

    QVERIFY(mainContainsVhf);
}

void OfflinePoliciesTest::clampsMemoryPollingInterval()
{
    QCOMPARE(sdr9700::clampMemoryPollIntervalSeconds(-1), 30);
    QCOMPARE(sdr9700::clampMemoryPollIntervalSeconds(600), 600);
    QCOMPARE(sdr9700::clampMemoryPollIntervalSeconds(9999), 3600);
}

void OfflinePoliciesTest::tracksMemorySynchronization()
{
    const QSet<quint32> expected = {0x00010001, 0x00010002, 0x00020001};
    QVERIFY(!sdr9700::memorySyncComplete(expected, {}));
    QVERIFY(!sdr9700::memorySyncComplete(expected, {0x00010001, 0x00010002, 0x00090009}));
    QVERIFY(sdr9700::memorySyncComplete(expected, {0x00010001, 0x00010002, 0x00020001}));
    QVERIFY(sdr9700::memorySyncComplete(expected, {0x00010001, 0x00010002, 0x00020001, 0x00090009}));
    QVERIFY(!sdr9700::memorySyncComplete({}, {}));
}

void OfflinePoliciesTest::retriesIncompleteOperationSynchronization()
{
    QVERIFY(sdr9700::shouldRetryIncompleteMemoryOperationSync(true, true, false, 1, 3));
    QVERIFY(sdr9700::shouldRetryIncompleteMemoryOperationSync(true, true, false, 2, 3));
    QVERIFY(!sdr9700::shouldRetryIncompleteMemoryOperationSync(true, true, false, 3, 3));
    QVERIFY(!sdr9700::shouldRetryIncompleteMemoryOperationSync(false, true, false, 1, 3));
    QVERIFY(!sdr9700::shouldRetryIncompleteMemoryOperationSync(true, false, false, 1, 3));
    QVERIFY(!sdr9700::shouldRetryIncompleteMemoryOperationSync(true, true, true, 1, 3));
}

void OfflinePoliciesTest::advancesMemorySynchronizationSlots()
{
    quint16 group = 1;
    quint16 channel = 1;
    QCOMPARE(sdr9700::memorySyncProgressIndex(group, channel, 1, 1, 99), 1);

    channel = 99;
    QCOMPARE(sdr9700::memorySyncProgressIndex(group, channel, 1, 1, 99), 99);
    sdr9700::advanceMemorySyncSlot(group, channel, 1, 99);
    QCOMPARE(group, quint16(2));
    QCOMPARE(channel, quint16(1));
    QCOMPARE(sdr9700::memorySyncProgressIndex(group, channel, 1, 1, 99), 100);

    group = 3;
    channel = 99;
    sdr9700::advanceMemorySyncSlot(group, channel, 1, 99);
    QCOMPARE(group, quint16(4));
    QCOMPARE(channel, quint16(1));
}

void OfflinePoliciesTest::identifiesExpectedMemoryWriteReadback()
{
    QVERIFY(sdr9700::memoryReadbackExpected(true, 0x00010002, 0x00010002));
    QVERIFY(!sdr9700::memoryReadbackExpected(false, 0x00010002, 0x00010002));
    QVERIFY(!sdr9700::memoryReadbackExpected(true, 0x00010002, 0x00010003));
    QVERIFY(!sdr9700::memoryReadbackExpected(true, 0, 0));
}

void OfflinePoliciesTest::retriesRecoverableRadioConnectionFailures()
{
    QVERIFY(sdr9700::shouldRetryRadioConnection(true, false, false, false, true));
    QVERIFY(sdr9700::shouldRetryRadioConnection(false, true, false, false, true));
    QVERIFY(!sdr9700::shouldRetryRadioConnection(false, false, false, false, true));
    QVERIFY(!sdr9700::shouldRetryRadioConnection(false, true, true, false, true));
    QVERIFY(!sdr9700::shouldRetryRadioConnection(false, true, false, true, true));
    QVERIFY(!sdr9700::shouldRetryRadioConnection(false, true, false, false, false));
}

void OfflinePoliciesTest::roundsTuningFrequency()
{
    QCOMPARE(sdr9700::roundFrequencyToStep(146520049, 100), quint64(146520000));
    QCOMPARE(sdr9700::roundFrequencyToStep(146520050, 100), quint64(146520100));
    QCOMPARE(sdr9700::roundFrequencyToStep(146520049, 1), quint64(146520049));
}

void OfflinePoliciesTest::clampsFrequencyAndScopeCenterToBand()
{
    QCOMPARE(sdr9700::clampFrequencyToBand(100000000, 146520000), quint64(144000000));
    QCOMPARE(sdr9700::clampFrequencyToBand(200000000, 146520000), quint64(148000000));
    QCOMPARE(sdr9700::clampScopeCenterToBand(144000000, 146520000, 1.0), quint64(144500000));
    QCOMPARE(sdr9700::clampScopeCenterToBand(148000000, 146520000, 1.0), quint64(147500000));
    QCOMPARE(sdr9700::clampScopeCenterToBand(146000000, 146520000, 10.0), quint64(146000000));
}

void OfflinePoliciesTest::requiresConsecutiveHighSwrReadings()
{
    sdr9700::TransmitSafetyPolicy policy;
    QVERIFY(!policy.observe(true, 3.0));
    QVERIFY(!policy.observe(true, 4.0));
    QVERIFY(policy.observe(true, 3.1));
    QCOMPARE(policy.highReadingCount(), 3);
    QVERIFY(!policy.observe(true, 2.99));
    QCOMPARE(policy.highReadingCount(), 0);
}

void OfflinePoliciesTest::resetsTransmitSafetyWhenNotTransmitting()
{
    sdr9700::TransmitSafetyPolicy policy;
    QVERIFY(!policy.observe(true, 5.0));
    QVERIFY(!policy.observe(false, 5.0));
    QCOMPARE(policy.highReadingCount(), 0);
    QVERIFY(!policy.observe(true, std::numeric_limits<double>::quiet_NaN()));
}

void OfflinePoliciesTest::keepsPttActiveUntilRadioConfirmsUnkey()
{
    sdr9700::PttConfirmationPolicy policy;
    QVERIFY(policy.requestOn());
    QVERIFY(policy.desiredActive());
    QVERIFY(policy.safetyActive());
    QVERIFY(policy.transmitMetersActive());

    policy.requestOff();
    QVERIFY(policy.offPending());
    QVERIFY(policy.safetyActive());
    QVERIFY(!policy.desiredActive());
    QVERIFY(!policy.transmitMetersActive());

    policy.reset();
    QVERIFY(policy.requestOn());
    policy.confirm(true);
    QVERIFY(policy.transmitMetersActive());
    policy.requestOff();
    QVERIFY(policy.confirmedActive());
    QVERIFY(policy.offPending());
    QVERIFY(policy.safetyActive());
    QVERIFY(!policy.transmitMetersActive());
    QVERIFY(policy.requestOn());
    QVERIFY(!policy.offPending());
    policy.requestOff();

    // A radio report that it is still keyed must remain authoritative.
    policy.confirm(true);
    QVERIFY(policy.confirmedActive());
    QVERIFY(policy.offPending());
    QVERIFY(policy.safetyActive());

    policy.confirm(false);
    QVERIFY(!policy.confirmedActive());
    QVERIFY(!policy.offPending());
    QVERIFY(!policy.safetyActive());
}

void OfflinePoliciesTest::validatesDuplexTransmitFrequency()
{
    QCOMPARE(sdr9700::duplexTransmitFrequency(446500000, dmSimplex, 5000000).value(), quint64(446500000));
    QCOMPARE(sdr9700::duplexTransmitFrequency(446500000, dmDupMinus, 5000000).value(), quint64(441500000));
    QCOMPARE(sdr9700::duplexTransmitFrequency(446500000, dmDupPlus, 5000000).value(), quint64(451500000));
    QVERIFY(!sdr9700::duplexTransmitFrequency(100, dmDupMinus, 200).has_value());

    // Every compiled IC-9700 band accepts its exact edges and rejects one hertz beyond them.
    const std::array<std::pair<quint64, quint64>, 3> bands = {
        std::pair<quint64, quint64>{144000000, 148000000},
        std::pair<quint64, quint64>{430000000, 450000000},
        std::pair<quint64, quint64>{1240000000, 1300000000},
    };
    for (const auto& [start, end] : bands)
    {
        const quint64 receiveHz = start + (end - start) / 2;
        QVERIFY(sdr9700::transmitFrequencyAllowed(receiveHz, start));
        QVERIFY(sdr9700::transmitFrequencyAllowed(receiveHz, end));
        QVERIFY(!sdr9700::transmitFrequencyAllowed(receiveHz, start - 1));
        QVERIFY(!sdr9700::transmitFrequencyAllowed(receiveHz, end + 1));
    }
}

void OfflinePoliciesTest::blocksPttUntilTransmitConfigurationIsConfirmed()
{
    sdr9700::TransmitConfigurationPolicy policy;
    policy.confirmFrequency(446500000);
    policy.confirmDuplexMode(dmSimplex);
    policy.confirmOffset(0);
    QVERIFY(policy.transmitFrequencyAllowed());

    policy.requestOffset(5000000);
    policy.requestDuplexMode(dmDupPlus);
    QVERIFY(policy.confirmationPending());
    QVERIFY(!policy.transmitFrequencyAllowed());

    policy.confirmOffset(5000000);
    QVERIFY(policy.confirmationPending());
    policy.confirmDuplexMode(dmDupPlus);
    QVERIFY(!policy.confirmationPending());
    QVERIFY(!policy.transmitFrequencyAllowed());

    policy.requestDuplexMode(dmDupMinus);
    policy.confirmDuplexMode(dmSimplex);
    QVERIFY(policy.confirmationPending());
    policy.confirmDuplexMode(dmDupMinus);
    QVERIFY(policy.transmitFrequencyAllowed());
}

QTEST_GUILESS_MAIN(OfflinePoliciesTest)

#include "OfflinePoliciesTest.moc"
