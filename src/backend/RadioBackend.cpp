#include "RadioBackend.h"

#include "Commander.h"
#include "CachingQueue.h"
#include "RadioRouter.h"
#include "ScopeController.h"
#include "Types.h"
#include "AppSettings.h"
#include "DtmfGenerator.h"
#include "LogCategories.h"
#include "RadioCapabilities.h"
#include "RadioIdentities.h"
#include "TransmitFrequencyPolicy.h"

#include <QMediaDevices>
#include <QHostAddress>
#include <QMetaType>
#include <QSemaphore>
#include <QThread>
#include <QTimer>
#include <QDebug>
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <memory>
#include <optional>

namespace
{
constexpr int kSyncWatchdogTimeoutMs = 10000;
constexpr int kSyncReconnectDelayMs = 3000;
constexpr int kMaxSyncReconnectAttempts = 1;
constexpr int kMemoryWriteReadbackDelayMs = 250;
constexpr int kVfoStatePollIntervalMs = 250;
constexpr int kPttReleaseTailMs = 150;
constexpr int kPttOffConfirmationMs = 1000;
constexpr int kMaxTransmitDurationMs = 180000;
constexpr uchar kHardwareTxTimeoutTimer = 1; // 3 minutes, the IC-9700's shortest non-off value.
constexpr uchar kMainReceiver = 0;
constexpr uchar kSubReceiver = 1;
constexpr quint32 kTxAudioSampleRate = 16000;

struct VfoStatePollItem
{
    Funcs func{};
    bool mainOnly{false};
};

// Read one stable control per timer tick. With dual watch enabled, MAIN and
// SUB alternate, so a complete radio-authoritative refresh takes about ten
// seconds without producing the large CI-V bursts that contend with tuning,
// meters, and PTT.
constexpr std::array kVfoStatePollItems{
    VfoStatePollItem{funcFreqGet, false},         VfoStatePollItem{funcModeGet, false},
    VfoStatePollItem{funcAGCTimeConstant, false}, VfoStatePollItem{funcAttenuator, false},
    VfoStatePollItem{funcNoiseBlanker, false},    VfoStatePollItem{funcAutoNotch, false},
    VfoStatePollItem{funcManualNotch, false},     VfoStatePollItem{funcNoiseReduction, false},
    VfoStatePollItem{funcPreamp, false},          VfoStatePollItem{funcRfGain, false},
    VfoStatePollItem{funcSquelch, false},         VfoStatePollItem{funcSplitStatus, false},
    VfoStatePollItem{funcReadFreqOffset, false},  VfoStatePollItem{funcToneSquelchType, false},
    VfoStatePollItem{funcToneFreq, false},        VfoStatePollItem{funcDTCSCode, false},
    VfoStatePollItem{funcRFPower, true},          VfoStatePollItem{funcCompressor, true},
    VfoStatePollItem{funcXFCStatus, true},
};

quint64 memoryGroupDefaultFrequencyHz(quint16 group)
{
    switch (group)
    {
    case 1:
        return sdr9700::radioBandDefaultFrequency(band2m);
    case 2:
        return sdr9700::radioBandDefaultFrequency(band70cm);
    case 3:
        return sdr9700::radioBandDefaultFrequency(band23cm);
    default:
        return 0;
    }
}

bool populateModeInfo(const QString& mode, ModeInfo* info)
{
    if (!info)
    {
        return false;
    }

    static constexpr quint8 kDefaultFilter = 1;
    static constexpr quint8 kDataModeOff = 0;

    info->filter = kDefaultFilter;
    info->data = kDataModeOff;
    info->VFO = activeVFO;
    info->name = mode;

    if (mode == "LSB")
    {
        info->mk = modeLSB;
        info->reg = 0;
    }
    else if (mode == "USB")
    {
        info->mk = modeUSB;
        info->reg = 1;
    }
    else if (mode == "AM")
    {
        info->mk = modeAM;
        info->reg = 2;
    }
    else if (mode == "CW")
    {
        info->mk = modeCW;
        info->reg = 3;
    }
    else if (mode == "RTTY")
    {
        info->mk = modeRTTY;
        info->reg = 4;
    }
    else if (mode == "FM")
    {
        info->mk = modeFM;
        info->reg = 5;
    }
    else if (mode == "CW-R")
    {
        info->mk = modeCW_R;
        info->reg = 7;
    }
    else if (mode == "RTTY-R")
    {
        info->mk = modeRTTY_R;
        info->reg = 8;
    }
    else if (mode == "DV")
    {
        info->mk = modeDV;
        info->reg = 17;
    }
    else if (mode == "DD")
    {
        info->mk = modeDD;
        info->reg = 22;
    }
    else
    {
        return false;
    }

    return true;
}

radioInput restoreInputForModSource(int reg)
{
    return radioInput(inputUnknown, static_cast<qint8>(qBound(0, reg, 99)), QStringLiteral("Previous"));
}

void sendDisconnectSafetyCommands(Commander* commandSession, std::optional<int> originalDataOffMod,
                                  std::optional<int> originalData1Mod)
{
    if (!commandSession)
    {
        return;
    }

    // Use a conservative IC-9700 LAN disconnect order: unkey first, then stop
    // unsolicited scope data before the UDP streams close.
    commandSession->setPttActive(false);
    commandSession->receiveCommandNoReadback(funcTransceiverStatus, QVariant::fromValue<bool>(false), 0);
    commandSession->receiveCommandNoReadback(funcScopeOnOff, QVariant::fromValue<bool>(false), 0);
    commandSession->receiveCommandNoReadback(funcScopeDataOutput, QVariant::fromValue<bool>(false), 0);

    if (originalDataOffMod.has_value())
    {
        commandSession->receiveCommandNoReadback(funcDATAOffMod,
                                                 QVariant::fromValue(restoreInputForModSource(*originalDataOffMod)), 0);
    }
    if (originalData1Mod.has_value())
    {
        commandSession->receiveCommandNoReadback(funcDATA1Mod,
                                                 QVariant::fromValue(restoreInputForModSource(*originalData1Mod)), 0);
    }
}

} // namespace

RadioBackend::RadioBackend(QObject* parent)
    : IRadioBackend(parent), m_workerThread(new QThread(this)), m_radioDataThread(new QThread(this))
{
    qRegisterMetaType<MemoryType>("MemoryType");

    m_workerThread->setObjectName("radio-worker");
    m_workerThread->start();

    m_radioDataThread->setObjectName("radio-data");
    m_radioDataThread->start();

    m_scopeController = new ScopeController();
    m_scopeController->moveToThread(m_radioDataThread);
    connect(m_radioDataThread, &QThread::finished, m_scopeController, &QObject::deleteLater);

    m_radioRouter = new RadioRouter();
    m_radioRouter->moveToThread(m_radioDataThread);
    connect(m_radioDataThread, &QThread::finished, m_radioRouter, &QObject::deleteLater);
    connect(m_scopeController, &ScopeController::scopeDataReceived, this,
            [this]()
            {
                if (m_scopeSyncDegraded)
                {
                    setScopeSyncDegraded(false);
                    emit statusMessage(QStringLiteral("Spectrum scope sync complete"), MessageSeverity::Info);
                }
                m_scopeDataReceived = true;
                updateReadyState();
            });
    connect(m_scopeController, &ScopeController::spectrumDataReady, this, &IRadioBackend::spectrumDataReady);

    connect(m_radioRouter, &RadioRouter::radioValueUpdated, this,
            [this](Funcs func, const QVariant& value, uchar receiver)
            {
                if (func == funcVFOBandMS && receiver == 0)
                {
                    m_activeVfo = value.toBool() ? Vfo::Sub : Vfo::Main;
                }
                else if (func == funcVFODualWatch && receiver == 0)
                {
                    m_dualWatchEnabled = value.toBool();
                }
                else if (func == funcSMeter && m_smeterPollPending)
                {
                    if (receiver != m_smeterPollPendingReceiver)
                    {
                        qWarning(logRadio()).noquote() << "Ignoring mismatched S-meter reply receiver=" << receiver
                                                       << "expected=" << m_smeterPollPendingReceiver;
                        return;
                    }

                    if (m_smeterPollSettlingSample)
                    {
                        // The IC-9700 can report one latched meter value from
                        // the previously selected VFO. Suppress that sample and
                        // read again after the receiver selection has settled.
                        m_smeterPollSettlingSample = false;
                        m_smeterPollPendingTicks = 0;
                        QTimer::singleShot(
                            50, this,
                            [this, receiver]()
                            {
                                if (!m_smeterPollPending || m_smeterPollPendingReceiver != receiver ||
                                    m_smeterPollSettlingSample)
                                {
                                    return;
                                }
                                invokeOnCurrentCommander(
                                    [receiver](Commander* commandSession)
                                    { commandSession->receiveCommand(funcSMeter, QVariant(), receiver); });
                            });
                        return;
                    }

                    m_smeterPollPending = false;
                    m_smeterPollPendingTicks = 0;
                    const Vfo restoreVfo = m_smeterRestoreVfo;
                    const Vfo measuredVfo = receiver == kSubReceiver ? Vfo::Sub : Vfo::Main;
                    if (measuredVfo != restoreVfo)
                    {
                        invokeOnCurrentCommander(
                            [restoreVfo](Commander* commandSession)
                            {
                                commandSession->receiveCommand(
                                    funcSelectVFO,
                                    QVariant::fromValue<vfo_t>(restoreVfo == Vfo::Sub ? vfoSub : vfoMain), 0);
                            });
                    }
                }
                emit radioValueUpdated(func, value, receiver);
            });
    connect(m_radioRouter, &RadioRouter::frequencyReported, this,
            [this](quint64 hz)
            {
                m_initialMainFrequencyReceived = true;
                m_initialFrequencyReceived = true;
                m_currentMainFrequencyHz = hz;
                m_transmitConfiguration.confirmFrequency(hz);
                handleReportedFrequency(hz);
                emit frequencyChanged(hz);
                updateReadyState();
            });

    m_mainSubExchangeRetryTimer = new QTimer(this);
    m_mainSubExchangeRetryTimer->setSingleShot(true);
    m_mainSubExchangeRetryTimer->setInterval(150);
    connect(m_mainSubExchangeRetryTimer, &QTimer::timeout, this,
            [this]()
            {
                if (!m_mainSubExchangePending)
                {
                    return;
                }
                const quint8 missing = static_cast<quint8>(0x03 & ~m_mainSubExchangeConfirmations);
                qInfo(logRadio()).noquote() << "Retrying missing MAIN/SUB exchange confirmations mask=" << missing;
                invokeOnCurrentCommander(
                    [missing](Commander* commandSession)
                    {
                        if (missing & 0x01)
                        {
                            commandSession->receiveCommand(funcVFOBandMS, QVariant(), 0);
                        }
                        if (missing & 0x02)
                        {
                            commandSession->receiveCommand(funcScopeMainSub, QVariant(), 0);
                        }
                    });
                m_mainSubExchangeRetryTimer->start();
            });

    m_vfoStatePollTimer = new QTimer(this);
    m_vfoStatePollTimer->setInterval(kVfoStatePollIntervalMs);
    m_vfoStatePollTimer->setTimerType(Qt::CoarseTimer);
    connect(m_vfoStatePollTimer, &QTimer::timeout, this,
            [this]()
            {
                if (!m_commander || !m_radioReady || m_pttState.safetyActive() || m_mainSubExchangePending ||
                    m_smeterPollPending)
                {
                    return;
                }

                const VfoStatePollItem item = kVfoStatePollItems[static_cast<std::size_t>(m_vfoStatePollPhase)];
                const bool pollSub = m_dualWatchEnabled && m_vfoStatePollSubNext;
                m_vfoStatePollSubNext = m_dualWatchEnabled && !m_vfoStatePollSubNext;

                // A MAIN-only phase deliberately leaves the SUB slot idle. It
                // preserves the alternating cadence instead of pulling the next
                // receiver transaction forward into the same timer interval.
                if (!pollSub || !item.mainOnly)
                {
                    const Vfo targetVfo = pollSub ? Vfo::Sub : Vfo::Main;
                    routeVfoReceiverCommand(targetVfo, [item](Commander* commandSession, uchar receiver)
                                            { commandSession->receiveCommand(item.func, QVariant(), receiver); });
                }

                if (!m_dualWatchEnabled || pollSub)
                {
                    m_vfoStatePollPhase = (m_vfoStatePollPhase + 1) % static_cast<qsizetype>(kVfoStatePollItems.size());
                }
            });

    connect(m_radioRouter, &RadioRouter::modeReported, this,
            [this](const QString& mode, int filter)
            {
                if (filter >= 1 && filter <= 3)
                {
                    m_currentMainFilter = filter;
                }
                m_initialMainModeReceived = true;
                m_initialModeReceived = true;
                emit modeChanged(mode);
                updateReadyState();
            });
    connect(m_radioRouter, &RadioRouter::repeaterOffsetChanged, this,
            [this](quint64 hz)
            {
                m_currentRepeaterOffsetHz = hz;
                m_transmitConfiguration.confirmOffset(hz);
                emit repeaterOffsetChanged(hz);
            });
    connect(m_radioRouter, &RadioRouter::toneAccessModeChanged, this, &IRadioBackend::toneAccessModeChanged);
    connect(m_radioRouter, &RadioRouter::toneFrequencyChanged, this, &IRadioBackend::toneFrequencyChanged);
    connect(m_radioRouter, &RadioRouter::dtcsCodeChanged, this, &IRadioBackend::dtcsCodeChanged);
    connect(m_radioRouter, &RadioRouter::smeterChanged, this, &IRadioBackend::smeterChanged);
    connect(m_radioRouter, &RadioRouter::nrChanged, this, &IRadioBackend::nrChanged);
    connect(m_radioRouter, &RadioRouter::nrLevelChanged, this, &IRadioBackend::nrLevelChanged);
    connect(m_radioRouter, &RadioRouter::nbChanged, this, &IRadioBackend::nbChanged);
    connect(m_radioRouter, &RadioRouter::nbLevelChanged, this, &IRadioBackend::nbLevelChanged);
    connect(m_radioRouter, &RadioRouter::preampChanged, this, &IRadioBackend::preampChanged);
    connect(m_radioRouter, &RadioRouter::preampLevelChanged, this, &IRadioBackend::preampLevelChanged);
    connect(m_radioRouter, &RadioRouter::attenuatorChanged, this, &IRadioBackend::attenuatorChanged);
    connect(m_radioRouter, &RadioRouter::autoNotchChanged, this, &IRadioBackend::autoNotchChanged);
    connect(m_radioRouter, &RadioRouter::manualNotchChanged, this, &IRadioBackend::manualNotchChanged);
    connect(m_radioRouter, &RadioRouter::compressorChanged, this, &IRadioBackend::compressorChanged);
    connect(m_radioRouter, &RadioRouter::compressorLevelChanged, this, &IRadioBackend::compressorLevelChanged);
    connect(m_radioRouter, &RadioRouter::xfcChanged, this, &IRadioBackend::xfcChanged);
    connect(m_radioRouter, &RadioRouter::ritEnabledChanged, this, &IRadioBackend::ritEnabledChanged);
    connect(m_radioRouter, &RadioRouter::ritOffsetChanged, this, &IRadioBackend::ritOffsetChanged);
    connect(m_radioRouter, &RadioRouter::agcModeChanged, this, &IRadioBackend::agcModeChanged);
    connect(m_radioRouter, &RadioRouter::rfGainChanged, this, &IRadioBackend::rfGainChanged);
    connect(m_radioRouter, &RadioRouter::txPowerChanged, this, &IRadioBackend::txPowerChanged);
    connect(m_radioRouter, &RadioRouter::squelchChanged, this, &IRadioBackend::squelchChanged);
    connect(m_radioRouter, &RadioRouter::swrMeterChanged, this, &RadioBackend::handleTransmitSwr);
    connect(m_radioRouter, &RadioRouter::powerMeterChanged, this, &IRadioBackend::powerMeterChanged);
    connect(m_radioRouter, &RadioRouter::alcChanged, this, &IRadioBackend::alcChanged);
    connect(m_radioRouter, &RadioRouter::compressionMeterChanged, this, &IRadioBackend::compressionMeterChanged);
    connect(m_radioRouter, &RadioRouter::voltageMeterChanged, this, &IRadioBackend::voltageMeterChanged);
    connect(m_radioRouter, &RadioRouter::currentMeterChanged, this, &IRadioBackend::currentMeterChanged);
    connect(m_radioRouter, &RadioRouter::duplexModeChanged, this,
            [this](duplexMode_t mode)
            {
                m_currentDuplexMode = mode;
                m_transmitConfiguration.confirmDuplexMode(mode);
                emit duplexModeChanged(mode);
            });
    connect(m_radioRouter, &RadioRouter::radioMemoryReceived, this, &IRadioBackend::radioMemoryReceived);
    connect(m_radioRouter, &RadioRouter::dataOffModChanged, this,
            [this](const radioInput& input)
            {
                if (!m_originalDataOffMod.has_value())
                {
                    m_originalDataOffMod = input.reg;
                    qInfo(logRadio()).noquote()
                        << "Captured original DATA OFF MOD source register" << *m_originalDataOffMod;
                }
            });
    connect(m_radioRouter, &RadioRouter::data1ModChanged, this,
            [this](const radioInput& input)
            {
                if (!m_originalData1Mod.has_value())
                {
                    m_originalData1Mod = input.reg;
                    qInfo(logRadio()).noquote() << "Captured original DATA1 MOD source register" << *m_originalData1Mod;
                }
            });
    connect(m_radioRouter, &RadioRouter::pttChanged, this,
            [this](bool on)
            {
                if (on && m_pttState.offPending() && m_pttOffConfirmationTimer && m_pttOffConfirmationTimer->isActive())
                {
                    // A queued status read can return the pre-unkey state after
                    // the radio accepted the PTT-off command. Keep the UI
                    // released during this short confirmation window; safety
                    // monitoring remains armed through PttConfirmationPolicy.
                    m_pttState.confirm(true);
                    return;
                }
                if (!on && m_pttReleaseDelayTimer)
                {
                    m_pttReleaseDelayTimer->stop();
                }
                if (!on && m_pttOffConfirmationTimer)
                {
                    m_pttOffConfirmationTimer->stop();
                }
                if (on)
                {
                    armTransmitSafety();
                }
                else
                {
                    disarmTransmitSafety();
                }
                m_pttState.confirm(on);
                emit pttChanged(on);
            });
    connect(m_radioRouter, &RadioRouter::scopeDataReady, m_scopeController, &ScopeController::acceptScopeData);

    m_pttReleaseDelayTimer = new QTimer(this);
    m_pttReleaseDelayTimer->setSingleShot(true);
    m_pttReleaseDelayTimer->setInterval(kPttReleaseTailMs);
    connect(m_pttReleaseDelayTimer, &QTimer::timeout, this, &RadioBackend::sendPttOffNow);

    m_pttOffConfirmationTimer = new QTimer(this);
    m_pttOffConfirmationTimer->setSingleShot(true);
    m_pttOffConfirmationTimer->setInterval(kPttOffConfirmationMs);
    connect(m_pttOffConfirmationTimer, &QTimer::timeout, this,
            [this]()
            {
                if (!m_pttState.offPending())
                {
                    return;
                }
                invokeOnCurrentCommander([](Commander* commandSession)
                                         { commandSession->receiveCommand(funcTransceiverStatus, QVariant(), 0); });
            });

    m_pttMaxDurationTimer = new QTimer(this);
    m_pttMaxDurationTimer->setSingleShot(true);
    m_pttMaxDurationTimer->setInterval(kMaxTransmitDurationMs);
    connect(m_pttMaxDurationTimer, &QTimer::timeout, this,
            [this]()
            {
                forcePttOffForSafety(
                    QStringLiteral("TX stopped: %1 second transmit safety timeout").arg(kMaxTransmitDurationMs / 1000));
            });

    /*
        IC-9700 LAN TX audio startup sequence

        SDR9700 keeps the radio's LAN MOD Level as a persistent radio setting
        through the IC-9700 CI-V command model. TX startup muting is handled in
        the audio path, not by forcing the radio menu value to zero. This keeps
        the radio front panel/menu in sync with the GUI LAN MOD control.

        IC-9700 LAN scope packets carry the complete sweep in one CI-V frame.
        Commander parses that frame directly; this is independent from the TX
        audio startup sequence above.
    */
    m_bandStateRefreshTimer = new QTimer(this);
    m_bandStateRefreshTimer->setSingleShot(true);
    m_bandStateRefreshTimer->setInterval(350);
    connect(m_bandStateRefreshTimer, &QTimer::timeout, this,
            [this]()
            {
                if (!m_commander || !m_radioReady)
                {
                    return;
                }
                emit statusMessage(QStringLiteral("Refreshing radio band state"), MessageSeverity::Info);
                requestInitialRadioState();
            });

    m_syncWatchdogTimer = new QTimer(this);
    m_syncWatchdogTimer->setSingleShot(true);
    m_syncWatchdogTimer->setInterval(kSyncWatchdogTimeoutMs);
    connect(m_syncWatchdogTimer, &QTimer::timeout, this, &RadioBackend::restartAfterSyncTimeout);
}

RadioBackend::~RadioBackend()
{
    shutdownConnection(false, false);
    if (m_scopeController && m_radioDataThread && m_radioDataThread->isRunning())
    {
        QMetaObject::invokeMethod(m_scopeController, &ScopeController::reset, Qt::QueuedConnection);
    }
    if (m_radioDataThread)
    {
        m_radioDataThread->quit();
        if (!m_radioDataThread->wait(3000))
        {
            qWarning(logRadio()).noquote()
                << "[SHUTDOWN] radio-data did not stop within 3000 ms; requesting interruption";
            m_radioDataThread->requestInterruption();
            m_radioDataThread->quit();
            if (!m_radioDataThread->wait(1000))
            {
                qCritical(logRadio()).noquote()
                    << "[SHUTDOWN] radio-data did not stop after bounded shutdown; leaving thread "
                       "detached to avoid blocking the UI";
                m_radioDataThread->setParent(nullptr);
                connect(m_radioDataThread, &QThread::finished, m_radioDataThread, &QObject::deleteLater,
                        Qt::DirectConnection);
                m_radioDataThread = nullptr;
            }
        }
    }
    m_workerThread->quit();
    if (!m_workerThread->wait(3000))
    {
        qWarning(logRadio()).noquote()
            << "[SHUTDOWN] radio-worker did not stop within 3000 ms; requesting interruption";
        m_workerThread->requestInterruption();
        m_workerThread->quit();
        if (!m_workerThread->wait(1000))
        {
            qCritical(logRadio()).noquote()
                << "[SHUTDOWN] radio-worker did not stop after bounded shutdown; leaving thread "
                   "detached to avoid blocking the UI";
            m_workerThread->setParent(nullptr);
            connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater, Qt::DirectConnection);
            m_workerThread = nullptr;
        }
    }
}

void RadioBackend::connectToRadio(const QString& host, quint16 port, const QString& user, const QString& pass)
{
    if (!m_syncReconnectPending)
    {
        m_syncReconnectAttempts = 0;
    }
    m_syncReconnectPending = false;
    // Must be called from the main thread only; m_commander is not guarded by a mutex.
    if (m_commander)
    {
        // Replacing one session is not an operator-requested disconnect. Reset
        // backend readiness without publishing a transient Disconnected state
        // that could start MainWindow's automatic reconnect timer.
        shutdownConnection(false, false);
    }
    emit connectionStageChanged(ConnectionStage::Connecting, QStringLiteral("Connecting to %1").arg(host));

    QHostAddress radioAddress;
    if (radioAddress.setAddress(host) && radioAddress.protocol() != QAbstractSocket::IPv4Protocol)
    {
        radioAddress.clear();
    }
    if (radioAddress.isNull())
    {
        const QString message = QStringLiteral("Invalid radio IPv4 address");
        emit connectionStageChanged(ConnectionStage::Failed, message);
        emit errorOccurred(ErrorCode::ConnectionFailed, message);
        return;
    }
    if (port == 0 || port > kIcomLanControlPortMax)
    {
        const QString message = QStringLiteral("Invalid LAN control port");
        emit connectionStageChanged(ConnectionStage::Failed, message);
        emit errorOccurred(ErrorCode::ConnectionFailed, message);
        return;
    }

    m_connectionHost = host;
    m_connectionPort = port;
    m_connectionUser = user;
    m_connectionPass = pass;
    m_originalDataOffMod.reset();
    m_originalData1Mod.reset();
    m_selectedRadioMemory.reset();
    m_lastUserVisibleNetworkMessage.clear();

    const quint64 session = ++m_sessionId;
    m_sessionActive = std::make_shared<std::atomic_bool>(true);
    const auto sessionActive = m_sessionActive;
    m_lanModLevel = qBound(0, AppSettings::instance().value("LANModLevel", m_lanModLevel).toInt(), 255);

    m_commander = new Commander();
    Commander* commandSession = m_commander;
    m_commander->moveToThread(m_workerThread);

    connect(m_commander, &RadioCommander::lanReady, this,
            [this, session, commandSession]()
            {
                if (isCurrentSession(session, commandSession))
                {
                    onLanReady();
                }
            });
    connect(m_commander, &RadioCommander::havePortError, this,
            [this, session, commandSession](errorType err)
            {
                if (isCurrentSession(session, commandSession))
                {
                    onPortError(err);
                }
            });
    connect(m_commander, &RadioCommander::haveStatusUpdate, this,
            [this, session, commandSession](networkStatus status)
            {
                if (isCurrentSession(session, commandSession))
                {
                    onNetworkStatus(status);
                }
            });
    connect(m_commander, &RadioCommander::haveSessionHeartbeat, this,
            [this, session, commandSession]()
            {
                if (isCurrentSession(session, commandSession))
                {
                    emit sessionHeartbeat();
                }
            });
    connect(m_commander, &RadioCommander::radioReplyReceived, this,
            [this, session, commandSession](Funcs func, const QVariant& value, uchar receiver)
            {
                if (!isCurrentSession(session, commandSession) || !m_mainSubExchangePending)
                {
                    return;
                }
                Q_UNUSED(receiver)
                if (func == funcVFOBandMS && !value.toBool())
                {
                    m_mainSubExchangeConfirmations |= 0x01;
                }
                else if (func == funcScopeMainSub && !value.toBool())
                {
                    m_mainSubExchangeConfirmations |= 0x02;
                }
                if (m_mainSubExchangeConfirmations == 0x03)
                {
                    m_mainSubExchangePending = false;
                    m_mainSubExchangeRetryTimer->stop();
                    qInfo(logRadio()).noquote() << "MAIN/SUB exchange confirmed on physical MAIN";
                    emit mainSubExchangeCompleted();
                }
            });
    connect(m_commander, &RadioCommander::haveAudioData, this,
            [this, session, commandSession](audioPacket pkt)
            {
                if (isCurrentSession(session, commandSession))
                {
                    onHaveAudioData(pkt);
                }
            });
    connect(m_commander, &RadioCommander::haveNetworkAudioLevels, this,
            [this, session, commandSession](const networkAudioLevels& levels)
            {
                if (!isCurrentSession(session, commandSession))
                {
                    return;
                }
                if (levels.haveTxLevels)
                {
                    emit txAudioLevelChanged(levels.txAudioPeak, levels.txAudioRMS);
                }
            });

    // All radio-to-UI data (frequency, mode, S-meter, scope) flows through the
    // CachingQueue's batched sendValues signal.  Connect once per connectToRadio()
    // call; disconnect the previous session's queue connection first because
    // CachingQueue is a process-wide singleton.
    CachingQueue* q = CachingQueue::getInstance();
    if (m_queueSendValuesConnection)
    {
        QObject::disconnect(m_queueSendValuesConnection);
        m_queueSendValuesConnection = {};
    }
    m_queueSendValuesConnection = connect(
        q, &CachingQueue::sendValues, this,
        [this, session, commandSession, sessionActive](const QVector<CacheItem>& items)
        {
            if (!isCurrentSession(session, commandSession))
            {
                return;
            }
            if (m_radioRouter)
            {
                RadioRouter* router = m_radioRouter;
                QMetaObject::invokeMethod(
                    router,
                    [sessionActive, router, items]()
                    {
                        if (sessionActive->load(std::memory_order_acquire))
                        {
                            router->routeBatch(items);
                        }
                    },
                    Qt::QueuedConnection);
            }
        },
        Qt::AutoConnection);

    // IC-9700 default LAN ports: control=50001, serial=50002, audio=50003
    UdpConnectionSettings udpSettings;
    udpSettings.ipAddress = host;
    udpSettings.controlLANPort = port;   // typically 50001
    udpSettings.civLANPort = port + 1;   // 50002
    udpSettings.audioLANPort = port + 2; // 50003
    udpSettings.scopeLANPort = port + 3; // 50004
    udpSettings.username = user;
    // passcode() applies the IC-9700 LAN proprietary XOR encoding (not encryption).
    passcode(pass, udpSettings.passwordEncoded);
    udpSettings.halfDuplex = false;
    udpSettings.adminLogin = false;
    // Load the saved audio device if no device was explicitly set via setRxAudioDevice().
    if (m_rxDevice.isNull())
    {
        const QByteArray savedId = AppSettings::instance().value("audioOutputDeviceID").toString().toUtf8();
        if (!savedId.isEmpty())
        {
            const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
            const auto it = std::find_if(outputs.cbegin(), outputs.cend(),
                                         [&savedId](const QAudioDevice& dev) { return dev.id() == savedId; });
            if (it != outputs.cend())
            {
                m_rxDevice = *it;
            }
        }
    }
    if (m_txDevice.isNull())
    {
        const QByteArray savedId = AppSettings::instance().value("audioInputDeviceID").toString().toUtf8();
        if (!savedId.isEmpty())
        {
            const QList<QAudioDevice> inputs = QMediaDevices::audioInputs();
            const auto it = std::find_if(inputs.cbegin(), inputs.cend(),
                                         [&savedId](const QAudioDevice& dev) { return dev.id() == savedId; });
            if (it != inputs.cend())
            {
                m_txDevice = *it;
            }
        }
    }

    QAudioDevice rxDev = m_rxDevice.isNull() ? QMediaDevices::defaultAudioOutput() : m_rxDevice;

    // IC-9700 LAN wire values for LPCM 16-bit signed audio.
    static constexpr quint8 kLpcmMono16 = 0x04;
    static constexpr quint8 kLpcmStereo16 = 0x10;
    static constexpr quint16 kIc9700CivAddress = 0xA2;
    static constexpr quint16 kUnusedTcpPort = 0;
    const int outputChannels = qBound(1, AppSettings::instance().value("audioOutputChannels", 2).toInt(), 2);
    m_rxChannelCount = outputChannels;
    const int outputVolume = qBound(0, AppSettings::instance().value("volumeLevel", 128).toInt(), 255);

    audioSetup rxSetup;
    rxSetup.type = qtAudio;
    rxSetup.isinput = false;
    rxSetup.sampleRate = m_rxSampleRate;
    rxSetup.latency = 80;
    rxSetup.codec = outputChannels == 2 ? kLpcmStereo16 : kLpcmMono16;
    rxSetup.resampleQuality = 4;
    rxSetup.localAFgain = static_cast<quint8>(outputVolume);
    rxSetup.port = rxDev;

    audioSetup txSetup;
    txSetup.type = qtAudio;
    txSetup.isinput = true;
    txSetup.sampleRate = kTxAudioSampleRate;
    txSetup.latency = 80;
    txSetup.codec = kLpcmMono16;
    txSetup.resampleQuality = 4;
    txSetup.localAFgain = 255;
    txSetup.port = m_txDevice.isNull() ? QMediaDevices::defaultAudioInput() : m_txDevice;

    // commSetup must be invoked on the worker thread
    QMetaObject::invokeMethod(
        m_commander,
        [sessionActive, commandSession, udpSettings, rxSetup, txSetup]()
        {
            if (!sessionActive->load(std::memory_order_acquire))
            {
                return;
            }
            commandSession->commSetup(kIc9700CivAddress, udpSettings, rxSetup, txSetup, QString(), kUnusedTcpPort);
            commandSession->process();
        },
        Qt::QueuedConnection);
}

void RadioBackend::disconnectFromRadio()
{
    if (m_syncWatchdogTimer)
    {
        m_syncWatchdogTimer->stop();
    }
    emit connectionStageChanged(ConnectionStage::Disconnecting, QStringLiteral("Disconnecting from radio"));
    stopLocalAudio();
    shutdownConnection();
    m_connectionHost.clear();
    m_connectionPort = 0;
    m_connectionUser.clear();
    m_connectionPass.clear();
}

void RadioBackend::setRxAudioDevice(const QAudioDevice& dev)
{
    if (dev.isNull() || m_rxDevice == dev)
    {
        return;
    }
    m_rxDevice = dev;
    invokeOnCurrentCommander([dev](Commander* commander) { commander->setRxAudioDevice(dev); });
}

void RadioBackend::setTxAudioDevice(const QAudioDevice& dev)
{
    if (dev.isNull() || m_txDevice == dev)
    {
        return;
    }
    m_txDevice = dev;
    invokeOnCurrentCommander([dev](Commander* commander) { commander->setTxAudioDevice(dev); });
}

void RadioBackend::stopLocalAudio()
{
    if (!m_commander || !m_sessionActive || !m_sessionActive->load(std::memory_order_acquire))
    {
        return;
    }

    Commander* commandSession = m_commander;
    auto stopDone = std::make_shared<QSemaphore>();
    const bool queued = QMetaObject::invokeMethod(
        commandSession,
        [commandSession, stopDone]()
        {
            commandSession->stopLocalAudio();
            stopDone->release();
        },
        Qt::QueuedConnection);
    if (!queued || !stopDone->tryAcquire(1, 3000))
    {
        qWarning(logAudio()).noquote() << "Timed out waiting for local audio playback to stop";
        return;
    }
    qInfo(logAudio()).noquote() << "[SHUTDOWN] local playback stopped";
}

void RadioBackend::shutdownConnection(bool emitDisconnectedSignal, bool emitDisconnectedStage)
{
    if (!m_commander)
    {
        return;
    }

    if (m_sessionActive)
    {
        m_sessionActive->store(false, std::memory_order_release);
    }
    ++m_sessionId;
    if (m_queueSendValuesConnection)
    {
        QObject::disconnect(m_queueSendValuesConnection);
        m_queueSendValuesConnection = {};
    }
    if (m_pttReleaseDelayTimer)
    {
        m_pttReleaseDelayTimer->stop();
    }
    if (m_pttOffConfirmationTimer)
    {
        m_pttOffConfirmationTimer->stop();
    }
    disarmTransmitSafety();
    if (m_syncWatchdogTimer)
    {
        m_syncWatchdogTimer->stop();
    }
    m_pttState.reset();
    m_activeVfo = Vfo::Main;
    m_dualWatchEnabled = false;
    m_smeterPollSubNext = false;
    m_vfoStatePollPhase = 0;
    m_vfoStatePollSubNext = false;
    m_smeterPollPending = false;
    m_smeterPollSettlingSample = false;
    m_smeterPollPendingTicks = 0;
    m_smeterRestoreVfo = Vfo::Main;
    m_mainSubExchangePending = false;
    m_mainSubExchangeConfirmations = 0;
    if (m_mainSubExchangeRetryTimer)
    {
        m_mainSubExchangeRetryTimer->stop();
    }

    if (m_smeterPollTimer)
    {
        m_smeterPollTimer->stop();
        m_smeterPollTimer->deleteLater();
        m_smeterPollTimer = nullptr;
    }
    if (m_vfoStatePollTimer)
    {
        m_vfoStatePollTimer->stop();
    }
    if (m_initialStateRetryTimer)
    {
        m_initialStateRetryTimer->stop();
        m_initialStateRetryTimer->deleteLater();
        m_initialStateRetryTimer = nullptr;
    }
    if (m_scopeRetryTimer)
    {
        m_scopeRetryTimer->stop();
        m_scopeRetryTimer->deleteLater();
        m_scopeRetryTimer = nullptr;
    }
    if (m_bandStateRefreshTimer)
    {
        m_bandStateRefreshTimer->stop();
    }

    Commander* commandSession = m_commander;
    const std::optional<int> originalDataOffMod = m_originalDataOffMod;
    const std::optional<int> originalData1Mod = m_originalData1Mod;

    if (QThread::currentThread() == m_commander->thread())
    {
        sendDisconnectSafetyCommands(m_commander, originalDataOffMod, originalData1Mod);
        m_commander->closeComm();
    }
    else
    {
        auto closeDone = std::make_shared<QSemaphore>();
        const bool queued = QMetaObject::invokeMethod(
            m_commander,
            [commandSession, closeDone, originalDataOffMod, originalData1Mod]()
            {
                sendDisconnectSafetyCommands(commandSession, originalDataOffMod, originalData1Mod);
                commandSession->closeComm();
                closeDone->release();
            },
            Qt::QueuedConnection);
        if (!queued || !closeDone->tryAcquire(1, 14000))
        {
            qWarning(logRadio()).noquote()
                << "[SHUTDOWN] closeComm() did not finish within 14000 ms; continuing disconnect";
        }
    }
    commandSession->deleteLater();
    m_commander = nullptr;
    m_radioReady = false;
    m_scopeDataReceived = false;
    setScopeSyncDegraded(false);
    resetScopeController();
    m_initialFrequencyReceived = false;
    m_initialModeReceived = false;
    m_initialMainFrequencyReceived = false;
    m_initialMainModeReceived = false;
    m_initialStateRequested = false;
    m_currentBandKey = -1;
    m_currentMainFrequencyHz = 0;
    m_currentDuplexMode = dmSimplex;
    m_currentRepeaterOffsetHz = 0;
    m_transmitConfiguration.reset();
    m_txMeterPollTick = 0;
    m_originalDataOffMod.reset();
    m_originalData1Mod.reset();
    m_lastUserVisibleNetworkMessage.clear();
    emit readyChanged(false);
    if (emitDisconnectedSignal)
    {
        emit disconnected();
    }
    if (emitDisconnectedStage)
    {
        emit connectionStageChanged(ConnectionStage::Disconnected, QStringLiteral("Radio disconnected"));
    }
}

void RadioBackend::setFrequencyHz(quint64 hz)
{
    m_transmitConfiguration.requestFrequency(hz);
    const bool transferSelectedMemory = m_selectedRadioMemory.has_value();
    m_selectedRadioMemory.reset();

    Frequency f;
    f.Hz = hz;
    f.MHzDouble = hz / 1e6;
    f.VFO = activeVFO;
    invokeOnCurrentCommander(
        [f, transferSelectedMemory](Commander* commandSession)
        {
            if (transferSelectedMemory)
            {
                commandSession->receiveCommand(funcMemoryToVFO, QVariant(), 0);
            }
            else
            {
                selectMainVfoForCommand(commandSession);
            }
            commandSession->receiveCommandNoReadback(funcFreqSet, QVariant::fromValue(f), 0);
            commandSession->receiveCommand(funcSelectedFreq, QVariant(), 0);
            commandSession->receiveCommand(funcSelectedMode, QVariant(), 0);
        });
}

void RadioBackend::setMode(const QString& mode)
{
    const bool transferSelectedMemory = m_selectedRadioMemory.has_value();
    m_selectedRadioMemory.reset();

    if (!m_commander)
    {
        return;
    }

    ModeInfo mi;
    if (!populateModeInfo(mode, &mi))
    {
        qWarning(logRadio()).noquote() << "Ignoring unsupported mode selection:" << mode;
        return;
    }
    mi.filter = static_cast<quint8>(qBound(1, m_currentMainFilter, 3));

    invokeOnCurrentCommander(
        [mi, transferSelectedMemory](Commander* commandSession)
        {
            if (transferSelectedMemory)
            {
                commandSession->receiveCommand(funcMemoryToVFO, QVariant(), 0);
            }
            else
            {
                selectMainVfoForCommand(commandSession);
            }
            commandSession->receiveCommandNoReadback(funcModeSet, QVariant::fromValue(mi), 0);
            commandSession->receiveCommand(funcSelectedFreq, QVariant(), 0);
            commandSession->receiveCommand(funcSelectedMode, QVariant(), 0);
        });
}

void RadioBackend::setFilterWidth(int lowHz, int highHz)
{
    Q_UNUSED(lowHz)
    Q_UNUSED(highHz)
    // IC-9700 filter select is via CI-V 0x26 (filter 1/2/3); deferred to v1.1.
}

void RadioBackend::setNrEnabled(bool on)
{
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcNoiseReduction, QVariant::fromValue<bool>(on), 0); });
}

void RadioBackend::setNrLevel(int level)
{
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcNRLevel, QVariant(level), 0); });
}

void RadioBackend::setNbEnabled(bool on)
{
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcNoiseBlanker, QVariant::fromValue<bool>(on), 0); });
}

void RadioBackend::setNbLevel(int level)
{
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcNBLevel, QVariant(level), 0); });
}

void RadioBackend::setPreampEnabled(bool on)
{
    setPreampLevel(on ? 1 : 0);
}

void RadioBackend::setPreampLevel(int level)
{
    const uchar val = static_cast<uchar>(qBound(0, level, 3));
    invokeOnCurrentCommander(
        [=](Commander* commandSession)
        {
            commandSession->receiveCommand(funcPreamp, QVariant::fromValue<uchar>(val), 0);
            commandSession->receiveCommand(funcAttenuator, QVariant(), 0);
        });
}

void RadioBackend::setAttenuatorEnabled(bool on)
{
    const uchar val = on ? 10 : 0;
    invokeOnCurrentCommander(
        [=](Commander* commandSession)
        {
            commandSession->receiveCommand(funcAttenuator, QVariant::fromValue<uchar>(val), 0);
            commandSession->receiveCommand(funcPreamp, QVariant(), 0);
        });
}

void RadioBackend::setAfGain(int level)
{
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcAfGain, QVariant(level), 0xff); });
}

void RadioBackend::setRfGain(int level)
{
    const ushort bounded = static_cast<ushort>(qBound(0, level, 255));
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcRfGain, QVariant::fromValue<ushort>(bounded), 0); });
}

void RadioBackend::setTxPower(int level)
{
    const ushort bounded = static_cast<ushort>(qBound(0, level, 255));
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcRFPower, QVariant::fromValue<ushort>(bounded), 0); });
}

void RadioBackend::setTuningStep(int step)
{
    const uchar val = static_cast<uchar>(qBound(0, step, 11));
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcTuningStep, QVariant::fromValue<uchar>(val), 0); });
}

void RadioBackend::selectVfo(Vfo vfo)
{
    invokeOnCurrentCommander(
        [vfo](Commander* commandSession)
        { commandSession->receiveCommand(funcVFOBandMS, QVariant::fromValue<bool>(vfo == Vfo::Sub), 0); });
}

void RadioBackend::exchangeMainSub()
{
    m_mainSubExchangePending = true;
    m_mainSubExchangeConfirmations = 0;
    m_mainSubExchangeRetryTimer->start();
    invokeOnCurrentCommander(
        [](Commander* commandSession)
        {
            // IC-9700 native MAIN/SUB exchange moves the VFO operating
            // context, but RF gain remains attached to the physical MAIN and
            // SUB receivers. Preserve that radio-authoritative behavior. If a
            // future design wants RF gain to follow the logical VFO boxes, it
            // must explicitly snapshot and restore both receiver values after
            // this command rather than assuming 07 B0 exchanges them.
            commandSession->receiveCommand(funcVFOSwapMS, QVariant(), 0);
            commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
            commandSession->receiveCommand(funcFreqGet, QVariant(), 0);
            commandSession->receiveCommand(funcModeGet, QVariant(), 0);
            requestSubVfoStateForCommand(commandSession);
            commandSession->receiveCommand(funcVFOBandMS, QVariant::fromValue<bool>(false), 0);
            commandSession->receiveCommand(funcScopeMainSub, QVariant::fromValue<bool>(false), 0);
            // This final readback is deliberately last: it proves that the
            // exchange, both receiver snapshots, and restoration to physical
            // MAIN have all completed before PTT is unlocked.
            commandSession->receiveCommand(funcFreqGet, QVariant(), 0);
        });
}

void RadioBackend::setVfoFrequencyHz(Vfo vfo, quint64 hz)
{
    if (vfo == Vfo::Main)
    {
        setFrequencyHz(hz);
        return;
    }

    Frequency frequency;
    frequency.Hz = hz;
    frequency.MHzDouble = hz / 1e6;
    frequency.VFO = activeVFO;
    invokeOnCurrentCommander(
        [frequency](Commander* commandSession)
        {
            commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoSub), 0);
            commandSession->receiveCommandNoReadback(funcFreqSet, QVariant::fromValue(frequency), 1);
            commandSession->receiveCommand(funcFreqGet, QVariant(), 1);
            commandSession->receiveCommand(funcModeGet, QVariant(), 1);
            commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
            commandSession->receiveCommand(funcVFOBandMS, QVariant::fromValue<bool>(false), 0);
        });
}

void RadioBackend::requestVfoState(Vfo vfo)
{
    if (vfo == Vfo::Sub)
    {
        invokeOnCurrentCommander(
            [](Commander* commandSession)
            {
                requestSubVfoStateForCommand(commandSession);
                commandSession->receiveCommand(funcVFOBandMS, QVariant::fromValue<bool>(false), 0);
            });
        return;
    }

    invokeOnCurrentCommander(
        [](Commander* commandSession)
        {
            selectMainVfoForCommand(commandSession);
            commandSession->receiveCommand(funcFreqGet, QVariant(), 0);
            commandSession->receiveCommand(funcModeGet, QVariant(), 0);
            for (const Funcs func :
                 {funcAGCTimeConstant, funcAttenuator, funcNoiseBlanker, funcAutoNotch, funcManualNotch,
                  funcNoiseReduction, funcPreamp, funcRfGain, funcSquelch, funcRFPower, funcSplitStatus,
                  funcReadFreqOffset, funcToneSquelchType, funcToneFreq, funcDTCSCode, funcCompressor, funcXFCStatus})
            {
                commandSession->receiveCommand(func, QVariant(), 0);
            }
        });
}

void RadioBackend::routeVfoReceiverCommand(Vfo vfo, const std::function<void(Commander*, uchar)>& command)
{
    const Vfo restoreVfo = m_activeVfo;
    const uchar receiver = vfo == Vfo::Main ? 0 : 1;
    invokeOnCurrentCommander(
        [vfo, restoreVfo, receiver, command](Commander* commandSession)
        {
            if (vfo != restoreVfo)
            {
                commandSession->receiveCommand(funcSelectVFO,
                                               QVariant::fromValue<vfo_t>(vfo == Vfo::Sub ? vfoSub : vfoMain), 0);
            }
            command(commandSession, receiver);
            if (vfo != restoreVfo)
            {
                commandSession->receiveCommand(
                    funcSelectVFO, QVariant::fromValue<vfo_t>(restoreVfo == Vfo::Sub ? vfoSub : vfoMain), 0);
            }
        });
}

void RadioBackend::setVfoMode(Vfo vfo, const QString& mode)
{
    ModeInfo modeInfo;
    if (!populateModeInfo(mode, &modeInfo))
    {
        qWarning(logRadio()).noquote() << "Ignoring unsupported VFO mode selection:" << mode;
        return;
    }
    modeInfo.filter = static_cast<quint8>(qBound(1, m_currentMainFilter, 3));
    routeVfoReceiverCommand(vfo,
                            [modeInfo](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommandNoReadback(funcModeSet, QVariant::fromValue(modeInfo),
                                                                         receiver);
                                commandSession->receiveCommand(funcModeGet, QVariant(), receiver);
                            });
}

void RadioBackend::setVfoAgcMode(Vfo vfo, const QString& mode)
{
    uchar agc = mode == QStringLiteral("fast") ? 1 : mode == QStringLiteral("slow") ? 3 : 2;
    routeVfoReceiverCommand(vfo,
                            [agc](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommandNoReadback(funcAGCTimeConstant, QVariant::fromValue(agc),
                                                                         receiver);
                                commandSession->receiveCommand(funcAGCTimeConstant, QVariant(), receiver);
                            });
}

void RadioBackend::setVfoAttenuatorEnabled(Vfo vfo, bool on)
{
    const uchar value = on ? 10 : 0;
    routeVfoReceiverCommand(vfo,
                            [value](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommandNoReadback(funcAttenuator, QVariant::fromValue(value),
                                                                         receiver);
                                commandSession->receiveCommand(funcAttenuator, QVariant(), receiver);
                                commandSession->receiveCommand(funcPreamp, QVariant(), receiver);
                            });
}

void RadioBackend::setVfoNbEnabled(Vfo vfo, bool on)
{
    routeVfoReceiverCommand(vfo,
                            [on](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommandNoReadback(funcNoiseBlanker, QVariant::fromValue(on),
                                                                         receiver);
                                commandSession->receiveCommand(funcNoiseBlanker, QVariant(), receiver);
                            });
}

void RadioBackend::setVfoNotch(Vfo vfo, VfoNotch notch)
{
    routeVfoReceiverCommand(vfo,
                            [notch](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommandNoReadback(
                                    funcAutoNotch, QVariant::fromValue(notch == VfoNotch::Auto), receiver);
                                commandSession->receiveCommandNoReadback(
                                    funcManualNotch, QVariant::fromValue(notch == VfoNotch::Manual), receiver);
                                commandSession->receiveCommand(funcAutoNotch, QVariant(), receiver);
                                commandSession->receiveCommand(funcManualNotch, QVariant(), receiver);
                            });
}

void RadioBackend::setVfoNrEnabled(Vfo vfo, bool on)
{
    routeVfoReceiverCommand(vfo,
                            [on](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommandNoReadback(funcNoiseReduction, QVariant::fromValue(on),
                                                                         receiver);
                                commandSession->receiveCommand(funcNoiseReduction, QVariant(), receiver);
                            });
}

void RadioBackend::setVfoPreampLevel(Vfo vfo, int level)
{
    const uchar value = static_cast<uchar>(qBound(0, level, 3));
    routeVfoReceiverCommand(vfo,
                            [value](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommandNoReadback(funcPreamp, QVariant::fromValue(value),
                                                                         receiver);
                                commandSession->receiveCommand(funcPreamp, QVariant(), receiver);
                                commandSession->receiveCommand(funcAttenuator, QVariant(), receiver);
                            });
}

void RadioBackend::setVfoRfGain(Vfo vfo, int level)
{
    const ushort value = static_cast<ushort>(qBound(0, level, 255));
    routeVfoReceiverCommand(vfo,
                            [value](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommandNoReadback(funcRfGain, QVariant::fromValue(value),
                                                                         receiver);
                                commandSession->receiveCommand(funcRfGain, QVariant(), receiver);
                            });
}

void RadioBackend::setVfoSquelch(Vfo vfo, int level)
{
    const ushort value = static_cast<ushort>(qBound(0, level, 255));
    routeVfoReceiverCommand(vfo,
                            [value](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommandNoReadback(funcSquelch, QVariant::fromValue(value),
                                                                         receiver);
                                commandSession->receiveCommand(funcSquelch, QVariant(), receiver);
                            });
}

void RadioBackend::setDualWatchEnabled(bool on)
{
    invokeOnCurrentCommander(
        [on](Commander* commandSession)
        {
            commandSession->receiveCommand(funcVFODualWatch, QVariant::fromValue<bool>(on), 0);
            if (on)
            {
                requestSubVfoStateForCommand(commandSession);
                commandSession->receiveCommand(funcVFOBandMS, QVariant::fromValue<bool>(false), 0);
            }
            else
            {
                selectMainVfoForCommand(commandSession);
            }
        });
}

void RadioBackend::setSquelch(bool on, int level)
{
    if (!m_commander)
    {
        return;
    }
    // On IC-9700, squelch level 0 = fully open, >0 = active.
    // Setting funcSquelch with 0 disables it; non-zero enables + sets level.
    const ushort squelchVal = on ? qMax<ushort>(1, static_cast<ushort>(qBound(0, level, 255))) : 0;
    invokeOnCurrentCommander(
        [squelchVal](Commander* commandSession)
        { commandSession->receiveCommand(funcSquelch, QVariant::fromValue<ushort>(squelchVal), 0); });
}

void RadioBackend::setAgcMode(const QString& mode)
{
    if (!m_commander)
    {
        return;
    }
    uchar agc = 2; // MID
    if (mode == "fast")
    {
        agc = 1;
    }
    else if (mode == "mid")
    {
        agc = 2;
    }
    else if (mode == "slow")
    {
        agc = 3;
    }
    invokeOnCurrentCommander(
        [agc](Commander* commandSession)
        { commandSession->receiveCommand(funcAGCTimeConstant, QVariant::fromValue<uchar>(agc), 0); });
}

void RadioBackend::setAutoNotch(bool on)
{
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcAutoNotch, QVariant::fromValue<bool>(on), 0); });
}

void RadioBackend::setManualNotch(bool on)
{
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcManualNotch, QVariant::fromValue<bool>(on), 0); });
}

void RadioBackend::setRitEnabled(bool on)
{
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcRitStatus, QVariant::fromValue<bool>(on), 0); });
}

void RadioBackend::setRitOffset(short hz)
{
    const short bounded = qBound(static_cast<short>(-999), hz, static_cast<short>(999));
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcRitFreq, QVariant::fromValue<short>(bounded), 0); });
}

void RadioBackend::setCompressor(bool on)
{
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcCompressor, QVariant::fromValue<bool>(on), 0); });
}

void RadioBackend::setCompressorLevel(int level)
{
    const ushort bounded = static_cast<ushort>(qBound(0, level, 255));
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcCompressorLevel, QVariant::fromValue(bounded), 0); });
}

void RadioBackend::setXfcEnabled(bool on)
{
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcXFCStatus, QVariant::fromValue<bool>(on), 0); });
}

void RadioBackend::setDuplexMode(duplexMode_t mode)
{
    m_transmitConfiguration.requestDuplexMode(mode);
    const uchar receiver = m_activeVfo == Vfo::Sub ? 1 : 0;
    invokeOnCurrentCommander(
        [=](Commander* commandSession)
        {
            commandSession->receiveCommand(funcSplitStatus, QVariant::fromValue(mode), receiver);
            commandSession->receiveCommand(funcSplitStatus, QVariant(), receiver);
        });
}

void RadioBackend::setRepeaterOffsetHz(quint64 hz)
{
    m_transmitConfiguration.requestOffset(hz);
    Frequency offset;
    offset.Hz = hz;
    offset.MHzDouble = hz / 1e6;
    offset.VFO = activeVFO;
    const uchar receiver = m_activeVfo == Vfo::Sub ? 1 : 0;
    invokeOnCurrentCommander(
        [=](Commander* commandSession)
        {
            commandSession->receiveCommand(funcSendFreqOffset, QVariant::fromValue(offset), receiver);
            commandSession->receiveCommand(funcReadFreqOffset, QVariant(), receiver);
        });
}

void RadioBackend::setToneAccessMode(rptAccessTxRx_t mode)
{
    if (!m_commander)
    {
        return;
    }

    RptrAccessData access;
    access.accessMode = mode;
    const uchar receiver = m_activeVfo == Vfo::Sub ? 1 : 0;
    invokeOnCurrentCommander(
        [=](Commander* commandSession)
        {
            commandSession->receiveCommand(funcToneSquelchType, QVariant::fromValue(access), receiver);
            commandSession->receiveCommand(funcToneSquelchType, QVariant(), receiver);
        });
}

void RadioBackend::setToneFrequency(ushort tone)
{
    if (!m_commander)
    {
        return;
    }

    ToneInfo info(tone);
    const uchar receiver = m_activeVfo == Vfo::Sub ? 1 : 0;
    invokeOnCurrentCommander(
        [=](Commander* commandSession)
        {
            commandSession->receiveCommand(funcToneFreq, QVariant::fromValue(info), receiver);
            commandSession->receiveCommand(funcTSQLFreq, QVariant::fromValue(info), receiver);
            commandSession->receiveCommand(funcToneFreq, QVariant(), receiver);
        });
}

void RadioBackend::setDtcsCode(ushort code)
{
    if (!m_commander)
    {
        return;
    }

    ToneInfo info(code);
    const uchar receiver = m_activeVfo == Vfo::Sub ? 1 : 0;
    invokeOnCurrentCommander(
        [=](Commander* commandSession)
        {
            commandSession->receiveCommand(funcDTCSCode, QVariant::fromValue(info), receiver);
            commandSession->receiveCommand(funcDTCSCode, QVariant(), receiver);
        });
}

void RadioBackend::selectMainVfoForCommand(Commander* commandSession)
{
    if (!commandSession)
    {
        return;
    }
    commandSession->receiveCommand(funcVFOBandMS, QVariant::fromValue<bool>(false), 0);
    commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
}

void RadioBackend::requestSubVfoStateForCommand(Commander* commandSession)
{
    if (!commandSession)
    {
        return;
    }
    // IC-9700 CI-V has no direct MAIN/SUB receiver prefix. Select SUB,
    // correlate the current-frequency/mode replies as receiver 1, and restore
    // MAIN before other application commands continue.
    commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoSub), 0);
    commandSession->receiveCommand(funcFreqGet, QVariant(), 1);
    commandSession->receiveCommand(funcModeGet, QVariant(), 1);
    for (const Funcs func : {funcAGCTimeConstant, funcAttenuator, funcNoiseBlanker, funcAutoNotch, funcManualNotch,
                             funcNoiseReduction, funcPreamp, funcRfGain, funcSquelch, funcSplitStatus,
                             funcReadFreqOffset, funcToneSquelchType, funcToneFreq, funcDTCSCode})
    {
        commandSession->receiveCommand(func, QVariant(), 1);
    }
    commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
}

void RadioBackend::selectMemoryBandForCommand(Commander* commandSession, quint16 group)
{
    if (!commandSession)
    {
        return;
    }

    const quint64 hz = memoryGroupDefaultFrequencyHz(group);
    if (hz == 0)
    {
        qWarning(logRadio()).noquote() << "Ignoring memory band selection for unsupported group" << group;
        return;
    }

    Frequency frequency;
    frequency.Hz = hz;
    frequency.MHzDouble = hz / 1e6;
    frequency.VFO = activeVFO;

    // IC-9700 memory channels are scoped by band. Select the MAIN VFO and tune
    // it inside the desired band before entering memory mode so command 08h
    // resolves channel N against the intended memory group.
    selectMainVfoForCommand(commandSession);
    commandSession->receiveCommand(funcVFOModeSelect, QVariant(), 0);
    commandSession->receiveCommandNoReadback(funcFreqSet, QVariant::fromValue(frequency), 0);
}

void RadioBackend::selectMemoryForCommand(Commander* commandSession, quint16 group, quint16 channel, bool prepareBand)
{
    if (!commandSession)
    {
        return;
    }
    if (memoryGroupDefaultFrequencyHz(group) == 0)
    {
        qWarning(logRadio()).noquote() << "Ignoring memory channel selection for unsupported group" << group;
        return;
    }

    if (prepareBand)
    {
        selectMemoryBandForCommand(commandSession, group);
    }
    // Command 08h carries the channel number. When prepareBand is false the
    // caller has already selected the desired band and is avoiding VFO tuning
    // commands in a timing-sensitive path such as PTT.
    commandSession->receiveCommand(funcMemoryMode, QVariant(), 0);
    commandSession->receiveCommand(funcMemoryMode, QVariant::fromValue(static_cast<uint>(channel)), 0);
}

void RadioBackend::setScopeEnabled(bool on)
{
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcScopeOnOff, QVariant::fromValue<bool>(on), 0); });
}

void RadioBackend::setScopeSpanHz(quint64 hz)
{
    if (!m_commander)
    {
        return;
    }

    static constexpr quint64 kSupportedSpans[] = {2500, 5000, 10000, 25000, 50000, 100000, 250000, 500000};
    quint64 selected = kSupportedSpans[std::size(kSupportedSpans) - 1];
    for (const quint64 span : kSupportedSpans)
    {
        selected = span;
        if (hz <= span)
        {
            break;
        }
    }

    centerSpanData span;
    span.freq = selected;
    span.name = QString::number(selected);
    invokeOnCurrentCommander(
        [span](Commander* commandSession)
        { commandSession->receiveCommand(funcScopeSpan, QVariant::fromValue<centerSpanData>(span), 0); });
}

void RadioBackend::setScopeMode(int mode)
{
    const uchar bounded = static_cast<uchar>(qBound(0, mode, 1));
    invokeOnCurrentCommander(
        [=](Commander* commandSession)
        { commandSession->receiveCommand(funcScopeMode, QVariant::fromValue<uchar>(bounded), 0); });
}

void RadioBackend::setScopeVfo(Vfo vfo)
{
    const bool sub = vfo == Vfo::Sub;
    invokeOnCurrentCommander([sub](Commander* commandSession)
                             { commandSession->receiveCommand(funcScopeMainSub, QVariant::fromValue<bool>(sub), 0); });
}

void RadioBackend::setScopeFixedRangeHz(quint64 startHz, quint64 endHz)
{
    if (endHz <= startHz)
    {
        return;
    }

    static constexpr uchar kPanScopeEdge = 1;
    SpectrumBounds bounds(startHz / 1e6, endHz / 1e6, kPanScopeEdge);
    invokeOnCurrentCommander(
        [bounds](Commander* commandSession)
        {
            commandSession->receiveCommand(funcScopeFixedEdgeFreq, QVariant::fromValue<SpectrumBounds>(bounds), 0);
            commandSession->receiveCommand(funcScopeEdge, QVariant::fromValue<uchar>(bounds.edge), 0);
            commandSession->receiveCommand(funcScopeMode, QVariant::fromValue<uchar>(1), 0);
        });
}

bool RadioBackend::setPtt(bool on)
{
    if (!m_commander)
    {
        return false;
    }

    if (on)
    {
        if (m_transmitConfiguration.confirmationPending())
        {
            emit statusMessage(QStringLiteral("PTT blocked: waiting for radio settings confirmation"),
                               MessageSeverity::Error);
            return false;
        }
        if (!m_transmitConfiguration.transmitFrequencyAllowed())
        {
            emit statusMessage(QStringLiteral("PTT blocked: transmit frequency outside band limits"),
                               MessageSeverity::Error);
            return false;
        }

        if (m_pttReleaseDelayTimer)
        {
            m_pttReleaseDelayTimer->stop();
        }
        if (!m_pttState.requestOn())
        {
            return true;
        }

        armTransmitSafety();
        const auto selectedMemory = m_selectedRadioMemory;
        qInfo(logRadio()).noquote() << "PTT route target= MAIN memory=" << (selectedMemory.has_value() ? "yes" : "no");
        invokeOnCurrentCommander(
            [selectedMemory](Commander* commandSession)
            {
                if (selectedMemory)
                {
                    const auto [group, channel] = *selectedMemory;
                    // The selected memory already established its band. Do
                    // not run selectMemoryBandForCommand() here: that sequence
                    // enters VFO mode and publishes a band-default frequency
                    // before returning to memory mode.
                    selectMemoryForCommand(commandSession, group, channel, false);
                }
                else
                {
                    commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
                }
                commandSession->setPttActive(true);
                commandSession->receiveCommand(funcTransceiverStatus, QVariant::fromValue<bool>(true), 0);
                if (selectedMemory)
                {
                    const auto [group, channel] = *selectedMemory;
                    selectMemoryForCommand(commandSession, group, channel, false);
                    commandSession->receiveCommand(funcSelectedFreq, QVariant(), 0);
                    commandSession->receiveCommand(funcSelectedMode, QVariant(), 0);
                }
            });
        return true;
    }
    else
    {
        if (m_pttReleaseDelayTimer && m_pttReleaseDelayTimer->isActive())
        {
            return true;
        }

        if (!m_pttState.confirmedActive() && !m_pttState.desiredActive())
        {
            sendPttOffNow();
            return true;
        }

        // Keep the radio keyed briefly so the final TX audio frames already
        // buffered locally or in the LAN path can reach the transmitter.
        if (m_pttReleaseDelayTimer)
        {
            m_pttReleaseDelayTimer->start();
        }
        else
        {
            sendPttOffNow();
        }
        return true;
    }
}

void RadioBackend::sendPttOffNow()
{
    if (!m_commander)
    {
        return;
    }

    if (m_pttReleaseDelayTimer)
    {
        m_pttReleaseDelayTimer->stop();
    }
    // Always send an unkey request. If local state ever gets stale, suppressing
    // this command can leave the radio transmitting until disconnect.
    m_pttState.requestOff();
    emit pttChanged(false);
    if (m_pttOffConfirmationTimer)
    {
        m_pttOffConfirmationTimer->start();
    }
    invokeOnCurrentCommander(
        [](Commander* commandSession)
        {
            commandSession->setPttActive(false);
            commandSession->receiveCommand(funcTransceiverStatus, QVariant::fromValue<bool>(false), 0);
        });
}

void RadioBackend::armTransmitSafety()
{
    if (!m_pttMaxDurationTimer)
    {
        return;
    }

    if (!m_pttMaxDurationTimer->isActive())
    {
        m_transmitSafetyPolicy.reset();
        m_pttMaxDurationTimer->start();
    }
}

void RadioBackend::disarmTransmitSafety()
{
    if (m_pttMaxDurationTimer)
    {
        m_pttMaxDurationTimer->stop();
    }
    m_transmitSafetyPolicy.reset();
}

void RadioBackend::resetScopeController()
{
    if (!m_scopeController)
    {
        return;
    }

    if (QThread::currentThread() == m_scopeController->thread())
    {
        m_scopeController->reset();
        return;
    }

    QMetaObject::invokeMethod(m_scopeController, &ScopeController::reset, Qt::QueuedConnection);
}

void RadioBackend::forcePttOffForSafety(const QString& message)
{
    if (!m_commander || !m_pttState.safetyActive())
    {
        disarmTransmitSafety();
        return;
    }

    qWarning(logRadio()).noquote() << message;
    emit statusMessage(message, MessageSeverity::Warning);
    sendPttOffNow();
}

void RadioBackend::handleTransmitSwr(double swr)
{
    emit swrChanged(swr);

    if (!m_pttState.safetyActive())
    {
        m_transmitSafetyPolicy.observe(false, swr);
        return;
    }

    if (!m_transmitSafetyPolicy.observe(true, swr))
    {
        return;
    }

    forcePttOffForSafety(QStringLiteral("TX stopped: SWR %1 exceeded safety cutoff").arg(swr, 0, 'f', 2));
}

void RadioBackend::sendDtmf(const QString& digits)
{
    if (!m_commander || digits.isEmpty())
    {
        return;
    }

    const QByteArray pcm = sdr9700::audio::generateDtmfPcm(digits, kTxAudioSampleRate);
    if (pcm.isEmpty())
    {
        return;
    }

    // Queue the full PCM buffer to UdpAudio in a single call. receiveAudioData
    // will substitute it for mic audio frame-by-frame while DTMF is active.
    // Both queueDtmfPcm and receiveAudioData run on udpHandlerThread, so there
    // is no race between the buffer being written and consumed.
    invokeOnCurrentCommander([pcm](Commander* c) { c->sendDtmfPcm(pcm); });
}

void RadioBackend::pollFrequency()
{
    invokeOnCurrentCommander([](Commander* commandSession)
                             { commandSession->receiveCommand(funcFreqGet, QVariant(), 0); });
}

void RadioBackend::selectVfoMode()
{
    const bool transferSelectedMemory = m_selectedRadioMemory.has_value();
    m_selectedRadioMemory.reset();
    invokeOnCurrentCommander(
        [transferSelectedMemory](Commander* commandSession)
        {
            commandSession->receiveCommand(transferSelectedMemory ? funcMemoryToVFO : funcVFOModeSelect, QVariant(), 0);
        });
}

void RadioBackend::selectRadioMemory(quint16 group, quint16 channel)
{
    m_selectedRadioMemory = std::make_pair(group, channel);
    const uint memoryAddress = (static_cast<uint>(group) << 16) | static_cast<uint>(channel);
    bool prepareBand = true;
    if (m_currentBandKey >= 0)
    {
        const auto currentBand = static_cast<availableBands>(m_currentBandKey);
        if (const sdr9700::RadioBandDef* definition = sdr9700::radioBandDefinition(currentBand))
        {
            prepareBand = definition->memGroup != group;
        }
    }
    invokeOnCurrentCommander(
        [group, channel, memoryAddress, prepareBand](Commander* commandSession)
        {
            // Memory channel numbers are band-scoped on the IC-9700. Only use
            // the intermediate band-routing tune when changing bands; within
            // the current band, selecting command 08h directly avoids an
            // unnecessary frequency transition.
            selectMemoryForCommand(commandSession, group, channel, prepareBand);
            commandSession->receiveCommand(funcMemoryContents, QVariant::fromValue(memoryAddress), 0);
            commandSession->receiveCommand(funcSelectedFreq, QVariant(), 0);
            commandSession->receiveCommand(funcSelectedMode, QVariant(), 0);
            commandSession->receiveCommand(funcSplitStatus, QVariant(), 0);
            commandSession->receiveCommand(funcReadFreqOffset, QVariant(), 0);
            commandSession->receiveCommand(funcToneSquelchType, QVariant(), 0);
            commandSession->receiveCommand(funcToneFreq, QVariant(), 0);
            commandSession->receiveCommand(funcTSQLFreq, QVariant(), 0);
            commandSession->receiveCommand(funcDTCSCode, QVariant(), 0);
        });
}

void RadioBackend::requestRadioMemory(quint16 group, quint16 channel)
{
    const uint memoryAddress = (static_cast<uint>(group) << 16) | static_cast<uint>(channel);
    invokeOnCurrentCommander(
        [memoryAddress](Commander* commandSession)
        { commandSession->receiveCommand(funcMemoryContents, QVariant::fromValue(memoryAddress), 0); });
}

void RadioBackend::writeRadioMemory(MemoryType memory)
{
    const uint memoryAddress = (static_cast<uint>(memory.group) << 16) | static_cast<uint>(memory.channel);
    invokeOnCurrentCommander(
        [memory, memoryAddress](Commander* commandSession)
        {
            commandSession->receiveCommand(funcMemoryContents, QVariant::fromValue(memory), 0);
            QTimer::singleShot(
                kMemoryWriteReadbackDelayMs, commandSession, [commandSession, memoryAddress]()
                { commandSession->receiveCommand(funcMemoryContents, QVariant::fromValue(memoryAddress), 0); });
        });
}

void RadioBackend::requestInitialRadioState()
{
    if (!m_commander)
    {
        return;
    }

    const bool firstRequest = !m_initialStateRequested;
    const auto requestMainVfoState = [this]()
    {
        if (!m_commander)
        {
            return;
        }
        invokeOnCurrentCommander(
            [](Commander* commandSession)
            {
                // Use raw current-VFO 03/04 reads for connection readiness.
                // In LAN startup captures the IC-9700 replied to scope/status
                // traffic but did not answer selected-VFO 25/26 probes before
                // the watchdog expired, causing an unnecessary reconnect loop.
                commandSession->readCurrentFrequencyAndMode();
            });
    };

    if (firstRequest)
    {
        m_initialStateRequested = true;
        requestMainVfoState();
    }

    if (m_initialMainFrequencyReceived && m_initialMainModeReceived)
    {
        return;
    }

    if (firstRequest)
    {
        // The first request already queued the critical VFO probe before the
        // full status snapshot above. Retries use requestMainVfoState() below.
        return;
    }

    qDebug(logRadio()).noquote().nospace()
        << "Retrying initial MAIN VFO state frequencyReceived=" << m_initialMainFrequencyReceived
        << " modeReceived=" << m_initialMainModeReceived;
    requestMainVfoState();
}

void RadioBackend::requestPostReadyRadioState()
{
    if (!m_commander)
    {
        return;
    }

    const ushort lanModLevel = static_cast<ushort>(m_lanModLevel);
    invokeOnCurrentCommander(
        [lanModLevel](Commander* commandSession)
        {
            const radioInput lanInput(inputLAN, 5, QStringLiteral("LAN"));
            commandSession->receiveCommand(funcDATAOffMod, QVariant(), 0);
            commandSession->receiveCommand(funcDATA1Mod, QVariant(), 0);
            commandSession->receiveCommandNoReadback(funcDATAOffMod, QVariant::fromValue(lanInput), 0);
            commandSession->receiveCommandNoReadback(funcDATA1Mod, QVariant::fromValue(lanInput), 0);

            commandSession->receiveCommand(funcLANModLevel, QVariant::fromValue(lanModLevel), 0);
            commandSession->receiveCommand(funcSatelliteMode, QVariant::fromValue<bool>(false), 0);
            // Keep both operating sides active for the independent MAIN and SUB
            // VFO controllers. MAIN remains selected for the existing control,
            // memory, and Spectrum Scope paths after the targeted SUB readback.
            commandSession->receiveCommand(funcVFODualWatch, QVariant::fromValue<bool>(true), 0);
            requestSubVfoStateForCommand(commandSession);
            commandSession->receiveCommand(funcVFOBandMS, QVariant::fromValue<bool>(false), 0);
            commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
            commandSession->receiveCommand(funcScopeMainSub, QVariant::fromValue<bool>(false), 0);
            commandSession->receiveCommand(funcTimeOutTimer, QVariant::fromValue<uchar>(kHardwareTxTimeoutTimer), 0);
            qInfo(logRadio()).noquote()
                << "Configured IC-9700 transmit modulation source for LAN audio and MAIN/SUB dual-watch operation";
            qInfo(logRadio()).noquote() << "Setting hardware TX timeout timer to 3 minutes";

            const Funcs statusCommands[] = {
                funcTransceiverStatus,
                funcRfGain,
                funcRFPower,
                funcSquelch,
                funcNoiseReduction,
                funcNoiseBlanker,
                funcPreamp,
                funcAttenuator,
                funcSplitStatus,
                funcReadFreqOffset,
                funcToneSquelchType,
                funcRepeaterTone,
                funcRepeaterTSQL,
                funcRepeaterDTCS,
                funcRepeaterCSQL,
                funcToneFreq,
                funcTSQLFreq,
                funcDTCSCode,
                funcCompressor,
                funcCompressorLevel,
                funcXFCStatus,
                funcAutoNotch,
                funcManualNotch,
                funcAGCTimeConstant,
                funcTuningStep,
                funcRitStatus,
                funcRitFreq,
                funcMonitor,
                funcVox,
                funcIPPlus,
            };

            for (const Funcs command : statusCommands)
            {
                commandSession->receiveCommand(command, QVariant(), 0);
            }
        });
}

void RadioBackend::updateReadyState()
{
    const bool mainControlReady = m_initialMainFrequencyReceived && m_initialMainModeReceived;
    // Do not gate radio control readiness on Spectrum Scope packets. The IC-9700
    // can accept CI-V commands and memory reads before the UDP scope stream
    // starts, and blocking here leaves the GUI stuck in "syncing" with no way
    // for MemoryController to begin its required startup sync. Backout point:
    // if scope data must become a hard startup gate again, restore the previous
    // `(m_scopeDataReceived || m_scopeSyncDegraded)` condition and revisit the
    // watchdog path below.
    const bool ready = mainControlReady;
    if (m_radioReady == ready)
    {
        return;
    }

    m_radioReady = ready;
    qInfo(logRadio()).noquote().nospace()
        << "Radio backend readiness changed ready=" << ready
        << " mainFrequencyReceived=" << m_initialMainFrequencyReceived
        << " mainModeReceived=" << m_initialMainModeReceived << " scopeReceived=" << m_scopeDataReceived
        << " scopeDegraded=" << m_scopeSyncDegraded;
    emit readyChanged(ready);
    if (ready)
    {
        m_syncReconnectAttempts = 0;
        m_syncReconnectPending = false;
        if (!m_scopeDataReceived)
        {
            setScopeSyncDegraded(true);
        }
        if (m_syncWatchdogTimer)
        {
            m_syncWatchdogTimer->stop();
        }
        if (m_initialStateRetryTimer)
        {
            m_initialStateRetryTimer->stop();
        }
        m_vfoStatePollPhase = 0;
        m_vfoStatePollSubNext = false;
        if (m_vfoStatePollTimer)
        {
            m_vfoStatePollTimer->start();
        }
        // Let MemoryController establish the startup memory poll before the
        // broader post-ready status snapshot queues dozens of CI-V reads. This
        // is intentionally a small, documented backout point: if future packet
        // captures show the IC-9700 handles the full burst without delaying
        // memory replies, this can return to a direct requestPostReadyRadioState()
        // call.
        QTimer::singleShot(500, this,
                           [this]()
                           {
                               if (m_radioReady && m_commander)
                               {
                                   requestPostReadyRadioState();
                               }
                           });
        emit connectionStageChanged(ConnectionStage::SyncingRadioState,
                                    m_scopeSyncDegraded
                                        ? QStringLiteral("Radio state synced; syncing memories and spectrum scope")
                                        : QStringLiteral("Radio state synced; syncing memories"));
        invokeOnCurrentCommander([](Commander* c) { c->enableAudio(); });
    }
}

void RadioBackend::setScopeSyncDegraded(bool degraded)
{
    if (m_scopeSyncDegraded == degraded)
    {
        return;
    }

    m_scopeSyncDegraded = degraded;
    emit scopeSyncDegradedChanged(degraded);
}

void RadioBackend::restartAfterSyncTimeout()
{
    if (!m_commander || m_radioReady)
    {
        return;
    }
    if (m_initialMainFrequencyReceived && m_initialMainModeReceived && !m_scopeDataReceived)
    {
        // The normal readiness path no longer waits on Spectrum Scope data, so
        // reaching this branch means a queued readiness update was delayed. Keep
        // it as a defensive fallback instead of reconnecting a usable radio.
        setScopeSyncDegraded(true);
        qWarning(logRadio()).noquote() << "Radio control sync completed but Spectrum Scope data did not arrive within"
                                       << kSyncWatchdogTimeoutMs << "ms; enabling controls while scope retry continues";
        updateReadyState();
        return;
    }
    if (m_connectionHost.isEmpty() || m_connectionPort == 0)
    {
        qWarning(logRadio()).noquote() << "Radio sync timed out, but no saved connection target is available";
        return;
    }

    const QString host = m_connectionHost;
    const quint16 port = m_connectionPort;
    const QString user = m_connectionUser;
    const QString pass = m_connectionPass;

    qWarning(logRadio()).noquote().nospace()
        << "Radio sync did not complete timeoutMs=" << kSyncWatchdogTimeoutMs
        << " scopeReceived=" << m_scopeDataReceived << " mainFrequencyReceived=" << m_initialMainFrequencyReceived
        << " mainModeReceived=" << m_initialMainModeReceived;
    if (m_syncReconnectAttempts >= kMaxSyncReconnectAttempts)
    {
        // A repeated failure here means LAN control authenticated but the CI-V
        // stream never produced frequency/mode readback. Reconnecting forever
        // hammers the IC-9700 LAN server and leaves the GUI stuck in Syncing,
        // so stop cleanly and let the operator choose when to retry or reboot.
        qWarning(logRadio()).noquote() << "Radio sync retry limit reached; stopping automatic reconnect loop";
        const QString message = QStringLiteral("Radio sync timed out; reconnect or restart the radio");
        shutdownConnection(true, false);
        emit connectionStageChanged(ConnectionStage::Failed, message);
        return;
    }

    ++m_syncReconnectAttempts;
    // Use the normal close path so the IC-9700 receives stream close/token
    // cleanup before the reconnect attempt. Publish Reconnecting after cleanup
    // so an internal Disconnected transition cannot overwrite the useful state.
    shutdownConnection(true, false);
    emit connectionStageChanged(ConnectionStage::Reconnecting, QStringLiteral("Radio sync timed out; reconnecting"));

    m_syncReconnectPending = true;
    QTimer::singleShot(kSyncReconnectDelayMs, this,
                       [this, host, port, user, pass]()
                       {
                           if (m_connectionHost != host || m_connectionPort != port)
                           {
                               m_syncReconnectPending = false;
                               return;
                           }
                           connectToRadio(host, port, user, pass);
                       });
}

bool RadioBackend::isCurrentSession(quint64 session, const Commander* commandSession) const
{
    Q_UNUSED(commandSession);
    // Session ID alone identifies the connection epoch. m_commander is not
    // read here because it is only safe to access on the main thread.
    return session == m_sessionId.load(std::memory_order_acquire);
}

void RadioBackend::invokeOnCurrentCommander(const std::function<void(Commander*)>& command)
{
    if (!m_commander)
    {
        return;
    }

    Commander* commandSession = m_commander;
    const auto sessionActive = m_sessionActive;
    if (!sessionActive)
    {
        return;
    }
    QMetaObject::invokeMethod(
        commandSession,
        [sessionActive, commandSession, command]()
        {
            if (!sessionActive->load(std::memory_order_acquire))
            {
                return;
            }
            command(commandSession);
        },
        Qt::QueuedConnection);
}

void RadioBackend::handleReportedFrequency(quint64 hz)
{
    const availableBands band = sdr9700::radioBandForFrequency(hz);
    const int bandKey = band != bandUnknown ? static_cast<int>(band) : -1;
    if (bandKey == m_currentBandKey)
    {
        return;
    }

    const bool hadKnownBand = m_currentBandKey != -1;
    m_currentBandKey = bandKey;

    if (!hadKnownBand || bandKey == -1 || !m_radioReady || !m_bandStateRefreshTimer)
    {
        return;
    }

    qInfo(logRadio()).noquote() << "Detected IC-9700 band change; scheduling radio state refresh";
    m_bandStateRefreshTimer->start();
}

void RadioBackend::sendLanModLevel(int level)
{
    if (!m_commander)
    {
        return;
    }

    const ushort val = static_cast<ushort>(qBound(0, level, 255));

    if (QThread::currentThread() == m_commander->thread())
    {
        m_commander->receiveCommand(funcLANModLevel, QVariant::fromValue(val), 0);
        return;
    }

    invokeOnCurrentCommander([val](Commander* commandSession)
                             { commandSession->receiveCommand(funcLANModLevel, QVariant::fromValue(val), 0); });
}

void RadioBackend::setLanModLevel(int level)
{
    m_lanModLevel = qBound(0, level, 255);
    sendLanModLevel(m_lanModLevel);
}

void RadioBackend::onLanReady()
{
    const quint64 session = m_sessionId.load(std::memory_order_relaxed);
    Commander* commandSession = m_commander;
    if (!isCurrentSession(session, commandSession))
    {
        return;
    }

    m_radioReady = false;
    m_scopeDataReceived = false;
    setScopeSyncDegraded(false);
    resetScopeController();
    m_initialFrequencyReceived = false;
    m_initialModeReceived = false;
    m_initialMainFrequencyReceived = false;
    m_initialMainModeReceived = false;
    m_initialStateRequested = false;
    m_txMeterPollTick = 0;
    emit readyChanged(false);

    invokeOnCurrentCommander(
        [](Commander* commandSession)
        {
            commandSession->setRadioID(0xA2);

            // Scope output is persistent on the IC-9700. If the previous
            // session died while scope streaming was enabled, the radio can
            // flood the fresh CI-V socket before basic 03/04 VFO readback
            // completes. Quiet scope data first; onLanReady() enables it again
            // only after frequency and mode have been confirmed.
            //
            // CI-V has no non-mutating query for whether the radio is in VFO
            // or memory mode, nor for the selected memory channel. Establish a
            // deterministic startup state by explicitly selecting MAIN VFO
            // mode before reading its frequency and mode.
            commandSession->receiveCommandNoReadback(funcScopeOnOff, QVariant::fromValue<bool>(false), 0);
            commandSession->receiveCommandNoReadback(funcScopeDataOutput, QVariant::fromValue<bool>(false), 0);
            commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
            commandSession->receiveCommand(funcVFOModeSelect, QVariant(), 0);
            commandSession->readCurrentFrequencyAndMode();
        });

    if (m_initialStateRetryTimer)
    {
        m_initialStateRetryTimer->stop();
        m_initialStateRetryTimer->deleteLater();
    }
    m_initialStateRetryTimer = new QTimer(this);
    m_initialStateRetryTimer->setInterval(1000);
    connect(m_initialStateRetryTimer, &QTimer::timeout, this,
            [this, session, commandSession]()
            {
                if (!isCurrentSession(session, commandSession) || m_radioReady)
                {
                    if (m_initialStateRetryTimer)
                    {
                        m_initialStateRetryTimer->stop();
                    }
                    return;
                }
                requestInitialRadioState();
            });

    QTimer::singleShot(250, this,
                       [this, session, commandSession]()
                       {
                           if (!isCurrentSession(session, commandSession))
                           {
                               return;
                           }
                           requestInitialRadioState();
                           if (m_initialStateRetryTimer && !m_radioReady)
                           {
                               m_initialStateRetryTimer->start();
                           }
                       });

    emit connected();
    emit connectionStageChanged(ConnectionStage::SyncingRadioState, QStringLiteral("Connected; syncing radio state"));
    if (m_syncWatchdogTimer)
    {
        m_syncWatchdogTimer->start();
    }

    // Retry sending scope enable commands every 2 seconds until CIV is
    // established and scope data actually starts flowing.  The CIV stream
    // opens asynchronously after the control handshake; a fixed one-shot
    // delay races against that timing.  We stop as soon as scope data
    // arrives (m_scopeDataReceived set by ScopeController).
    if (m_scopeRetryTimer)
    {
        m_scopeRetryTimer->stop();
        m_scopeRetryTimer->deleteLater();
    }
    m_scopeRetryTimer = new QTimer(this);
    m_scopeRetryTimer->setInterval(1000);

    auto sendScopeEnable = [this, session, commandSession]()
    {
        if (!isCurrentSession(session, commandSession))
        {
            return;
        }
        if (!m_initialMainFrequencyReceived || !m_initialMainModeReceived)
        {
            return;
        }
        const ushort lanModLevel = static_cast<ushort>(m_lanModLevel);
        invokeOnCurrentCommander(
            [lanModLevel](Commander* commandSession)
            {
                commandSession->receiveCommand(funcScopeOnOff, QVariant::fromValue<bool>(true), 0);
                commandSession->receiveCommand(funcScopeDataOutput, QVariant::fromValue<bool>(true), 0);
                commandSession->receiveCommand(funcScopeMainSub, QVariant::fromValue<bool>(false), 0);
                centerSpanData span;
                span.freq = 500000;
                span.name = QStringLiteral("500 kHz");
                commandSession->receiveCommand(funcScopeSpan, QVariant::fromValue<centerSpanData>(span), 0);
                commandSession->receiveCommand(funcLANModLevel, QVariant::fromValue(lanModLevel), 0);
            });
    };

    connect(m_scopeRetryTimer, &QTimer::timeout, this,
            [this, session, commandSession, sendScopeEnable]()
            {
                if (!isCurrentSession(session, commandSession) || m_scopeDataReceived)
                {
                    m_scopeRetryTimer->stop();
                    return;
                }
                sendScopeEnable();
            });

    // First attempt shortly after the LAN CI-V stream opens, then retry until
    // scope data arrives. The send helper intentionally waits for current VFO
    // frequency/mode first so readiness cannot be starved by scope streaming.
    QTimer::singleShot(250, this,
                       [this, session, commandSession, sendScopeEnable]()
                       {
                           if (!isCurrentSession(session, commandSession))
                           {
                               return;
                           }
                           if (m_scopeRetryTimer)
                           {
                               m_scopeRetryTimer->start();
                           }
                           if (m_scopeDataReceived)
                           {
                               return;
                           }
                           sendScopeEnable();
                       });

    // Poll front-panel meters at 10 Hz; the IC-9700 only sends meter data on request.
    if (m_smeterPollTimer)
    {
        m_smeterPollTimer->stop();
        m_smeterPollTimer->deleteLater();
    }
    m_smeterPollTimer = new QTimer(this);
    m_smeterPollTimer->setInterval(100);
    m_smeterPollTimer->setTimerType(Qt::PreciseTimer);
    connect(m_smeterPollTimer, &QTimer::timeout, this,
            [this, session, commandSession]()
            {
                if (!isCurrentSession(session, commandSession) || !m_radioReady)
                {
                    return;
                }
                if (m_pttState.transmitMetersActive())
                {
                    const int pollTick = m_txMeterPollTick++;
                    invokeOnCurrentCommander(
                        [pollTick](Commander* commandSession)
                        {
                            commandSession->receiveCommand(funcSWRMeter, QVariant(), kMainReceiver);
                            commandSession->receiveCommand(funcPowerMeter, QVariant(), kMainReceiver);
                            commandSession->receiveCommand(funcALCMeter, QVariant(), kMainReceiver);
                            if (pollTick % 2 == 0)
                            {
                                commandSession->receiveCommand(funcCompMeter, QVariant(), kMainReceiver);
                            }
                            if (pollTick % 5 == 0)
                            {
                                commandSession->receiveCommand(funcVdMeter, QVariant(), kMainReceiver);
                                commandSession->receiveCommand(funcIdMeter, QVariant(), kMainReceiver);
                            }
                        });
                    return;
                }

                m_txMeterPollTick = 0;
                if (m_smeterPollPending)
                {
                    if (++m_smeterPollPendingTicks < 3)
                    {
                        return;
                    }

                    const Vfo restoreVfo = m_smeterRestoreVfo;
                    invokeOnCurrentCommander(
                        [restoreVfo](Commander* commandSession)
                        {
                            commandSession->discardPendingReplies(funcSMeter);
                            commandSession->receiveCommand(
                                funcSelectVFO, QVariant::fromValue<vfo_t>(restoreVfo == Vfo::Sub ? vfoSub : vfoMain),
                                0);
                        });
                    m_smeterPollPending = false;
                    m_smeterPollSettlingSample = false;
                    m_smeterPollPendingTicks = 0;
                    qWarning(logRadio()).noquote() << "S-meter poll timed out; receiver routing was reset";
                    return;
                }

                const bool pollSub = m_dualWatchEnabled && m_smeterPollSubNext;
                m_smeterPollSubNext = m_dualWatchEnabled && !m_smeterPollSubNext;
                const Vfo restoreVfo = m_activeVfo;
                m_smeterPollPending = true;
                m_smeterPollPendingReceiver = pollSub ? kSubReceiver : kMainReceiver;
                m_smeterPollSettlingSample = (pollSub ? Vfo::Sub : Vfo::Main) != restoreVfo;
                m_smeterPollPendingTicks = 0;
                m_smeterRestoreVfo = restoreVfo;
                invokeOnCurrentCommander(
                    [pollSub, restoreVfo](Commander* commandSession)
                    {
                        const Vfo targetVfo = pollSub ? Vfo::Sub : Vfo::Main;
                        const uchar receiver = pollSub ? kSubReceiver : kMainReceiver;
                        if (targetVfo != restoreVfo)
                        {
                            commandSession->receiveCommand(funcSelectVFO,
                                                           QVariant::fromValue<vfo_t>(pollSub ? vfoSub : vfoMain), 0);
                        }
                        commandSession->receiveCommand(funcSMeter, QVariant(), receiver);
                    });
            });
    m_smeterPollTimer->start();
}

void RadioBackend::onPortError(errorType err)
{
    QString message;
    switch (err.code)
    {
    case ErrorCode::AuthFailure:
        message = QStringLiteral("Login denied; check the radio username and password");
        break;
    case ErrorCode::ConnectionFailed:
        message = err.message.trimmed();
        if (message.isEmpty())
        {
            message = QStringLiteral("Radio connection failed");
        }
        break;
    case ErrorCode::Disconnected:
        message = QStringLiteral("Radio disconnected");
        break;
    default:
        message = err.message.trimmed();
        if (message.isEmpty())
        {
            message = QStringLiteral("Radio connection failed");
        }
        break;
    }

    // Tear down the failed transport without emitting a generic disconnect
    // toast. The typed error below is the authoritative explanation and also
    // drives MainWindow's reconnect policy.
    shutdownConnection(false, false);
    m_connectionHost.clear();
    m_connectionPort = 0;
    m_connectionUser.clear();
    m_connectionPass.clear();
    emit connectionStageChanged(
        err.code == ErrorCode::Disconnected ? ConnectionStage::Disconnected : ConnectionStage::Failed, message);
    emit errorOccurred(err.code, message);
}

void RadioBackend::onNetworkStatus(networkStatus status)
{
    const int ms = static_cast<int>(status.networkLatency);
    if (ms > 0)
    {
        emit networkQualityChanged(ms);
    }
    if (status.userVisibleMessage)
    {
        const QString message = status.message.trimmed();
        if (!message.isEmpty() && message != m_lastUserVisibleNetworkMessage)
        {
            m_lastUserVisibleNetworkMessage = message;
            if (status.connectionStage != ConnectionStage::Unchanged)
            {
                emit connectionStageChanged(status.connectionStage, message);
            }
            else
            {
                emit statusMessage(message, status.messageSeverity);
            }
        }
    }
}

void RadioBackend::onHaveAudioData(const audioPacket& pkt)
{
    // IC-9700 delivers LPCM16 audio; sampleRate comes from rxSetup.
    emit audioDataReady(pkt.data, static_cast<int>(m_rxSampleRate), m_rxChannelCount);
}
