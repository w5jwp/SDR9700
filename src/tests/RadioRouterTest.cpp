#include "CachingQueue.h"
#include "RadioRouter.h"

#include <QSignalSpy>
#include <QTest>

class RadioRouterTest : public QObject
{
    Q_OBJECT

  private slots:
    void routesOnlyMainReceiverFrequencyAndMode();
    void clampsMeterAndLevelValues();
    void mapsAgcAndPreampValues();
    void routesProtocolPayloadTypes();
    void ignoresUnknownCommands();
};

void RadioRouterTest::routesOnlyMainReceiverFrequencyAndMode()
{
    RadioRouter router;
    QSignalSpy frequencySpy(&router, &RadioRouter::frequencyReported);
    QSignalSpy modeSpy(&router, &RadioRouter::modeReported);

    Frequency frequency;
    frequency.Hz = 146520000;
    router.route(CacheItem(funcFreqGet, QVariant::fromValue(frequency), 1));
    router.route(CacheItem(funcUnselectedFreq, QVariant::fromValue(frequency), 0));
    QCOMPARE(frequencySpy.count(), 0);
    router.route(CacheItem(funcSelectedFreq, QVariant::fromValue(frequency), 0));
    QCOMPARE(frequencySpy.count(), 1);
    QCOMPARE(frequencySpy.takeFirst().at(0).toULongLong(), quint64(146520000));

    ModeInfo mode;
    mode.mk = modeFM;
    mode.filter = 2;
    router.route(CacheItem(funcSelectedMode, QVariant::fromValue(mode), 0));
    QCOMPARE(modeSpy.count(), 1);
    QCOMPARE(modeSpy.at(0).at(0).toString(), QStringLiteral("FM"));
    QCOMPARE(modeSpy.at(0).at(1).toInt(), 2);
}

void RadioRouterTest::clampsMeterAndLevelValues()
{
    RadioRouter router;
    QSignalSpy smeterSpy(&router, &RadioRouter::smeterChanged);
    QSignalSpy rfSpy(&router, &RadioRouter::rfGainChanged);
    QSignalSpy powerSpy(&router, &RadioRouter::txPowerChanged);
    QSignalSpy squelchSpy(&router, &RadioRouter::squelchChanged);

    router.route(CacheItem(funcSMeter, -999.0));
    router.route(CacheItem(funcSMeter, 999.0));
    QCOMPARE(smeterSpy.at(0).at(0).toInt(), 0);
    QCOMPARE(smeterSpy.at(1).at(0).toInt(), 255);

    router.route(CacheItem(funcRfGain, -1));
    router.route(CacheItem(funcRFPower, 999));
    router.route(CacheItem(funcSquelch, 0));
    QCOMPARE(rfSpy.takeFirst().at(0).toInt(), 0);
    QCOMPARE(powerSpy.takeFirst().at(0).toInt(), 255);
    QCOMPARE(squelchSpy.at(0).at(0).toBool(), false);
    QCOMPARE(squelchSpy.at(0).at(1).toInt(), 0);
}

void RadioRouterTest::mapsAgcAndPreampValues()
{
    RadioRouter router;
    QSignalSpy agcSpy(&router, &RadioRouter::agcModeChanged);
    QSignalSpy preampEnabledSpy(&router, &RadioRouter::preampChanged);
    QSignalSpy preampLevelSpy(&router, &RadioRouter::preampLevelChanged);

    router.route(CacheItem(funcAGCTimeConstant, -5));
    router.route(CacheItem(funcAGCTimeConstant, 99));
    QCOMPARE(agcSpy.at(0).at(0).toString(), QStringLiteral("off"));
    QCOMPARE(agcSpy.at(1).at(0).toString(), QStringLiteral("slow"));

    router.route(CacheItem(funcPreamp, 9));
    QCOMPARE(preampLevelSpy.takeFirst().at(0).toInt(), 3);
    QCOMPARE(preampEnabledSpy.takeFirst().at(0).toBool(), true);
}

void RadioRouterTest::routesProtocolPayloadTypes()
{
    RadioRouter router;
    QSignalSpy offsetSpy(&router, &RadioRouter::repeaterOffsetChanged);
    QSignalSpy pttSpy(&router, &RadioRouter::pttChanged);
    QSignalSpy scopeSpy(&router, &RadioRouter::scopeDataReady);

    Frequency offset;
    offset.Hz = 600000;
    router.route(CacheItem(funcReadFreqOffset, QVariant::fromValue(offset)));
    router.route(CacheItem(funcTransceiverStatus, true));
    ScopeData scope;
    scope.valid = true;
    scope.data = QByteArray::fromHex("0102");
    router.route(CacheItem(funcScopeWaveData, QVariant::fromValue(scope)));

    QCOMPARE(offsetSpy.takeFirst().at(0).toULongLong(), quint64(600000));
    QCOMPARE(pttSpy.takeFirst().at(0).toBool(), true);
    QCOMPARE(scopeSpy.takeFirst().at(0).value<ScopeData>().data, scope.data);
}

void RadioRouterTest::ignoresUnknownCommands()
{
    RadioRouter router;
    QSignalSpy valueSpy(&router, &RadioRouter::radioValueUpdated);
    router.route(CacheItem(funcNone, 42));
    QCOMPARE(valueSpy.count(), 0);
}

QTEST_GUILESS_MAIN(RadioRouterTest)

#include "RadioRouterTest.moc"
