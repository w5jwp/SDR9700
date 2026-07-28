#include "SystemStats.h"

#include <QTest>

class SystemStatsTest : public QObject
{
    Q_OBJECT

  private slots:
    void calculatesCpuPercentage();
    void rejectsInvalidSamples();
    void samplesCurrentProcess();
};

void SystemStatsTest::calculatesCpuPercentage()
{
    const auto percent = SystemStatsProvider::calculateCpuPercent(CpuTicks{100, 100}, CpuTicks{175, 125});
    QVERIFY(percent.has_value());
    QCOMPARE(*percent, 75.0);
}

void SystemStatsTest::rejectsInvalidSamples()
{
    QVERIFY(!SystemStatsProvider::calculateCpuPercent(CpuTicks{100, 100}, CpuTicks{100, 100}).has_value());
    QVERIFY(!SystemStatsProvider::calculateCpuPercent(CpuTicks{100, 100}, CpuTicks{99, 110}).has_value());
}

void SystemStatsTest::samplesCurrentProcess()
{
    SystemStatsProvider provider;
    const SystemStats stats = provider.sample();
    QVERIFY(stats.processResidentBytes.has_value());
    QVERIFY(*stats.processResidentBytes > 0);
}

QTEST_APPLESS_MAIN(SystemStatsTest)
#include "SystemStatsTest.moc"
