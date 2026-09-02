#include "VfoController.h"

#include "MainWindowHelpers.h"
#include "UiTheme.h"
#include "VfoDisplay.h"
#include "backend/IRadioBackend.h"
#include "backend/TransmitFrequencyPolicy.h"
#include "models/VfoModel.h"
#include "models/RadioState.h"

#include <QAction>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <memory>

VfoController::VfoController(Vfo vfo, IRadioBackend* backend, sdr9700::RadioState* radioState, QWidget* displayParent,
                             QObject* parent)
    : QObject(parent),
      m_vfo(vfo),
      m_backend(backend),
      m_radioState(radioState),
      m_display(new VfoDisplay(vfo, displayParent))
{
    m_initialPublishTimer.setSingleShot(true);
    m_initialPublishTimer.setInterval(250);
    connect(&m_initialPublishTimer, &QTimer::timeout, this,
            [this]()
            {
                m_initialStatePublished = true;
                publishConfirmedState();
            });
    m_bandRecallSettleTimer.setSingleShot(true);
    m_bandRecallSettleTimer.setInterval(250);
    connect(&m_bandRecallSettleTimer, &QTimer::timeout, this, &VfoController::finishPendingBandRecall);
    m_bandRecallTimeoutTimer.setSingleShot(true);
    m_bandRecallTimeoutTimer.setInterval(5000);
    connect(&m_bandRecallTimeoutTimer, &QTimer::timeout, this,
            [this]()
            {
                qWarning() << "Band recall confirmation timed out; refreshing receiver state"
                           << "vfo=" << (m_vfo == Vfo::Main ? "MAIN" : "SUB")
                           << "band=" << static_cast<int>(m_pendingBandRecall.value_or(bandUnknown));
                m_pendingBandRecall.reset();
                updateDisplayEnabled();
                if (m_backend)
                {
                    m_backend->requestVfoState(m_vfo);
                }
            });

    connect(m_display, &VfoDisplay::frequencySubmitted, this,
            [this](const QString& text)
            {
                quint64 hz = 0;
                const bool valid = sdr9700::ui::main_window::parseFrequencyText(text, &hz) &&
                                   sdr9700::ui::main_window::vfoBandIndexForHz(hz) >= 0;
                if (valid)
                {
                    requestFrequencyHz(hz);
                }
                if (frequencyHz() > 0)
                {
                    m_display->setFrequencyHz(frequencyHz());
                }
                else
                {
                    m_display->clearFrequency();
                }
            });
    connect(m_display, &VfoDisplay::vfoClicked, this, [this]() { emit selectionRequested(m_vfo); });
    connect(m_display, &VfoDisplay::bandClicked, this,
            [this]() { emit bandMenuRequested(m_vfo, m_display->bandMenuPosition()); });
    connect(m_display, &VfoDisplay::modeClicked, this, &VfoController::showModeMenu);
    connect(m_display, &VfoDisplay::receiverControlClicked, this,
            [this](const QString& control)
            {
                if (control == QStringLiteral("TONE"))
                {
                    emit toneMenuRequested(m_vfo, m_display->receiverControlMenuPosition(control));
                    return;
                }
                if (control == QStringLiteral("OFFSET"))
                {
                    emit offsetMenuRequested(m_vfo, m_display->receiverControlMenuPosition(control));
                    return;
                }
                if (control == QStringLiteral("XFC") && m_vfo == Vfo::Main && m_backend)
                {
                    m_backend->setXfcEnabled(!m_xfcEnabled);
                    return;
                }
                if (control == QStringLiteral("COMP") && m_vfo == Vfo::Main)
                {
                    emit compressorMenuRequested(m_vfo, m_display->receiverControlMenuPosition(control));
                    return;
                }
                showReceiverControlMenu(control);
            });
    if (m_backend)
    {
        if (m_radioState)
        {
            connect(m_radioState, &sdr9700::RadioState::receiverStateChanged, this,
                    [this](Vfo changedVfo)
                    {
                        if (changedVfo == m_vfo)
                        {
                            applyRadioState();
                        }
                    });
        }
        if (m_vfo == Vfo::Main)
        {
            connect(m_backend, &IRadioBackend::powerMeterChanged, m_display, &VfoDisplay::setTransmitPowerWatts);
            connect(m_backend, &IRadioBackend::swrChanged, m_display, &VfoDisplay::setTransmitSwr);
            connect(m_backend, &IRadioBackend::xfcChanged, this,
                    [this](bool enabled)
                    {
                        m_xfcEnabled = enabled;
                        updateReceiverControlDisplay();
                    });
            connect(m_backend, &IRadioBackend::compressorChanged, this,
                    [this](bool enabled)
                    {
                        m_compressorEnabled = enabled;
                        updateReceiverControlDisplay();
                    });
        }
        connect(m_backend, &IRadioBackend::radioValueUpdated, this,
                [this](Funcs func, const QVariant& value, uchar receiver)
                {
                    const uchar expectedReceiver = m_vfo == Vfo::Main ? 0 : 1;
                    if (receiver != expectedReceiver ||
                        (func != funcAGCTimeConstant && func != funcAttenuator && func != funcNoiseBlanker &&
                         func != funcNBLevel && func != funcAutoNotch && func != funcManualNotch &&
                         func != funcNoiseReduction && func != funcNRLevel && func != funcPreamp &&
                         func != funcRfGain && func != funcSquelch && func != funcRFPower && func != funcSMeter))
                    {
                        return;
                    }
                    switch (func)
                    {
                    case funcAGCTimeConstant:
                        m_agcMode = qBound(0, value.toInt(), 3);
                        updateReceiverControlDisplay();
                        return;
                    case funcAttenuator:
                        m_attenuatorEnabled = value.toInt() != 0;
                        updateReceiverControlDisplay();
                        return;
                    case funcNoiseBlanker:
                        m_nbEnabled = value.toBool();
                        updateReceiverControlDisplay();
                        return;
                    case funcNBLevel:
                        m_nbLevel = qBound(1, qRound(value.toInt() * 9.0 / 255.0) + 1, 10);
                        m_nbLevelReceived = true;
                        updateReceiverControlDisplay();
                        return;
                    case funcAutoNotch:
                        m_autoNotchEnabled = value.toBool();
                        updateReceiverControlDisplay();
                        return;
                    case funcManualNotch:
                        m_manualNotchEnabled = value.toBool();
                        updateReceiverControlDisplay();
                        return;
                    case funcNoiseReduction:
                        m_nrEnabled = value.toBool();
                        updateReceiverControlDisplay();
                        return;
                    case funcNRLevel:
                        m_nrLevel = qBound(1, qRound(value.toInt() * 14.0 / 255.0) + 1, 15);
                        m_nrLevelReceived = true;
                        updateReceiverControlDisplay();
                        return;
                    case funcPreamp:
                        m_preampLevel = qBound(0, value.toInt(), 3);
                        updateReceiverControlDisplay();
                        return;
                    case funcRfGain:
                        m_rfGain = qBound(0, value.toInt(), 255);
                        updateReceiverControlDisplay();
                        return;
                    case funcSquelch:
                        m_squelch = qBound(0, value.toInt(), 255);
                        updateReceiverControlDisplay();
                        return;
                    case funcRFPower:
                        if (m_vfo == Vfo::Main)
                        {
                            m_txPower = qBound(0, value.toInt(), 255);
                            updateReceiverControlDisplay();
                        }
                        return;
                    case funcSMeter:
                        if (stateReady())
                        {
                            m_display->setSMeterValue(qBound(0, value.toInt(), 255));
                        }
                        return;
                    default:
                        return;
                    }
                });
        connect(m_backend, &IRadioBackend::disconnected, this,
                [this]()
                {
                    // Level values are radio-authoritative. A value learned in
                    // one LAN session must not be presented during the next
                    // session before that radio has reported its current state.
                    m_nbLevelReceived = false;
                    m_nrLevelReceived = false;
                    m_display->setReceiverControlState(QStringLiteral("FILTERS"), QString(), false);
                    m_display->setReceiverControlToolTip(QStringLiteral("FILTERS"), QString());
                });
        connect(m_backend, &IRadioBackend::readyChanged, this,
                [this](bool ready)
                {
                    if (!ready)
                    {
                        clearFrequency();
                        return;
                    }
                    if (m_vfo == Vfo::Sub)
                    {
                        m_backend->requestVfoState(m_vfo);
                        QTimer::singleShot(750, this,
                                           [this]()
                                           {
                                               if (confirmedMode().isEmpty() && m_backend)
                                               {
                                                   m_backend->requestVfoState(m_vfo);
                                               }
                                           });
                    }
                });
    }
    updateReceiverControlDisplay();
    updateDisplayEnabled();
}

const sdr9700::RadioState::Receiver* VfoController::confirmedReceiverState() const
{
    return m_radioState ? &m_radioState->receiver(m_vfo) : nullptr;
}

availableBands VfoController::band() const
{
    const auto* state = confirmedReceiverState();
    return state ? state->band : m_fallbackBand;
}

quint64 VfoController::frequencyHz() const
{
    const auto* state = confirmedReceiverState();
    return state ? state->frequencyHz.value_or(0) : m_fallbackFrequencyHz.value_or(0);
}

QString VfoController::confirmedMode() const
{
    const auto* state = confirmedReceiverState();
    return state ? state->mode.value_or(QString()) : m_fallbackMode;
}

std::optional<int> VfoController::confirmedFilter() const
{
    const auto* state = confirmedReceiverState();
    return state ? state->filter : std::nullopt;
}

std::optional<duplexMode_t> VfoController::confirmedDuplexMode() const
{
    const auto* state = confirmedReceiverState();
    return state ? state->duplexMode : std::optional<duplexMode_t>(m_fallbackDuplexMode);
}

std::optional<quint64> VfoController::confirmedRepeaterOffsetHz() const
{
    const auto* state = confirmedReceiverState();
    return state ? state->repeaterOffsetHz : std::optional<quint64>(m_fallbackRepeaterOffsetHz);
}

std::optional<rptAccessTxRx_t> VfoController::confirmedToneAccessMode() const
{
    const auto* state = confirmedReceiverState();
    return state ? state->toneAccessMode : std::optional<rptAccessTxRx_t>(m_fallbackToneAccessMode);
}

std::optional<ushort> VfoController::confirmedToneFrequency() const
{
    const auto* state = confirmedReceiverState();
    if (!state)
    {
        return m_fallbackToneFrequency;
    }
    const std::optional<rptAccessTxRx_t> accessMode = confirmedToneAccessMode();
    if (!accessMode.has_value())
    {
        return std::nullopt;
    }
    const bool displayRxTone = *accessMode == ratrNT || *accessMode == ratrDT;
    return displayRxTone ? state->toneSquelchFrequency : state->toneFrequency;
}

std::optional<ushort> VfoController::confirmedDtcsCode() const
{
    const auto* state = confirmedReceiverState();
    return state ? state->dtcsCode : std::optional<ushort>(m_fallbackDtcsCode);
}

void VfoController::applyRadioState()
{
    if (frequencyHz() == 0)
    {
        clearFrequency();
        return;
    }
    publishConfirmedState();
    if (m_pendingBandRecall.has_value())
    {
        if (pendingBandRecallIdentityIsConfirmed())
        {
            // Once the complete identity is present, begin one quiet-period
            // deadline. Stable polling updates RadioState at the same cadence
            // as this timer; restarting it for every unrelated confirmation
            // can postpone completion forever and leave the VFO locked until
            // the five-second recovery timeout fires.
            if (!m_bandRecallSettleTimer.isActive())
            {
                m_bandRecallSettleTimer.start();
            }
        }
        else
        {
            m_bandRecallSettleTimer.stop();
        }
    }
}

void VfoController::setFrequencyHz(quint64 hz)
{
    if (m_radioState)
    {
        applyRadioState();
        return;
    }
    m_fallbackFrequencyHz = hz;
    m_fallbackBand = sdr9700::radioBandForFrequency(hz);
    const int bandIndex = sdr9700::radioBandUiIndex(m_fallbackBand);
    if (bandIndex >= 0)
    {
        m_lastBandFrequencyHz[static_cast<std::size_t>(bandIndex)] = hz;
    }
    publishConfirmedState();
}

void VfoController::requestFrequencyHz(quint64 hz)
{
    if (hz == frequencyHz())
    {
        return;
    }
    if (!m_backend)
    {
        setFrequencyHz(hz);
        return;
    }

    emit frequencyRecenterRequested(m_vfo, hz);
    m_backend->setVfoFrequencyHz(m_vfo, hz);
}

void VfoController::clearFrequency()
{
    m_initialPublishTimer.stop();
    m_initialStatePublished = false;
    m_fallbackFrequencyHz.reset();
    m_publishedFrequencyHz.reset();
    m_fallbackBand = bandUnknown;
    m_display->clearFrequency();
    m_fallbackMode.clear();
    m_display->setSMeterValue(0);
    updateDisplayEnabled();
}

void VfoController::setOperatingEnabled(bool enabled)
{
    m_operatingEnabled = enabled;
    updateDisplayEnabled();
}

void VfoController::setUserInteractionEnabled(bool enabled)
{
    m_userInteractionEnabled = enabled;
    updateDisplayEnabled();
}

void VfoController::setTuningInteractionEnabled(bool enabled)
{
    m_display->setTuningEnabled(enabled);
}

void VfoController::setSelected(bool selected)
{
    m_display->setSelected(selected);
}

void VfoController::setTransmitting(bool transmitting)
{
    m_display->setTransmitting(transmitting);
    if (m_vfo == Vfo::Main)
    {
        m_display->setTransmitPowerMode(transmitting);
    }
}

void VfoController::setLanModLevel(int value)
{
    m_lanModLevel = qBound(0, value, 255);
    if (m_vfo == Vfo::Main)
    {
        m_display->setReceiverControlState(
            QStringLiteral("MOD"), QStringLiteral("%1%").arg(qRound(m_lanModLevel * 100.0 / 255.0)), m_lanModLevel > 0);
    }
}

void VfoController::captureExchangeableControlState()
{
    m_capturedExchangeState = ExchangeableControlState{
        m_agcMode,   m_attenuatorEnabled, m_nbEnabled, m_autoNotchEnabled, m_manualNotchEnabled,
        m_nrEnabled, m_preampLevel,       m_squelch};
}

void VfoController::discardCapturedExchangeableControlState()
{
    m_capturedExchangeState.reset();
}

void VfoController::applyCapturedControlExchange(VfoController* other)
{
    if (!other || !m_capturedExchangeState.has_value() || !other->m_capturedExchangeState.has_value())
    {
        return;
    }

    const ExchangeableControlState ownState = *m_capturedExchangeState;
    const ExchangeableControlState otherState = *other->m_capturedExchangeState;
    applyExchangeableControlState(otherState);
    other->applyExchangeableControlState(ownState);
    m_capturedExchangeState.reset();
    other->m_capturedExchangeState.reset();
}

void VfoController::applyExchangeableControlState(const ExchangeableControlState& state)
{
    m_agcMode = state.agcMode;
    m_attenuatorEnabled = state.attenuatorEnabled;
    m_nbEnabled = state.nbEnabled;
    m_autoNotchEnabled = state.autoNotchEnabled;
    m_manualNotchEnabled = state.manualNotchEnabled;
    m_nrEnabled = state.nrEnabled;
    m_preampLevel = state.preampLevel;
    m_squelch = state.squelch;
    updateReceiverControlDisplay();
}

bool VfoController::selectBand(availableBands requestedBand)
{
    const int bandIndex = sdr9700::radioBandUiIndex(requestedBand);
    if (!m_backend || bandIndex < 0)
    {
        return false;
    }
    if (requestedBand == band())
    {
        return true;
    }
    if (m_pendingBandRecall.has_value())
    {
        return false;
    }
    const sdr9700::RadioState::BandRecall* recall =
        m_radioState ? m_radioState->bandRecall(m_vfo, requestedBand) : nullptr;
    const quint64 stateRemembered = recall ? recall->frequencyHz.value_or(0) : 0;
    const quint64 remembered =
        stateRemembered > 0 ? stateRemembered : m_lastBandFrequencyHz[static_cast<std::size_t>(bandIndex)];
    const quint64 hz = remembered > 0 ? remembered : sdr9700::radioBandDefaultFrequency(requestedBand);
    if (hz > 0)
    {
        m_pendingBandRecall = requestedBand;
        m_bandRecallSettleTimer.stop();
        m_bandRecallTimeoutTimer.start();
        updateDisplayEnabled();
        emit frequencyRecenterRequested(m_vfo, hz);
        if (recall && recall->frequencyHz.has_value())
        {
            VfoBandRecallRequest request;
            request.frequencyHz = hz;
            request.mode = recall->mode;
            request.filter = recall->filter;
            request.duplexMode = recall->duplexMode;
            request.repeaterOffsetHz = recall->repeaterOffsetHz;
            request.toneAccessMode = recall->toneAccessMode;
            request.toneFrequency = recall->toneFrequency;
            request.toneSquelchFrequency = recall->toneSquelchFrequency;
            request.dtcsCode = recall->dtcsCode;
            m_backend->applyVfoBandRecall(m_vfo, request);
        }
        else
        {
            m_backend->setVfoFrequencyHz(m_vfo, hz);
        }
        return true;
    }
    return false;
}

bool VfoController::pendingBandRecallIdentityIsConfirmed() const
{
    if (!m_pendingBandRecall.has_value() || !m_radioState)
    {
        return false;
    }
    const auto& receiver = m_radioState->receiver(m_vfo);
    return receiver.band == *m_pendingBandRecall && receiver.frequencyHz.has_value() && receiver.mode.has_value() &&
           !receiver.mode->isEmpty() && receiver.filter.has_value();
}

void VfoController::finishPendingBandRecall()
{
    if (!pendingBandRecallIdentityIsConfirmed())
    {
        return;
    }
    m_pendingBandRecall.reset();
    m_bandRecallTimeoutTimer.stop();
    updateDisplayEnabled();
}

bool VfoController::stateReady() const
{
    // Controllers without a backend are used as inert UI fixtures in tests and
    // previews. Live radio controllers require both authoritative fields.
    return frequencyHz() > 0 && (!m_backend || !confirmedMode().isEmpty());
}

void VfoController::publishConfirmedState()
{
    if (!stateReady())
    {
        updateDisplayEnabled();
        return;
    }

    // Let the radio's remaining per-VFO replies settle before exposing the
    // first confirmed snapshot. MAIN and SUB retain independent timers so one
    // side never delays or prematurely publishes the other.
    if (m_backend && !m_initialStatePublished)
    {
        if (!m_initialPublishTimer.isActive())
        {
            m_initialPublishTimer.start();
        }
        return;
    }

    const quint64 confirmedFrequencyHz = frequencyHz();
    const availableBands confirmedBand = band();
    const QString mode = confirmedMode();
    m_display->setFrequencyHz(confirmedFrequencyHz);
    m_display->setBandText(confirmedBand == bandUnknown ? QStringLiteral("--")
                                                        : sdr9700::radioBandShortLabel(confirmedBand));
    if (m_vfo == Vfo::Main)
    {
        m_display->setMaxTransmitPowerWatts(sdr9700::radioBandMaxPowerWatts(confirmedBand));
    }
    if (!mode.isEmpty())
    {
        m_display->setModeText(mode);
    }
    updateReceiverControlDisplay();
    updateDisplayEnabled();
    emit statePublished(m_vfo);

    if (!m_publishedFrequencyHz.has_value() || *m_publishedFrequencyHz != confirmedFrequencyHz)
    {
        m_publishedFrequencyHz = confirmedFrequencyHz;
        emit frequencyChanged(confirmedFrequencyHz);
    }
}

void VfoController::updateDisplayEnabled()
{
    m_display->setOperatingEnabled(m_operatingEnabled && (!m_backend || m_userInteractionEnabled) && stateReady() &&
                                   !m_pendingBandRecall.has_value());
}

void VfoController::updateReceiverControlDisplay()
{
    if (!stateReady())
    {
        m_display->clearTransmitFrequency();
        return;
    }
    static const char* const kAgcLabels[] = {"--", "FAST", "MID", "SLOW"};
    m_display->setReceiverControlState(QStringLiteral("AGC"), QString::fromLatin1(kAgcLabels[m_agcMode]),
                                       m_agcMode > 0);
    m_display->setReceiverControlState(QStringLiteral("ATT"), QString(), m_attenuatorEnabled);
    const std::optional<int> filter = confirmedFilter();
    const QString filterLabel = filter.has_value() ? QStringLiteral("FIL%1").arg(*filter) : QStringLiteral("FIL—");
    const QString nbLabel = !m_nbLevelReceived ? QStringLiteral("NB —")
                            : m_nbEnabled      ? QStringLiteral("NB %1").arg(m_nbLevel)
                                               : QStringLiteral("NB OFF");
    const QString nrLabel = !m_nrLevelReceived ? QStringLiteral("NR —")
                            : m_nrEnabled      ? QStringLiteral("NR %1").arg(m_nrLevel)
                                               : QStringLiteral("NR OFF");
    const QString notchLabel = m_autoNotchEnabled ? QStringLiteral("NOTCH ON") : QStringLiteral("NOTCH OFF");
    m_display->setReceiverControlState(QStringLiteral("FILTERS"), QString(), filter.has_value());
    m_display->setReceiverControlToolTip(
        QStringLiteral("FILTERS"), QStringLiteral("%1 • %2 • %3 • %4").arg(filterLabel, nbLabel, notchLabel, nrLabel));
    m_display->setReceiverControlState(QStringLiteral("PRE"), QString(), (m_preampLevel & 0x01) != 0);
    const int rfPercent = qBound(0, qRound(m_rfGain * 100.0 / 255.0), 100);
    m_display->setReceiverControlState(QStringLiteral("RFG"), QString::number(rfPercent), m_rfGain > 0);
    const std::optional<duplexMode_t> duplexMode = confirmedDuplexMode();
    const std::optional<quint64> repeaterOffsetHz = confirmedRepeaterOffsetHz();
    const bool offsetKnown = duplexMode.has_value() && repeaterOffsetHz.has_value();
    const bool offsetActive = offsetKnown && (*duplexMode == dmDupMinus || *duplexMode == dmDupPlus);
    const QString offsetLabel =
        offsetKnown ? sdr9700::ui::main_window::offsetModeLabel(*duplexMode, *repeaterOffsetHz) : QStringLiteral("--");
    m_display->setReceiverControlState(QStringLiteral("OFFSET"), offsetLabel, offsetActive);
    updateTransmitFrequencyDisplay();
    const std::optional<rptAccessTxRx_t> toneAccessMode = confirmedToneAccessMode();
    const bool toneActive = toneAccessMode.has_value() && *toneAccessMode != ratrNN;
    const std::optional<ushort> toneValue = !toneActive                       ? std::nullopt
                                            : isDtcsToneMode(*toneAccessMode) ? confirmedDtcsCode()
                                                                              : confirmedToneFrequency();
    const QString toneValueLabel = toneValue.has_value()
                                       ? sdr9700::ui::main_window::memoryToneFrequencyLabel(*toneAccessMode, *toneValue)
                                       : QStringLiteral("--");
    const QString toneStatus =
        toneActive
            ? QStringLiteral("%1 %2").arg(sdr9700::ui::main_window::toneOptionLabel(*toneAccessMode), toneValueLabel)
            : QString();
    m_display->setReceiverControlState(QStringLiteral("TONE"), toneStatus, toneActive);
    if (m_vfo == Vfo::Main)
    {
        m_display->setReceiverControlState(QStringLiteral("XFC"), QString(), m_xfcEnabled);
        m_display->setReceiverControlState(QStringLiteral("COMP"), QString(), m_compressorEnabled);
    }
    const int squelchPercent = qBound(0, qRound(m_squelch * 100.0 / 255.0), 100);
    m_display->setReceiverControlState(QStringLiteral("SQL"), QStringLiteral("%1%").arg(squelchPercent), m_squelch > 0);
    if (m_vfo == Vfo::Main)
    {
        const int txPowerPercent = qBound(0, qRound(m_txPower * 100.0 / 255.0), 100);
        m_display->setReceiverControlState(QStringLiteral("TX PWR"), QStringLiteral("%1%").arg(txPowerPercent),
                                           m_txPower > 0);
    }
}

void VfoController::updateTransmitFrequencyDisplay()
{
    const std::optional<duplexMode_t> duplexMode = confirmedDuplexMode();
    const std::optional<quint64> repeaterOffsetHz = confirmedRepeaterOffsetHz();
    const quint64 receiveFrequencyHz = frequencyHz();
    const bool offsetActive = duplexMode.has_value() && repeaterOffsetHz.has_value() &&
                              (*duplexMode == dmDupMinus || *duplexMode == dmDupPlus) && *repeaterOffsetHz > 0;
    if (!offsetActive || receiveFrequencyHz == 0)
    {
        m_display->clearTransmitFrequency();
        return;
    }

    const std::optional<quint64> transmitHz =
        sdr9700::duplexTransmitFrequency(receiveFrequencyHz, *duplexMode, *repeaterOffsetHz);
    if (!transmitHz.has_value())
    {
        m_display->clearTransmitFrequency();
        return;
    }
    m_display->setTransmitFrequencyHz(*transmitHz);
}

void VfoController::showModeMenu()
{
    if (!m_backend)
    {
        return;
    }
    QMenu menu(m_display);
    sdr9700::ui::main_window::styleCompactMenu(&menu);
    for (const QString& mode : VfoModel::availableModes())
    {
        QAction* action = menu.addAction(mode);
        action->setObjectName(QStringLiteral("%1VfoMode%2Action")
                                  .arg(m_vfo == Vfo::Main ? QStringLiteral("main") : QStringLiteral("sub"), mode));
        action->setCheckable(true);
        action->setChecked(mode == confirmedMode());
        connect(action, &QAction::triggered, this, [this, mode]() { m_backend->setVfoMode(m_vfo, mode); });
    }
    menu.exec(m_display->modeMenuPosition());
}

void VfoController::showReceiverControlMenu(const QString& control)
{
    if (!m_backend)
    {
        return;
    }
    if (control == QStringLiteral("ATT"))
    {
        m_backend->setVfoAttenuatorEnabled(m_vfo, !m_attenuatorEnabled);
        return;
    }
    if (control == QStringLiteral("FILTERS"))
    {
        QMenu menu(m_display);
        sdr9700::ui::main_window::styleCompactMenu(&menu);
        const std::optional<int> currentFilter = confirmedFilter();
        const QString currentMode = confirmedMode();
        auto* panel = new QWidget(&menu);
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(8, 6, 8, 6);
        panel->setFixedWidth(176);
        constexpr int kFiltersComboWidth = 84;
        const QString vfoName = m_vfo == Vfo::Main ? QStringLiteral("MAIN") : QStringLiteral("SUB");
        auto addControlCombo = [panel, layout](const QString& name)
        {
            auto* row = new QWidget(panel);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(8);
            auto* nameLabel = new QLabel(name, row);
            nameLabel->setObjectName(QStringLiteral("vfoFilters%1Name").arg(name));
            nameLabel->setFixedWidth(48);
            auto* combo = new QComboBox(row);
            combo->setObjectName(QStringLiteral("vfoFilters%1Combo").arg(name));
            combo->setFixedWidth(kFiltersComboWidth);
            rowLayout->addWidget(nameLabel);
            rowLayout->addStretch(1);
            rowLayout->addWidget(combo);
            layout->addWidget(row);
            return combo;
        };
        auto* filterCombo = addControlCombo(QStringLiteral("FILTER"));
        filterCombo->setAccessibleName(QStringLiteral("%1 VFO receive filter").arg(vfoName));
        for (int filter = 1; filter <= 3; ++filter)
        {
            filterCombo->addItem(QStringLiteral("FIL%1").arg(filter), filter);
        }
        filterCombo->setCurrentIndex(currentFilter.has_value() ? *currentFilter - 1 : -1);
        filterCombo->setEnabled(currentFilter.has_value() && !currentMode.isEmpty());
        connect(filterCombo, &QComboBox::currentIndexChanged, this,
                [this, currentMode, filterCombo](int index)
                {
                    if (index >= 0)
                    {
                        m_backend->setVfoFilter(m_vfo, currentMode, filterCombo->itemData(index).toInt());
                    }
                });
        auto addLevelCombo = [this, &addControlCombo, &vfoName](const QString& name, int maximum, int value,
                                                                bool valueKnown, bool enabled)
        {
            auto* combo = addControlCombo(name);
            combo->setAccessibleName(QStringLiteral("%1 VFO %2 level").arg(vfoName, name));
            for (int level = 0; level <= maximum; ++level)
            {
                const QString text = level == 0                        ? QStringLiteral("OFF")
                                     : name == QStringLiteral("NOTCH") ? QStringLiteral("ON")
                                                                       : QString::number(level);
                combo->addItem(text, level);
            }
            combo->setCurrentIndex(valueKnown && enabled ? value : 0);
            combo->setEnabled(valueKnown);
            auto requestedEnabled = std::make_shared<bool>(enabled);
            connect(combo, &QComboBox::currentIndexChanged, this,
                    [this, name, combo, requestedEnabled](int index)
                    {
                        if (index < 0)
                        {
                            return;
                        }
                        const int requestedLevel = combo->itemData(index).toInt();
                        if (name == QStringLiteral("NOTCH"))
                        {
                            const bool requestedOn = requestedLevel > 0;
                            if (*requestedEnabled != requestedOn)
                            {
                                *requestedEnabled = requestedOn;
                                m_backend->setVfoNotch(m_vfo, requestedOn ? VfoNotch::Auto : VfoNotch::Off);
                            }
                            return;
                        }
                        if (requestedLevel == 0)
                        {
                            if (*requestedEnabled)
                            {
                                *requestedEnabled = false;
                                if (name == QStringLiteral("NB"))
                                {
                                    m_backend->setVfoNbEnabled(m_vfo, false);
                                }
                                else
                                {
                                    m_backend->setVfoNrEnabled(m_vfo, false);
                                }
                            }
                            return;
                        }
                        if (name == QStringLiteral("NB"))
                        {
                            m_backend->setVfoNbLevel(m_vfo, requestedLevel);
                            if (!*requestedEnabled)
                            {
                                *requestedEnabled = true;
                                m_backend->setVfoNbEnabled(m_vfo, true);
                            }
                        }
                        else
                        {
                            m_backend->setVfoNrLevel(m_vfo, requestedLevel);
                            if (!*requestedEnabled)
                            {
                                *requestedEnabled = true;
                                m_backend->setVfoNrEnabled(m_vfo, true);
                            }
                        }
                    });
        };
        auto addControlDivider = [panel, layout]()
        {
            auto* divider = new QWidget(panel);
            divider->setFixedHeight(1);
            divider->setStyleSheet(QStringLiteral("background: %1;").arg(UiTheme::Color::Border));
            layout->addWidget(divider);
        };
        addControlDivider();
        addLevelCombo(QStringLiteral("NB"), 10, m_nbLevel, m_nbLevelReceived, m_nbEnabled);
        addControlDivider();
        addLevelCombo(QStringLiteral("NOTCH"), 1, m_autoNotchEnabled ? 1 : 0, true, m_autoNotchEnabled);
        addControlDivider();
        addLevelCombo(QStringLiteral("NR"), 15, m_nrLevel, m_nrLevelReceived, m_nrEnabled);
        auto* action = new QWidgetAction(&menu);
        action->setDefaultWidget(panel);
        menu.addAction(action);
        menu.exec(m_display->receiverControlMenuPosition(control));
        return;
    }
    if (control == QStringLiteral("PRE"))
    {
        m_backend->setVfoPreampLevel(m_vfo, (m_preampLevel & 0x01) != 0 ? 0 : 1);
        return;
    }
    QMenu menu(m_display);
    sdr9700::ui::main_window::styleCompactMenu(&menu);
    if (control == QStringLiteral("AGC"))
    {
        if (confirmedMode() == QStringLiteral("FM"))
        {
            return;
        }
        for (const auto& item : {qMakePair(QStringLiteral("FAST"), QStringLiteral("fast")),
                                 qMakePair(QStringLiteral("MID"), QStringLiteral("mid")),
                                 qMakePair(QStringLiteral("SLOW"), QStringLiteral("slow"))})
        {
            QAction* const action = menu.addAction(item.first);
            action->setObjectName(
                QStringLiteral("%1VfoAgc%2Action")
                    .arg(m_vfo == Vfo::Main ? QStringLiteral("main") : QStringLiteral("sub"), item.first));
            connect(action, &QAction::triggered, this,
                    [this, mode = item.second]() { m_backend->setVfoAgcMode(m_vfo, mode); });
        }
    }
    else if (control == QStringLiteral("RFG") || control == QStringLiteral("SQL") ||
             (control == QStringLiteral("MOD") && m_vfo == Vfo::Main) ||
             (control == QStringLiteral("TX PWR") && m_vfo == Vfo::Main))
    {
        const int currentValue = control == QStringLiteral("RFG")   ? m_rfGain
                                 : control == QStringLiteral("SQL") ? m_squelch
                                 : control == QStringLiteral("MOD") ? m_lanModLevel
                                                                    : m_txPower;
        auto* panel = new QWidget(&menu);
        auto* layout = new QHBoxLayout(panel);
        layout->setContentsMargins(8, 6, 8, 6);
        layout->setSpacing(6);
        auto* label = new QLabel(QStringLiteral("%1%").arg(currentValue * 100 / 255), panel);
        auto* slider = new QSlider(Qt::Horizontal, panel);
        slider->setObjectName(QStringLiteral("vfo%1LevelSlider").arg(control.simplified().remove(QLatin1Char(' '))));
        slider->setAccessibleName(
            QStringLiteral("%1 VFO %2 level")
                .arg(m_vfo == Vfo::Main ? QStringLiteral("MAIN") : QStringLiteral("SUB"), control));
        slider->setRange(0, 255);
        slider->setValue(currentValue);
        slider->setFixedWidth(110);
        slider->setFixedHeight(20);
        label->setObjectName(QStringLiteral("vfo%1LevelLabel").arg(control.simplified().remove(QLatin1Char(' '))));
        label->setFixedWidth(30);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setStyleSheet(
            QStringLiteral("QLabel { color: %1; font-size: 10px; font-weight: bold; background: transparent; }")
                .arg(UiTheme::Color::TextMuted));
        bool submitted = false;
        connect(slider, &QSlider::valueChanged, label,
                [label](int value) { label->setText(QStringLiteral("%1%").arg(value * 100 / 255)); });
        connect(slider, &QSlider::sliderReleased, this,
                [this, control, slider, &submitted]()
                {
                    submitted = true;
                    if (control == QStringLiteral("RFG"))
                    {
                        m_backend->setVfoRfGain(m_vfo, slider->value());
                    }
                    else if (control == QStringLiteral("SQL"))
                    {
                        m_backend->setVfoSquelch(m_vfo, slider->value());
                    }
                    else if (control == QStringLiteral("MOD"))
                    {
                        setLanModLevel(slider->value());
                        emit lanModLevelChanged(slider->value());
                    }
                    else
                    {
                        m_backend->setTxPower(slider->value());
                    }
                });
        layout->addWidget(slider);
        layout->addWidget(label);
        auto* action = new QWidgetAction(&menu);
        action->setDefaultWidget(panel);
        menu.addAction(action);
        menu.exec(m_display->receiverControlMenuPosition(control));
        if (!submitted && slider->value() != currentValue)
        {
            if (control == QStringLiteral("RFG"))
            {
                m_backend->setVfoRfGain(m_vfo, slider->value());
            }
            else if (control == QStringLiteral("SQL"))
            {
                m_backend->setVfoSquelch(m_vfo, slider->value());
            }
            else if (control == QStringLiteral("MOD"))
            {
                setLanModLevel(slider->value());
                emit lanModLevelChanged(slider->value());
            }
            else
            {
                m_backend->setTxPower(slider->value());
            }
        }
        return;
    }
    else
    {
        return;
    }
    menu.exec(m_display->receiverControlMenuPosition(control));
}
