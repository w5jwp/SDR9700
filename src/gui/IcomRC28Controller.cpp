#include "IcomRC28Controller.h"

#include "AppSettings.h"
#include "LogCategories.h"
#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "models/BandscopeModel.h"
#include "models/RadioModel.h"
#include "models/VfoModel.h"
#ifdef HAVE_HIDAPI
#include "core/IcomRC28Manager.h"
#endif

#include <algorithm>
#include <cmath>
#include <QTimer>

using namespace sdr9700::ui::main_window;

#define m_model m_window->m_model
#define m_vfo m_window->m_vfo
#define m_controlsLocked m_window->m_controlsLocked
#define m_muted m_window->m_muted
#define m_txActive m_window->m_txActive
#define m_displayBandscopeTuneHz m_window->m_displayBandscopeTuneHz
#define m_bandscopeTuneCommitTimer m_window->m_bandscopeTuneCommitTimer
#define m_bandscopeTuneReleaseTimer m_window->m_bandscopeTuneReleaseTimer
#define m_pendingBandscopeTuneHz m_window->m_pendingBandscopeTuneHz
#define m_bandscopeDisplayCenterHz m_window->m_bandscopeDisplayCenterHz
#define m_bandscopeFixedPanStartHz m_window->m_bandscopeFixedPanStartHz
#define m_bandscopeFixedPanEndHz m_window->m_bandscopeFixedPanEndHz
#define m_bandscope m_window->m_bandscope
#define m_icomRC28Manager m_window->m_icomRC28Manager
#define m_icomRC28HoldTimers m_window->m_icomRC28HoldTimers
#define m_icomRC28HoldConsumed m_window->m_icomRC28HoldConsumed
#define m_icomRC28ButtonDown m_window->m_icomRC28ButtonDown
#define m_icomRC28PttLatched m_window->m_icomRC28PttLatched
#define m_icomRC28SnapTimer m_window->m_icomRC28SnapTimer
#define m_icomRC28PulseAccum m_window->m_icomRC28PulseAccum
#define m_icomRC28Sensitivity m_window->m_icomRC28Sensitivity
#define m_icomRC28AutoSnap m_window->m_icomRC28AutoSnap
#define toggleMute m_window->toggleMute
#define toggleControlLock m_window->toggleControlLock
#define tuningStepHz m_window->tuningStepHz
#define updateStepButton m_window->updateStepButton
#define applyRadioTuningStep m_window->applyRadioTuningStep
#define toggleRit m_window->toggleRit
#define cycleMode m_window->cycleMode
#define scheduleBandscopeTune m_window->scheduleBandscopeTune
#define clampFrequencyHzToActiveBand m_window->clampFrequencyHzToActiveBand
#define clearActiveMemory m_window->clearActiveMemory

IcomRC28Controller::IcomRC28Controller(MainWindow* window) : QObject(window), m_window(window) {}

void IcomRC28Controller::initialize()
{
#ifdef HAVE_HIDAPI
    m_icomRC28Manager = new IcomRC28Manager(m_window);
    refreshIcomRC28EncoderSettings();
    m_icomRC28SnapTimer = new QTimer(m_window);
    m_icomRC28SnapTimer->setSingleShot(true);
    m_icomRC28SnapTimer->setInterval(600);
    connect(m_icomRC28SnapTimer, &QTimer::timeout, this, &IcomRC28Controller::snapIcomRC28FrequencyToKhz);
    for (int i = 0; i < 2; ++i)
    {
        m_icomRC28HoldTimers[i] = new QTimer(m_window);
        m_icomRC28HoldTimers[i]->setSingleShot(true);
        connect(m_icomRC28HoldTimers[i], &QTimer::timeout, this,
                [this, i]()
                {
                    if (!m_icomRC28ButtonDown[i] || m_icomRC28HoldConsumed[i])
                    {
                        return;
                    }
                    m_icomRC28HoldConsumed[i] = true;
                    const QString field = i == 0 ? QStringLiteral("F1Hold") : QStringLiteral("F2Hold");
                    dispatchIcomRC28Action(IcomRC28Manager::settingsField(field, QStringLiteral("None")));
                });
    }
    connect(m_icomRC28Manager, &IcomRC28Manager::buttonPressed, this, &IcomRC28Controller::handleIcomRC28Button);
    connect(m_icomRC28Manager, &IcomRC28Manager::tuneSteps, this, &IcomRC28Controller::handleIcomRC28Tune);
    connect(m_icomRC28Manager, &IcomRC28Manager::multipleDevicesDetected, this,
            [this](const QString& deviceName)
            {
                qInfo(logIcomRC28()) << "Multiple devices detected:" << deviceName;
                m_window->showToast(QStringLiteral("Duplicate accessory blocked (%1)").arg(deviceName), 8000,
                                    MainWindow::ToastKind::Warning);
            });
    connect(m_icomRC28Manager, &IcomRC28Manager::connectionChanged, this,
            [this](bool connected, const QString& deviceName)
            {
                qInfo(logIcomRC28()) << (connected ? "Connected" : "Disconnected") << deviceName;
                if (connected)
                {
                    updateIcomRC28Leds();
                }
                m_window->showToast(connected ? QStringLiteral("Accessory connected (%1)").arg(deviceName)
                                              : QStringLiteral("Accessory disconnected (%1)").arg(deviceName),
                                    4000, connected ? MainWindow::ToastKind::Info : MainWindow::ToastKind::Warning);
            });
    m_icomRC28Manager->loadSettings();
#endif
}

void IcomRC28Controller::close()
{
#ifdef HAVE_HIDAPI
    if (m_icomRC28Manager)
    {
        m_icomRC28Manager->close();
    }
#endif
}

void IcomRC28Controller::dispatchIcomRC28Action(const QString& action)
{
    if (action.isEmpty() || action == QLatin1String("None"))
    {
        return;
    }

    if (action == QLatin1String("ToggleMute"))
    {
        toggleMute();
        return;
    }

    if (action == QLatin1String("ToggleLock"))
    {
        toggleControlLock();
        return;
    }

    if (!m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    if (action == QLatin1String("CycleStep"))
    {
        const int current = tuningStepHz();
        constexpr int nPresets = static_cast<int>(std::size(kStepPresets));
        int nextIdx = 0;
        for (int i = 0; i < nPresets; ++i)
        {
            if (kStepPresets[i].hz == current)
            {
                nextIdx = (i + 1) % nPresets;
                break;
            }
        }
        AppSettings::instance().setValue(QString::fromLatin1(kTuningStepHZSettingsKey), kStepPresets[nextIdx].hz);
        updateStepButton();
        applyRadioTuningStep();
    }
    else if (action == QLatin1String("ToggleRit"))
    {
        toggleRit();
    }
    else if (action == QLatin1String("CycleMode"))
    {
        cycleMode();
    }
}

void IcomRC28Controller::setIcomRC28Ptt(bool on)
{
    if (!m_vfo || !m_model->isReady())
    {
        return;
    }
    m_vfo->setPtt(on);
}

void IcomRC28Controller::updateIcomRC28Leds()
{
#ifdef HAVE_HIDAPI
    if (!m_icomRC28Manager || !m_icomRC28Manager->isOpen())
    {
        return;
    }

    // Active-low: clearing a bit turns the LED on.
    uint8_t b = IcomRC28Manager::kLedsAllOff;
    b &= ~IcomRC28Manager::kLedBitLink; // LINK always on while connected

    if (m_txActive)
    {
        b &= ~IcomRC28Manager::kLedBitTx;
    }

    // F-key LEDs reflect their hold action's active/toggled state
    auto holdActionActive = [this](const QString& actionId) -> bool
    {
        if (actionId == QLatin1String("ToggleMute"))
        {
            return m_muted;
        }
        if (actionId == QLatin1String("ToggleLock"))
        {
            return m_controlsLocked;
        }
        if (actionId == QLatin1String("ToggleRit"))
        {
            return m_vfo && m_vfo->ritOn();
        }
        return false;
    };

    if (holdActionActive(IcomRC28Manager::settingsField(QStringLiteral("F1Hold"), QStringLiteral("None"))))
    {
        b &= ~IcomRC28Manager::kLedBitF1;
    }
    if (holdActionActive(IcomRC28Manager::settingsField(QStringLiteral("F2Hold"), QStringLiteral("None"))))
    {
        b &= ~IcomRC28Manager::kLedBitF2;
    }

    m_icomRC28Manager->setIcomRC28Leds(b);
#endif
}

void IcomRC28Controller::handleIcomRC28Tune(int steps)
{
    qInfo(logIcomRC28()) << "Tune steps:" << steps;

    if (!m_vfo || !m_model->isReady())
    {
        return;
    }

    if (steps == 0)
    {
        return;
    }

    refreshIcomRC28EncoderSettings();
    if (m_icomRC28Sensitivity > 1)
    {
        if (m_icomRC28PulseAccum != 0 && ((steps > 0) != (m_icomRC28PulseAccum > 0)))
        {
            m_icomRC28PulseAccum = 0;
        }
        m_icomRC28PulseAccum += steps;
        const int dividedSteps = m_icomRC28PulseAccum / m_icomRC28Sensitivity;
        m_icomRC28PulseAccum -= dividedSteps * m_icomRC28Sensitivity;
        if (dividedSteps == 0)
        {
            if (m_icomRC28AutoSnap && m_icomRC28SnapTimer)
            {
                m_icomRC28SnapTimer->start();
            }
            return;
        }
        steps = dividedSteps;
    }
    if (m_icomRC28AutoSnap && m_icomRC28SnapTimer)
    {
        m_icomRC28SnapTimer->start();
    }

    const int stepHz = tuningStepHz();
    const qint64 currentHz =
        static_cast<qint64>(m_displayBandscopeTuneHz > 0 ? m_displayBandscopeTuneHz : m_vfo->frequencyHz());
    const qint64 targetHz = currentHz + static_cast<qint64>(steps) * stepHz;
    scheduleBandscopeTune(
        static_cast<quint64>(std::max<qint64>(static_cast<qint64>(kMinimumTuneFrequencyHz), targetHz)));
}

void IcomRC28Controller::refreshIcomRC28EncoderSettings()
{
    const int sensitivity =
        qBound(1, IcomRC28Manager::settingsField(QStringLiteral("sensitivity"), QStringLiteral("1")).toInt(), 10);
    if (sensitivity != m_icomRC28Sensitivity)
    {
        m_icomRC28PulseAccum = 0;
    }
    m_icomRC28Sensitivity = sensitivity;
    m_icomRC28AutoSnap =
        IcomRC28Manager::settingsField(QStringLiteral("autoSnap"), QStringLiteral("False")) == QLatin1String("True");
}

void IcomRC28Controller::snapIcomRC28FrequencyToKhz()
{
    if (!m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    const quint64 currentHz = m_displayBandscopeTuneHz > 0 ? m_displayBandscopeTuneHz : m_vfo->frequencyHz();
    const quint64 snappedHz =
        clampFrequencyHzToActiveBand(static_cast<quint64>(std::llround(currentHz / 1000.0)) * 1000ULL);
    if (snappedHz == currentHz)
    {
        return;
    }

    if (m_bandscopeTuneCommitTimer)
    {
        m_bandscopeTuneCommitTimer->stop();
    }
    if (m_bandscopeTuneReleaseTimer)
    {
        m_bandscopeTuneReleaseTimer->stop();
    }
    m_pendingBandscopeTuneHz = 0;
    m_displayBandscopeTuneHz = 0;
    m_bandscopeDisplayCenterHz = 0;
    m_bandscopeFixedPanStartHz = 0;
    m_bandscopeFixedPanEndHz = 0;
    if (m_bandscope)
    {
        m_bandscope->clearDisplayCenterHold();
    }
    clearActiveMemory();
    m_vfo->setFrequencyHz(snappedHz);
}

void IcomRC28Controller::handleIcomRC28Button(int button, int action)
{
    qInfo(logIcomRC28()) << "Button" << button << (action == 0 ? "press" : "release");

    if (!m_icomRC28Manager)
    {
        return;
    }

    if (button == 1 || button == 2)
    {
        const int index = button - 1;
        if (action == 0)
        {
            m_icomRC28ButtonDown[index] = true;
            m_icomRC28HoldConsumed[index] = false;
            if (m_icomRC28HoldTimers[index])
            {
                m_icomRC28HoldTimers[index]->start(600);
            }
            return;
        }

        m_icomRC28ButtonDown[index] = false;
        if (m_icomRC28HoldTimers[index] && m_icomRC28HoldTimers[index]->isActive() && !m_icomRC28HoldConsumed[index])
        {
            m_icomRC28HoldTimers[index]->stop();
            const QString field = index == 0 ? QStringLiteral("F1Press") : QStringLiteral("F2Press");
            dispatchIcomRC28Action(IcomRC28Manager::settingsField(field, QStringLiteral("None")));
        }
        m_icomRC28HoldConsumed[index] = false;
        return;
    }

    if (button != 3)
    {
        return;
    }

    const QString mode = IcomRC28Manager::settingsField(QStringLiteral("PTTMode"), QStringLiteral("Disabled"));
    if (mode == QLatin1String("Disabled"))
    {
        return;
    }

    if (!m_vfo || !m_model->isReady() || m_controlsLocked)
    {
        return;
    }

    if (action == 0)
    {
        if (mode == QLatin1String("Latched"))
        {
            m_icomRC28PttLatched = !m_icomRC28PttLatched;
            setIcomRC28Ptt(m_icomRC28PttLatched);
        }
        else
        {
            m_icomRC28PttLatched = true;
            setIcomRC28Ptt(true);
        }
    }
    else if (mode == QLatin1String("Momentary"))
    {
        m_icomRC28PttLatched = false;
        setIcomRC28Ptt(false);
    }
}
