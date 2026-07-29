// QtTest invokes private slots through the generated meta-object.
#include "IRadioBackend.h"
#include "VfoModel.h"

#include <QtTest>

class FakeRadioBackend : public IRadioBackend
{
  public:
    using IRadioBackend::IRadioBackend;

    void connectToRadio(const QString&, quint16, const QString&, const QString&) override {}
    void disconnectFromRadio() override {}
    void setFrequencyHz(quint64 hz) override
    {
        frequencyHz = hz;
        ++frequencyCalls;
    }
    void setMode(const QString& value) override
    {
        mode = value;
        ++modeCalls;
    }
    void setFilterWidth(int low, int high) override
    {
        filterLow = low;
        filterHigh = high;
    }
    void setNrEnabled(bool) override {}
    void setNrLevel(int value) override { nrLevel = value; }
    void setNbEnabled(bool) override {}
    void setNbLevel(int value) override { nbLevel = value; }
    void setPreampEnabled(bool) override {}
    void setPreampLevel(int value) override { preampLevel = value; }
    void setAttenuatorEnabled(bool) override {}
    void setAfGain(int value) override { afGain = value; }
    void setRfGain(int value) override { rfGain = value; }
    void setSquelch(bool on, int value) override
    {
        squelchOn = on;
        squelchLevel = value;
    }
    void setAgcMode(const QString&) override {}
    void setAutoNotch(bool) override {}
    void setManualNotch(bool) override {}
    void setCompressor(bool) override {}
    void setXfcEnabled(bool) override {}
    void setRitEnabled(bool) override {}
    void setRitOffset(short value) override { ritOffset = value; }
    void setDuplexMode(duplexMode_t) override {}
    void setRepeaterOffsetHz(quint64) override {}
    void setToneAccessMode(rptAccessTxRx_t) override {}
    void setToneFrequency(ushort) override {}
    void setDtcsCode(ushort) override {}
    void setScopeEnabled(bool) override {}
    void setScopeSpanHz(quint64) override {}
    void setScopeMode(int) override {}
    void setScopeFixedRangeHz(quint64, quint64) override {}
    void setPtt(bool value) override { ptt = value; }
    void setTxPower(int value) override { txPower = value; }
    void setTuningStep(int) override {}
    void pollFrequency() override {}
    void selectVfoMode() override {}
    void selectRadioMemory(quint16, quint16) override {}
    void requestRadioMemory(quint16, quint16) override {}
    void writeRadioMemory(MemoryType) override {}

    quint64 frequencyHz{0};
    QString mode;
    int filterLow{0};
    int filterHigh{0};
    int preampLevel{-1};
    int nrLevel{-1};
    int nbLevel{-1};
    int afGain{-1};
    int rfGain{-1};
    bool squelchOn{false};
    int squelchLevel{-1};
    short ritOffset{0};
    bool ptt{false};
    int txPower{-1};
    int frequencyCalls{0};
    int modeCalls{0};
};

class VfoBackendTest : public QObject
{
    Q_OBJECT

  private slots:
    void radioBackedRequestsWaitForConfirmation();
    void localControlsUpdateAndForward();
    void boundedRequestsAreClampedBeforeForwarding();
};

void VfoBackendTest::radioBackedRequestsWaitForConfirmation()
{
    FakeRadioBackend backend;
    VfoModel model(&backend);
    QSignalSpy frequencySpy(&model, &VfoModel::frequencyChanged);
    QSignalSpy modeSpy(&model, &VfoModel::modeChanged);

    model.setFrequencyHz(146520000);
    model.setMode(QStringLiteral("DV"));

    QCOMPARE(backend.frequencyHz, quint64(146520000));
    QCOMPARE(backend.frequencyCalls, 1);
    QCOMPARE(backend.mode, QStringLiteral("DV"));
    QCOMPARE(backend.modeCalls, 1);
    QCOMPARE(model.frequencyHz(), quint64(145000000));
    QCOMPARE(model.mode(), QStringLiteral("FM"));
    QCOMPARE(frequencySpy.count(), 0);
    QCOMPARE(modeSpy.count(), 0);
}

void VfoBackendTest::localControlsUpdateAndForward()
{
    FakeRadioBackend backend;
    VfoModel model(&backend);
    QSignalSpy filterSpy(&model, &VfoModel::filterChanged);
    QSignalSpy afGainSpy(&model, &VfoModel::afGainChanged);

    model.setFilterWidth(-3000, 3000);
    model.setAfGain(200);
    model.setPtt(true);

    QCOMPARE(model.filterLow(), -3000);
    QCOMPARE(model.filterHigh(), 3000);
    QCOMPARE(backend.filterLow, -3000);
    QCOMPARE(backend.filterHigh, 3000);
    QCOMPARE(filterSpy.count(), 1);
    QCOMPARE(backend.afGain, 200);
    QCOMPARE(afGainSpy.count(), 1);
    QVERIFY(backend.ptt);
}

void VfoBackendTest::boundedRequestsAreClampedBeforeForwarding()
{
    FakeRadioBackend backend;
    VfoModel model(&backend);

    model.setPreampLevel(10);
    model.setRitOffset(1200);
    model.setNrLevel(300);
    model.setNbLevel(300);
    model.setRfGain(300);
    model.setSquelch(true, 300);
    model.setTxPower(300);

    QCOMPARE(backend.preampLevel, 3);
    QCOMPARE(backend.nrLevel, 15);
    QCOMPARE(backend.nbLevel, 10);
    QCOMPARE(model.preampLevel(), 0);
    QCOMPARE(backend.ritOffset, short(999));
    QCOMPARE(model.ritHz(), short(0));
    QCOMPARE(backend.rfGain, 255);
    QVERIFY(backend.squelchOn);
    QCOMPARE(backend.squelchLevel, 255);
    QCOMPARE(backend.txPower, 255);
}

QTEST_GUILESS_MAIN(VfoBackendTest)
#include "VfoBackendTest.moc"
