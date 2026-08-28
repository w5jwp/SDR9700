#include "VfoController.h"

#include "MainWindowHelpers.h"
#include "VfoDisplay.h"
#include "backend/IRadioBackend.h"
#include "models/VfoModel.h"

#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>

VfoController::VfoController(Vfo vfo, IRadioBackend* backend, QWidget* displayParent, QObject* parent)
    : QObject(parent), m_vfo(vfo), m_backend(backend), m_display(new VfoDisplay(vfo, displayParent))
{
    connect(m_display, &VfoDisplay::frequencySubmitted, this,
            [this](const QString& text)
            {
                quint64 hz = 0;
                const bool valid = sdr9700::ui::main_window::parseFrequencyText(text, &hz) &&
                                   sdr9700::ui::main_window::vfoBandIndexForHz(hz) >= 0;
                if (valid && m_backend)
                {
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
    connect(m_display, &VfoDisplay::receiverControlClicked, this, &VfoController::showReceiverControlMenu);
    if (m_backend)
    {
        connect(m_backend, &IRadioBackend::radioValueUpdated, this,
                [this](Funcs func, const QVariant& value, uchar receiver)
                {
                    const uchar expectedReceiver = m_vfo == Vfo::Main ? 0 : 1;
                    if (receiver != expectedReceiver ||
                        (func != funcFreqGet && func != funcFreqSet && func != funcSelectedFreq &&
                         func != funcModeGet && func != funcModeSet && func != funcSelectedMode &&
                         func != funcAGCTimeConstant && func != funcAttenuator && func != funcNoiseBlanker &&
                         func != funcAutoNotch && func != funcManualNotch && func != funcNoiseReduction &&
                         func != funcPreamp && func != funcRfGain && func != funcSquelch && func != funcRFPower))
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
                            m_display->setModeText(mode);
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
}

void VfoController::setFrequencyHz(quint64 hz)
{
    const bool changed = !m_confirmedFrequencyHz.has_value() || *m_confirmedFrequencyHz != hz;
    m_confirmedFrequencyHz = hz;
    m_band = sdr9700::radioBandForFrequency(hz);
    const int bandIndex = sdr9700::radioBandUiIndex(m_band);
    if (bandIndex >= 0)
    {
        m_lastBandFrequencyHz[static_cast<std::size_t>(bandIndex)] = hz;
    }
    m_display->setFrequencyHz(hz);
    m_display->setBandText(m_band == bandUnknown ? QStringLiteral("--") : sdr9700::radioBandShortLabel(m_band));
    if (changed)
    {
        emit frequencyChanged(hz);
    }
}

void VfoController::clearFrequency()
{
    m_confirmedFrequencyHz.reset();
    m_band = bandUnknown;
    m_display->clearFrequency();
    m_mode.clear();
}

void VfoController::setOperatingEnabled(bool enabled)
{
    m_display->setOperatingEnabled(enabled);
}

void VfoController::setSelected(bool selected)
{
    m_display->setSelected(selected);
}

void VfoController::setTransmitting(bool transmitting)
{
    m_display->setTransmitting(transmitting);
}

void VfoController::captureExchangeableControlState()
{
    m_capturedExchangeState = ExchangeableControlState{
        m_agcMode,   m_attenuatorEnabled, m_nbEnabled, m_autoNotchEnabled, m_manualNotchEnabled,
        m_nrEnabled, m_preampLevel,       m_squelch};
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

void VfoController::selectBand(availableBands requestedBand)
{
    const int bandIndex = sdr9700::radioBandUiIndex(requestedBand);
    if (!m_backend || bandIndex < 0)
    {
        return;
    }
    const quint64 remembered = m_lastBandFrequencyHz[static_cast<std::size_t>(bandIndex)];
    const quint64 hz = remembered > 0 ? remembered : sdr9700::radioBandDefaultFrequency(requestedBand);
    if (hz > 0)
    {
        m_backend->setVfoFrequencyHz(m_vfo, hz);
    }
}

void VfoController::updateReceiverControlDisplay()
{
    static const char* const kAgcLabels[] = {"--", "FAST", "MID", "SLOW"};
    m_display->setReceiverControlState(QStringLiteral("AGC"), QString::fromLatin1(kAgcLabels[m_agcMode]),
                                       m_agcMode > 0);
    m_display->setReceiverControlState(QStringLiteral("ATT"), QString(), m_attenuatorEnabled);
    m_display->setReceiverControlState(QStringLiteral("NB"), QString(), m_nbEnabled);
    const QString notch = m_autoNotchEnabled     ? QStringLiteral("AUTO")
                          : m_manualNotchEnabled ? QStringLiteral("MAN")
                                                 : QString();
    m_display->setReceiverControlState(QStringLiteral("NOTCH"), notch, m_autoNotchEnabled || m_manualNotchEnabled);
    m_display->setReceiverControlState(QStringLiteral("NR"), QString(), m_nrEnabled);
    static const char* const kPreampLabels[] = {"", "INT", "EXT", "BOTH"};
    m_display->setReceiverControlState(QStringLiteral("PRE"), QString::fromLatin1(kPreampLabels[m_preampLevel]),
                                       m_preampLevel > 0);
    const int rfPercent = qBound(0, qRound(m_rfGain * 100.0 / 255.0), 100);
    m_display->setReceiverControlState(QStringLiteral("RFG"), QString::number(rfPercent), m_rfGain > 0);
    const int squelchPercent = qBound(0, qRound(m_squelch * 100.0 / 255.0), 100);
    m_display->setReceiverControlState(QStringLiteral("SQL"), QStringLiteral("%1%").arg(squelchPercent), m_squelch > 0);
    if (m_vfo == Vfo::Main)
    {
        const int txPowerPercent = qBound(0, qRound(m_txPower * 100.0 / 255.0), 100);
        m_display->setReceiverControlState(QStringLiteral("TX PWR"), QStringLiteral("%1%").arg(txPowerPercent),
                                           m_txPower > 0);
    }
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
            QAction* action = menu.addAction(item.first);
            connect(action, &QAction::triggered, this,
                    [this, mode = item.second]() { m_backend->setVfoAgcMode(m_vfo, mode); });
        }
    }
    else if (control == QStringLiteral("NOTCH"))
    {
        for (const auto& item :
             {qMakePair(QStringLiteral("OFF"), VfoNotch::Off), qMakePair(QStringLiteral("AUTO"), VfoNotch::Auto),
              qMakePair(QStringLiteral("MANUAL"), VfoNotch::Manual)})
        {
            QAction* action = menu.addAction(item.first);
            connect(action, &QAction::triggered, this,
                    [this, notch = item.second]() { m_backend->setVfoNotch(m_vfo, notch); });
        }
    }
    else if (control == QStringLiteral("PRE"))
    {
        const QStringList labels = {QStringLiteral("OFF"), QStringLiteral("INT"), QStringLiteral("EXT"),
                                    QStringLiteral("INT+EXT")};
        for (int level = 0; level < labels.size(); ++level)
        {
            QAction* action = menu.addAction(labels.at(level));
            connect(action, &QAction::triggered, this, [this, level]() { m_backend->setVfoPreampLevel(m_vfo, level); });
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
