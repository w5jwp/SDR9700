#include "RadioBackend.h"

#include "Commander.h"
#include "CachingQueue.h"
#include "RadioRouter.h"
#include "ScopeController.h"
#include "Types.h"
#include "AppSettings.h"
#include "LogCategories.h"
#include "RadioCapabilities.h"
#include "RadioIdentities.h"

#include <QMediaDevices>
#include <QSemaphore>
#include <QThread>
#include <QTimer>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <optional>

namespace
{
constexpr int kSyncWatchdogTimeoutMs = 10000;
constexpr int kSyncReconnectDelayMs = 500;
constexpr int kPttReleaseTailMs = 150;
constexpr int kMaxTransmitDurationMs = 180000;
constexpr int kHighSwrConsecutiveReadings = 3;
constexpr uchar kHardwareTxTimeoutTimer = 1; // 3 minutes, the IC-9700's shortest non-off value.
constexpr uchar kMainReceiver = 0;
constexpr quint32 kTxAudioSampleRate = 16000;
constexpr double kHighSwrCutoff = 3.0;

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

// Standard DTMF dual-tone frequencies (row × column).
// Row:    697, 770, 852, 941 Hz
// Column: 1209, 1336, 1477, 1633 Hz
struct DtmfEntry
{
    char digit;
    double f1; // row
    double f2; // column
};

constexpr DtmfEntry kDtmfTable[] = {
    {'0', 941.0, 1336.0}, {'1', 697.0, 1209.0}, {'2', 697.0, 1336.0}, {'3', 697.0, 1477.0},
    {'4', 770.0, 1209.0}, {'5', 770.0, 1336.0}, {'6', 770.0, 1477.0}, {'7', 852.0, 1209.0},
    {'8', 852.0, 1336.0}, {'9', 852.0, 1477.0}, {'*', 941.0, 1209.0}, {'#', 941.0, 1477.0},
    {'A', 697.0, 1633.0}, {'B', 770.0, 1633.0}, {'C', 852.0, 1633.0}, {'D', 941.0, 1633.0},
};

// Generates interleaved 16-bit signed little-endian mono PCM for a DTMF digit sequence.
// Each digit is 100 ms of dual-tone followed by 100 ms of silence (200 ms total).
// The buffer uses the same LPCM16 mono sample rate requested for TX audio.
QByteArray generateDtmfPcm(const QString& digits)
{
    constexpr int kToneMs = 200;
    constexpr int kGapMs = 200;
    constexpr double kAmplitude = 0.45; // each sine; combined peak ≤ 0.9 of full scale
    constexpr double kPi = 3.14159265358979323846;

    const int toneSamples = kTxAudioSampleRate * kToneMs / 1000;
    const int gapSamples = kTxAudioSampleRate * kGapMs / 1000;

    QByteArray pcm;
    pcm.reserve(digits.size() * (toneSamples + gapSamples) * int(sizeof(qint16)));

    for (QChar qch : digits)
    {
        const char ch = qch.toUpper().toLatin1();
        const auto entryIt = std::find_if(std::begin(kDtmfTable), std::end(kDtmfTable),
                                          [ch](const auto& entry) { return entry.digit == ch; });
        if (entryIt == std::end(kDtmfTable))
        {
            continue;
        }
        const double f1 = entryIt->f1;
        const double f2 = entryIt->f2;

        for (int i = 0; i < toneSamples; ++i)
        {
            const double t = double(i) / kTxAudioSampleRate;
            const double v = kAmplitude * (std::sin(2.0 * kPi * f1 * t) + std::sin(2.0 * kPi * f2 * t));
            const qint16 s = qint16(std::clamp(v, -1.0, 1.0) * 32767.0);
            pcm.append(reinterpret_cast<const char*>(&s), sizeof(s));
        }

        pcm.append(gapSamples * int(sizeof(qint16)), '\0');
    }

    return pcm;
}

} // namespace

RadioBackend::RadioBackend(QObject* parent)
    : IRadioBackend(parent), m_workerThread(new QThread(this)), m_radioDataThread(new QThread(this))
{
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
                m_scopeDataReceived = true;
                updateReadyState();
            });
    connect(m_scopeController, &ScopeController::spectrumDataReady, this, &IRadioBackend::spectrumDataReady);

    connect(m_radioRouter, &RadioRouter::radioValueUpdated, this, &IRadioBackend::radioValueUpdated);
    connect(m_radioRouter, &RadioRouter::frequencyReported, this,
            [this](quint64 hz)
            {
                m_initialMainFrequencyReceived = true;
                m_initialFrequencyReceived = true;
                handleReportedFrequency(hz);
                emit frequencyChanged(hz);
                updateReadyState();
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
    connect(m_radioRouter, &RadioRouter::vfoBandMSRequested, this,
            [this]()
            {
                invokeOnCurrentCommander(
                    [](Commander* commandSession)
                    {
                        commandSession->receiveCommand(funcVFOBandMS, QVariant::fromValue<bool>(false), 0);
                        commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
                    });
            });
    connect(m_radioRouter, &RadioRouter::repeaterOffsetChanged, this, &IRadioBackend::repeaterOffsetChanged);
    connect(m_radioRouter, &RadioRouter::toneAccessModeChanged, this, &IRadioBackend::toneAccessModeChanged);
    connect(m_radioRouter, &RadioRouter::toneFrequencyChanged, this, &IRadioBackend::toneFrequencyChanged);
    connect(m_radioRouter, &RadioRouter::dtcsCodeChanged, this, &IRadioBackend::dtcsCodeChanged);
    connect(m_radioRouter, &RadioRouter::smeterChanged, this, &IRadioBackend::smeterChanged);
    connect(m_radioRouter, &RadioRouter::nrChanged, this, &IRadioBackend::nrChanged);
    connect(m_radioRouter, &RadioRouter::nbChanged, this, &IRadioBackend::nbChanged);
    connect(m_radioRouter, &RadioRouter::preampChanged, this, &IRadioBackend::preampChanged);
    connect(m_radioRouter, &RadioRouter::preampLevelChanged, this, &IRadioBackend::preampLevelChanged);
    connect(m_radioRouter, &RadioRouter::attenuatorChanged, this, &IRadioBackend::attenuatorChanged);
    connect(m_radioRouter, &RadioRouter::autoNotchChanged, this, &IRadioBackend::autoNotchChanged);
    connect(m_radioRouter, &RadioRouter::manualNotchChanged, this, &IRadioBackend::manualNotchChanged);
    connect(m_radioRouter, &RadioRouter::compressorChanged, this, &IRadioBackend::compressorChanged);
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
    connect(m_radioRouter, &RadioRouter::duplexModeChanged, this, &IRadioBackend::duplexModeChanged);
    connect(m_radioRouter, &RadioRouter::dataOffModChanged, this,
            [this](const radioInput& input)
            {
                if (!m_originalDataOffMod.has_value())
                {
                    m_originalDataOffMod = input.reg;
                    qInfo(logRadio()) << "Captured original DATA OFF MOD source register" << *m_originalDataOffMod;
                }
            });
    connect(m_radioRouter, &RadioRouter::data1ModChanged, this,
            [this](const radioInput& input)
            {
                if (!m_originalData1Mod.has_value())
                {
                    m_originalData1Mod = input.reg;
                    qInfo(logRadio()) << "Captured original DATA1 MOD source register" << *m_originalData1Mod;
                }
            });
    connect(m_radioRouter, &RadioRouter::pttChanged, this,
            [this](bool on)
            {
                if (on && !m_pttActive && m_pttStaleOnGuardTimer && m_pttStaleOnGuardTimer->isActive())
                {
                    qDebug(logRadio()) << "Ignoring stale PTT-on status after PTT-off request";
                    return;
                }
                if (!on && m_pttStaleOnGuardTimer)
                {
                    m_pttStaleOnGuardTimer->stop();
                }
                if (!on && m_pttReleaseDelayTimer)
                {
                    m_pttReleaseDelayTimer->stop();
                }
                if (on)
                {
                    armTransmitSafety();
                }
                else
                {
                    disarmTransmitSafety();
                }
                m_pttActive = on;
                emit pttChanged(on);
            });
    connect(m_radioRouter, &RadioRouter::scopeDataReady, m_scopeController, &ScopeController::acceptScopeData);

    m_pttStaleOnGuardTimer = new QTimer(this);
    m_pttStaleOnGuardTimer->setSingleShot(true);

    m_pttReleaseDelayTimer = new QTimer(this);
    m_pttReleaseDelayTimer->setSingleShot(true);
    m_pttReleaseDelayTimer->setInterval(kPttReleaseTailMs);
    connect(m_pttReleaseDelayTimer, &QTimer::timeout, this, &RadioBackend::sendPttOffNow);

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
                emit statusMessage("Refreshing band state...");
                requestInitialRadioState();
            });

    m_syncWatchdogTimer = new QTimer(this);
    m_syncWatchdogTimer->setSingleShot(true);
    m_syncWatchdogTimer->setInterval(kSyncWatchdogTimeoutMs);
    connect(m_syncWatchdogTimer, &QTimer::timeout, this, &RadioBackend::restartAfterSyncTimeout);
}

RadioBackend::~RadioBackend()
{
    shutdownConnection();
    if (m_scopeController && m_radioDataThread && m_radioDataThread->isRunning())
    {
        QMetaObject::invokeMethod(m_scopeController, &ScopeController::reset, Qt::QueuedConnection);
    }
    if (m_radioDataThread)
    {
        m_radioDataThread->quit();
        if (!m_radioDataThread->wait(3000))
        {
            qWarning(logRadio()) << "[SHUTDOWN] radio-data did not stop within 3000 ms; requesting interruption";
            m_radioDataThread->requestInterruption();
            m_radioDataThread->quit();
            if (!m_radioDataThread->wait(1000))
            {
                qCritical(logRadio()) << "[SHUTDOWN] radio-data did not stop after bounded shutdown; leaving thread "
                                         "detached to avoid blocking the UI";
                m_radioDataThread->setParent(nullptr);
                connect(m_radioDataThread, &QThread::finished, m_radioDataThread, &QObject::deleteLater);
                m_radioDataThread = nullptr;
            }
        }
    }
    m_workerThread->quit();
    if (!m_workerThread->wait(3000))
    {
        qWarning(logRadio()) << "[SHUTDOWN] radio-worker did not stop within 3000 ms; requesting interruption";
        m_workerThread->requestInterruption();
        m_workerThread->quit();
        if (!m_workerThread->wait(1000))
        {
            qCritical(logRadio()) << "[SHUTDOWN] radio-worker did not stop after bounded shutdown; leaving thread "
                                     "detached to avoid blocking the UI";
            m_workerThread->setParent(nullptr);
            connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);
            m_workerThread = nullptr;
        }
    }
}

void RadioBackend::connectToRadio(const QString& host, quint16 port, const QString& user, const QString& pass)
{
    // Must be called from the main thread only; m_commander is not guarded by a mutex.
    if (m_commander)
    {
        shutdownConnection();
    }

    m_connectionHost = host;
    m_connectionPort = port;
    m_connectionUser = user;
    m_connectionPass = pass;
    m_originalDataOffMod.reset();
    m_originalData1Mod.reset();
    m_lastUserVisibleNetworkMessage.clear();

    const quint64 session = ++m_sessionId;
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
    // call; the connection is torn down automatically when the queue is destroyed
    // with the Commander on disconnect.
    CachingQueue* q = CachingQueue::getInstance(m_commander);
    connect(
        q, &CachingQueue::sendValues, this,
        [this, session, commandSession](const QVector<CacheItem>& items)
        {
            if (!isCurrentSession(session, commandSession))
            {
                return;
            }
            if (m_radioRouter)
            {
                QMetaObject::invokeMethod(
                    m_radioRouter, [router = m_radioRouter, items]() { router->routeBatch(items); },
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
        [this, session, commandSession, udpSettings, rxSetup, txSetup]()
        {
            if (!isCurrentSession(session, commandSession))
            {
                return;
            }
            m_commander->commSetup(kIc9700CivAddress, udpSettings, rxSetup, txSetup, QString(), kUnusedTcpPort);
            m_commander->process();
        },
        Qt::QueuedConnection);
}

void RadioBackend::disconnectFromRadio()
{
    if (m_syncWatchdogTimer)
    {
        m_syncWatchdogTimer->stop();
    }
    shutdownConnection();
    m_connectionHost.clear();
    m_connectionPort = 0;
    m_connectionUser.clear();
    m_connectionPass.clear();
}

void RadioBackend::shutdownConnection()
{
    if (!m_commander)
    {
        return;
    }

    ++m_sessionId;
    if (m_pttStaleOnGuardTimer)
    {
        m_pttStaleOnGuardTimer->stop();
    }
    if (m_pttReleaseDelayTimer)
    {
        m_pttReleaseDelayTimer->stop();
    }
    disarmTransmitSafety();
    if (m_syncWatchdogTimer)
    {
        m_syncWatchdogTimer->stop();
    }
    m_pttActive = false;

    if (m_smeterPollTimer)
    {
        m_smeterPollTimer->stop();
        m_smeterPollTimer->deleteLater();
        m_smeterPollTimer = nullptr;
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
        if (!queued || !closeDone->tryAcquire(1, 5000))
        {
            qWarning(logRadio()) << "[SHUTDOWN] closeComm() did not finish within 5000 ms; continuing disconnect";
        }
    }
    commandSession->deleteLater();
    m_commander = nullptr;
    m_radioReady = false;
    m_scopeDataReceived = false;
    resetScopeController();
    m_initialFrequencyReceived = false;
    m_initialModeReceived = false;
    m_initialMainFrequencyReceived = false;
    m_initialMainModeReceived = false;
    m_initialStateRequested = false;
    m_currentBandKey = -1;
    m_txMeterPollTick = 0;
    m_originalDataOffMod.reset();
    m_originalData1Mod.reset();
    m_lastUserVisibleNetworkMessage.clear();
    emit readyChanged(false);
    emit disconnected();
}

void RadioBackend::setFrequencyHz(quint64 hz)
{
    Frequency f;
    f.Hz = hz;
    f.MHzDouble = hz / 1e6;
    f.VFO = activeVFO;
    invokeOnCurrentCommander(
        [this, f](Commander* commandSession)
        {
            selectMainVfoForCommand(commandSession);
            commandSession->receiveCommandNoReadback(funcFreqSet, QVariant::fromValue(f), 0);
            commandSession->receiveCommand(funcFreqGet, QVariant(), 0);
        });
}

void RadioBackend::setMode(const QString& mode)
{
    if (!m_commander)
    {
        return;
    }

    ModeInfo mi;
    if (!populateModeInfo(mode, &mi))
    {
        qWarning(logRadio()) << "Ignoring unsupported mode selection:" << mode;
        return;
    }
    mi.filter = static_cast<quint8>(qBound(1, m_currentMainFilter, 3));

    invokeOnCurrentCommander(
        [this, mi](Commander* commandSession)
        {
            selectMainVfoForCommand(commandSession);
            commandSession->receiveCommandNoReadback(funcModeSet, QVariant::fromValue(mi), 0);
            commandSession->receiveCommand(funcModeGet, QVariant(), 0);
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
    int agc = 2; // MID
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
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcAGCTimeConstant, QVariant(agc), 0); });
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

void RadioBackend::setXfcEnabled(bool on)
{
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcXFCStatus, QVariant::fromValue<bool>(on), 0); });
}

void RadioBackend::setDuplexMode(duplexMode_t mode)
{
    invokeOnCurrentCommander(
        [=](Commander* commandSession)
        {
            commandSession->receiveCommand(funcSplitStatus, QVariant::fromValue(mode), 0);
            commandSession->receiveCommand(funcSplitStatus, QVariant(), 0);
        });
}

void RadioBackend::setRepeaterOffsetHz(quint64 hz)
{
    Frequency offset;
    offset.Hz = hz;
    offset.MHzDouble = hz / 1e6;
    offset.VFO = activeVFO;
    invokeOnCurrentCommander(
        [=](Commander* commandSession)
        {
            commandSession->receiveCommand(funcSendFreqOffset, QVariant::fromValue(offset), 0);
            commandSession->receiveCommand(funcReadFreqOffset, QVariant(), 0);
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
    invokeOnCurrentCommander(
        [=](Commander* commandSession)
        {
            commandSession->receiveCommand(funcToneSquelchType, QVariant::fromValue(access), 0);
            commandSession->receiveCommand(funcToneSquelchType, QVariant(), 0);
        });
}

void RadioBackend::setToneFrequency(ushort tone)
{
    if (!m_commander)
    {
        return;
    }

    ToneInfo info(tone);
    invokeOnCurrentCommander(
        [=](Commander* commandSession)
        {
            commandSession->receiveCommand(funcToneFreq, QVariant::fromValue(info), 0);
            commandSession->receiveCommand(funcTSQLFreq, QVariant::fromValue(info), 0);
            commandSession->receiveCommand(funcToneFreq, QVariant(), 0);
        });
}

void RadioBackend::setDtcsCode(ushort code)
{
    if (!m_commander)
    {
        return;
    }

    ToneInfo info(code);
    invokeOnCurrentCommander(
        [=](Commander* commandSession)
        {
            commandSession->receiveCommand(funcDTCSCode, QVariant::fromValue(info), 0);
            commandSession->receiveCommand(funcDTCSCode, QVariant(), 0);
        });
}

void RadioBackend::selectMainVfoForCommand(Commander* commandSession) const
{
    if (!commandSession)
    {
        return;
    }
    commandSession->receiveCommand(funcVFOBandMS, QVariant::fromValue<bool>(false), 0);
    commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
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

void RadioBackend::setPtt(bool on)
{
    if (!m_commander)
    {
        return;
    }

    if (on)
    {
        if (m_pttReleaseDelayTimer)
        {
            m_pttReleaseDelayTimer->stop();
        }
        if (m_pttActive)
        {
            return;
        }

        if (m_pttStaleOnGuardTimer)
        {
            m_pttStaleOnGuardTimer->stop();
        }
        m_pttActive = true;
        armTransmitSafety();
        invokeOnCurrentCommander(
            [](Commander* commandSession)
            {
                commandSession->receiveCommand(funcVFOBandMS, QVariant::fromValue<bool>(false), 0);
                commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
                commandSession->setPttActive(true);
                commandSession->receiveCommand(funcTransceiverStatus, QVariant::fromValue<bool>(true), 0);
            });
    }
    else
    {
        if (m_pttReleaseDelayTimer && m_pttReleaseDelayTimer->isActive())
        {
            return;
        }

        if (!m_pttActive)
        {
            sendPttOffNow();
            return;
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
    m_pttActive = false;
    disarmTransmitSafety();
    if (m_pttStaleOnGuardTimer)
    {
        m_pttStaleOnGuardTimer->start(1000);
    }
    emit pttChanged(false);
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
        m_highSwrReadingCount = 0;
        m_pttMaxDurationTimer->start();
    }
}

void RadioBackend::disarmTransmitSafety()
{
    if (m_pttMaxDurationTimer)
    {
        m_pttMaxDurationTimer->stop();
    }
    m_highSwrReadingCount = 0;
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
    if (!m_commander || !m_pttActive)
    {
        disarmTransmitSafety();
        return;
    }

    qWarning(logRadio()).noquote() << message;
    emit statusMessage(message);
    sendPttOffNow();
}

void RadioBackend::handleTransmitSwr(double swr)
{
    emit swrChanged(swr);

    if (!m_pttActive)
    {
        m_highSwrReadingCount = 0;
        return;
    }

    if (swr < kHighSwrCutoff)
    {
        m_highSwrReadingCount = 0;
        return;
    }

    ++m_highSwrReadingCount;
    if (m_highSwrReadingCount < kHighSwrConsecutiveReadings)
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

    const QByteArray pcm = generateDtmfPcm(digits);
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

void RadioBackend::requestInitialRadioState()
{
    if (!m_commander)
    {
        return;
    }

    const bool firstRequest = !m_initialStateRequested;
    if (firstRequest)
    {
        m_initialStateRequested = true;

        invokeOnCurrentCommander(
            [](Commander* commandSession)
            {
                commandSession->receiveCommand(funcVFODualWatch, QVariant::fromValue<bool>(false), 0);
                commandSession->receiveCommand(funcSatelliteMode, QVariant::fromValue<bool>(false), 0);
                commandSession->receiveCommand(funcVFOBandMS, QVariant::fromValue<bool>(false), 0);
                commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
                commandSession->receiveCommand(funcTimeOutTimer, QVariant::fromValue<uchar>(kHardwareTxTimeoutTimer),
                                               0);
                qInfo(logRadio()) << "Setting hardware TX timeout timer to 3 minutes";

                const Funcs statusCommands[] = {
                    funcTransceiverStatus,
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

    if (m_initialMainFrequencyReceived && m_initialMainModeReceived)
    {
        return;
    }

    const auto requestMainVfoState = [this]()
    {
        if (!m_commander)
        {
            return;
        }
        invokeOnCurrentCommander(
            [](Commander* commandSession)
            {
                commandSession->receiveCommand(funcSelectedFreq, QVariant(), 0);
                commandSession->receiveCommand(funcSelectedMode, QVariant(), 0);
                commandSession->receiveCommand(funcRfGain, QVariant(), 0);
                commandSession->receiveCommand(funcRFPower, QVariant(), 0);
                commandSession->receiveCommand(funcSquelch, QVariant(), 0);
            });
    };

    if (firstRequest)
    {
        QTimer::singleShot(300, this,
                           [this, requestMainVfoState]()
                           {
                               if (!m_commander || m_radioReady)
                               {
                                   return;
                               }
                               requestMainVfoState();
                           });
        return;
    }

    qDebug(logRadio()) << "Retrying initial MAIN VFO state; frequencyReceived=" << m_initialMainFrequencyReceived
                       << "modeReceived=" << m_initialMainModeReceived;
    requestMainVfoState();
}

void RadioBackend::updateReadyState()
{
    const bool ready = m_scopeDataReceived && m_initialMainFrequencyReceived && m_initialMainModeReceived;
    if (m_radioReady == ready)
    {
        return;
    }

    m_radioReady = ready;
    emit readyChanged(ready);
    if (ready)
    {
        if (m_syncWatchdogTimer)
        {
            m_syncWatchdogTimer->stop();
        }
        if (m_initialStateRetryTimer)
        {
            m_initialStateRetryTimer->stop();
        }
        emit statusMessage("Radio connection ready...");
        invokeOnCurrentCommander([](Commander* c) { c->enableAudio(); });
    }
}

void RadioBackend::restartAfterSyncTimeout()
{
    if (!m_commander || m_radioReady)
    {
        return;
    }
    if (m_connectionHost.isEmpty() || m_connectionPort == 0)
    {
        qWarning(logRadio()) << "Radio sync timed out, but no saved connection target is available";
        return;
    }

    const QString host = m_connectionHost;
    const quint16 port = m_connectionPort;
    const QString user = m_connectionUser;
    const QString pass = m_connectionPass;

    qWarning(logRadio()) << "Radio sync did not complete within" << kSyncWatchdogTimeoutMs
                         << "ms; scopeReceived=" << m_scopeDataReceived
                         << "mainFrequencyReceived=" << m_initialMainFrequencyReceived
                         << "mainModeReceived=" << m_initialMainModeReceived << "; disconnecting and reconnecting";
    emit statusMessage("Radio sync timed out... reconnecting");

    // Use the normal close path so the IC-9700 receives stream close/token
    // cleanup before the reconnect attempt.
    shutdownConnection();

    QTimer::singleShot(kSyncReconnectDelayMs, this,
                       [this, host, port, user, pass]()
                       {
                           if (m_connectionHost != host || m_connectionPort != port)
                           {
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

    const quint64 session = m_sessionId.load(std::memory_order_relaxed);
    Commander* commandSession = m_commander;
    QMetaObject::invokeMethod(
        commandSession,
        [this, session, commandSession, command]()
        {
            if (!isCurrentSession(session, commandSession))
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

    qInfo(logRadio()) << "Detected IC-9700 band change; scheduling radio state refresh";
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
    resetScopeController();
    m_initialFrequencyReceived = false;
    m_initialModeReceived = false;
    m_initialMainFrequencyReceived = false;
    m_initialMainModeReceived = false;
    m_initialStateRequested = false;
    m_txMeterPollTick = 0;
    emit readyChanged(false);

    const ushort lanModLevel = static_cast<ushort>(m_lanModLevel);
    invokeOnCurrentCommander(
        [lanModLevel](Commander* commandSession)
        {
            commandSession->setRadioID(0xA2);

            const radioInput lanInput(inputLAN, 5, QStringLiteral("LAN"));
            commandSession->receiveCommand(funcDATAOffMod, QVariant(), 0);
            commandSession->receiveCommand(funcDATA1Mod, QVariant(), 0);
            commandSession->receiveCommandNoReadback(funcDATAOffMod, QVariant::fromValue(lanInput), 0);
            commandSession->receiveCommandNoReadback(funcDATA1Mod, QVariant::fromValue(lanInput), 0);

            commandSession->receiveCommand(funcLANModLevel, QVariant::fromValue(lanModLevel), 0);
            commandSession->receiveCommand(funcVFODualWatch, QVariant::fromValue<bool>(false), 0);
            commandSession->receiveCommand(funcSatelliteMode, QVariant::fromValue<bool>(false), 0);
            commandSession->receiveCommand(funcVFOBandMS, QVariant::fromValue<bool>(false), 0);
            commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
            commandSession->receiveCommand(funcScopeMainSub, QVariant::fromValue<bool>(false), 0);
            qInfo(logRadio())
                << "Configured IC-9700 transmit modulation source for LAN audio and single MAIN VFO operation";
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
    emit statusMessage("Connected to radio... syncing radio state");
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

    // First attempt shortly after the LAN CI-V stream opens, then retry until scope data arrives.
    QTimer::singleShot(250, this,
                       [this, session, commandSession, sendScopeEnable]()
                       {
                           if (!isCurrentSession(session, commandSession))
                           {
                               return;
                           }
                           if (m_scopeDataReceived)
                           {
                               return;
                           }
                           sendScopeEnable();
                           if (m_scopeRetryTimer)
                           {
                               m_scopeRetryTimer->start();
                           }
                       });

    // Poll front-panel meters at 10 Hz; the IC-9700 only sends meter data on request.
    if (m_smeterPollTimer)
    {
        m_smeterPollTimer->stop();
        m_smeterPollTimer->deleteLater();
    }
    m_smeterPollTimer = new QTimer(this);
    m_smeterPollTimer->setInterval(100);
    connect(m_smeterPollTimer, &QTimer::timeout, this,
            [this, session, commandSession]()
            {
                if (!isCurrentSession(session, commandSession) || !m_radioReady)
                {
                    return;
                }
                if (m_pttActive)
                {
                    const uchar receiver = kMainReceiver;
                    const int pollTick = m_txMeterPollTick++;
                    invokeOnCurrentCommander(
                        [receiver, pollTick](Commander* commandSession)
                        {
                            commandSession->receiveCommand(funcSWRMeter, QVariant(), receiver);
                            commandSession->receiveCommand(funcPowerMeter, QVariant(), receiver);
                            commandSession->receiveCommand(funcALCMeter, QVariant(), receiver);
                            if (pollTick % 2 == 0)
                            {
                                commandSession->receiveCommand(funcCompMeter, QVariant(), receiver);
                            }
                            if (pollTick % 5 == 0)
                            {
                                commandSession->receiveCommand(funcVdMeter, QVariant(), receiver);
                                commandSession->receiveCommand(funcIdMeter, QVariant(), receiver);
                            }
                        });
                    return;
                }

                m_txMeterPollTick = 0;
                const uchar receiver = kMainReceiver;
                invokeOnCurrentCommander([receiver](Commander* commandSession)
                                         { commandSession->receiveCommand(funcSMeter, QVariant(), receiver); });
            });
    m_smeterPollTimer->start();
}

void RadioBackend::onPortError(errorType err)
{
    QString message;
    switch (err.code)
    {
    case ErrorCode::AuthFailure:
        message = QStringLiteral("Unable to connect to radio (authentication failed)");
        break;
    case ErrorCode::ConnectionFailed:
        message = QStringLiteral("Unable to connect to radio (unreachable)");
        break;
    case ErrorCode::Disconnected:
        message = QStringLiteral("Radio disconnected");
        break;
    default:
        message = err.message.trimmed();
        if (message.isEmpty())
        {
            message = QStringLiteral("Unable to connect to radio");
        }
        break;
    }

    emit errorOccurred(message);
    disconnectFromRadio();
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
            emit statusMessage(message);
        }
    }
}

void RadioBackend::onHaveAudioData(const audioPacket& pkt)
{
    // IC-9700 delivers LPCM16 audio; sampleRate comes from rxSetup.
    emit audioDataReady(pkt.data, static_cast<int>(m_rxSampleRate));
}
