#include "SystemStats.h"

#include <QTest>

class SystemStatsTest : public QObject
{
    Q_OBJECT

  private slots:
    void calculatesCpuPercentage();
    void rejectsInvalidSamples();
    void parsesLinuxCpuTicksWithoutDoubleCountingGuests();
    void rejectsMalformedLinuxCpuTicks();
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

void SystemStatsTest::parsesLinuxCpuTicksWithoutDoubleCountingGuests()
{
    const auto ticks = SystemStatsProvider::parseLinuxCpuTicks("cpu  100 20 30 400 10 5 6 7 8 9\n");
    QVERIFY(ticks.has_value());
    QCOMPARE(ticks->idle, quint64{410});
    QCOMPARE(ticks->active, quint64{168});
}

void SystemStatsTest::rejectsMalformedLinuxCpuTicks()
{
    QVERIFY(!SystemStatsProvider::parseLinuxCpuTicks("cpu 1 2 3").has_value());
    QVERIFY(!SystemStatsProvider::parseLinuxCpuTicks("cpu 1 2 invalid 4").has_value());
    QVERIFY(!SystemStatsProvider::parseLinuxCpuTicks("cpu0 1 2 3 4").has_value());
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
