#include "VfoController.h"

#include "MainWindowHelpers.h"
#include "VfoDisplay.h"
#include "backend/IRadioBackend.h"
#include "backend/TransmitFrequencyPolicy.h"
#include "models/VfoModel.h"
#include "models/RadioState.h"

#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>

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

    connect(m_display, &VfoDisplay::frequencySubmitted, this,
            [this](const QString& text)
            {
                quint64 hz = 0;
                const bool valid = sdr9700::ui::main_window::parseFrequencyText(text, &hz) &&
                                   sdr9700::ui::main_window::vfoBandIndexForHz(hz) >= 0;
                if (valid && m_backend)
                {
                    emit frequencyRecenterRequested(m_vfo, hz);
                    m_backend->setVfoFrequencyHz(m_vfo, hz);
                }
                if (m_confirmedFrequencyHz.has_value())
                {
                    m_display->setFrequencyHz(*m_confirmedFrequencyHz);
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
        if (m_vfo == Vfo::Main)
        {
            connect(m_backend, &IRadioBackend::powerMeterChanged, m_display, &VfoDisplay::setTransmitPowerWatts);
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
                        (func != funcFreqGet && func != funcFreqSet && func != funcSelectedFreq &&
                         func != funcModeGet && func != funcModeSet && func != funcSelectedMode &&
                         func != funcAGCTimeConstant && func != funcAttenuator && func != funcNoiseBlanker &&
                         func != funcAutoNotch && func != funcManualNotch && func != funcNoiseReduction &&
                         func != funcPreamp && func != funcRfGain && func != funcSquelch && func != funcRFPower &&
                         func != funcSMeter && func != funcSplitStatus && func != funcReadFreqOffset &&
                         func != funcToneSquelchType && func != funcToneFreq && func != funcTSQLFreq &&
                         func != funcDTCSCode))
                    {
                        return;
                    }
                    switch (func)
                    {
                    case funcModeGet:
                    case funcModeSet:
                    case funcSelectedMode:
                    {
                        const auto mode = value.value<ModeInfo>().name.trimmed().toUpper();
                        if (!mode.isEmpty())
                        {
                            m_mode = mode;
                            publishConfirmedState();
                        }
                        return;
                    }
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
                    case funcSplitStatus:
                        m_duplexMode = value.value<duplexMode_t>();
                        updateReceiverControlDisplay();
                        return;
                    case funcReadFreqOffset:
                        m_repeaterOffsetHz = value.value<Frequency>().Hz;
                        updateReceiverControlDisplay();
                        return;
                    case funcToneSquelchType:
                        m_toneAccessMode = value.value<RptrAccessData>().accessMode;
                        updateReceiverControlDisplay();
                        return;
                    case funcToneFreq:
                    case funcTSQLFreq:
                    {
                        const bool displayRxTone = m_toneAccessMode == ratrNT || m_toneAccessMode == ratrDT;
                        if ((displayRxTone && func == funcTSQLFreq) || (!displayRxTone && func == funcToneFreq))
                        {
                            m_toneFrequency = value.value<ToneInfo>().tone;
                            updateReceiverControlDisplay();
                        }
                        return;
                    }
                    case funcDTCSCode:
                        m_dtcsCode = value.value<ToneInfo>().tone;
                        updateReceiverControlDisplay();
                        return;
                    default:
                        break;
                    }
                    const auto frequency = value.value<Frequency>();
                    if (frequency.Hz > 0)
                    {
                        setFrequencyHz(frequency.Hz);
                    }
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
                                               if (m_mode.isEmpty() && m_backend)
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

void VfoController::setFrequencyHz(quint64 hz)
{
    m_confirmedFrequencyHz = hz;
    m_band = sdr9700::radioBandForFrequency(hz);
    const int bandIndex = sdr9700::radioBandUiIndex(m_band);
    if (bandIndex >= 0)
    {
        m_lastBandFrequencyHz[static_cast<std::size_t>(bandIndex)] = hz;
    }
    publishConfirmedState();
}

void VfoController::clearFrequency()
{
    m_initialPublishTimer.stop();
    m_initialStatePublished = false;
    m_confirmedFrequencyHz.reset();
    m_publishedFrequencyHz.reset();
    m_band = bandUnknown;
    m_display->clearFrequency();
    m_mode.clear();
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

void VfoController::captureExchangeableControlState()
{
    m_capturedExchangeState = ExchangeableControlState{
        m_agcMode,        m_attenuatorEnabled, m_nbEnabled, m_autoNotchEnabled, m_manualNotchEnabled,
        m_nrEnabled,      m_preampLevel,       m_squelch,   m_duplexMode,       m_repeaterOffsetHz,
        m_toneAccessMode, m_toneFrequency,     m_dtcsCode};
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
    m_duplexMode = state.duplexMode;
    m_repeaterOffsetHz = state.repeaterOffsetHz;
    m_toneAccessMode = state.toneAccessMode;
    m_toneFrequency = state.toneFrequency;
    m_dtcsCode = state.dtcsCode;
    updateReceiverControlDisplay();
}

void VfoController::selectBand(availableBands requestedBand)
{
    const int bandIndex = sdr9700::radioBandUiIndex(requestedBand);
    if (!m_backend || bandIndex < 0)
    {
        return;
    }
    const sdr9700::RadioState::BandRecall* recall =
        m_radioState ? m_radioState->bandRecall(m_vfo, requestedBand) : nullptr;
    const quint64 stateRemembered = recall ? recall->frequencyHz.value_or(0) : 0;
    const quint64 remembered =
        stateRemembered > 0 ? stateRemembered : m_lastBandFrequencyHz[static_cast<std::size_t>(bandIndex)];
    const quint64 hz = remembered > 0 ? remembered : sdr9700::radioBandDefaultFrequency(requestedBand);
    if (hz > 0)
    {
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
    }
}

bool VfoController::stateReady() const
{
    // Controllers without a backend are used as inert UI fixtures in tests and
    // previews. Live radio controllers require both authoritative fields.
    return m_confirmedFrequencyHz.has_value() && (!m_backend || !m_mode.isEmpty());
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

    const quint64 confirmedFrequencyHz = *m_confirmedFrequencyHz;
    m_display->setFrequencyHz(confirmedFrequencyHz);
    m_display->setBandText(m_band == bandUnknown ? QStringLiteral("--") : sdr9700::radioBandShortLabel(m_band));
    if (m_vfo == Vfo::Main)
    {
        m_display->setMaxTransmitPowerWatts(sdr9700::radioBandMaxPowerWatts(m_band));
    }
    if (!m_mode.isEmpty())
    {
        m_display->setModeText(m_mode);
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
    m_display->setOperatingEnabled(m_operatingEnabled && (!m_backend || m_userInteractionEnabled) && stateReady());
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
    m_display->setReceiverControlState(QStringLiteral("NB"), QString(), m_nbEnabled);
    m_display->setReceiverControlState(QStringLiteral("NOTCH"), QString(), m_autoNotchEnabled);
    m_display->setReceiverControlState(QStringLiteral("NR"), QString(), m_nrEnabled);
    m_display->setReceiverControlState(QStringLiteral("PRE"), QString(), (m_preampLevel & 0x01) != 0);
    const int rfPercent = qBound(0, qRound(m_rfGain * 100.0 / 255.0), 100);
    m_display->setReceiverControlState(QStringLiteral("RFG"), QString::number(rfPercent), m_rfGain > 0);
    const bool offsetActive = m_duplexMode == dmDupMinus || m_duplexMode == dmDupPlus;
    m_display->setReceiverControlState(QStringLiteral("OFFSET"),
                                       sdr9700::ui::main_window::offsetModeLabel(m_duplexMode, m_repeaterOffsetHz),
                                       offsetActive);
    updateTransmitFrequencyDisplay();
    const bool toneActive = m_toneAccessMode != ratrNN;
    const ushort toneValue = isDtcsToneMode(m_toneAccessMode) ? m_dtcsCode : m_toneFrequency;
    const QString toneValueLabel = sdr9700::ui::main_window::memoryToneFrequencyLabel(m_toneAccessMode, toneValue);
    const QString toneStatus =
        toneActive
            ? QStringLiteral("%1 %2").arg(sdr9700::ui::main_window::toneOptionLabel(m_toneAccessMode), toneValueLabel)
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
    const bool offsetActive = (m_duplexMode == dmDupMinus || m_duplexMode == dmDupPlus) && m_repeaterOffsetHz > 0;
    if (!offsetActive || !m_confirmedFrequencyHz.has_value())
    {
        m_display->clearTransmitFrequency();
        return;
    }

    const std::optional<quint64> transmitHz =
        sdr9700::duplexTransmitFrequency(*m_confirmedFrequencyHz, m_duplexMode, m_repeaterOffsetHz);
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
        action->setCheckable(true);
        action->setChecked(mode == m_mode);
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
    if (control == QStringLiteral("NB"))
    {
        m_backend->setVfoNbEnabled(m_vfo, !m_nbEnabled);
        return;
    }
    if (control == QStringLiteral("NR"))
    {
        m_backend->setVfoNrEnabled(m_vfo, !m_nrEnabled);
        return;
    }
    if (control == QStringLiteral("PRE"))
    {
        m_backend->setVfoPreampLevel(m_vfo, (m_preampLevel & 0x01) != 0 ? 0 : 1);
        return;
    }
    if (control == QStringLiteral("NOTCH"))
    {
        m_backend->setVfoNotch(m_vfo, m_autoNotchEnabled ? VfoNotch::Off : VfoNotch::Auto);
        return;
    }

    QMenu menu(m_display);
    sdr9700::ui::main_window::styleCompactMenu(&menu);
    if (control == QStringLiteral("AGC"))
    {
        if (m_mode == QStringLiteral("FM"))
        {
            return;
        }
        for (const auto& item : {qMakePair(QStringLiteral("FAST"), QStringLiteral("fast")),
                                 qMakePair(QStringLiteral("MID"), QStringLiteral("mid")),
                                 qMakePair(QStringLiteral("SLOW"), QStringLiteral("slow"))})
        {
            QAction* const action = menu.addAction(item.first);
            connect(action, &QAction::triggered, this,
                    [this, mode = item.second]() { m_backend->setVfoAgcMode(m_vfo, mode); });
        }
    }
    else if (control == QStringLiteral("RFG") || control == QStringLiteral("SQL") ||
             (control == QStringLiteral("TX PWR") && m_vfo == Vfo::Main))
    {
        const int currentValue = control == QStringLiteral("RFG")   ? m_rfGain
                                 : control == QStringLiteral("SQL") ? m_squelch
                                                                    : m_txPower;
        auto* panel = new QWidget(&menu);
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(8, 6, 8, 6);
        auto* label = new QLabel(QStringLiteral("%1%").arg(qRound(currentValue * 100.0 / 255.0)), panel);
        label->setAlignment(Qt::AlignCenter);
        auto* slider = new QSlider(Qt::Horizontal, panel);
        slider->setRange(0, 255);
        slider->setValue(currentValue);
        bool submitted = false;
        connect(slider, &QSlider::valueChanged, label,
                [label](int value) { label->setText(QStringLiteral("%1%").arg(qRound(value * 100.0 / 255.0))); });
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
                    else
                    {
                        m_backend->setTxPower(slider->value());
                    }
                });
        layout->addWidget(label);
        layout->addWidget(slider);
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
