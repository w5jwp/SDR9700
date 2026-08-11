// QtTest invokes private slots through the generated meta-object.
#include "MeterController.h"

#include <QtTest>

class MeterControllerTest : public QObject
{
    Q_OBJECT

  private slots:
    void batchesUpdatesIntoOneSnapshot();
    void clampsMeterValues();
    void resetTransmitMetersPreservesReceiveMeter();
    void swrRequiresForwardPower();
    void resetPublishesDefaultSnapshotImmediately();
};

void MeterControllerTest::batchesUpdatesIntoOneSnapshot()
{
    MeterController controller;
    QSignalSpy snapshotSpy(&controller, &MeterController::snapshotChanged);

    controller.setSMeter(100);
    controller.setPowerMeter(50.0);
    controller.setTransmitAudioLevel(80, 40);

    QTRY_COMPARE(snapshotSpy.count(), 1);
    const MeterSnapshot snapshot = snapshotSpy.constFirst().constFirst().value<MeterSnapshot>();
    QCOMPARE(snapshot.sMeter, 100);
    QVERIFY(snapshot.sMeterValid);
    QCOMPARE(snapshot.powerWatts, 50.0);
    QVERIFY(snapshot.powerValid);
    QCOMPARE(snapshot.txAudioPeak, 80);
    QCOMPARE(snapshot.txAudioRms, 40);
}

void MeterControllerTest::clampsMeterValues()
{
    MeterController controller;
    MeterSnapshot snapshot;
    connect(&controller, &MeterController::snapshotChanged, this,
            [&snapshot](const MeterSnapshot& value) { snapshot = value; });

    controller.setSMeter(999);
    controller.setPowerMeter(-1.0);
    controller.setSwr(10.0);
    controller.setAlc(-2.0);
    controller.setCompressionMeter(99.0);
    controller.setVoltageMeter(99.0);
    controller.setCurrentMeter(-1.0);
    controller.setTransmitAudioLevel(999, -1);

    QTRY_VERIFY(snapshot.sMeterValid);
    QCOMPARE(snapshot.sMeter, 255);
    QCOMPARE(snapshot.powerWatts, 0.0);
    QCOMPARE(snapshot.swr, 6.0);
    QCOMPARE(snapshot.alc, 0.0);
    QCOMPARE(snapshot.compressionDb, 25.5);
    QCOMPARE(snapshot.voltageVolts, 16.0);
    QCOMPARE(snapshot.currentAmps, 0.0);
    QCOMPARE(snapshot.txAudioPeak, 255);
    QCOMPARE(snapshot.txAudioRms, 0);
}

void MeterControllerTest::resetTransmitMetersPreservesReceiveMeter()
{
    MeterController controller;
    MeterSnapshot snapshot;
    connect(&controller, &MeterController::snapshotChanged, this,
            [&snapshot](const MeterSnapshot& value) { snapshot = value; });

    controller.setSMeter(90);
    controller.setPowerMeter(25.0);
    controller.setSwr(2.0);
    QTRY_VERIFY(snapshot.powerValid);

    controller.resetTransmitMeters();
    QTRY_VERIFY(!snapshot.powerValid);
    QCOMPARE(snapshot.sMeter, 90);
    QVERIFY(snapshot.sMeterValid);
    QCOMPARE(snapshot.powerWatts, 0.0);
    QCOMPARE(snapshot.swr, 1.0);
    QVERIFY(!snapshot.swrValid);
}

void MeterControllerTest::swrRequiresForwardPower()
{
    MeterController controller;
    MeterSnapshot snapshot;
    connect(&controller, &MeterController::snapshotChanged, this,
            [&snapshot](const MeterSnapshot& value) { snapshot = value; });

    controller.setPowerMeter(0.0);
    controller.setSwr(1.5);
    QTRY_VERIFY(snapshot.powerValid);
    QVERIFY(!snapshot.swrValid);

    controller.setPowerMeter(10.0);
    controller.setSwr(1.5);
    QTRY_VERIFY(snapshot.swrValid);
    QCOMPARE(snapshot.swr, 1.5);

    controller.setPowerMeter(0.0);
    QTRY_VERIFY(!snapshot.swrValid);
}

void MeterControllerTest::resetPublishesDefaultSnapshotImmediately()
{
    MeterController controller;
    QSignalSpy snapshotSpy(&controller, &MeterController::snapshotChanged);

    controller.setSMeter(200);
    controller.reset();

    QCOMPARE(snapshotSpy.count(), 1);
    const MeterSnapshot snapshot = snapshotSpy.constFirst().constFirst().value<MeterSnapshot>();
    QCOMPARE(snapshot.sMeter, 0);
    QVERIFY(!snapshot.sMeterValid);
    QVERIFY(!snapshot.powerValid);
}

QTEST_GUILESS_MAIN(MeterControllerTest)
#include "MeterControllerTest.moc"
