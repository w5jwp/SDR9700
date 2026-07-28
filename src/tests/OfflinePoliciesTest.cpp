#include "MemorySyncPolicy.h"
#include "SpectrumTuningPolicy.h"
#include "TransmitSafetyPolicy.h"

#include <QTest>
#include <limits>

class OfflinePoliciesTest : public QObject
{
    Q_OBJECT

  private slots:
    void clampsMemoryPollingInterval();
    void tracksMemorySynchronization();
    void roundsTuningFrequency();
    void clampsFrequencyAndScopeCenterToBand();
    void requiresConsecutiveHighSwrReadings();
    void resetsTransmitSafetyWhenNotTransmitting();
};

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

QTEST_GUILESS_MAIN(OfflinePoliciesTest)

#include "OfflinePoliciesTest.moc"
