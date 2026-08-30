#include <QtTest>

#include "RadioSessionWatchdog.h"

class RadioSessionWatchdogTest : public QObject
{
    Q_OBJECT

  private slots:
    void healthyTrafficDoesNothing();
    void healthRequiresFreshControlAndCivTraffic();
    void retriesCivThreeTimesThenDisconnects();
    void civTrafficResetsRecoveryBudget();
    void controlSilenceDisconnectsImmediately();
    void resetStartsIndependentRecoveryEpoch();
};

void RadioSessionWatchdogTest::healthyTrafficDoesNothing()
{
    RadioSessionWatchdog watchdog;
    QCOMPARE(watchdog.evaluate(100, 100), RadioSessionWatchdog::Action::None);
}

void RadioSessionWatchdogTest::healthRequiresFreshControlAndCivTraffic()
{
    QVERIFY(RadioSessionWatchdog::isHealthy(100, 100));
    QVERIFY(!RadioSessionWatchdog::isHealthy(1500, 100));
    QVERIFY(!RadioSessionWatchdog::isHealthy(100, 2000));
}

void RadioSessionWatchdogTest::retriesCivThreeTimesThenDisconnects()
{
    RadioSessionWatchdog watchdog;
    QCOMPARE(watchdog.evaluate(100, 2000), RadioSessionWatchdog::Action::RestartCiv);
    QCOMPARE(watchdog.evaluate(100, 2500), RadioSessionWatchdog::Action::None);
    QCOMPARE(watchdog.evaluate(100, 3000), RadioSessionWatchdog::Action::RestartCiv);
    QCOMPARE(watchdog.evaluate(100, 4000), RadioSessionWatchdog::Action::RestartCiv);
    QCOMPARE(watchdog.evaluate(100, 5000), RadioSessionWatchdog::Action::Disconnect);
}

void RadioSessionWatchdogTest::civTrafficResetsRecoveryBudget()
{
    RadioSessionWatchdog watchdog;
    QCOMPARE(watchdog.evaluate(100, 2000), RadioSessionWatchdog::Action::RestartCiv);
    QCOMPARE(watchdog.evaluate(100, 50), RadioSessionWatchdog::Action::None);
    QCOMPARE(watchdog.civRecoveryAttempts(), 0);
    QCOMPARE(watchdog.evaluate(100, 2000), RadioSessionWatchdog::Action::RestartCiv);
    QCOMPARE(watchdog.civRecoveryAttempts(), 1);
}

void RadioSessionWatchdogTest::controlSilenceDisconnectsImmediately()
{
    RadioSessionWatchdog watchdog;
    QCOMPARE(watchdog.evaluate(5000, 100), RadioSessionWatchdog::Action::Disconnect);
}

void RadioSessionWatchdogTest::resetStartsIndependentRecoveryEpoch()
{
    RadioSessionWatchdog watchdog;
    QCOMPARE(watchdog.evaluate(100, 2000), RadioSessionWatchdog::Action::RestartCiv);
    QCOMPARE(watchdog.evaluate(100, 3000), RadioSessionWatchdog::Action::RestartCiv);
    QCOMPARE(watchdog.civRecoveryAttempts(), 2);

    watchdog.reset();

    QCOMPARE(watchdog.civRecoveryAttempts(), 0);
    QCOMPARE(watchdog.evaluate(100, 1999), RadioSessionWatchdog::Action::None);
    QCOMPARE(watchdog.evaluate(100, 2000), RadioSessionWatchdog::Action::RestartCiv);
    QCOMPARE(watchdog.civRecoveryAttempts(), 1);
}

QTEST_GUILESS_MAIN(RadioSessionWatchdogTest)
#include "RadioSessionWatchdogTest.moc"
