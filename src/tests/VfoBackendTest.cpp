// QtTest invokes private slots through the generated meta-object.
#include "IRadioBackend.h"
#include "SameBandRefreshPolicy.h"
#include "VfoReceiverCommandRoute.h"
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
    void setCompressorLevel(int value) override { compressorLevel = value; }
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
    void setScopeVfo(Vfo) override {}
    void setScopeFixedRangeHz(quint64, quint64) override {}
    bool setPtt(bool value) override
    {
        ptt = value;
        return pttAccepted;
    }
    void setTxPower(int value) override { txPower = value; }
    void setTuningStep(int) override {}
    void selectVfo(Vfo value) override { selectedVfo = value; }
    void exchangeMainSub() override { ++exchangeCalls; }
    void setVfoFrequencyHz(Vfo, quint64) override {}
    void setVfoMode(Vfo, const QString&) override {}
    void requestVfoState(Vfo value) override { requestedVfoState = value; }
    void setVfoAgcMode(Vfo, const QString&) override {}
    void setVfoAttenuatorEnabled(Vfo, bool) override {}
    void setVfoNbEnabled(Vfo, bool) override {}
    void setVfoNotch(Vfo, VfoNotch) override {}
    void setVfoNrEnabled(Vfo, bool) override {}
    void setVfoPreampLevel(Vfo, int) override {}
    void setVfoRfGain(Vfo, int) override {}
    void setVfoSquelch(Vfo, int) override {}
    void setDualWatchEnabled(bool enabled) override { dualWatchEnabled = enabled; }
    void pollFrequency() override {}
    void selectVfoMode() override {}
    void selectRadioMemory(quint16, quint16) override {}
    void requestRadioMemory(quint16, quint16) override {}
    void writeRadioMemory(MemoryType) override {}

    quint64 frequencyHz{0};
    QString mode;
    int filterLow{0};
    int filterHigh{0};
    int compressorLevel{0};
    int preampLevel{-1};
    int nrLevel{-1};
    int nbLevel{-1};
    int afGain{-1};
    int rfGain{-1};
    bool squelchOn{false};
    int squelchLevel{-1};
    short ritOffset{0};
    bool ptt{false};
    bool pttAccepted{true};
    int txPower{-1};
    int frequencyCalls{0};
    int modeCalls{0};
    Vfo selectedVfo{Vfo::Main};
    Vfo requestedVfoState{Vfo::Main};
    bool dualWatchEnabled{false};
    int exchangeCalls{0};
};

class VfoBackendTest : public QObject
{
    Q_OBJECT

  private slots:
    void radioBackedRequestsWaitForConfirmation();
    void localControlsUpdateAndForward();
    void boundedRequestsAreClampedBeforeForwarding();
    void reportsRejectedPttRequest();
    void receiverCommandRouteSelectsCommandsAndRestoresInOrder();
    void mapsVfoToReceiverByte();
    void alternatesInactiveMeterSamplesDuringDualWatch();
    void suppressesReceiverMeterPollingDuringContextTransitions();
    void survivesRepeatedExchangeAndTunePressure();
    void sameBandRefreshIsEdgeTriggered();
};

void VfoBackendTest::sameBandRefreshIsEdgeTriggered()
{
    sdr9700::SameBandRefreshPolicy policy;

    QVERIFY(!policy.observe(0, 0));
    QVERIFY(!policy.observe(144200000, 432100000));
    QVERIFY(!policy.observe(144200000, 145000000));
    QVERIFY(policy.observe(144200000, 144200000));
    QVERIFY(!policy.observe(144200000, 144200000));

    QVERIFY(!policy.observe(432100000, 145100000));
    QVERIFY(!policy.observe(432100000, 433000000));
    QVERIFY(policy.observe(432100000, 432100000));

    policy.reset();
    QVERIFY(policy.observe(1296100000, 1296100000));
}

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
    model.setCompressorLevel(300);
    model.setRfGain(300);
    model.setSquelch(true, 300);
    model.setTxPower(300);

    QCOMPARE(backend.preampLevel, 3);
    QCOMPARE(backend.nrLevel, 15);
    QCOMPARE(backend.nbLevel, 10);
    QCOMPARE(backend.compressorLevel, 255);
    QCOMPARE(model.preampLevel(), 0);
    QCOMPARE(backend.ritOffset, short(999));
    QCOMPARE(model.ritHz(), short(0));
    QCOMPARE(backend.rfGain, 255);
    QVERIFY(backend.squelchOn);
    QCOMPARE(backend.squelchLevel, 255);
    QCOMPARE(backend.txPower, 255);
}

void VfoBackendTest::reportsRejectedPttRequest()
{
    FakeRadioBackend backend;
    VfoModel model(&backend);
    backend.pttAccepted = false;

    QVERIFY(!model.setPtt(true));
    QVERIFY(backend.ptt);
}

void VfoBackendTest::receiverCommandRouteSelectsCommandsAndRestoresInOrder()
{
    QStringList events;
    sdr9700::backend::routeVfoReceiverCommand(
        Vfo::Sub, Vfo::Main, [&events](Vfo selected)
        { events.append(selected == Vfo::Sub ? QStringLiteral("select-sub") : QStringLiteral("select-main")); },
        [&events](uchar receiver) { events.append(QStringLiteral("command-%1").arg(receiver)); });
    QCOMPARE(events,
             QStringList({QStringLiteral("select-sub"), QStringLiteral("command-1"), QStringLiteral("select-main")}));

    events.clear();
    sdr9700::backend::routeVfoReceiverCommand(
        Vfo::Sub, Vfo::Sub, [&events](Vfo) { events.append(QStringLiteral("select-sub")); },
        [&events](uchar receiver) { events.append(QStringLiteral("command-%1").arg(receiver)); });
    QCOMPARE(events, QStringList({QStringLiteral("select-sub"), QStringLiteral("command-1")}));

    events.clear();
    sdr9700::backend::routeVfoReceiverCommand(
        Vfo::Main, Vfo::Main, [&events](Vfo) { events.append(QStringLiteral("select-main")); },
        [&events](uchar receiver) { events.append(QStringLiteral("command-%1").arg(receiver)); });
    QCOMPARE(events, QStringList({QStringLiteral("select-main"), QStringLiteral("command-0")}));
}

void VfoBackendTest::mapsVfoToReceiverByte()
{
    QCOMPARE(sdr9700::backend::receiverForVfo(Vfo::Main), uchar(0));
    QCOMPARE(sdr9700::backend::receiverForVfo(Vfo::Sub), uchar(1));
}

void VfoBackendTest::alternatesInactiveMeterSamplesDuringDualWatch()
{
    for (int tick = 0; tick < 10; ++tick)
    {
        const Vfo expected = tick % 5 == 4 ? Vfo::Sub : Vfo::Main;
        QCOMPARE(sdr9700::backend::meterPollTarget(Vfo::Main, true, tick), expected);
    }
    QCOMPARE(sdr9700::backend::meterPollTarget(Vfo::Sub, true, 4), Vfo::Main);
    QCOMPARE(sdr9700::backend::meterPollTarget(Vfo::Sub, false, 4), Vfo::Sub);
}

void VfoBackendTest::suppressesReceiverMeterPollingDuringContextTransitions()
{
    using sdr9700::backend::receiverMeterPollAllowed;
    QVERIFY(receiverMeterPollAllowed(true, false, false, false));
    QVERIFY(!receiverMeterPollAllowed(false, false, false, false));
    QVERIFY(!receiverMeterPollAllowed(true, true, false, false));
    QVERIFY(!receiverMeterPollAllowed(true, false, true, false));
    QVERIFY(!receiverMeterPollAllowed(true, false, false, true));
}

void VfoBackendTest::survivesRepeatedExchangeAndTunePressure()
{
    Vfo activeVfo = Vfo::Main;
    int pollTick = 0;
    int activeSamples = 0;
    int inactiveSamples = 0;

    for (int iteration = 0; iteration < 500; ++iteration)
    {
        const bool exchangePending = iteration % 7 == 0;
        const bool tuningHoldoff = iteration % 11 == 0;
        const bool pttTransition = iteration % 37 == 0;
        if (exchangePending)
        {
            activeVfo = activeVfo == Vfo::Main ? Vfo::Sub : Vfo::Main;
        }

        if (!sdr9700::backend::receiverMeterPollAllowed(true, pttTransition, exchangePending, tuningHoldoff))
        {
            continue;
        }

        const Vfo target = sdr9700::backend::meterPollTarget(activeVfo, true, pollTick++);
        if (target == activeVfo)
        {
            ++activeSamples;
        }
        else
        {
            ++inactiveSamples;
        }
    }

    QVERIFY(activeSamples > 0);
    QVERIFY(inactiveSamples > 0);
    QCOMPARE(activeSamples, inactiveSamples * 4 + (pollTick % 5));
}

QTEST_GUILESS_MAIN(VfoBackendTest)
#include "VfoBackendTest.moc"
