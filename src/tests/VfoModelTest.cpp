// QtTest invokes private slots through the generated meta-object.
#include "VfoModel.h"

#include <QtTest>

class VfoModelTest : public QObject
{
    Q_OBJECT

  private slots:
    void startsWithDocumentedDefaults();
    void confirmedFrequencyAndModeAreDeduplicated();
    void confirmedLevelsAreClampedAndDeduplicated();
    void preampLevelUpdatesDerivedState();
    void ritOffsetIsClamped();
    void filterAndPttSignalsAreDeduplicated();
};

void VfoModelTest::startsWithDocumentedDefaults()
{
    const VfoModel model(nullptr);

    QCOMPARE(model.frequencyHz(), quint64(145000000));
    QCOMPARE(model.mode(), QStringLiteral("FM"));
    QCOMPARE(model.filterLow(), -8000);
    QCOMPARE(model.filterHigh(), 8000);
    QVERIFY(!model.txActive());
    QCOMPARE(VfoModel::availableModes(), QStringList({"FM", "USB", "LSB", "AM", "CW", "CW-R", "RTTY", "DV", "DD"}));
}

void VfoModelTest::confirmedFrequencyAndModeAreDeduplicated()
{
    VfoModel model(nullptr);
    QSignalSpy frequencySpy(&model, &VfoModel::frequencyChanged);
    QSignalSpy modeSpy(&model, &VfoModel::modeChanged);

    model.applyFrequency(146520000);
    model.applyFrequency(146520000);
    model.applyMode(QStringLiteral("DV"));
    model.applyMode(QStringLiteral("DV"));

    QCOMPARE(model.frequencyHz(), quint64(146520000));
    QCOMPARE(model.mode(), QStringLiteral("DV"));
    QCOMPARE(frequencySpy.count(), 1);
    QCOMPARE(frequencySpy.constFirst().constFirst().toULongLong(), quint64(146520000));
    QCOMPARE(modeSpy.count(), 1);
    QCOMPARE(modeSpy.constFirst().constFirst().toString(), QStringLiteral("DV"));
}

void VfoModelTest::confirmedLevelsAreClampedAndDeduplicated()
{
    VfoModel model(nullptr);
    QSignalSpy rfGainSpy(&model, &VfoModel::rfGainChanged);
    QSignalSpy squelchSpy(&model, &VfoModel::squelchChanged);
    QSignalSpy txPowerSpy(&model, &VfoModel::txPowerChanged);

    model.applyRfGain(-1);
    model.applyRfGain(0);
    model.applySquelch(true, 300);
    model.applySquelch(true, 255);
    model.applyTxPower(300);
    model.applyTxPower(255);

    QCOMPARE(rfGainSpy.count(), 1);
    QCOMPARE(rfGainSpy.constFirst().constFirst().toInt(), 0);
    QCOMPARE(squelchSpy.count(), 1);
    QCOMPARE(squelchSpy.constFirst().at(0).toBool(), true);
    QCOMPARE(squelchSpy.constFirst().at(1).toInt(), 255);
    QCOMPARE(txPowerSpy.count(), 1);
    QCOMPARE(txPowerSpy.constFirst().constFirst().toInt(), 255);
}

void VfoModelTest::preampLevelUpdatesDerivedState()
{
    VfoModel model(nullptr);
    QSignalSpy levelSpy(&model, &VfoModel::preampLevelChanged);
    QSignalSpy enabledSpy(&model, &VfoModel::preampChanged);

    model.applyPreampLevel(9);
    model.applyPreampLevel(3);
    QCOMPARE(model.preampLevel(), 3);
    QVERIFY(model.preampOn());
    QCOMPARE(levelSpy.count(), 1);
    QCOMPARE(enabledSpy.count(), 1);

    model.applyPreampEnabled(false);
    QCOMPARE(model.preampLevel(), 0);
    QVERIFY(!model.preampOn());
    QCOMPARE(levelSpy.count(), 2);
    QCOMPARE(enabledSpy.count(), 2);
}

void VfoModelTest::ritOffsetIsClamped()
{
    VfoModel model(nullptr);
    QSignalSpy ritSpy(&model, &VfoModel::ritChanged);

    model.applyRitOffset(1200);
    model.applyRitOffset(999);
    QCOMPARE(model.ritHz(), short(999));
    QCOMPARE(ritSpy.count(), 1);

    model.applyRitEnabled(true);
    QVERIFY(model.ritOn());
    QCOMPARE(ritSpy.count(), 2);
    QCOMPARE(ritSpy.constLast().at(0).toBool(), true);
    QCOMPARE(ritSpy.constLast().at(1).value<short>(), short(999));
}

void VfoModelTest::filterAndPttSignalsAreDeduplicated()
{
    VfoModel model(nullptr);
    QSignalSpy filterSpy(&model, &VfoModel::filterChanged);
    QSignalSpy pttSpy(&model, &VfoModel::txActiveChanged);

    model.setFilterWidth(-8000, 8000);
    model.setFilterWidth(-6000, 6000);
    model.setFilterWidth(-6000, 6000);
    model.applyPtt(true);
    model.applyPtt(true);

    QCOMPARE(model.filterLow(), -6000);
    QCOMPARE(model.filterHigh(), 6000);
    QCOMPARE(filterSpy.count(), 1);
    QVERIFY(model.txActive());
    QCOMPARE(pttSpy.count(), 1);
}

QTEST_GUILESS_MAIN(VfoModelTest)
#include "VfoModelTest.moc"
