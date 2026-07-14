#include "RadioBackend.h"
#include "ScopeAdapter.h"

#include "Commander.h"
#include "CachingQueue.h"
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
#include <memory>

namespace
{
constexpr int kSyncWatchdogTimeoutMs = 10000;
constexpr int kSyncReconnectDelayMs = 500;
constexpr uchar kMainReceiver = 0;

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

int uiLevelFromRadioPercent(int percent)
{
    return qBound(0, qRound(qBound(0, percent, 100) * 255.0 / 100.0), 255);
}

ushort radioPercentFromUiLevel(int level)
{
    return static_cast<ushort>(qBound(0, qRound(qBound(0, level, 255) * 100.0 / 255.0), 100));
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
// The IC-9700 LAN TX path expects 48000 Hz LPCM16 mono.
QByteArray generateDtmfPcm(const QString& digits)
{
    constexpr quint32 kSampleRate = 48000;
    constexpr int kToneMs = 200;
    constexpr int kGapMs = 200;
    constexpr double kAmplitude = 0.45; // each sine; combined peak ≤ 0.9 of full scale
    constexpr double kPi = 3.14159265358979323846;

    const int toneSamples = kSampleRate * kToneMs / 1000; // 4800
    const int gapSamples = kSampleRate * kGapMs / 1000;   // 4800

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
            const double t = double(i) / kSampleRate;
            const double v = kAmplitude * (std::sin(2.0 * kPi * f1 * t) + std::sin(2.0 * kPi * f2 * t));
            const qint16 s = qint16(std::clamp(v, -1.0, 1.0) * 32767.0);
            pcm.append(reinterpret_cast<const char*>(&s), sizeof(s));
        }

        pcm.append(gapSamples * int(sizeof(qint16)), '\0');
    }

    return pcm;
}

} // namespace

RadioBackend::RadioBackend(QObject* parent) : IRadioBackend(parent), m_workerThread(new QThread(this))
{
    m_workerThread->setObjectName("radio-worker");
    m_workerThread->start();

    /*
        IC-9700 LAN TX audio startup sequence

        The radio produced one or two short wideband bursts at the start of PTT
        when SDR9700 only keyed TX and then sent LAN audio normally. The issue
        was reproducible across PC microphone devices, which ruled out the local
        audio input as the primary source.

        The quiet sequence has three required parts:
        1. Force the radio's TX modulation source to LAN on connect. The radio
           can otherwise use/mix a non-LAN source during the transition. Do not
           resend the modulation-source commands on every PTT press; changing
           that state during key-up can itself produce a later transient.
        2. Keep LAN modulation level at 0 while in RX and during the PTT-on
           transition. Testing showed the burst follows an abrupt LAN modulation
           gain restore, not the selected PC microphone device.
        3. Let UdpAudio feed zero PCM at PTT-on, ramp the user's LAN modulation
           level up while that audio gate is still closed, then fade live mic
           audio in after the radio-side LAN audio path has settled.

        Do not collapse this into a single immediate "PTT on + LAN gain" command
        sequence unless it is re-tested on real IC-9700 hardware while monitoring
        a second receiver/waterfall for startup noise.

        The control connection later sets UdpConnectionSettings::waterfallFormat to 2
        because IC-9700 scope packets can carry paired spectrum/waterfall
        payloads. UdpCivData splits that format before Commander parses scope
        data; this is independent from the TX audio startup sequence above.
    */
    m_txAudioEnableTimer = new QTimer(this);
    m_txAudioEnableTimer->setSingleShot(true);
    connect(m_txAudioEnableTimer, &QTimer::timeout, this,
            [this]()
            {
                // Step 2 of the TX startup sequence: after PTT is asserted and
                // UdpAudio has started sending zero PCM, begin restoring LAN
                // modulation gain while live mic audio is still gated.
                if (!m_commander || !m_pttActive)
                {
                    return;
                }

                startTxGainRamp(m_lanModLevel);
            });

    m_txGainRampTimer = new QTimer(this);
    m_txGainRampTimer->setInterval(40);
    connect(m_txGainRampTimer, &QTimer::timeout, this,
            [this]()
            {
                // Step 3 of the TX startup sequence: spread the LAN modulation
                // gain restore across several CI-V writes so the radio-side LAN
                // audio path settles before live microphone samples fade in.
                if (!m_commander || !m_pttActive)
                {
                    m_txGainRampTimer->stop();
                    return;
                }

                static constexpr int kRampSteps = 12;
                ++m_txGainRampStep;
                const int delta = m_txGainRampTarget - m_txGainRampStart;
                const int rampedValue = qBound(0, m_txGainRampStart + (delta * m_txGainRampStep) / kRampSteps, 255);
                sendLanModLevel(rampedValue);

                if (m_txGainRampStep >= kRampSteps)
                {
                    sendLanModLevel(m_txGainRampTarget);
                    m_txGainRampTimer->stop();
                }
            });

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

    const quint64 session = ++m_sessionId;
    m_lanModLevel = qBound(0, AppSettings::instance().value("LanModLevel", m_lanModLevel).toInt(), 255);

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
    // CachingQueue's sendValue signal.  Connect once per connectToRadio() call;
    // the connection is torn down automatically when the queue is destroyed with
    // the Commander on disconnect.
    CachingQueue* q = CachingQueue::getInstance(m_commander);
    connect(
        q, &CachingQueue::sendValue, this,
        [this, session, commandSession](const CacheItem& item)
        {
            if (!isCurrentSession(session, commandSession))
            {
                return;
            }
            switch (item.command)
            {
            case funcFreqGet:
            case funcFreqSet:
            case funcSelectedFreq:
            case funcUnselectedFreq:
            {
                if (item.command == funcUnselectedFreq || item.receiver != kMainReceiver)
                {
                    break;
                }

                emit radioValueUpdated(item.command, item.value, kMainReceiver);
                auto f = item.value.value<Frequency>();
                if (f.Hz > 0)
                {
                    m_initialMainFrequencyReceived = true;
                    m_initialFrequencyReceived = true;
                    handleReportedFrequency(f.Hz);
                    emit frequencyChanged(f.Hz);
                    updateReadyState();
                }
                break;
            }
            case funcModeGet:
            case funcModeSet:
            case funcSelectedMode:
            case funcUnselectedMode:
            {
                if (item.command == funcUnselectedMode || item.receiver != kMainReceiver)
                {
                    break;
                }

                emit radioValueUpdated(item.command, item.value, kMainReceiver);
                auto mi = item.value.value<ModeInfo>();
                m_initialMainModeReceived = true;
                m_initialModeReceived = true;
                emit modeChanged(modeInfoToString(mi));
                updateReadyState();
                break;
            }
            case funcSplitStatus:
                emit duplexModeChanged(item.value.value<duplexMode_t>());
                break;
            case funcVFOBandMS:
                emit radioValueUpdated(item.command, QVariant::fromValue<bool>(false), kMainReceiver);
                if (item.value.toBool())
                {
                    invokeOnCurrentCommander(
                        [](Commander* commandSession)
                        {
                            commandSession->receiveCommand(funcVFOBandMS, QVariant::fromValue<bool>(false), 0);
                            commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
                        });
                }
                break;
            case funcReadFreqOffset:
                emit repeaterOffsetChanged(item.value.value<Frequency>().Hz);
                break;
            case funcToneSquelchType:
                emit radioValueUpdated(item.command, item.value, item.receiver);
                emit toneAccessModeChanged(item.value.value<RptrAccessData>().accessMode);
                break;
            case funcToneFreq:
            case funcTSQLFreq:
                emit toneFrequencyChanged(item.value.value<ToneInfo>().tone);
                break;
            case funcDTCSCode:
                emit dtcsCodeChanged(item.value.value<ToneInfo>().tone);
                break;
            case funcSMeter:
            {
                // Display S0..S9 over the first 60% and S9..S9+40 over the remaining 40%.
                // The radio can report higher CI-V values, but SDR9700 pegs the displayed meter at +40.
                static constexpr double kS0Dbm = -147.0;
                static constexpr double kS9Dbm = -93.0;
                static constexpr double kS9p40Dbm = -53.0;

                const double dbm = item.value.toDouble();
                double fraction = 0.0;
                if (dbm <= kS0Dbm)
                {
                    fraction = 0.0;
                }
                else if (dbm <= kS9Dbm)
                {
                    fraction = 0.6 * (dbm - kS0Dbm) / (kS9Dbm - kS0Dbm);
                }
                else
                {
                    fraction = 0.6 + 0.4 * (dbm - kS9Dbm) / (kS9p40Dbm - kS9Dbm);
                }

                const int s = qBound(0, static_cast<int>(fraction * 255.0 + 0.5), 255);
                if (item.receiver == kMainReceiver)
                {
                    emit radioValueUpdated(item.command, QVariant(s), kMainReceiver);
                    emit smeterChanged(s);
                }
                break;
            }
            case funcNoiseReduction:
                emit nrChanged(item.value.toBool());
                break;
            case funcNoiseBlanker:
                emit nbChanged(item.value.toBool());
                break;
            case funcPreamp:
                emit preampChanged(item.value.toInt() != 0);
                break;
            case funcAttenuator:
                emit attenuatorChanged(item.value.toInt() != 0);
                break;
            case funcAutoNotch:
                emit autoNotchChanged(item.value.toBool());
                break;
            case funcCompressor:
                emit compressorChanged(item.value.toBool());
                break;
            case funcRitStatus:
                emit ritEnabledChanged(item.value.toBool());
                break;
            case funcRitFreq:
                emit ritOffsetChanged(item.value.value<short>());
                break;
            case funcAGCTimeConstant:
            {
                static const char* const kAgcModes[] = {"off", "fast", "mid", "slow"};
                const int idx = qBound(0, item.value.toInt(), 3);
                emit agcModeChanged(QString::fromLatin1(kAgcModes[idx]));
                break;
            }
            case funcRfGain:
                emit radioValueUpdated(item.command, item.value, item.receiver);
                emit rfGainChanged(qBound(0, item.value.toInt(), 255));
                break;
            case funcRFPower:
            {
                const int level = uiLevelFromRadioPercent(item.value.toInt());
                emit radioValueUpdated(item.command, QVariant(level), item.receiver);
                emit txPowerChanged(level);
                break;
            }
            case funcMicGain:
                emit micGainChanged(qBound(0, item.value.toInt(), 255));
                break;
            case funcSquelch:
            {
                const int level = uiLevelFromRadioPercent(item.value.toInt());
                emit radioValueUpdated(item.command, QVariant(level), item.receiver);
                emit squelchChanged(level > 0, level);
                break;
            }
            case funcSWRMeter:
                emit radioValueUpdated(item.command, item.value, item.receiver);
                emit swrChanged(item.value.toDouble());
                break;
            case funcTransceiverStatus:
            {
                const bool on = item.value.toBool();
                m_pttActive = on;
                emit pttChanged(on);
                break;
            }
            case funcScopeWaveData:
            {
                auto d = item.value.value<ScopeData>();
                qDebug(logRadio()) << "ScopeWaveData: valid=" << d.valid << "dataLen=" << d.data.size()
                                   << "start=" << d.startFreq << "end=" << d.endFreq;
                if (d.valid && !d.data.isEmpty())
                {
                    m_scopeDataReceived = true;
                    QVector<float> bins = ScopeAdapter::toDbm(d.data, m_scopeMinDbm, m_scopeMaxDbm);
                    emit spectrumDataReady(bins, d.startFreq, d.endFreq);
                    updateReadyState();
                }
                break;
            }
            default:
                break;
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
    // IC-9700 LAN bundles all 11 scope packets into one UDP datagram.
    // waterfallFormat=2 tells UdpCivData to split them before Commander parses.
    udpSettings.waterfallFormat = 2;

    // Load the saved audio device if no device was explicitly set via setRxAudioDevice().
    if (m_rxDevice.isNull())
    {
        const QByteArray savedId = AppSettings::instance().value("AudioOutputDeviceId").toString().toUtf8();
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
        const QByteArray savedId = AppSettings::instance().value("AudioInputDeviceId").toString().toUtf8();
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
    const int outputChannels = qBound(1, AppSettings::instance().value("AudioOutputChannels", 2).toInt(), 2);
    const int outputVolume = qBound(0, AppSettings::instance().value("VolumeLevel", 128).toInt(), 255);

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
    txSetup.sampleRate = 48000;
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
    m_txAudioEnableTimer->stop();
    m_txGainRampTimer->stop();
    if (m_syncWatchdogTimer)
    {
        m_syncWatchdogTimer->stop();
    }
    m_pttActive = false;
    m_txLanModApplied = 0;

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

    if (QThread::currentThread() == m_commander->thread())
    {
        m_commander->closeComm();
    }
    else
    {
        auto closeDone = std::make_shared<QSemaphore>();
        const bool queued = QMetaObject::invokeMethod(
            m_commander,
            [commandSession, closeDone]()
            {
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
    m_initialFrequencyReceived = false;
    m_initialModeReceived = false;
    m_initialMainFrequencyReceived = false;
    m_initialMainModeReceived = false;
    m_initialStateRequested = false;
    m_currentBandKey = -1;
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
    const uchar val = on ? 1 : 0;
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcPreamp, QVariant::fromValue<uchar>(val), 0); });
}

void RadioBackend::setAttenuatorEnabled(bool on)
{
    const uchar val = on ? 10 : 0;
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcAttenuator, QVariant::fromValue<uchar>(val), 0); });
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
    const ushort bounded = radioPercentFromUiLevel(level);
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcRFPower, QVariant::fromValue<ushort>(bounded), 0); });
}

void RadioBackend::setMicGain(int level)
{
    const ushort bounded = static_cast<ushort>(qBound(0, level, 255));
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcMicGain, QVariant::fromValue<ushort>(bounded), 0); });
}

void RadioBackend::setSquelch(bool on, int level)
{
    if (!m_commander)
    {
        return;
    }
    // On IC-9700, squelch level 0 = fully open, >0 = active.
    // Setting funcSquelch with 0 disables it; non-zero enables + sets level.
    const ushort squelchVal = on ? qMax<ushort>(1, radioPercentFromUiLevel(level)) : 0;
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
    else if (mode == "off")
    {
        // IC-9700 CI-V AGC: 0=off not supported; value 1 (FAST) is the
        // fastest time constant available and is used as the "off" analogue.
        agc = 1;
    }
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcAGCTimeConstant, QVariant(agc), 0); });
}

void RadioBackend::setAutoNotch(bool on)
{
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcAutoNotch, QVariant::fromValue<bool>(on), 0); });
}

void RadioBackend::setRitEnabled(bool on)
{
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcRitStatus, QVariant::fromValue<bool>(on), 0); });
}

void RadioBackend::setRitOffset(short hz)
{
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcRitFreq, QVariant::fromValue<short>(hz), 0); });
}

void RadioBackend::setCompressor(bool on)
{
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcCompressor, QVariant::fromValue<bool>(on), 0); });
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
    // The embedded IC-9700 definition uses span indices 0-7:
    // 0=2.5k, 1=5k, 2=10k, 3=25k, 4=50k, 5=100k, 6=250k, 7=500k
    uchar span = 7; // default 500 kHz
    if (hz <= 2500)
    {
        span = 0;
    }
    else if (hz <= 5000)
    {
        span = 1;
    }
    else if (hz <= 10000)
    {
        span = 2;
    }
    else if (hz <= 25000)
    {
        span = 3;
    }
    else if (hz <= 50000)
    {
        span = 4;
    }
    else if (hz <= 100000)
    {
        span = 5;
    }
    else if (hz <= 250000)
    {
        span = 6;
    }

    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcScopeSpan, QVariant::fromValue<uchar>(span), 0); });
}

void RadioBackend::setScopeMode(int mode)
{
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcScopeMode, QVariant(mode), 0); });
}

void RadioBackend::setPtt(bool on)
{
    if (!m_commander)
    {
        return;
    }

    if (on)
    {
        if (m_pttActive)
        {
            return;
        }

        m_txAudioEnableTimer->stop();
        m_txGainRampTimer->stop();
        m_txLanModApplied = 0;
        m_pttActive = true;
        invokeOnCurrentCommander(
            [](Commander* commandSession)
            {
                commandSession->receiveCommand(funcVFOBandMS, QVariant::fromValue<bool>(false), 0);
                commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
                commandSession->setPttActive(true);
                commandSession->receiveCommand(funcTransceiverStatus, QVariant::fromValue<bool>(true), 0);
            });
        // 220 ms: empirical delay for the IC-9700 to settle its LAN TX path
        // before the gain ramp begins. Too short produces a burst; too long
        // delays useful TX audio. Do not change without hardware testing.
        m_txAudioEnableTimer->start(220);
    }
    else
    {
        // Always send an unkey request. If local state ever gets stale, suppressing
        // this command can leave the radio transmitting until disconnect.
        m_txAudioEnableTimer->stop();
        m_txGainRampTimer->stop();
        m_pttActive = false;
        m_txLanModApplied = 0;
        invokeOnCurrentCommander(
            [](Commander* commandSession)
            {
                commandSession->receiveCommand(funcLANModLevel, QVariant::fromValue<quint16>(0), 0);
                commandSession->setPttActive(false);
                commandSession->receiveCommand(funcTransceiverStatus, QVariant::fromValue<bool>(false), 0);
            });
    }
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
    // will substitute it for mic audio frame-by-frame once the TX gate expires.
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

                const Funcs statusCommands[] = {
                    funcTransceiverStatus,
                    funcNoiseReduction,
                    funcNoiseBlanker,
                    funcPreamp,
                    funcAttenuator,
                    funcMicGain,
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
                    funcAutoNotch,
                    funcAGCTimeConstant,
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

    m_txLanModApplied = qBound(0, level, 255);
    const ushort val = static_cast<ushort>(m_txLanModApplied);

    if (QThread::currentThread() == m_commander->thread())
    {
        m_commander->receiveCommand(funcLANModLevel, QVariant::fromValue(val), 0);
        return;
    }

    invokeOnCurrentCommander([val](Commander* commandSession)
                             { commandSession->receiveCommand(funcLANModLevel, QVariant::fromValue(val), 0); });
}

void RadioBackend::startTxGainRamp(int targetLevel)
{
    m_txGainRampTimer->stop();
    m_txGainRampStart = m_txLanModApplied;
    m_txGainRampTarget = qBound(0, targetLevel, 255);
    m_txGainRampStep = 0;

    if (m_txGainRampStart == m_txGainRampTarget)
    {
        return;
    }

    m_txGainRampTimer->start();
}

void RadioBackend::setLanModLevel(int level)
{
    m_lanModLevel = qBound(0, level, 255);
    if (!m_commander || !m_pttActive)
    {
        return;
    }

    if (m_txAudioEnableTimer->isActive())
    {
        return;
    }

    if (m_txGainRampTimer->isActive())
    {
        m_txGainRampTarget = m_lanModLevel;
        return;
    }

    // Only push the new level while transmitting; during RX the radio's LAN
    // modulation input is intentionally held at 0.
    startTxGainRamp(m_lanModLevel);
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
    m_initialFrequencyReceived = false;
    m_initialModeReceived = false;
    m_initialMainFrequencyReceived = false;
    m_initialMainModeReceived = false;
    m_initialStateRequested = false;
    emit readyChanged(false);

    invokeOnCurrentCommander(
        [](Commander* commandSession)
        {
            commandSession->setRadioID(0xA2);

            const radioInput lanInput(inputLAN, 5, QStringLiteral("LAN"));
            commandSession->receiveCommand(funcDATAOffMod, QVariant::fromValue(lanInput), 0);
            commandSession->receiveCommand(funcDATA1Mod, QVariant::fromValue(lanInput), 0);

            commandSession->receiveCommand(funcLANModLevel, QVariant::fromValue<quint16>(0), 0);
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
    // arrives (m_scopeDataReceived set in funcScopeWaveData handler).
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
        invokeOnCurrentCommander(
            [](Commander* commandSession)
            {
                commandSession->receiveCommand(funcScopeOnOff, QVariant::fromValue<bool>(true), 0);
                commandSession->receiveCommand(funcScopeDataOutput, QVariant::fromValue<bool>(true), 0);
                commandSession->receiveCommand(funcScopeMainSub, QVariant::fromValue<bool>(false), 0);
                commandSession->receiveCommand(funcScopeSpan, QVariant::fromValue<uchar>(7), 0);
                commandSession->receiveCommand(funcLANModLevel, QVariant::fromValue<quint16>(0), 0);
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

    // Poll S-meter at 10 Hz; the IC-9700 only sends meter data on request.
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
                    invokeOnCurrentCommander([receiver](Commander* commandSession)
                                             { commandSession->receiveCommand(funcSWRMeter, QVariant(), receiver); });
                    return;
                }

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
}

void RadioBackend::onHaveAudioData(const audioPacket& pkt)
{
    // IC-9700 delivers LPCM16 audio; sampleRate comes from rxSetup.
    emit audioDataReady(pkt.data, static_cast<int>(m_rxSampleRate));
}


QString RadioBackend::modeInfoToString(const ModeInfo& mi) const
{
    radioMode_t mode = mi.mk;
    if (mode == modeUnknown && mi.reg != 0xff)
    {
        mode = static_cast<radioMode_t>(mi.reg);
    }

    switch (mode)
    {
    case modeUSB:
        return "USB";
    case modeLSB:
        return "LSB";
    case modeAM:
        return "AM";
    case modeFM:
        return "FM";
    case modeCW:
        return "CW";
    case modeCW_R:
        return "CW-R";
    case modeRTTY:
        return "RTTY";
    case modeRTTY_R:
        return "RTTY-R";
    case modeDV:
        return "DV";
    case modeDD:
        return "DD";
    default:
        qWarning(logRadio()) << "modeInfoToString: unrecognised mode" << mode << "reg" << mi.reg
                             << "- defaulting to FM";
        return "FM";
    }
}
