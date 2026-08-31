// QtTest invokes private slots through the generated meta-object.
#include "IRadioBackend.h"
#include "SameBandRefreshPolicy.h"
#include "VfoReceiverCommandRoute.h"
#include "VfoModel.h"
#include "RadioState.h"
#include "VfoController.h"
#include "VfoDisplay.h"

#include <QLabel>
#include <QPushButton>
#include <QWidget>
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
    void applyVfoBandRecall(Vfo vfo, const VfoBandRecallRequest& recall) override
    {
        recalledVfo = vfo;
        recalledBand = recall;
        ++bandRecallCalls;
    }
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
    void selectRadioMemory(quint16 group, quint16 channel, Vfo targetVfo) override
    {
        selectedMemoryGroup = group;
        selectedMemoryChannel = channel;
        selectedMemoryVfo = targetVfo;
        ++selectedMemoryCalls;
    }
    void requestRadioMemory(quint16, quint16) override {}
    void requestSatelliteMemory(quint16) override {}
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
    quint16 selectedMemoryGroup{0};
    quint16 selectedMemoryChannel{0};
    Vfo selectedMemoryVfo{Vfo::Main};
    int selectedMemoryCalls{0};
    Vfo recalledVfo{Vfo::Main};
    VfoBandRecallRequest recalledBand;
    int bandRecallCalls{0};
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
    void radioStateKeepsReceiverAndBandRecallIsolated();
    void radioStateInvalidatesLiveStateButKeepsSessionRecallSeparate();
    void vfoDisplayConsumesConfirmedRadioStateWithoutReceiverBleed();
};

void VfoBackendTest::vfoDisplayConsumesConfirmedRadioStateWithoutReceiverBleed()
{
    FakeRadioBackend backend;
    sdr9700::RadioState state(&backend);
    QWidget parent;
    VfoController mainController(Vfo::Main, &backend, &state, &parent);
    VfoController subController(Vfo::Sub, &backend, &state, &parent);

    Frequency mainFrequency;
    mainFrequency.Hz = 145250000;
    Frequency subFrequency;
    subFrequency.Hz = 443250000;
    ModeInfo mode;
    mode.mk = modeFM;
    mode.name = QStringLiteral("FM");
    mode.filter = 1;
    Frequency mainOffset;
    mainOffset.Hz = 600000;
    Frequency subOffset;
    subOffset.Hz = 5000000;

    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(mainFrequency), 0);
    emit backend.radioValueConfirmed(funcModeGet, QVariant::fromValue(mode), 0);
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(subFrequency), 1);
    emit backend.radioValueConfirmed(funcModeGet, QVariant::fromValue(mode), 1);

    QTRY_COMPARE(mainController.display()->frequencyText(), QStringLiteral("145.250.000"));
    QTRY_COMPARE(subController.display()->frequencyText(), QStringLiteral("443.250.000"));
    QCOMPARE(mainController.band(), band2m);
    QCOMPARE(subController.band(), band70cm);
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoModeButton"))->text(),
             QStringLiteral("FM"));
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoOFFSETButton"))->text(),
             QStringLiteral("--"));

    // A duplex direction without its companion offset is partial state. It
    // must not fabricate a confirmed zero-offset display while replies are
    // still arriving.
    emit backend.radioValueConfirmed(funcSplitStatus, QVariant::fromValue(dmDupMinus), 0);
    emit backend.radioValueConfirmed(funcSplitStatus, QVariant::fromValue(dmDupPlus), 1);
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoOFFSETButton"))->text(),
             QStringLiteral("--"));
    emit backend.radioValueConfirmed(funcReadFreqOffset, QVariant::fromValue(mainOffset), 0);
    emit backend.radioValueConfirmed(funcReadFreqOffset, QVariant::fromValue(subOffset), 1);
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoOFFSETButton"))->text(),
             QStringLiteral("-0.600"));
    QCOMPARE(subController.display()->findChild<QPushButton*>(QStringLiteral("vfoOFFSETButton"))->text(),
             QStringLiteral("+5.000"));

    RptrAccessData toneAccess;
    toneAccess.accessMode = ratrTN;
    emit backend.radioValueConfirmed(funcToneSquelchType, QVariant::fromValue(toneAccess), 0);
    emit backend.radioValueConfirmed(funcToneSquelchType, QVariant::fromValue(toneAccess), 1);
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoTONEButton"))->text(),
             QStringLiteral("TONE --"));
    QCOMPARE(subController.display()->findChild<QPushButton*>(QStringLiteral("vfoTONEButton"))->text(),
             QStringLiteral("TONE --"));
    emit backend.radioValueConfirmed(funcToneFreq, QVariant::fromValue(ToneInfo(885)), 0);
    emit backend.radioValueConfirmed(funcToneFreq, QVariant::fromValue(ToneInfo(1035)), 1);
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoTONEButton"))->text(),
             QStringLiteral("TONE 88.5"));
    QCOMPARE(subController.display()->findChild<QPushButton*>(QStringLiteral("vfoTONEButton"))->text(),
             QStringLiteral("TONE 103.5"));

    for (int i = 0; i < 10000; ++i)
    {
        mainFrequency.Hz = 145250000 + static_cast<quint64>(i);
        emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(mainFrequency), 0);
        QCOMPARE(subController.frequencyHz(), quint64(443250000));
        QCOMPARE(subController.display()->frequencyText(), QStringLiteral("443.250.000"));
    }

    // Disabling Dual Watch removes the live SUB receiver but intentionally
    // retains its session recall for a later re-enable.
    emit backend.radioValueConfirmed(funcVFODualWatch, QVariant::fromValue(false), 0);
    QCOMPARE(subController.frequencyHz(), quint64(0));
    QCOMPARE(subController.display()->frequencyText(), QStringLiteral("---.---.---"));
    QCOMPARE(state.bandRecall(Vfo::Sub, band70cm)->frequencyHz, std::optional<quint64>(443250000));
}

void VfoBackendTest::radioStateKeepsReceiverAndBandRecallIsolated()
{
    FakeRadioBackend backend;
    sdr9700::RadioState state(&backend);

    Frequency mainFrequency;
    mainFrequency.Hz = 145250000;
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(mainFrequency), 0);
    emit backend.radioValueConfirmed(funcSplitStatus, QVariant::fromValue(dmDupMinus), 0);
    Frequency mainOffset;
    mainOffset.Hz = 600000;
    emit backend.radioValueConfirmed(funcReadFreqOffset, QVariant::fromValue(mainOffset), 0);

    Frequency subFrequency;
    subFrequency.Hz = 443250000;
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(subFrequency), 1);
    emit backend.radioValueConfirmed(funcSplitStatus, QVariant::fromValue(dmDupPlus), 1);
    Frequency subOffset;
    subOffset.Hz = 5000000;
    emit backend.radioValueConfirmed(funcReadFreqOffset, QVariant::fromValue(subOffset), 1);

    QCOMPARE(state.receiver(Vfo::Main).frequencyHz, std::optional<quint64>(145250000));
    QCOMPARE(state.receiver(Vfo::Main).duplexMode, std::optional<duplexMode_t>(dmDupMinus));
    QCOMPARE(state.receiver(Vfo::Sub).frequencyHz, std::optional<quint64>(443250000));
    QCOMPARE(state.receiver(Vfo::Sub).duplexMode, std::optional<duplexMode_t>(dmDupPlus));

    const sdr9700::RadioState::BandRecall* main2m = state.bandRecall(Vfo::Main, band2m);
    const sdr9700::RadioState::BandRecall* sub70cm = state.bandRecall(Vfo::Sub, band70cm);
    QVERIFY(main2m != nullptr);
    QVERIFY(sub70cm != nullptr);
    QCOMPARE(main2m->repeaterOffsetHz, std::optional<quint64>(600000));
    QCOMPARE(sub70cm->repeaterOffsetHz, std::optional<quint64>(5000000));

    // Hammer receiver-tagged values to ensure no update can bleed across the
    // logical receiver boundary under sustained polling pressure.
    for (int i = 0; i < 10000; ++i)
    {
        mainOffset.Hz = 600000 + static_cast<quint64>(i);
        emit backend.radioValueConfirmed(funcReadFreqOffset, QVariant::fromValue(mainOffset), 0);
        QCOMPARE(state.receiver(Vfo::Sub).repeaterOffsetHz, std::optional<quint64>(5000000));
    }
}

void VfoBackendTest::radioStateInvalidatesLiveStateButKeepsSessionRecallSeparate()
{
    FakeRadioBackend backend;
    sdr9700::RadioState state(&backend);

    Frequency frequency;
    frequency.Hz = 146940000;
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(frequency), 0);
    emit backend.radioValueConfirmed(funcSplitStatus, QVariant::fromValue(dmDupMinus), 0);

    frequency.Hz = 1296100000;
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(frequency), 0);
    QVERIFY(!state.receiver(Vfo::Main).duplexMode.has_value());
    QCOMPARE(state.receiver(Vfo::Main).band, band23cm);
    QCOMPARE(state.bandRecall(Vfo::Main, band2m)->duplexMode, std::optional<duplexMode_t>(dmDupMinus));

    // A confirmed value must repopulate the new receiver snapshot even when
    // its payload is identical to the prior band's value. The protocol cache
    // may regard this as unchanged, but it is new evidence for RadioState
    // after the band transition invalidated the old operating context.
    emit backend.radioValueConfirmed(funcSplitStatus, QVariant::fromValue(dmDupMinus), 0);
    QCOMPARE(state.receiver(Vfo::Main).duplexMode, std::optional<duplexMode_t>(dmDupMinus));

    emit backend.readyChanged(true);
    emit backend.readyChanged(false);
    QVERIFY(!state.receiver(Vfo::Main).frequencyHz.has_value());
    // A temporary loss of readiness invalidates the live snapshot, but the
    // confirmed recall remains useful if this same radio session recovers.
    QCOMPARE(state.bandRecall(Vfo::Main, band2m)->duplexMode, std::optional<duplexMode_t>(dmDupMinus));

    emit backend.disconnected();
    QVERIFY(!state.receiver(Vfo::Main).frequencyHz.has_value());
    QVERIFY(!state.shared().selectedVfo.has_value());
    // Recall is session-local and must never leak from one connection into a
    // later radio session, even when the application process remains alive.
    QVERIFY(!state.bandRecall(Vfo::Main, band2m)->duplexMode.has_value());
}

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
    constexpr int kIterationCount = 10000;
    // A five-digit mixed-pressure run covers many overlapping exchange,
    // tuning-holdoff, PTT-transition, and dual-watch meter phases.
    Vfo activeVfo = Vfo::Main;
    int pollTick = 0;
    int activeSamples = 0;
    int inactiveSamples = 0;

    for (int iteration = 0; iteration < kIterationCount; ++iteration)
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

QTEST_MAIN(VfoBackendTest)
#include "VfoBackendTest.moc"
