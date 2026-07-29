#include "Commander.h"
#include <QDebug>
#include <QMetaType>
#include <QSemaphore>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <memory>

#include "RadioCapabilities.h"
#include "RadioIdentities.h"
#include "LogCategories.h"

// Parses IC-9700 CI-V frames and forms outbound CI-V commands.
// Encoding follows the local IC-9700 reference material in resources/.

Commander::Commander(RadioCommander* parent) : RadioCommander(parent)
{

    qInfo(logRadio()) << "creating instance of Commander()";
}

Commander::Commander(quint8 guid[GUIDLEN], RadioCommander* parent) : RadioCommander(parent)
{
    Q_ASSERT(guid != nullptr);
    qInfo(logRadio()) << "creating instance of Commander() with GUID";
    memcpy(this->guid, guid, GUIDLEN);
}

Commander::~Commander()
{
    qInfo(logRadio()) << "[SHUTDOWN] ~Commander enter";

    emit requestRadioSelection(QList<radio_cap_packet>());

    shutdownComm();

    qDebug(logRadio()) << "[SHUTDOWN] ~Commander complete";
}

void Commander::commSetup(quint16 radioCivAddr, UdpConnectionSettings settings, audioSetup rxSetup, audioSetup txSetup,
                          QString vsp, quint16 tcp)
{
    Q_UNUSED(vsp)
    Q_UNUSED(tcp)

    this->settings = settings;
    civAddr = radioCivAddr;

    if (udp != nullptr)
    {
        closeComm();
    }

    udp = new UdpHandler(settings, rxSetup, txSetup);

    udpHandlerThread = new QThread(this);
    udpHandlerThread->setObjectName("UdpHandler()");

    udp->moveToThread(udpHandlerThread);

    connect(this, &Commander::initUdpHandler, udp, &UdpHandler::init);
    connect(udpHandlerThread, &QThread::finished, udp, &QObject::deleteLater);
    udpHandlerThread->start();

    emit initUdpHandler();

    connect(udp, &UdpHandler::haveDataFromPort, this, &Commander::handleNewData);
    connect(this, &Commander::dataForComm, udp, &UdpHandler::receiveDataFromUserToRadio);
    connect(udp, &UdpHandler::haveAudioData, this, &Commander::receiveAudioData);

    connect(this, &Commander::haveChangeLatency, udp, &UdpHandler::changeLatency);
    connect(this, &Commander::haveSetVolume, udp, &UdpHandler::setVolume);
    connect(udp, &UdpHandler::haveBaudRate, this, &Commander::receiveBaudRate);
    connect(udp, &UdpHandler::haveNetworkError, this, &Commander::handlePortError);
    connect(udp, &UdpHandler::haveNetworkStatus, this, &Commander::handleStatusUpdate);
    connect(udp, &UdpHandler::haveNetworkAudioLevels, this, &Commander::handleNetworkAudioLevels);
    connect(udp, &UdpHandler::requestRadioSelection, this, &Commander::radioSelection);
    connect(udp, &UdpHandler::setRadioUsage, this, &Commander::radioUsage);
    connect(this, &Commander::selectedRadio, udp, &UdpHandler::setCurrentRadio);
    connect(this, &Commander::requestEnableAudio, udp, &UdpHandler::enableAudio);
    connect(udp, &UdpHandler::streamReady, this, &Commander::lanReady);

    commonSetup();
}

void Commander::closeComm()
{
    shutdownComm();
}

void Commander::shutdownComm()
{
    qDebug(logRadio()) << "[SHUTDOWN] closeComm() enter";
    if (udpHandlerThread != nullptr)
    {
        if (udp)
        {
            qDebug(logRadio()) << "[SHUTDOWN] closeComm() calling udp->shutdown() ...";
            auto shutdownDone = std::make_shared<QSemaphore>();
            UdpHandler* udpSession = udp;
            const bool invoked = QMetaObject::invokeMethod(
                udp,
                [udpSession, shutdownDone]()
                {
                    udpSession->shutdown();
                    shutdownDone->release();
                },
                Qt::QueuedConnection);
            if (!invoked || !shutdownDone->tryAcquire(1, 3000))
            {
                qWarning(logRadio()) << "[SHUTDOWN] UdpHandler shutdown did not finish within 3000 ms";
            }
        }
        qDebug(logRadio()) << "[SHUTDOWN] closeComm() udpHandlerThread->quit()";
        udpHandlerThread->quit();
        qDebug(logRadio()) << "[SHUTDOWN] closeComm() udpHandlerThread->wait(3000) ...";
        if (!udpHandlerThread->wait(3000))
        {
            qWarning(logRadio()) << "[SHUTDOWN] closeComm() udpHandlerThread did not stop within 3000 ms; "
                                    "requesting interruption";
            udpHandlerThread->requestInterruption();
            udpHandlerThread->quit();
            if (!udpHandlerThread->wait(1000))
            {
                qCritical(logRadio()) << "[SHUTDOWN] closeComm() udpHandlerThread did not stop after bounded "
                                         "shutdown; leaving thread detached";
                udpHandlerThread->setParent(nullptr);
                connect(udpHandlerThread, &QThread::finished, udpHandlerThread, &QObject::deleteLater);
                udpHandlerThread = nullptr;
            }
        }
        else
        {
            qDebug(logRadio()) << "[SHUTDOWN] closeComm() udpHandlerThread done";
        }
        if (udpHandlerThread != nullptr)
        {
            delete udpHandlerThread;
            udpHandlerThread = nullptr;
        }
    }
    if (queue != nullptr)
    {
        queue->resetSessionState();
    }
    m_pendingReplies.clear();
    m_pendingSetCommands.clear();
    udp = nullptr;
}

void Commander::commonSetup()
{

    setCIVAddr(civAddr);
    spectSeqMax = 0;

    payloadSuffix = QByteArray("\xFD");

    lookingForRadio = true;
    foundRadio = false;
    m_pendingReplies.clear();
    m_pendingSetCommands.clear();
    m_pendingCommandClock.start();

    // Minimal commands used before the built-in IC-9700 capability table is loaded.
    radioCaps.commands.clear();
    radioCaps.commandsReverse.clear();
    radioCaps.commands.insert(funcTransceiverId,
                              FuncType(funcTransceiverId, QString("Transceiver ID"), QByteArrayLiteral("\x19\x00"), 0,
                                       0, false, false, true, false, 1, false));
    radioCaps.commandsReverse.insert(QByteArrayLiteral("\x19\x00"), funcTransceiverId);

    radioCaps.commands.insert(funcPowerControl,
                              FuncType(funcPowerControl, QString("Power Control"), QByteArrayLiteral("\x18"), 0, 0,
                                       false, false, true, false, 1, false));
    radioCaps.commandsReverse.insert(QByteArrayLiteral("\x18"), funcPowerControl);

    connect(queue, &CachingQueue::haveCommand, this, &Commander::receiveCommand, Qt::UniqueConnection);
    oldScopeMode = 0xff;

    emit commReady();
}

void Commander::process() {}

void Commander::receiveBaudRate(quint32 baudrate)
{
    radioCaps.baudRate = baudrate;
    emit haveBaudRate(baudrate);
}

void Commander::prepDataAndSend(QByteArray data)
{
    data.prepend(payloadPrefix);
    data.append(payloadSuffix);

    if (data[4] != '\x15')
    {
        // Meter polling is high-volume; keep CI-V traffic useful by suppressing it.
        qInfo(logRadioTraffic()).noquote() << "CI-V TX" << data.toHex(' ');
    }
    emit dataForComm(data);
}

void Commander::rememberPendingReply(Funcs func, uchar receiver)
{
    static constexpr int kMaxPendingReplies = 64;

    if (func == funcNone)
    {
        return;
    }

    discardExpiredPendingReplies();
    m_pendingReplies.append(PendingReply{func, receiver, m_pendingCommandClock.elapsed()});
    if (m_pendingReplies.size() > kMaxPendingReplies)
    {
        m_pendingReplies.remove(0, m_pendingReplies.size() - kMaxPendingReplies);
    }
}

bool Commander::takePendingReplyReceiver(Funcs func, uchar* receiver)
{
    if (!receiver)
    {
        return false;
    }

    discardExpiredPendingReplies();
    for (int i = 0; i < m_pendingReplies.size(); ++i)
    {
        if (m_pendingReplies.at(i).func == func)
        {
            *receiver = m_pendingReplies.at(i).receiver;
            m_pendingReplies.removeAt(i);
            return true;
        }
    }

    return false;
}

void Commander::discardExpiredPendingReplies()
{
    static constexpr qint64 kPendingReplyLifetimeMs = 5000;
    const qint64 oldestAllowed = m_pendingCommandClock.elapsed() - kPendingReplyLifetimeMs;

    // A command reply that arrives after this window can no longer be routed
    // confidently to MAIN or SUB. Discarding stale hints is safer than applying
    // an unsolicited update to a receiver selected several seconds earlier.
    m_pendingReplies.erase(std::remove_if(m_pendingReplies.begin(), m_pendingReplies.end(),
                                          [oldestAllowed](const PendingReply& reply)
                                          { return reply.createdAtMs < oldestAllowed; }),
                           m_pendingReplies.end());
}

void Commander::rememberPendingSetCommand(Funcs func, const QByteArray& payload, const QVariant& value, uchar receiver,
                                          const FuncType& command)
{
    static constexpr qsizetype kMaxPendingSetCommands = 128;

    // CI-V FB/FA frames do not identify the command they acknowledge. Preserve
    // send order so each acknowledgement is applied to the oldest outstanding
    // set operation instead of whichever command happened to be sent last.
    m_pendingSetCommands.enqueue(
        CommandErrorType(func, payload, value, receiver, command.minVal, command.maxVal, command.bytes));
    if (m_pendingSetCommands.size() > kMaxPendingSetCommands)
    {
        qCritical(logRadio()) << "CI-V acknowledgement queue overflow; dropping the oldest command correlation";
        m_pendingSetCommands.dequeue();
    }
}

bool Commander::takePendingSetCommand(CommandErrorType* command)
{
    if (!command || m_pendingSetCommands.isEmpty())
    {
        return false;
    }

    *command = m_pendingSetCommands.dequeue();
    return true;
}

FuncType Commander::getCommand(Funcs func, QByteArray& payload, int value, uchar receiver)
{
    FuncType cmd;
    // INT_MIN marks a get request and is outside supported set-value ranges.
    auto it = radioCaps.commands.find(func);
    if (it != radioCaps.commands.end())
    {
        if (value == INT_MIN || (value >= it.value().minVal && value <= it.value().maxVal))
        {
            if (radioCaps.hasCommand29 && it.value().cmd29)
            {
                // CI-V command 29h prefixes receiver-scoped commands.
                payload.append('\x29');
                payload.append(static_cast<uchar>(receiver));
            }
            else if (!radioCaps.hasCommand29 && receiver && receiver != 0xff)
            {
                // CI-V 29h is required for direct receiver-scoped writes.
                // Scope commands remain valid because the payload carries receiver data.
                switch (func)
                {
                case funcFreqGet:
                case funcFreqSet:
                case funcModeGet:
                case funcModeSet:
                case funcSMeter:
                case funcSWRMeter:
                case funcPowerMeter:
                case funcALCMeter:
                case funcAfGain:
                case funcRfGain:
                case funcSquelch:
                case funcRFPower:
                case funcScopeMode:
                case funcScopeSpan:
                case funcScopeRef:
                case funcScopeHold:
                case funcScopeSpeed:
                case funcScopeRBW:
                case funcScopeVBW:
                case funcScopeCenterType:
                case funcScopeEdge:
                    break;
                default:
                    qDebug(logRadio()) << "Radio has no Command29, removing command:" << funcString[func] << "VFO"
                                       << receiver;
                    queue->del(func, receiver);
                    break;
                }
            }
            payload.append(it.value().data);
            cmd = it.value();
        }
        else if (value != INT_MIN)
        {
            qDebug(logRadio()) << QString("Value %1 for %2 is outside of allowed range (%3-%4)")
                                      .arg(value)
                                      .arg(funcString[func])
                                      .arg(it.value().minVal)
                                      .arg(it.value().maxVal);
        }
    }
    else
    {
        // The built-in IC-9700 table does not support this command.
        qDebug(logRadio()) << "Removing unsupported command from queue" << funcString[func] << "VFO" << receiver;
        queue->del(func, receiver);
    }
    return cmd;
}

QByteArray Commander::makeFreqPayload(Frequency freq, uchar numchars)
{
    QByteArray result;
    quint64 freqInt = freq.Hz;

    if (numchars == 5 && freq.Hz >= 1E10)
    {
        // Retain six-byte CI-V frequency encoding for protocol-range safety above 10 GHz.
        numchars = 6;
    }

    for (int i = 0; i < numchars; i++)
    {
        quint8 a = 0;
        a |= (freqInt) % 10;
        freqInt /= 10;
        a |= ((freqInt) % 10) << 4;

        freqInt /= 10;

        result.append(a);
    }

    return result;
}

QByteArray Commander::makeFreqPayload(double frequency)
{
    quint64 freqInt = (quint64)(frequency * 1E6);

    QByteArray result;
    int numchars = 5;
    if (freqInt >= 1E10)
    {
        numchars = 6;
    }

    for (int i = 0; i < numchars; i++)
    {
        quint8 a = 0;
        a |= (freqInt) % 10;
        freqInt /= 10;
        a |= ((freqInt) % 10) << 4;

        freqInt /= 10;

        result.append(a);
    }
    return result;
}

QByteArray Commander::encodeTone(quint16 tone) const
{
    return encodeTone(tone, false, false);
}

QByteArray Commander::encodeTone(quint16 tone, bool tinv, bool rinv) const
{
    // CTCSS and DTCS use the same packed decimal tone payload.
    QByteArray enct;

    quint8 inv = 0;
    inv |= static_cast<quint8>(rinv);
    inv |= static_cast<quint8>(tinv) << 4;

    enct.append(inv);

    quint8 hundreds = tone / 1000;
    quint8 tens = (tone - (hundreds * 1000)) / 100;
    quint8 ones = (tone - (hundreds * 1000) - (tens * 100)) / 10;
    quint8 dec = (tone - (hundreds * 1000) - (tens * 100) - (ones * 10));

    enct.append(tens | (hundreds << 4));
    enct.append(dec | (ones << 4));

    return enct;
}

ToneInfo Commander::decodeTone(const QByteArray& eTone)
{
    // index:  00 01  02 03 04
    // CTCSS:  1B 01  00 12 73 = PL 127.3, decode as 1273
    // D(T)CS: 1B 01  TR 01 23 = T/R Invert bits + DCS code 123

    ToneInfo t;
    if (eTone.length() < 3)
    {
        return t;
    }

    ushort tone = (eTone.at(2) & 0x0f);
    tone += ((eTone.at(2) & 0xf0) >> 4) * 10;
    tone += (eTone.at(1) & 0x0f) * 100;
    tone += ((eTone.at(1) & 0xf0) >> 4) * 1000;

    // Always set the decoded value first so DCS codes (which are not in the
    // CTCSS list) don't fall through to the default-constructed tone of 670.
    t.tone = tone;

    const auto toneIt = std::find_if(radioCaps.ctcss.cbegin(), radioCaps.ctcss.cend(),
                                     [tone](const ToneInfo& info) { return info.tone == tone; });
    if (toneIt != radioCaps.ctcss.cend())
    {
        t = *toneIt;
    }

    if ((eTone.at(0) & 0x01) == 0x01)
    {
        t.tinv = true;
    }
    if ((eTone.at(0) & 0x10) == 0x10)
    {
        t.rinv = true;
    }

    return t;
}

void Commander::setCIVAddr(quint16 newCivAddr)
{
    // The controller CI-V address is defined in the header.

    civAddr = newCivAddr;
    payloadPrefix = QByteArray("\xFE\xFE");
    payloadPrefix.append((char)newCivAddr);
    payloadPrefix.append((char)compCivAddr);
}

void Commander::handleNewData(const QByteArray& data)
{
    const bool scopeDataFrame = data.size() > 64 && static_cast<uchar>(data[4]) == 0x27;
    if (!scopeDataFrame)
    {
        qInfo(logRadioTraffic()).noquote() << "CI-V RX" << data.toHex(' ');
    }
    // Spectrum Scope frames arrive continuously and are hundreds of bytes long.
    // Logging every frame hides startup and memory-sync evidence, which is
    // exactly what we need when diagnosing radio readiness hangs. Suppress only
    // the log line; parsing and routing still receive the complete frame.
    emit haveDataForServer(data);
    parseData(data);
}

void Commander::parseData(const QByteArray& dataInput)
{
    int index = 0;

    QList<QByteArray> dataList = dataInput.split('\xFD');
    QByteArray data;
    if (dataList.last().isEmpty())
    {
        dataList.removeLast();
    }
    for (index = 0; index < dataList.count(); index++)
    {
        data = dataList[index];
        data.append('\xFD');

        // Echoed command:
        // fe fe 94 e0 ...... fd

        // Query reply:
        // fe fe e0 94 ...... fd

        // Radio-initiated update:
        // fe fe 00 94 ...... fd

        if (data.length() < 4)
        {
            if (data.length())
            {
                qDebug(logRadio()) << "Short CI-V fragment while parsing LAN data:" << data.length() << "bytes";
            }
            // Keep parsing the remaining byte stream. A short fragment can
            // appear ahead of a valid frame after LAN packet coalescing or
            // recovery from a malformed frame.
            continue;
        }

        if (!data.startsWith("\xFE\xFE"))
        {
            // Recover a frame that lost one leading FE byte.
            if (data.startsWith('\xFE'))
            {
                data.prepend('\xFE');
                parseData(data);
            }
            continue;
        }

        if ((quint8)data[02] == civAddr)
        {
            // Echoed local command; normal reply handling happens on controller-addressed frames.
        }

        incomingCIVAddr = data[03] & 0xff; // track the CIV of the sender.

        switch (data[02])
        {
        case (char)0xE0:
        case (char)compCivAddr:
            // Query reply addressed to this controller.
            payloadIn = data.right(data.length() - 4);
            if (payloadIn.contains("\xFE"))
            {
                break;
            }
            parseCommand();
            if (!radioPoweredOn && !payloadIn.isEmpty())
            {
                queue->receiveValue(funcPowerControl, QVariant::fromValue<bool>(true), 0);
                radioPoweredOn = true;
            }

            break;
        case '\x00':
            // Radio-initiated update.
            if ((quint8)data[03] == compCivAddr)
            {
                // Echo of a local broadcast request.
                if (radioPoweredOn)
                {
                    qDebug(logRadio()) << "Echo caught:" << data.toHex(' ');
                    queue->message("Radio is available but may be powered-off");
                    queue->receiveValue(funcPowerControl, QVariant::fromValue<bool>(false), 0);
                    radioPoweredOn = false;
                }
            }
            else
            {
                payloadIn = data.right(data.length() - 4);
                if (payloadIn.contains("\xFE"))
                {
                    break;
                }
                parseCommand();
            }
            break;
        default:
            // Ignore frames addressed to other CI-V devices.
            break;
        }
    }
}

void Commander::parseCommand()
{

#ifdef DEBUG_PARSE
    QElapsedTimer performanceTimer;
    performanceTimer.start();
#endif

    Funcs func = funcNone;
    uchar receiver = 0;
    uchar vfo = 0;

    if (payloadIn.endsWith((char)0xfd))
    {
        payloadIn.chop(1);
    }

    if (radioCaps.hasCommand29 && payloadIn.size() >= 2 && payloadIn.at(0) == '\x29')
    {
        receiver = static_cast<uchar>(payloadIn.at(1));
        payloadIn.remove(0, 2);
    }

    // Some CI-V commands have both single-byte and multi-byte forms. Match the
    // longest command first so multi-byte commands are not split early.
    int count = 0;
    for (int i = 4; i > 0; i--)
    {
        auto it = radioCaps.commandsReverse.find(payloadIn.left(i));
        if (it != radioCaps.commandsReverse.end())
        {
            func = it.value();
            count = i;
            break;
        }
    }

    // Remove the matched command prefix; only payload bytes remain.
    payloadIn.remove(0, count);

#ifdef DEBUG_PARSE
    int currentParse = performanceTimer.nsecsElapsed();
#endif

    if (!radioCaps.commands.contains(func))
    {
        // Capability detection may still be in progress.
        if (haveRadioCaps)
        {
            qInfo(logRadio()) << "Unsupported command received from radio" << payloadIn.toHex().mid(0, 10)
                              << "Check radio file";
        }
        return;
    }

    // When CI-V 29h is unavailable, most IC-9700 replies do not identify MAIN
    // or SUB. Prefer the receiver from the matching outstanding request; fall
    // back to the current selected receiver for unsolicited updates.
    if (!radioCaps.hasCommand29 && func != funcSelectedFreq && func != funcSelectedMode && func != funcUnselectedFreq &&
        func != funcUnselectedMode)
    {
        uchar pendingReceiver = 0;
        receiver = takePendingReplyReceiver(func, &pendingReceiver) ? pendingReceiver : queue->getState().receiver;
    }

    Frequency test;
    QVector<MemParserFormat> memParser;
    QVariant value;
    auto payloadTooShort = [&](int requiredBytes) -> bool
    {
        if (payloadIn.size() >= requiredBytes)
        {
            return false;
        }

        qWarning(logRadio()) << "Ignoring short CI-V payload for" << funcString[func] << "required" << requiredBytes
                             << "got" << payloadIn.size() << "data:" << payloadIn.toHex(' ');
        return true;
    };

    switch (func)
    {
    case funcVFODualWatch:
        if (payloadTooShort(1))
        {
            return;
        }
        value.setValue(static_cast<bool>(bool(payloadIn.at(0))));
        break;
    case funcFreq:
        if (payloadTooShort(1))
        {
            return;
        }
        receiver = payloadIn.at(0);
        payloadIn.remove(0, 1);
        [[fallthrough]];
    case funcSelectedFreq:
    case funcUnselectedFreq:
    case funcFreqGet:
    case funcFreqTR:
    case funcTXFreq:
    {
        if (func == funcFreqTR || func == funcFreqGet)
        {
            if (radioCaps.commands.contains(funcFreq))
            {
                func = funcFreq;
            }
            else if (radioCaps.commands.contains(funcSelectedFreq))
            {
                func = funcSelectedFreq;
            }
            else
            {
                func = funcFreqGet;
            }
        }
        else if (func == funcUnselectedFreq)
        {
            vfo = 1;
        }

        value.setValue(parseFreqData(payloadIn, vfo));
        break;
    }
    case funcMode:
        if (payloadTooShort(1))
        {
            return;
        }
        receiver = payloadIn.at(0);
        payloadIn.remove(0, 1);
        [[fallthrough]];
    case funcModeGet:
    case funcModeTR:
    case funcSelectedMode:
    case funcUnselectedMode:
    case funcDataModeWithFilter:
    {
        Funcs origFunc = func;
        // Normalize alternate mode reply commands to the command SDR9700 caches.
        if (func == funcModeTR || func == funcModeGet || func == funcDataModeWithFilter)
        {
            if (radioCaps.commands.contains(funcMode))
            {
                func = funcMode;
            }
            else if (radioCaps.commands.contains(funcSelectedMode))
            {
                func = funcSelectedMode;
            }
            else
            {
                func = funcModeGet;
            }
        }
        else if (func == funcUnselectedMode)
        {
            vfo = 1;
        }

        ModeInfo mi;

        // Preserve cached mode fields not present in this reply.
        CacheItem ci = queue->getCache(func, receiver);
        if (ci.value.isValid())
        {
            mi = queue->getCache(func, receiver).value.value<ModeInfo>();
        }

        if (origFunc == funcDataModeWithFilter)
        {
            if (payloadTooShort(2))
            {
                return;
            }
            // Data-mode replies use the two-byte CI-V payload shape.
            mi.filter = bcdHexToUChar(payloadIn.at(1));
            mi.data = bcdHexToUChar(payloadIn.at(0));
        }
        else
        {
            if (payloadIn.size())
            {
                mi.reg = bcdHexToUChar(payloadIn.at(0));
            }
            if (payloadIn.size() == 2)
            {
                mi.filter = payloadIn.at(1);
            }
            if (payloadIn.size() == 3)
            {
                mi.data = payloadIn.at(1);
                mi.filter = payloadIn.at(2);
            }
        }

        mi = parseMode(mi.reg, mi.data, mi.filter, receiver, vfo);
        mi.VFO = selVFO_t(receiver);
        value.setValue(mi);
        break;
    }

    case funcVFOBandMS:
        if (payloadTooShort(1))
        {
            return;
        }
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        break;
    case funcMemoryMode:
        qInfo(logRadio()) << "Memory Mode command!";
        break;
    case funcSatelliteMemory:
        memParser = radioCaps.satParser;
        [[fallthrough]];
    case funcMemoryContents:
    {
        qDebug(logRadio()) << "Received mem:" << payloadIn.toHex(' ');
        MemoryType mem;
        if (memParser.isEmpty())
        {
            memParser = radioCaps.memParser;
            mem.sat = false;
        }
        else
        {
            mem.sat = true;
        }

        parseMemory(&memParser, &mem);
        value.setValue(mem);
        break;
    }
    case funcMemoryClear:
    case funcBandStackReg:
    case funcMemoryToVFO:
    case funcMemoryWrite:
        break;
    case funcScanning:
        break;
    case funcReadFreqOffset:
        value.setValue(parseFrequencyRptOffset(payloadIn));
        break;
    // Single-byte BCD values converted to uchar.
    case funcTuningStep:
    case funcAttenuator:
        if (payloadTooShort(1))
        {
            return;
        }
        value.setValue(bcdHexToUChar(payloadIn.at(0)));
        break;
    case funcSplitStatus:
        if (payloadTooShort(1))
        {
            return;
        }
        value.setValue(static_cast<duplexMode_t>(uchar(payloadIn.at(0))));
        break;
    case funcQuickSplit:
        if (payloadTooShort(1))
        {
            return;
        }
        value.setValue(bcdHexToUChar(payloadIn.at(0)));
        break;
    case funcAntenna:
    {
        if (payloadTooShort(1))
        {
            return;
        }
        AntennaInfo ant;
        ant.rx = false;
        ant.antenna = bcdHexToUChar(payloadIn.at(0));
        if (payloadIn.size() > 1)
        {
            ant.rx = static_cast<bool>(payloadIn.at(1));
        }
        value.setValue(ant);
        break;
    }
    case funcAfGain:
        if (payloadTooShort(2))
        {
            return;
        }
        value.setValue(bcdHexToUChar(payloadIn.at(0), payloadIn.at(1)));
        break;
    // Two-byte CI-V levels with SDR9700-specific unit mappings.
    case funcKeySpeed:
    {
        if (payloadTooShort(2))
        {
            return;
        }
        uchar level = bcdHexToUChar(payloadIn.at(0), payloadIn.at(1));
        value.setValue<ushort>(round((level / 6.071) + 6));
        break;
    }
    case funcCwPitch:
    {
        if (payloadTooShort(2))
        {
            return;
        }
        uchar level = bcdHexToUChar(payloadIn.at(0), payloadIn.at(1));
        value.setValue<ushort>(round((((600.0 / 255.0) * level) + 300) / 5.0) * 5.0);
        break;
    }
    // CI-V group 15: meter readings.
    case funcSMeter:
        if (payloadTooShort(2))
        {
            return;
        }
        {
            const quint8 rawS = bcdHexToUChar(payloadIn.at(0), payloadIn.at(1));
            const double sMeter = getMeterCal(meterS, rawS);
            qDebug(logRadioTraffic()).nospace() << "S meter raw=" << static_cast<int>(rawS) << " calibrated=" << sMeter;
            value.setValue(sMeter);
            break;
        }
    case funcCenterMeter:
        if (payloadTooShort(2))
        {
            return;
        }
        value.setValue(getMeterCal(meterCenter, bcdHexToUChar(payloadIn.at(0), payloadIn.at(1))));
        break;
    case funcPowerMeter:
        if (payloadTooShort(2))
        {
            return;
        }
        value.setValue(getMeterCal(meterPower, bcdHexToUChar(payloadIn.at(0), payloadIn.at(1))));
        break;
    case funcSWRMeter:
        if (payloadTooShort(2))
        {
            return;
        }
        {
            const quint8 rawSwr = bcdHexToUChar(payloadIn.at(0), payloadIn.at(1));
            const double swr = getMeterCal(meterSWR, rawSwr);
            qDebug(logRadioTraffic()).nospace()
                << "SWR meter raw=" << static_cast<int>(rawSwr) << " calibrated=" << swr;
            value.setValue(swr);
            break;
        }
    case funcALCMeter:
        if (payloadTooShort(2))
        {
            return;
        }
        {
            const quint8 rawAlc = bcdHexToUChar(payloadIn.at(0), payloadIn.at(1));
            const double alc = getMeterCal(meterALC, rawAlc);
            qDebug(logRadioTraffic()).nospace()
                << "ALC meter raw=" << static_cast<int>(rawAlc) << " calibrated=" << alc;
            value.setValue(alc);
            break;
        }
    case funcCompMeter:
        if (payloadTooShort(2))
        {
            return;
        }
        value.setValue(getMeterCal(meterComp, bcdHexToUChar(payloadIn.at(0), payloadIn.at(1))));
        break;
    case funcVdMeter:
        if (payloadTooShort(2))
        {
            return;
        }
        value.setValue(getMeterCal(meterVoltage, bcdHexToUChar(payloadIn.at(0), payloadIn.at(1))));
        break;
    case funcIdMeter:
        if (payloadTooShort(2))
        {
            return;
        }
        value.setValue(getMeterCal(meterCurrent, bcdHexToUChar(payloadIn.at(0), payloadIn.at(1))));
        break;

    case funcRfGain:
    case funcSquelch:
    case funcAPFLevel:
    case funcNRLevel:
    case funcPBTInner:
    case funcPBTOuter:
    case funcIFShift:
    case funcRFPower:
    case funcNotchFilter:
    case funcCompressorLevel:
    case funcBreakInDelay:
    case funcNBLevel:
    case funcDigiSelShift:
    case funcDriveGain:
    case funcMonitorGain:
    case funcVoxGain:
    case funcAntiVoxGain:
    case funcBackLightLevel:

    case funcBeepLevel:
    case funcBeepMain:
    case funcBeepSub:

    case funcRFSQLControl:
    case funcTXDelayHF:
    case funcTXDelay50m:
    case funcTimeOutCIV:

        if (payloadTooShort(2))
        {
            return;
        }
        value.setValue(bcdHexToUChar(payloadIn.at(0), payloadIn.at(1)));
        break;
    case funcAGC:
    case funcAGCTimeConstant:
    case funcBreakIn:
    case funcPreamp:
    case funcManualNotchWidth:
    case funcSSBTXBandwidth:
    case funcRoofingFilter:
    case funcFilterShape:
    // Tone-control registers under CI-V 1A05.
    case funcSSBRXBass:
    case funcSSBRXTreble:
    case funcAMRXBass:
    case funcAMRXTreble:
    case funcFMRXBass:
    case funcFMRXTreble:
    case funcSSBTXBass:
    case funcSSBTXTreble:
    case funcAMTXBass:
    case funcAMTXTreble:
    case funcFMTXBass:
    case funcFMTXTreble:
    case funcTimeOutTimer:
    case funcBandEdgeBeep:
        if (payloadTooShort(1))
        {
            return;
        }
        value.setValue(bcdHexToUChar(payloadIn.at(0)));
        break;

    // RX audio passband limits.
    case funcSSBRXHPFLPF:
    case funcAMRXHPFLPF:
    case funcFMRXHPFLPF:
    case funcCWRXHPFLPF:
    case funcRTTYRXHPFLPF:
        if (payloadTooShort(2))
        {
            return;
        }
        value.setValue(LpfHpf(ushort(payloadIn.at(0)) * 100, ushort(payloadIn.at(1)) * 100));
        break;
    case funcAbsoluteMeter:
    {
        if (payloadTooShort(4))
        {
            return;
        }
        MeterKind m;
        m.value = double(bcdHexToUInt(payloadIn.at(0), payloadIn.at(1))) / 10.0;
        if (payloadIn.at(2) != '\0')
        {
            m.value = -m.value;
        }
        if (payloadIn.at(3) == 0)
        {
            m.type = meterdBu;
        }
        else if (payloadIn.at(3) == 1)
        {
            m.type = meterdBuEMF;
        }
        else if (payloadIn.at(3) == 2)
        {
            m.type = meterdBm;
        }
        else
        {
            qWarning(logRadio()) << "Unknown meter type received!";
            m.type = meterNone;
        }
        value.setValue(m);
        break;
    }
    case funcMeterType:
    {
        if (payloadTooShort(1))
        {
            return;
        }
        meter_t m;
        if (payloadIn.at(0) == 0)
        {
            m = meterS;
        }
        else if (payloadIn.at(0) == 1)
        {
            m = meterdBu;
        }
        else if (payloadIn.at(0) == 2)
        {
            m = meterdBuEMF;
        }
        else if (payloadIn.at(0) == 3)
        {
            m = meterdBm;
        }
        else
        {
            qWarning(logRadio()) << "Unknown meterType received!";
            m = meterNone;
        }
        value.setValue(m);
        break;
    }
    // Single-byte CI-V boolean flags.
    case funcMainSubTracking:
    case funcSatelliteMode:
    case funcNoiseBlanker:
    case funcAudioPeakFilter:
    case funcNoiseReduction:
    case funcAutoNotch:
    case funcRepeaterTone:
    case funcRepeaterTSQL:
    case funcRepeaterDTCS:
    case funcRepeaterCSQL:
    case funcCompressor:
    case funcMonitor:
    case funcVox:
    case funcManualNotch:
    case funcDigiSel:
    case funcTwinPeakFilter:
    case funcDialLock:
    case funcOverflowStatus:
    case funcSMeterSqlStatus:
    case funcVariousSql:
    case funcRXAntenna:
    case funcIPPlus:
    case funcBeepLevelLimit:
    case funcBeepConfirmation:
        if (payloadTooShort(1))
        {
            return;
        }
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        break;
    case funcToneSquelchType:
    {
        if (payloadTooShort(1))
        {
            return;
        }
        RptrAccessData r;
        r.accessMode = static_cast<rptAccessTxRx_t>(bcdHexToUChar(payloadIn.at(0)));
        r.useSecondaryVFO = static_cast<bool>(vfo);
        value.setValue(r);
        break;
    }
    case funcTransceiverId:
        if (payloadTooShort(1))
        {
            return;
        }
        if (!radioCaps.modelID)
        {
            radioCaps.modelID = static_cast<quint16>(payloadIn.at(0)) & 0xff;
            if (radioCaps.modelID == kRadioModelId)
            {
                this->model = kRadioModelId;
            }
            qInfo(logRadio()) << QString("Have new radio ID: 0x%1").arg(radioCaps.modelID, 2, 16);
            determineRadioCaps();
        }
        value.setValue(radioCaps.modelID);
        break;
    case funcFilterWidth:
    {
        if (payloadTooShort(1))
        {
            return;
        }
        quint16 calc;
        quint8 pass = bcdHexToUChar((quint8)payloadIn.at(0));
        VfoCommandType t = queue->getVfoCommand(vfoA, receiver, false);
        ModeInfo m = queue->getCache(t.modeFunc, t.receiver).value.value<ModeInfo>();

        if (m.mk == modeAM)
        {
            calc = 200 + (pass * 200);
        }
        else if (pass <= 10)
        {
            calc = 50 + (pass * 50);
        }
        else
        {
            calc = 600 + ((pass - 10) * 100);
        }
        value.setValue(calc);
        break;
    }
    case funcAFMute:
        qWarning(logRadio()) << "AF mute response parsing is not implemented";
        break;
    // CI-V 1A05 two-byte level registers.
    case funcREFAdjust:
    case funcREFAdjustFine:
    case funcACCAModLevel:
    case funcACCBModLevel:
    case funcUSBModLevel:
    case funcLANModLevel:
    case funcSPDIFModLevel:
    case funcNBWidth:
        if (payloadTooShort(2))
        {
            return;
        }
        value.setValue(bcdHexToUChar(payloadIn.at(0), payloadIn.at(1)));
        break;
    // Single byte returned as uchar (0-99).
    case funcDATAOffMod:
    case funcDATA1Mod:
    case funcDATA2Mod:
    case funcDATA3Mod:
    {
        if (payloadTooShort(1))
        {
            return;
        }
        const auto inputIt =
            std::find_if(radioCaps.inputs.cbegin(), radioCaps.inputs.cend(),
                         [this](const radioInput& input) { return input.reg == bcdHexToUChar(payloadIn.at(0)); });
        if (inputIt != radioCaps.inputs.cend())
        {
            value.setValue(*inputIt);
        }
        break;
    }
    case funcDashRatio:
    case funcNBDepth:
    case funcVOXDelay:
        if (payloadTooShort(1))
        {
            return;
        }
        value.setValue(bcdHexToUChar(payloadIn.at(0)));
        break;
    case funcUTCOffset:
    case funcDate:
    case funcTime:
        break;
    // Fixed-frequency scope edge payloads.
    case funcScopeEdge1a:
    case funcScopeEdge2a:
    case funcScopeEdge3a:
    case funcScopeEdge4a:
    case funcScopeEdge1b:
    case funcScopeEdge2b:
    case funcScopeEdge3b:
    case funcScopeEdge4b:
    case funcScopeEdge1c:
    case funcScopeEdge2c:
    case funcScopeEdge3c:
    case funcScopeEdge4c:
    case funcScopeEdge1d:
    case funcScopeEdge2d:
    case funcScopeEdge3d:
    case funcScopeEdge4d:
    case funcScopeEdge1e:
    case funcScopeEdge2e:
    case funcScopeEdge3e:
    case funcScopeEdge4e:
    case funcScopeEdge1f:
    case funcScopeEdge2f:
    case funcScopeEdge3f:
    case funcScopeEdge4f:
    case funcScopeEdge1g:
    case funcScopeEdge2g:
    case funcScopeEdge3g:
    case funcScopeEdge4g:
    case funcScopeEdge1h:
    case funcScopeEdge2h:
    case funcScopeEdge3h:
    case funcScopeEdge4h:
    case funcScopeEdge1i:
    case funcScopeEdge2i:
    case funcScopeEdge3i:
    case funcScopeEdge4i:
    case funcScopeEdge1j:
    case funcScopeEdge2j:
    case funcScopeEdge3j:
    case funcScopeEdge4j:
    case funcScopeEdge1k:
    case funcScopeEdge2k:
    case funcScopeEdge3k:
    case funcScopeEdge4k:
    case funcScopeEdge1l:
    case funcScopeEdge2l:
    case funcScopeEdge3l:
    case funcScopeEdge4l:
    case funcScopeEdge1m:
    case funcScopeEdge2m:
    case funcScopeEdge3m:
    case funcScopeEdge4m:
    case funcScopeEdge1n:
    case funcScopeEdge2n:
    case funcScopeEdge3n:
    case funcScopeEdge4n:
    case funcScopeEdge1o:
    case funcScopeEdge2o:
    case funcScopeEdge3o:
    case funcScopeEdge4o:
    case funcScopeEdge1p:
    case funcScopeEdge2p:
    case funcScopeEdge3p:
    case funcScopeEdge4p:
    case funcScopeEdge1q:
    case funcScopeEdge2q:
    case funcScopeEdge3q:
    case funcScopeEdge4q:
    case funcScopeEdge1r:
    case funcScopeEdge2r:
    case funcScopeEdge3r:
    case funcScopeEdge4r:
    case funcScopeEdge1s:
    case funcScopeEdge2s:
    case funcScopeEdge3s:
    case funcScopeEdge4s:
        break;
    // CI-V 1B: tone and squelch code registers.
    case funcToneFreq:
    case funcTSQLFreq:
    case funcDTCSCode:
    case funcCSQLCode:
        value.setValue(decodeTone(payloadIn));
        break;
    // CI-V 1C: boolean status registers.
    case funcRitStatus:
    case funcTransceiverStatus:
    case funcXFCStatus:
        if (payloadTooShort(1))
        {
            return;
        }
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        break;
    case funcTunerStatus:
        if (payloadTooShort(1))
        {
            return;
        }
        value.setValue(bcdHexToUChar(payloadIn.at(0)));
        break;
    // CI-V 21: RIT offset register.
    // IC-9700 format: 2 BCD bytes (offset Hz) + 1 sign byte (0=+, 1=-).
    case funcRitFreq:
    {
        if (payloadTooShort(3))
        {
            return;
        }
        Frequency f;
        QByteArray longfreq = payloadIn.mid(0, 2);
        longfreq.append(QByteArray(3, '\x00'));
        f = parseFrequency(longfreq, 3);
        const short ritHz = static_cast<short>(f.Hz) * ((payloadIn.at(2) == '\x01') ? -1 : 1);
        value.setValue(ritHz);
        break;
    }
    case funcRitTXStatus:
        if (payloadTooShort(1))
        {
            return;
        }
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        break;
    case funcTXFreqMon:
        if (payloadTooShort(1))
        {
            return;
        }
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        break;
    case funcScopeWaveData:
    {
        if (payloadTooShort(1))
        {
            return;
        }
        receiver = payloadIn.at(0);
        payloadIn.remove(0, 1);
        ScopeData d;
        if (parseSpectrum(d, receiver))
        {
            value.setValue(d);
        }
        break;
    }
    case funcScopeOnOff:
    case funcScopeDataOutput:
    case funcScopeMainSub:
    case funcScopeSingleDual:
        if (payloadTooShort(1))
        {
            return;
        }
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        break;
    case funcScopeMode:
        // Scope mode: 0x00=center, 0x01=fixed, 0x02=scroll-C, 0x03=scroll-F.
        if (payloadTooShort(2))
        {
            return;
        }
        receiver = payloadIn.at(0);
        value.setValue(static_cast<uchar>(payloadIn.at(1)));
        break;
    case funcScopeSpan:
    {
        if (payloadTooShort(1))
        {
            return;
        }
        receiver = payloadIn.at(0);
        payloadIn.remove(0, 1);
        Frequency f = parseFrequency(payloadIn, 3);
        for (auto& s : radioCaps.scopeCenterSpans)
        {
            if (s.freq == f.Hz)
            {
                value.setValue(s);
            }
        }
        break;
    }
    case funcScopeEdge:
        // Fixed edge selection: 0x01, 0x02, or 0x03.
        if (payloadTooShort(2))
        {
            return;
        }
        receiver = payloadIn.at(0);
        value.setValue(bcdHexToUChar(payloadIn.at(1)));
        break;
    case funcBandEdgeFreq:
        // Band-edge payload is currently not surfaced by SDR9700.
        break;
    case funcScopeHold:
        if (payloadTooShort(2))
        {
            return;
        }
        receiver = payloadIn.at(0);
        value.setValue(static_cast<bool>(payloadIn.at(1)));
        break;
    case funcScopeRef:
    {
        if (payloadTooShort(4))
        {
            return;
        }
        receiver = payloadIn.at(0);
        // Scope reference level: BCD dB value plus sign byte.
        quint8 negative = payloadIn.at(3);
        short ref = bcdHexToUInt(payloadIn.at(1), payloadIn.at(2));
        ref = ref / 10;
        if (negative)
        {
            ref = -ref;
        }
        value.setValue(ref);
        break;
    }
    case funcScopeSpeed:
        if (payloadTooShort(2))
        {
            return;
        }
        receiver = payloadIn.at(0);
        value.setValue(static_cast<uchar>(payloadIn.at(1)));
        break;
    case funcScopeVBW:
        if (payloadTooShort(1))
        {
            return;
        }
        receiver = payloadIn.at(0);
        break;
    case funcScopeRBW:
        if (payloadTooShort(1))
        {
            return;
        }
        receiver = payloadIn.at(0);
        break;
    case funcScopeFixedEdgeFreq:
    case funcScopeDuringTX:
    case funcScopeCenterType:
        break;
    case funcVoiceTX:
        break;
    // CI-V command 29h prefixes receiver-scoped commands.
    case funcMainSubPrefix:
        break;
    case funcPowerControl:
        qWarning(logRadio()) << "Received power control command from radio" << payloadIn;
        break;
    case funcFB:
    {
        CommandErrorType acknowledgedCommand;
        if (takePendingSetCommand(&acknowledgedCommand) && acknowledgedCommand.value.isValid() && queue != nullptr)
        {
            qDebug(logRadio()) << "Radio (FB) acknowledged set command:" << funcString[acknowledgedCommand.func];
            queue->receiveValue(acknowledgedCommand.func, acknowledgedCommand.value, acknowledgedCommand.receiver);
        }
        break;
    }
    case funcFA:
    {
        CommandErrorType rejectedCommand;
        if (takePendingSetCommand(&rejectedCommand))
        {
            qWarning(logRadio()) << "Radio rejected CI-V set command (FA):" << funcString[rejectedCommand.func]
                                 << "(min:" << rejectedCommand.minValue << "max:" << rejectedCommand.maxValue
                                 << "bytes:" << rejectedCommand.bytes << ") data:" << rejectedCommand.data.toHex(' ');
        }
        else
        {
            qWarning(logRadio()) << "Radio returned CI-V rejection (FA) with no pending set command";
        }
        break;
    }
    default:
        qWarning(logRadio()).noquote() << "Unhandled command received from radio:" << funcString[func]
                                       << "value:" << payloadIn.toHex().mid(0, 10);
        break;
    }
    if (func != funcScopeWaveData && func != funcSMeter && func != funcAbsoluteMeter && func != funcCenterMeter &&
        func != funcPowerMeter && func != funcSWRMeter && func != funcALCMeter && func != funcCompMeter &&
        func != funcVdMeter && func != funcIdMeter)
    {
        // Spectrum and meter replies are high-volume and obscure useful traffic.
        qDebug(logRadioTraffic()) << QString("Received from radio: %1").arg(funcString[func]);
        qDebug(logRadioTraffic()) << payloadIn.toHex(' ');
    }

#ifdef DEBUG_PARSE
    averageParseTime += currentParse;
    if (lowParse > currentParse)
    {
        lowParse = currentParse;
    }
    else if (highParse < currentParse)
    {
        highParse = currentParse;
    }

    numParseSamples++;
    if (lastParseReport.msecsTo(QTime::currentTime()) >= 10000)
    {
        qInfo(logRadio()) << QString("10 second average command parse time %1 ns (low=%2, high=%3, num=%4:")
                                 .arg(averageParseTime / numParseSamples)
                                 .arg(lowParse)
                                 .arg(highParse)
                                 .arg(numParseSamples);
        averageParseTime = 0;
        numParseSamples = 0;
        lowParse = 9999;
        highParse = 0;
        lastParseReport = QTime::currentTime();
    }
#endif

    if (value.isValid() && queue != nullptr)
    {
        queue->receiveValue(func, value, receiver);
    }
}

void Commander::determineRadioCaps()
{
    // Reset capability state before loading the built-in IC-9700 table.
    radioCaps.preamps.clear();
    radioCaps.attenuators.clear();
    radioCaps.inputs.clear();
    radioCaps.scopeCenterSpans.clear();
    radioCaps.bands.clear();
    radioCaps.modes.clear();
    radioCaps.commands.clear();
    radioCaps.commandsReverse.clear();
    radioCaps.antennas.clear();
    radioCaps.filters.clear();
    radioCaps.steps.clear();
    radioCaps.memParser.clear();
    radioCaps.satParser.clear();
    radioCaps.periodic.clear();
    radioCaps.roofing.clear();
    radioCaps.scopeModes.clear();

    for (int i = meterNone; i < meterUnknown; i++)
    {
        radioCaps.meters[i].clear();
        radioCaps.meterLines[i] = 0.0;
    }

    // The IC-9700 transceiver ID response normally sets modelID before this point.
    if (radioCaps.modelID != kRadioModelId)
    {
        qWarning(logRadio()) << QString("Unsupported CI-V radio ID: 0x%1. SDR9700 only supports the IC-9700.")
                                    .arg(radioCaps.modelID, 2, 16);
        return;
    }

    sdr9700::populateRadioCapabilities(radioCaps);

    qInfo(logRadio()) << QString("Loading Radio: %1 from built-in IC-9700 capabilities").arg(radioCaps.modelName);

    // Publish half-duplex capability before the queue starts normal polling.
    emit setHalfDuplex(!radioCaps.hasFDcomms);

    // Compile memory parser formats from the built-in IC-9700 table.
    static QRegularExpression memFmtEx("%(?<flags>[-+#0])?(?<pos>\\d+|\\*)?(?:\\.(?<width>\\d+|\\*))?(?<spec>["
                                       "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ+])");
    QRegularExpressionMatchIterator i = memFmtEx.globalMatch(radioCaps.memFormat);
    while (i.hasNext())
    {
        QRegularExpressionMatch qmatch = i.next();

        if (qmatch.hasCaptured("spec") && qmatch.hasCaptured("pos") && qmatch.hasCaptured("width"))
        {
            radioCaps.memParser.append(MemParserFormat(qmatch.captured("spec").at(0).toLatin1(),
                                                       qmatch.captured("pos").toInt(),
                                                       qmatch.captured("width").toInt()));
        }
    }

    QRegularExpressionMatchIterator i2 = memFmtEx.globalMatch(radioCaps.satFormat);

    while (i2.hasNext())
    {
        QRegularExpressionMatch qmatch = i2.next();
        if (qmatch.hasCaptured("spec") && qmatch.hasCaptured("pos") && qmatch.hasCaptured("width"))
        {
            radioCaps.satParser.append(MemParserFormat(qmatch.captured("spec").at(0).toLatin1(),
                                                       qmatch.captured("pos").toInt(),
                                                       qmatch.captured("width").toInt()));
        }
    }

    // Preserve the GUID reported by the selected radio.
    memcpy(radioCaps.guid, this->guid, GUIDLEN);

    haveRadioCaps = true;
    queue->setRadioCaps(&radioCaps);

    if (lookingForRadio)
    {
        lookingForRadio = false;
        foundRadio = true;

        qDebug(logRadio()) << "---Radio FOUND from broadcast query:";
        this->civAddr = incomingCIVAddr & 0xff; // Override and use immediately.
        payloadPrefix = QByteArray("\xFE\xFE");
        payloadPrefix.append((char)civAddr);
        payloadPrefix.append((char)compCivAddr);
        qInfo(logRadio()) << "Using incomingCIVAddr: (int): " << this->civAddr
                          << " hex: " << QString("0x%1").arg(this->civAddr, 0, 16);
        emit discoveredRadioID(radioCaps);
    }
    else
    {
        if (!foundRadio)
        {
            emit discoveredRadioID(radioCaps);
            foundRadio = true;
        }
        emit haveRadioID(radioCaps);
    }
}

bool Commander::parseSpectrum(ScopeData& d, uchar receiver)
{
    bool ret = false;

    if (!haveRadioCaps)
    {
        qDebug(logSpectrumScope()) << "Spectrum received in Commander, but radioID is incomplete.";
        return ret;
    }
    if (radioCaps.spectSeqMax == 0)
    {
        qInfo(logSpectrumScope()) << "Spectrum received before IC-9700 scope capabilities were ready.";
        return ret;
    }

    if (receiver)
    {
        d = subScopeData;
    }
    else
    {
        d = mainScopeData;
    }

    // Scope data may arrive as one LAN frame or as a sequence of wave-info plus
    // waveform chunks. The first bytes describe the sequence layout.

    Frequency fStart;
    Frequency fEnd;

    d.receiver = receiver;
    if (payloadIn.size() < 2)
    {
        qWarning(logSpectrumScope()) << "Ignoring short scope payload:" << payloadIn.toHex(' ');
        return false;
    }

    quint8 sequence = bcdHexToUChar(payloadIn.at(0));
    quint8 sequenceMax = bcdHexToUChar(payloadIn.at(1));

    constexpr int freqLen = 5;
    constexpr int sequenceHeaderBytes = 2;
    constexpr int waveInfoBytes = sequenceHeaderBytes + 2 + (freqLen * 2);

    if (sequenceMax <= 1)
    {
        if (payloadIn.size() < waveInfoBytes)
        {
            qWarning(logSpectrumScope()) << "Ignoring short single-frame scope payload. required" << waveInfoBytes
                                         << "got" << payloadIn.size() << "data:" << payloadIn.toHex(' ');
            return false;
        }

        d.mode = static_cast<uchar>(payloadIn.at(sequenceHeaderBytes));
        d.oor = static_cast<bool>(payloadIn.at(sequenceHeaderBytes + 1 + (freqLen * 2)));
        d.data.clear();

        fStart = parseFreqData(payloadIn.mid(sequenceHeaderBytes + 1, freqLen), receiver);
        fEnd = parseFreqData(payloadIn.mid(sequenceHeaderBytes + 1 + freqLen, freqLen), receiver);
        if (d.mode == 0)
        {
            // IC-9700 center-scope wave-info reports the center frequency and
            // the selected half-span (for example, the "+/-500 kHz" span is
            // reported as 0.500 MHz). Treating this as total width and halving
            // it makes every displayed signal appear halfway between the VFO
            // center and its real frequency, which in turn makes click-to-tune
            // walk halfway toward a signal on every click.
            const double halfSpanMhz = fEnd.MHzDouble;
            d.startFreq = fStart.MHzDouble - halfSpanMhz;
            d.endFreq = fStart.MHzDouble + halfSpanMhz;
        }
        else
        {
            d.startFreq = fStart.MHzDouble;
            d.endFreq = fEnd.MHzDouble;
        }

        if (d.oor)
        {
            d.data = QByteArray(radioCaps.spectLenMax, '\0');
            d.valid = true;
            return true;
        }

        d.data = payloadIn.mid(waveInfoBytes);
        if (d.data.size() > radioCaps.spectLenMax)
        {
            d.data.truncate(radioCaps.spectLenMax);
        }
        ret = !d.data.isEmpty();
        d.valid = ret;
        qInfo(logSpectrumScope()) << "Spectrum single-frame start:" << d.startFreq << "end:" << d.endFreq
                                  << "mode:" << d.mode << "oor:" << d.oor << "dataLen:" << d.data.size();
        return ret;
    }

    // Sequence 2, index 05 starts waveform data. Sequence 11 carries the final
    // waveform pixels seen from IC-9700 LAN scope data.
    if (sequence == 1)
    {
        const int multiFrameWaveInfoBytes = 4 + (freqLen * 2);
        if (payloadIn.size() < multiFrameWaveInfoBytes)
        {
            qWarning(logSpectrumScope()) << "Ignoring short scope wave-info payload. required"
                                         << multiFrameWaveInfoBytes << "got" << payloadIn.size()
                                         << "data:" << payloadIn.toHex(' ');
            return false;
        }

        d.mode = static_cast<uchar>(payloadIn.at(2));

        if (d.mode != oldScopeMode)
        {
            // Modes:
            // 0x00 Center
            // 0x01 Fixed
            // 0x02 Scroll-C
            // 0x03 Scroll-F
            oldScopeMode = d.mode;
        }

        d.oor = (bool)payloadIn[3 + (freqLen * 2)];
        if (d.oor)
        {
            d.data = QByteArray(radioCaps.spectLenMax, '\0');
            d.valid = true;
            return true;
        }

        d.data.clear();

        // The first two frequency fields are mode-dependent. In fixed/scroll
        // modes they are explicit start/end frequencies; in center mode they
        // are center frequency and half-span, matching the single-frame parser.
        fStart = parseFreqData(payloadIn.mid(3, freqLen), receiver);
        d.startFreq = fStart.MHzDouble;
        fEnd = parseFreqData(payloadIn.mid(3 + freqLen, freqLen), receiver);
        d.endFreq = fEnd.MHzDouble;

        if (d.mode == 0)
        {
            // In center mode the second frequency field is the half-span, not
            // the right edge or total span. See the single-frame path above.
            const double halfSpanMhz = d.endFreq;
            d.startFreq -= halfSpanMhz;
            d.endFreq = d.startFreq + (2 * halfSpanMhz);
        }

        if (sequence == sequenceMax)
        {
            d.data.append(payloadIn.right(payloadIn.length() - 4 - (freqLen * 2)));
            ret = true;
        }

        qInfo(logSpectrumScope()) << "Spectrum seq 1/" << sequenceMax << "start:" << d.startFreq << "end:" << d.endFreq
                                  << "mode:" << d.mode << "oor:" << d.oor << "payloadLen:" << payloadIn.length();
    }
    else if ((sequence > 1) && (sequence < sequenceMax))
    {
        // Intermediate scope chunks carry 50 pixels each.
        d.data.insert(d.data.length(), payloadIn.right(payloadIn.length() - 2));
        ret = false;
        qInfo(logSpectrumScope()) << "Spectrum seq" << sequence << "/" << sequenceMax << "dataAccum:" << d.data.size()
                                  << "payloadLen:" << payloadIn.length();
    }
    else if (sequence == sequenceMax)
    {
        // Final IC-9700 scope chunk carries the remaining waveform pixels.
        d.data.insert(d.data.length(), payloadIn.right(payloadIn.length() - 2));
        ret = true;
        qInfo(logSpectrumScope()) << "Spectrum seq" << sequence << "/" << sequenceMax
                                  << "(LAST) totalData:" << d.data.size() << "payloadLen:" << payloadIn.length();
    }
    d.valid = ret;

    if (!ret)
    {
        // Store partial multi-packet scope data until the final sequence arrives.
        if (receiver)
        {
            subScopeData = d;
        }
        else
        {
            mainScopeData = d;
        }
    }
    return ret;
}

quint8 Commander::bcdHexToUChar(quint8 in)
{
    quint8 out = 0;
    out = in & 0x0f;
    out += ((in & 0xf0) >> 4) * 10;
    return out;
}

unsigned int Commander::bcdHexToUInt(quint8 hundreds, quint8 tensunits)
{
    // Packed BCD: 0x41 0x23 -> 4123.
    quint8 thousands = ((hundreds & 0xf0) >> 4);
    unsigned int rtnVal;
    rtnVal = (hundreds & 0x0f) * 100;
    rtnVal += ((tensunits & 0xf0) >> 4) * 10;
    rtnVal += (tensunits & 0x0f);
    rtnVal += thousands * 1000;

    return rtnVal;
}

unsigned int Commander::bcdHexToUInt(quint8 tenthou, quint8 hundreds, quint8 tensunits)
{
    // Packed BCD: 0x65 0x43 0x21 -> 654321.
    quint8 thousands = ((hundreds & 0xf0) >> 4);
    unsigned int rtnVal;
    rtnVal = (hundreds & 0x0f) * 100;
    rtnVal += ((tensunits & 0xf0) >> 4) * 10;
    rtnVal += (tensunits & 0x0f);
    rtnVal += thousands * 1000;
    rtnVal += (tenthou & 0x0f) * 10000;
    rtnVal += ((tenthou & 0xf0) >> 4) * 100000;
    return rtnVal;
}
quint8 Commander::bcdHexToUChar(quint8 hundreds, quint8 tensunits)
{
    // Packed BCD: 0x01 0x23 -> 123. Use int to avoid truncating the
    // intermediate (hundreds & 0x0f) * 100 before the full sum is known.
    const int rtnVal = (hundreds & 0x0f) * 100 + ((tensunits & 0xf0) >> 4) * 10 + (tensunits & 0x0f);
    if (rtnVal > 255)
    {
        qWarning(logRadio()) << "bcdHexToUChar: decoded value" << rtnVal << "exceeds quint8 range; clamping";
    }
    return static_cast<quint8>(qMin(rtnVal, 255));
}

QByteArray Commander::bcdEncodeInt(quint16 num)
{
    if (num > 9999)
    {
        qInfo(logRadio()) << __FUNCTION__ << "Error, number is too big for four-digit conversion: " << num;
        return QByteArray();
    }

    char thousands = num / 1000;
    char hundreds = (num - (1000 * thousands)) / 100;
    char tens = (num - (1000 * thousands) - (100 * hundreds)) / 10;
    char units = (num - (1000 * thousands) - (100 * hundreds) - (10 * tens));

    char b0 = hundreds | (thousands << 4);
    char b1 = units | (tens << 4);

    QByteArray result;
    result.append(b0).append(b1);
    return result;
}

QByteArray Commander::bcdEncodeInt(unsigned int num)
{
    if (num > 999999)
    {
        qInfo(logRadio()) << __FUNCTION__ << "Error, number is too big for six-digit conversion: " << num;
        return QByteArray();
    }

    char tenthou = num / 10000;
    char thousands = (num - (10000 * tenthou)) / 1000;
    char hundreds = (num - (10000 * tenthou) - (1000 * thousands)) / 100;
    char tens = (num - (10000 * tenthou) - (1000 * thousands) - (100 * hundreds)) / 10;
    char units = (num - (10000 * tenthou) - (1000 * thousands) - (100 * hundreds) - (10 * tens));

    char b0 = static_cast<char>(((tenthou / 10) << 4) | (tenthou % 10));
    char b1 = hundreds | (thousands << 4);
    char b2 = units | (tens << 4);

    QByteArray result;
    result.append(b0).append(b1).append(b2);
    qInfo(logRadio()) << __FUNCTION__ << " encoding value " << num << " as hex:" << result.toHex(' ');

    return result;
}
QByteArray Commander::bcdEncodeChar(quint8 num)
{
    if (num > 99)
    {
        qInfo(logRadio()) << __FUNCTION__ << "Error, number is too big for two-digit conversion: " << num;
        return QByteArray();
    }

    uchar tens = num / 10;
    uchar units = num - (10 * tens);

    uchar b0 = units | (tens << 4);

    QByteArray result;
    result.append(b0);
    return result;
}

Frequency Commander::parseFrequency()
{
    Frequency freq;
    freq.Hz = 0;
    freq.MHzDouble = 0;

    // Minimum meaningful payload is 5 bytes (indices 1-4, octal literals).
    if (payloadIn.length() < 5)
    {
        qWarning(logRadio()) << "parseFrequency(): payload too short:" << payloadIn.length();
        return freq;
    }

    frequencyMhz = 0.0;
    if (payloadIn.length() == 7)
    {
        // IC-9700 can report 100 MHz and 1 GHz digits.
        frequencyMhz += 100 * (payloadIn[05] & 0x0f);
        frequencyMhz += (1000 * ((payloadIn[05] & 0xf0) >> 4));

        freq.Hz += (payloadIn[05] & 0x0f) * 1E6 * 100;
        freq.Hz += ((payloadIn[05] & 0xf0) >> 4) * 1E6 * 1000;
    }

    freq.Hz += (payloadIn[04] & 0x0f) * 1E6;
    freq.Hz += ((payloadIn[04] & 0xf0) >> 4) * 1E6 * 10;

    frequencyMhz += payloadIn[04] & 0x0f;
    frequencyMhz += 10 * ((payloadIn[04] & 0xf0) >> 4);

    frequencyMhz += ((payloadIn[03] & 0xf0) >> 4) / 10.0;
    frequencyMhz += (payloadIn[03] & 0x0f) / 100.0;

    frequencyMhz += ((payloadIn[02] & 0xf0) >> 4) / 1000.0;
    frequencyMhz += (payloadIn[02] & 0x0f) / 10000.0;

    frequencyMhz += ((payloadIn[01] & 0xf0) >> 4) / 100000.0;
    frequencyMhz += (payloadIn[01] & 0x0f) / 1000000.0;

    freq.Hz += payloadIn[01] & 0x0f;
    freq.Hz += ((payloadIn[01] & 0xf0) >> 4) * 10;

    freq.Hz += (payloadIn[02] & 0x0f) * 100;
    freq.Hz += ((payloadIn[02] & 0xf0) >> 4) * 1000;

    freq.Hz += (payloadIn[03] & 0x0f) * 10000;
    freq.Hz += ((payloadIn[03] & 0xf0) >> 4) * 100000;

    freq.MHzDouble = frequencyMhz;

    return freq;
}

Frequency Commander::parseFrequencyRptOffset(QByteArray data)
{
    Frequency f;
    f.Hz = 0;
    f.MHzDouble = 0.0;
    f.VFO = activeVFO;

    if (data.size() < 3)
    {
        qWarning(logRadio()) << "Repeater offset response too short:" << data.toHex(' ');
        return f;
    }

    f.Hz += (data[2] & 0x0f) * 1E6;             // 1 MHz
    f.Hz += ((data[2] & 0xf0) >> 4) * 1E6 * 10; // 10 MHz
    f.Hz += (data[1] & 0x0f) * 10E3;            // 10 KHz
    f.Hz += ((data[1] & 0xf0) >> 4) * 100E3;    // 100 KHz
    f.Hz += (data[0] & 0x0f) * 100;             // 100 Hz
    f.Hz += ((data[0] & 0xf0) >> 4) * 1000;     // 1 KHz

    f.MHzDouble = f.Hz / 1E6;
    return f;
}

Frequency Commander::parseFrequency(QByteArray data, quint8 lastPosition)
{
    // IC-9700 frequencies can reach 1240 MHz, so 100 MHz and 1 GHz digits occupy position +1.

    Frequency freqs;
    freqs.MHzDouble = 0;
    freqs.Hz = 0;

    if (data.length() <= lastPosition)
    {
        qWarning(logRadio()) << "parseFrequency() given last position:" << lastPosition << "but data is only"
                             << data.length() << "bytes";
        return freqs;
    }
    // Optional high-frequency bytes carry 100 MHz and GHz digits.
    if (data.length() > lastPosition + 2)
    {
        freqs.Hz += (data[lastPosition + 2] & 0x0f) * 1E9;             //  1 GHz
        freqs.Hz += ((data[lastPosition + 2] & 0xf0) >> 4) * 1E9 * 10; // 10 GHz
    }
    if (data.length() > lastPosition + 1)
    {
        freqs.Hz += (data[lastPosition + 1] & 0x0f) * 1E6 * 100;         //  100 MHz
        freqs.Hz += ((data[lastPosition + 1] & 0xf0) >> 4) * 1E6 * 1000; // 1000 MHz
    }

    // CI-V command 25h prepends a VFO byte before the frequency payload.
    if (lastPosition - 4 >= 0 && (quint8)data[lastPosition - 4] < 0x02)
    {
        freqs.VFO = (selVFO_t)(quint8)data[lastPosition - 4];
    }

    freqs.Hz += (data[lastPosition] & 0x0f) * 1E6;
    freqs.Hz += ((data[lastPosition] & 0xf0) >> 4) * 1E6 * 10;  //   10 MHz

    freqs.Hz += (data[lastPosition - 1] & 0x0f) * 10E3;         // 10 KHz
    freqs.Hz += ((data[lastPosition - 1] & 0xf0) >> 4) * 100E3; // 100 KHz

    freqs.Hz += (data[lastPosition - 2] & 0x0f) * 100;          // 100 Hz
    freqs.Hz += ((data[lastPosition - 2] & 0xf0) >> 4) * 1000;  // 1 KHz

    freqs.Hz += (data[lastPosition - 3] & 0x0f) * 1;            // 1 Hz
    freqs.Hz += ((data[lastPosition - 3] & 0xf0) >> 4) * 10;    // 10 Hz

    freqs.MHzDouble = (double)(freqs.Hz / 1000000.0);
    return freqs;
}

Frequency Commander::parseFreqData(const QByteArray& data, uchar receiver)
{
    Frequency freq;
    freq.Hz = parseFreqDataToInt(data);
    freq.MHzDouble = freq.Hz / 1000000.0;
    freq.VFO = selVFO_t(receiver);
    return freq;
}

quint64 Commander::parseFreqDataToInt(QByteArray data)
{
    // Parse packed BCD frequency bytes with a lookup table.
    quint64 val = 0;

    Q_ASSERT(data.size() * 2 < static_cast<int>(std::size(kPow10)));
    for (int i = 0; i < data.size() * 2; i = i + 2)
    {
        val += (data[i / 2] & 0x0f) * kPow10[i];
        val += ((data[i / 2] & 0xf0) >> 4) * kPow10[i + 1];
    }

    return val;
}

ModeInfo Commander::parseMode(uchar mode, uchar data, uchar filter, uchar receiver, uchar vfo)
{
    ModeInfo mi;
    bool found = false;
    if (mode == 0xff)
    {
        mi.reg = mode;
        mi.mk = modeUnknown;
        mi.filter = filter;
        mi.data = data;
        found = true;
    }
    else
    {
        const auto modeIt = std::find_if(radioCaps.modes.cbegin(), radioCaps.modes.cend(),
                                         [mode](const ModeInfo& candidate) { return candidate.reg == mode; });
        if (modeIt != radioCaps.modes.cend())
        {
            mi = *modeIt;
            mi.filter = filter;
            mi.data = data;
            found = true;
        }
    }

    if (!found)
    {
        qInfo(logRadio()) << QString("parseMode() No such mode %1 with filter %2").arg(mode).arg(filter)
                          << payloadIn.toHex(' ');
    }

    // When CI-V 29h is unavailable, only the active receiver's filter width is queryable.
    if (!radioCaps.hasCommand29)
    {
        receiver = 0;
    }

    CacheItem item;

    // Use cached filter width when the current IC-9700 mode exposes one.
    if (vfo == 0 && mi.bwMin > 0 && mi.bwMax > 0)
    {
        item = queue->getCache(funcFilterWidth, receiver);
    }

    if (item.value.isValid())
    {
        mi.pass = item.value.toInt();
    }
    else
    {
        // Use IC-9700 default filter widths until the radio reports a value.
        if (mi.mk == modeCW || mi.mk == modeCW_R || mi.mk == modePSK || mi.mk == modePSK_R)
        {
            switch (filter)
            {
            case 1:
                mi.pass = 1200;
                break;
            case 2:
                mi.pass = 500;
                break;
            case 3:
                mi.pass = 250;
                break;
            }
        }
        else if (mi.mk == modeRTTY || mi.mk == modeRTTY_R)
        {
            switch (filter)
            {
            case 1:
                mi.pass = 2400;
                break;
            case 2:
                mi.pass = 500;
                break;
            case 3:
                mi.pass = 250;
                break;
            }
        }
        else if (mi.mk == modeAM)
        {
            switch (filter)
            {
            case 1:
                mi.pass = 9000;
                break;
            case 2:
                mi.pass = 6000;
                break;
            case 3:
                mi.pass = 3000;
                break;
            }
        }
        else if (mi.mk == modeFM)
        {
            switch (filter)
            {
            case 1:
                mi.pass = 15000;
                break;
            case 2:
                mi.pass = 10000;
                break;
            case 3:
                mi.pass = 7000;
                break;
            }
        }
        else if (mi.mk == modeWFM)
        {
            mi.pass = 200000;
        }
        else
        {
            switch (filter)
            {
            case 1:
                mi.pass = 3000;
                break;
            case 2:
                mi.pass = 2400;
                break;
            case 3:
                mi.pass = 1800;
                break;
            }
        }
    }

    return mi;
}

bool Commander::parseMemory(QVector<MemParserFormat>* memParser, MemoryType* mem)
{
    // Initialize optional fields before applying the parsed memory payload.
    mem->frequency.Hz = 0;
    mem->frequency.VFO = activeVFO;
    mem->frequency.MHzDouble = 0.0;
    mem->frequencyB = mem->frequency;
    mem->duplexOffset = mem->frequency;
    mem->duplexOffsetB = mem->frequency;
    mem->scan = 0xfe;
    memset(mem->UR, 0x0, sizeof(mem->UR));
    memset(mem->URB, 0x0, sizeof(mem->URB));
    memset(mem->R1, 0x0, sizeof(mem->R1));
    memset(mem->R1B, 0x0, sizeof(mem->R1B));
    memset(mem->R2, 0x0, sizeof(mem->R2));
    memset(mem->R2B, 0x0, sizeof(mem->R2B));
    memset(mem->name, 0x0, sizeof(mem->name));
    // Memory parser positions are one-based and include the command prefix.
    payloadIn.insert(0, "**");
    for (auto& parse : *memParser)
    {
        // Empty radio memory records are short; return the fields parsed so far.
        if (payloadIn.size() < (parse.pos + 1 + parse.len) && parse.spec != 'Z')
        {
            return true;
        }
        QByteArray data = payloadIn.mid(parse.pos + 1, parse.len);
        switch (parse.spec)
        {
        case 'a':
            if (parse.len == 1)
            {
                mem->group = bcdHexToUChar(data[0]);
            }
            else
            {
                mem->group = bcdHexToUChar(data[0], data[1]);
            }
            break;
        case 'b':
            mem->channel = bcdHexToUChar(data[0], data[1]);
            break;
        case 'c':
            mem->scan = data[0];
            break;
        case 'C':
            mem->skip = data[0] >> 4 & 0xf;
            mem->scan = data[0] & 0xf;
            break;
        case 'd': // combined split and scan
            mem->split = quint8(data[0] >> 4 & 0x0f);
            mem->scan = quint8(data[0] & 0x0f);
            break;
        case 'D': // duplex only
            mem->duplex = quint8(data[0] & 0x0f);
            break;
        case 'e':
            mem->vfo = data[0];
            break;
        case 'E':
            mem->vfoB = data[0];
            break;
        case 'f':
            mem->frequency.Hz = parseFreqDataToInt(data);
            break;
        case 'F':
            mem->frequencyB.Hz = parseFreqDataToInt(data);
            break;
        case 'g':
            mem->mode = bcdHexToUChar(data[0]);
            break;
        case 'G':
            mem->modeB = bcdHexToUChar(data[0]);
            break;
        case 'h':
            mem->filter = bcdHexToUChar(data[0]);
            break;
        case 'H':
            mem->filterB = bcdHexToUChar(data[0]);
            break;
        case 'i': // single datamode
            mem->datamode = bcdHexToUChar(data[0]);
            break;
        case 'I': // single datamode
            mem->datamodeB = bcdHexToUChar(data[0]);
            break;
        case 'j': // combined duplex and tonemode
            mem->duplex = duplexMode_t(quint8(data[0] >> 4 & 0x0f));
            mem->tonemode = quint8(quint8(data[0] & 0x0f));
            break;
        case 'J': // combined duplex and tonemodeB
            mem->duplexB = duplexMode_t((data[0] >> 4 & 0x0f));
            mem->tonemodeB = data[0] & 0x0f;
            break;
        case 'k': // combined datamode and tonemode
            mem->datamode = (quint8(data[0] >> 4 & 0x0f));
            mem->tonemode = data[0] & 0x0f;
            break;
        case 'K': // combined datamode and tonemode
            mem->datamodeB = (quint8(data[0] >> 4 & 0x0f));
            mem->tonemodeB = data[0] & 0x0f;
            break;
        case 'l': // tonemode
            mem->tonemode = data[0] & 0x0f;
            break;
        case 'L': // tonemode
            mem->tonemodeB = data[0] & 0x0f;
            break;
        case 'm':
            mem->dsql = (quint8(data[0] >> 4 & 0x0f));
            break;
        case 'M':
            mem->dsqlB = (quint8(data[0] >> 4 & 0x0f));
            break;
        case 'n':
            for (const auto& tn : radioCaps.ctcss)
            {
                if (tn.tone == bcdHexToUInt(data[1], data[2]))
                {
                    mem->tone = tn.name;
                }
            }
            break;
        case 'N':
            for (const auto& tn : radioCaps.ctcss)
            {
                if (tn.tone == bcdHexToUInt(data[1], data[2]))
                {
                    mem->toneB = tn.name;
                }
            }
            break;
        case 'o':
            for (const auto& tn : radioCaps.ctcss)
            {
                if (tn.tone == bcdHexToUInt(data[1], data[2]))
                {
                    mem->tsql = tn.name;
                }
            }
            break;
        case 'O':
            for (const auto& tn : radioCaps.ctcss)
            {
                if (tn.tone == bcdHexToUInt(data[1], data[2]))
                {
                    mem->tsqlB = tn.name;
                }
            }
            break;
        case 'p':
            mem->dtcsp = (quint8(data[0] >> 3 & 0x02) | quint8(data[0] & 0x01));
            break;
        case 'P':
            mem->dtcspB = (quint8(data[0] >> 3 & 0x10) | quint8(data[0] & 0x01));
            break;
        case 'q':
            mem->dtcs = bcdHexToUInt(data[0], data[1]);
            break;
        case 'Q':
            mem->dtcsB = bcdHexToUInt(data[0], data[1]);
            break;
        case 'r':
            mem->dvsql = bcdHexToUChar(data[0]);
            break;
        case 'R':
            mem->dvsqlB = bcdHexToUChar(data[0]);
            break;
        case 's':
            mem->duplexOffset.Hz = parseFreqDataToInt(data);
            break;
        case 'S':
            mem->duplexOffsetB.Hz = parseFreqDataToInt(data);
            break;
        case 't':
            memcpy(mem->UR, data.data(), qMin(int(sizeof mem->UR), data.size()));
            break;
        case 'T':
            memcpy(mem->URB, data.data(), qMin(int(sizeof mem->URB), data.size()));
            break;
        case 'u':
            memcpy(mem->R1, data.data(), qMin(int(sizeof mem->R1), data.size()));
            break;
        case 'U':
            memcpy(mem->R1B, data.data(), qMin(int(sizeof mem->R1B), data.size()));
            break;
        case 'v':
            memcpy(mem->R2, data.data(), qMin(int(sizeof mem->R2), data.size()));
            break;
        case 'V':
            memcpy(mem->R2B, data.data(), qMin(int(sizeof mem->R2B), data.size()));
            break;
        case 'w': // Tuning step
            if (bool(data[0]))
            {
                mem->tuningStep = bcdHexToUChar(data[1]);
                mem->progTs = bcdHexToUInt(data[2], data[3]);
            }
            else
            {
                mem->tuningStep = 0;
                mem->progTs = 5;
            }
            break;
        case 'x': // Attenuator & Preamp
            mem->atten = bcdHexToUChar(data[0]);
            mem->preamp = bcdHexToUChar(data[1]);
            break;
        case 'y': // Antenna
            mem->antenna = bcdHexToUChar(data[0]);
            break;
        case '+': // IP Plus
            mem->ipplus = bool(data[0] & 0x0f);
            break;
        case 'z':
            if (mem->scan == 0xfe)
            {
                mem->scan = 0;
            }
            memcpy(mem->name, data.data(), qMin(int(sizeof mem->name), data.size()));
            break;
        case 'Z': // Mode-dependent extension block.
            for (const auto& m : radioCaps.modes)
            {
                if (m.reg == mem->mode)
                {
                    switch (m.mk)
                    {
                    case modeFM:
                        if (data.size() < 7)
                        {
                            return true;
                        }
                        mem->tonemode = data[0] & 0x0f;
                        for (const auto& tn : radioCaps.ctcss)
                        {
                            if (tn.tone == bcdHexToUInt(data[2], data[3]))
                            {
                                mem->tsql = tn.name;
                            }
                        }
                        mem->dtcsp = quint8(data[4] & 0x0f);
                        mem->dtcs = bcdHexToUInt(data[5], data[6]);
                        break;
                    case modeDV:
                        if (data.size() < 2)
                        {
                            return true;
                        }
                        mem->dsql = (quint8(data[0] & 0x0f));
                        mem->dvsql = bcdHexToUChar(data[1]);
                        break;
                    default:
                        break;
                    }
                    break;
                }
            }

            break;
        default:
            qInfo(logRadio()) << "Parser didn't match!" << "spec:" << parse.spec << "pos:" << parse.pos << "len"
                              << parse.len;
            break;
        }
    }

    return true;
}

void Commander::setRadioID(quint16 radioID)
{
    // Used by tests and manual diagnostics to force the IC-9700 capability path.

    qInfo(logRadio()).noquote() << QString("Setting radio ID to: 0x%1").arg(radioID & 0xff, 1, 16);

    lookingForRadio = true;
    foundRadio = false;

    // A forced ID has no incoming frame; use the configured CI-V address.
    this->incomingCIVAddr = this->civAddr & 0xff;

    if (radioID == kRadioModelId)
    {
        this->model = radioID & 0xff;
    }
    radioCaps.modelID = radioID & 0xff;
    radioCaps.model = this->model;
    determineRadioCaps();
}

uchar Commander::makeFilterWidth(ushort width, uchar receiver)
{
    quint8 calc;
    VfoCommandType t = queue->getVfoCommand(vfoA, receiver, false);
    ModeInfo mi = queue->getCache(t.modeFunc, receiver).value.value<ModeInfo>();

    if (mi.mk == modeAM)
    {
        calc = width < 200 ? 0 : quint16((width / 200) - 1);
        if (calc > 49)
        {
            calc = 49;
        }
    }
    else if (width >= 600) // SSB/CW/PSK 10-40 (10-31 for RTTY)
    {
        calc = quint16((width / 100) + 4);
        if (((calc > 31) && (mi.mk == modeRTTY || mi.mk == modeRTTY_R)))
        {
            calc = 31;
        }
        else if (calc > 40)
        {
            calc = 40;
        }
    }
    else // SSB etc 0-9
    {
        calc = quint16((width / 50) - 1);
    }

    char tens = (calc / 10);
    char units = (calc - (10 * tens));

    char b1 = (units) | (tens << 4);

    return b1;
}

unsigned char Commander::convertNumberToHex(unsigned char num)
{
    // BCD-encode a two-digit decimal value.
    if (num > 99)
    {
        qInfo(logRadio()) << "Invalid numeric conversion from num " << num << " to hex.";
        return 0xFA;
    }
    unsigned char result = 0;
    result = (num / 10) << 4;
    result |= (num - 10 * (num / 10));
    return result;
}

void Commander::enableAudio()
{
    emit requestEnableAudio();
}

void Commander::setPttActive(bool active)
{
    if (udp == nullptr)
    {
        return;
    }

    const Qt::ConnectionType connectionType =
        QThread::currentThread() == udp->thread() ? Qt::DirectConnection : Qt::BlockingQueuedConnection;
    QMetaObject::invokeMethod(udp, "setPttActive", connectionType, Q_ARG(bool, active));
}

void Commander::sendDtmfPcm(const QByteArray& pcm)
{
    if (udp == nullptr)
    {
        return;
    }
    QMetaObject::invokeMethod(udp, "queueDtmfPcm", Qt::QueuedConnection, Q_ARG(QByteArray, pcm));
}

void Commander::readCurrentFrequencyAndMode()
{
    // Startup readiness needs the simplest IC-9700 current-VFO readback. The
    // normal routing layer may prefer selected-VFO commands 25/26, but field
    // logs showed the radio streaming scope data while not replying to those
    // selected-VFO probes during LAN startup. Raw 03/04 replies still parse
    // through the same cache/router path, so this bypass stays limited to
    // connection sync and can be backed out cleanly if later captures differ.
    QByteArray frequencyCommand;
    frequencyCommand.append(char(0x03));
    rememberPendingReply(funcFreqGet, 0);
    prepDataAndSend(frequencyCommand);

    QByteArray modeCommand;
    modeCommand.append(char(0x04));
    rememberPendingReply(funcModeGet, 0);
    prepDataAndSend(modeCommand);
}

void Commander::receiveCommand(Funcs func, QVariant value, uchar receiver)
{
    int val = INT_MIN;
    if (value.isValid() && value.canConvert<int>() && func != funcSendCW)
    {
        // Integer values can be range-checked against the capability table.
        val = value.value<int>();
        if (func == funcMemoryContents || func == funcMemoryClear || func == funcMemoryWrite || func == funcMemoryMode)
        {
            // Strip memory group bits before range-checking the channel number.
            qDebug(logRadio()) << "Memory Command" << funcString[func] << "with valuetype "
                               << QString(value.typeName());
            val = val & 0xffff;
        }
    }

    if (func == funcAfGain && value.isValid() && udp != nullptr && receiver == 0xff)
    {
        // AF gain maps to local Qt audio volume while connected over LAN.
        emit haveSetVolume(static_cast<uchar>(value.toInt()));
        queue->receiveValue(func, value, false);
        return;
    }

    if (func == funcSelectVFO)
    {
        vfo_t v = value.value<vfo_t>();
        queue->recordLocalRoutingState(func, value, receiver);
        func = (v == vfoA)      ? funcVFOASelect
               : (v == vfoB)    ? funcVFOBSelect
               : (v == vfoMain) ? funcVFOMainSelect
               : (v == vfoSub)  ? funcVFOSubSelect
               : (v == vfoMem)  ? funcMemoryMode
                                : funcNone;
        value.clear();
        val = INT_MIN;
    }

    QByteArray payload;
    FuncType cmd;
    cmd = getCommand(func, payload, val, receiver);
    if (cmd.cmd != funcNone)
    {
        // Receiver-scoped commands carry the receiver byte before command data.
        switch (cmd.cmd)
        {
        case funcFreq:
        case funcMode:
        case funcScopeMode:
        case funcScopeSpan:
        case funcScopeRef:
        case funcScopeHold:
        case funcScopeSpeed:
        case funcScopeRBW:
        case funcScopeVBW:
        case funcScopeCenterType:
        case funcScopeEdge:
            payload.append(receiver);
            break;
        default:
            break;
        }

        if (value.isValid())
        {

            if (!cmd.setCmd)
            {
                qDebug(logRadio()) << "Removing unsupported set command from queue" << funcString[func] << "VFO"
                                   << receiver;
                queue->del(func, receiver);
                return;
            }

            if (!isRadioAdmin && cmd.admin)
            {
                qWarning(logRadio()) << "Admin permission required for set command" << funcString[func]
                                     << "access denied";
                return;
            }

            if (m_suppressReadbackForCurrentCommand)
            {
                // The caller is building a receiver-scoped command sequence and
                // will issue any required readback before restoring MAIN/SUB.
            }
            else if (func == funcFreqSet)
            {
                queue->addUnique(kPriorityImmediate, funcFreqGet, false, receiver);
            }
            else if (func == funcModeSet)
            {
                queue->addUnique(kPriorityImmediate, funcModeGet, false, receiver);
            }
            else if (cmd.getCmd && func != funcScopeFixedEdgeFreq && func != funcSpeech && func != funcMemoryContents &&
                     func != funcSatelliteMemory && func != funcBandStackReg && func != funcSendCW)
            {
                // Confirm radio-backed state by querying after the set command.
                queue->addUnique(kPriorityImmediate, func, false, receiver);
            }

            const int ValueType = value.userType();
            const auto valueHolds = [ValueType](int type) { return ValueType == type; };

            if (valueHolds(qMetaTypeId<bool>()))
            {
                payload.append(value.value<bool>());
            }
            else if (valueHolds(qMetaTypeId<QString>()))
            {
                QString text = value.value<QString>();
                if (func == funcSendCW)
                {
                    QByteArray textData = text.toLatin1();
                    qDebug(logRadio()) << "CW input:" << textData;
                    for (int c = 0; c < textData.length(); c++)
                    {
                        const quint8 p = textData.at(c);
                        if (((p >= 0x30) && (p <= 0x39)) || ((p >= 0x41) && (p <= 0x5A)) ||
                            ((p >= 0x61) && (p <= 0x7A)) || (p == 0x2F) || (p == 0x3F) || (p == 0x2E) || (p == 0x2D) ||
                            (p == 0x2C) || (p == 0x3A) || (p == 0x27) || (p == 0x28) || (p == 0x29) || (p == 0x3D) ||
                            (p == 0x2B) || (p == 0x22) || (p == 0x40) || (p == 0x20))
                        {
                            // Allowed CW character.
                        }
                        else
                        {
                            qWarning(logRadio()) << "Invalid character detected in CW message at position " << c
                                                 << ", the character is " << text.at(c);
                            textData[c] = 0x3F; // "?"
                        }
                    }
                    if (textData.isEmpty())
                    {
                        emit stopsidetone();
                        payload.append(uchar(0xff));
                    }
                    else
                    {
                        emit sidetone(QString(textData));
                        payload.append(textData);
                        qDebug(logRadio()) << "CW output::" << textData;
                    }
                    qDebug(logRadio()) << "Sending CW: payload:" << payload.toHex(' ');
                }
            }
            else if (valueHolds(qMetaTypeId<uchar>()))
            {
                if (func == funcRoofingFilter || func == funcFilterShape)
                {
                    // The IC-9700 only sets shape for the active filter.
                    payload.append(bcdEncodeChar(value.value<uchar>() % 10));
                }
                else
                {
                    payload.append(bcdEncodeChar(value.value<uchar>()));
                }
            }
            else if (valueHolds(qMetaTypeId<ushort>()))
            {
                if (func == funcFilterWidth)
                {
                    payload.append(makeFilterWidth(value.value<ushort>(), receiver));
                }
                else if (func == funcKeySpeed)
                {
                    ushort wpm = round((value.value<ushort>() - 6) * (6.071));
                    qDebug(logRadio()) << "Sending key speed orig:" << value.value<ushort>() << "sent:" << wpm;
                    payload.append(bcdEncodeInt(wpm));
                }
                else if (func == funcCwPitch)
                {
                    ushort pitch = 0;
                    pitch = ceil((value.value<ushort>() - 300) * (255.0 / 600.0));
                    payload.append(bcdEncodeInt(pitch));
                }
                else
                {
                    payload.append(bcdEncodeInt(value.value<ushort>()));
                }
            }
            else if (valueHolds(qMetaTypeId<short>()) && func == funcRitFreq)
            {
                // RIT/XIT offset payload.
                bool isNegative = false;
                short ritValue = value.value<short>();
                qDebug(logRadio()) << "Setting RIT to " << ritValue;
                if (ritValue < 0)
                {
                    isNegative = true;
                    ritValue *= -1;
                }
                Frequency f;
                QByteArray freqBytes;
                f.Hz = ritValue;
                freqBytes = makeFreqPayload(f);
                freqBytes.truncate(2);
                payload.append(freqBytes);
                payload.append(QByteArray(1, (char)isNegative));
            }
            else if (valueHolds(qMetaTypeId<uint>()) && (func == funcMemoryContents || func == funcMemoryMode))
            {
                qDebug(logRadio()) << "Get Memory Contents" << (value.value<uint>() & 0xffff);
                qDebug(logRadio()) << "Get Memory Group (if exists)" << (value.value<uint>() >> 16 & 0xffff);
                if (func == funcMemoryContents)
                {
                    const auto groupFormat =
                        std::find_if(radioCaps.memParser.cbegin(), radioCaps.memParser.cend(),
                                     [](const MemParserFormat& parse) { return parse.spec == 'a'; });
                    if (groupFormat != radioCaps.memParser.cend())
                    {
                        // Include memory group when the IC-9700 memory format uses it.
                        if (groupFormat->len == 1)
                        {
                            payload.append(bcdEncodeChar(quint16(value.value<uint>() >> 16 & 0xff)));
                        }
                        else if (groupFormat->len == 2)
                        {
                            payload.append(bcdEncodeInt(quint16(value.value<uint>() >> 16 & 0xffff)));
                        }
                    }
                }
                payload.append(bcdEncodeInt(quint16(value.value<uint>() & 0xffff)));
            }
            else if (valueHolds(qMetaTypeId<MemoryType>()))
            {
                // Build the memory payload from the compiled IC-9700 memory format.
                bool finished = false;
                char nul = 0x0;
                uchar ffchar = 0xff;
                QVector<MemParserFormat> parser;
                MemoryType mem = value.value<MemoryType>();
                if (mem.sat)
                {
                    parser = radioCaps.satParser;
                }
                else
                {
                    parser = radioCaps.memParser;
                }
                const auto appendToneByName = [this, &payload](const QString& name)
                {
                    const auto tone = std::find_if(radioCaps.ctcss.cbegin(), radioCaps.ctcss.cend(),
                                                   [&name](const ToneInfo& info) { return info.name == name; });
                    if (tone != radioCaps.ctcss.cend())
                    {
                        payload.append(bcdEncodeInt(tone->tone));
                    }
                };

                // Memory format legend for the compiled IC-9700 definition:
                // a/b identify group/channel, f/F carry RX/TX frequency, g/G
                // carry mode, h/H filter, i/I data mode, j/k/l tone access,
                // n/o tone frequencies, p/q DTCS polarity/code, s/S repeater
                // offset, t/u/v D-STAR callsigns, z name, and Z is an
                // IC-9700 mode-specific extension block. Deletion records are
                // intentionally short: once a delete marker is written, the
                // loop stops so the radio receives an empty memory record for
                // that slot instead of a partially populated one.
                for (auto& parse : parser)
                {
                    switch (parse.spec)
                    {
                    case 'a':
                        if (parse.len == 1)
                        {
                            payload.append(mem.group);
                        }
                        else if (parse.len == 2)
                        {
                            payload.append(bcdEncodeInt(mem.group));
                        }
                        break;
                    case 'b':
                        payload.append(bcdEncodeInt(mem.channel));
                        break;
                    case 'c':
                        // Empty record marker for memory deletion.
                        if (mem.del)
                        {
                            payload.append(ffchar);
                            finished = true;
                            break;
                        }
                        else
                        {
                            payload.append(mem.scan);
                        }
                        break;
                    case 'C':
                        // Empty record marker for memory deletion.
                        if (mem.del)
                        {
                            payload.append(ffchar);
                            finished = true;
                            break;
                        }
                        else
                        {
                            payload.append(mem.scan);
                        }
                        break;
                    case 'd': // combined split and scan
                        if (mem.del)
                        {
                            payload.append(ffchar);
                            finished = true;
                            break;
                        }
                        else
                        {
                            payload.append(quint8((mem.split << 4 & 0xf0) | (mem.scan & 0x0f)));
                        }
                        break;
                    case 'D': // Duplex only
                        payload.append(mem.duplex);
                        break;
                    case 'e':
                        payload.append(mem.vfo);
                        break;
                    case 'E':
                        payload.append(mem.vfoB);
                        break;
                    case 'f':
                        if (mem.del)
                        {
                            qDebug(logRadio()) << "Pre deleting f" << payload.toHex(' ');
                            payload.append(ffchar);
                            qDebug(logRadio()) << "Deleting f" << payload.toHex(' ');
                            finished = true;
                            break;
                        }
                        else
                        {
                            // Pad to the IC-9700 memory field width.
                            QByteArray f = makeFreqPayload(mem.frequency);
                            for (int i = f.size(); i < parse.len; i++)
                            {
                                f.append(nul);
                            }
                            payload.append(f);
                        }
                        break;
                    case 'F':
                    {
                        QByteArray f = makeFreqPayload(mem.frequencyB);
                        for (int i = f.size(); i < parse.len; i++)
                        {
                            f.append(nul);
                        }
                        payload.append(f);
                        break;
                    }
                    case 'g':
                        payload.append(bcdEncodeChar(mem.mode));
                        break;
                    case 'G':
                        payload.append(bcdEncodeChar(mem.modeB));
                        break;
                    case 'h':
                        payload.append(bcdEncodeChar(mem.filter));
                        break;
                    case 'H':
                        payload.append(bcdEncodeChar(mem.filterB));
                        break;
                    case 'i': // single datamode
                        payload.append(bcdEncodeChar(mem.datamode));
                        break;
                    case 'I':
                        payload.append(bcdEncodeChar(mem.datamodeB));
                        break;
                    case 'j': // combined duplex and tonemode
                        payload.append((mem.duplex << 4) | mem.tonemode);
                        break;
                    case 'J': // combined duplex and tonemode
                        payload.append((mem.duplexB << 4) | mem.tonemodeB);
                        break;
                    case 'k': // combined datamode and tonemode
                        payload.append((mem.datamode << 4 & 0xf0) | (mem.tonemode & 0x0f));
                        break;
                    case 'K': // combined datamode and tonemode
                        payload.append((mem.datamodeB << 4 & 0xf0) | (mem.tonemodeB & 0x0f));
                        break;
                    case 'l': // tonemode
                        payload.append(mem.tonemode);
                        break;
                    case 'L':
                        payload.append(mem.tonemodeB);
                        break;
                    case 'm':
                        payload.append(mem.dsql << 4);
                        break;
                    case 'M':
                        payload.append(mem.dsqlB << 4);
                        break;
                    case 'n':
                        payload.append(nul);
                        appendToneByName(mem.tone);
                        break;
                    case 'N':
                        payload.append(nul);
                        appendToneByName(mem.toneB);
                        break;
                    case 'o':
                        payload.append(nul);
                        appendToneByName(mem.tsql);
                        break;
                    case 'O':
                        payload.append(nul);
                        appendToneByName(mem.tsqlB);
                        break;
                    case 'p':
                        payload.append((mem.dtcsp << 3 & 0x10) | (mem.dtcsp & 0x01));
                        break;
                    case 'P':
                        payload.append((mem.dtcspB << 3 & 0x10) | (mem.dtcspB & 0x01));
                        break;
                    case 'q':
                        payload.append(bcdEncodeInt(mem.dtcs));
                        break;
                    case 'Q':
                        payload.append(bcdEncodeInt(mem.dtcsB));
                        break;
                    case 'r':
                        payload.append(bcdEncodeChar(mem.dvsql));
                        break;
                    case 'R':
                        payload.append(bcdEncodeChar(mem.dvsqlB));
                        break;
                    case 's':
                        payload.append(makeFreqPayload(mem.duplexOffset).mid(1, parse.len));
                        break;
                    case 'S':
                        payload.append(makeFreqPayload(mem.duplexOffsetB).mid(1, parse.len));
                        break;
                    case 't':
                        payload.append(QByteArray(mem.UR).leftJustified(parse.len, ' ', true));
                        break;
                    case 'T':
                        payload.append(QByteArray(mem.URB).leftJustified(parse.len, ' ', true));
                        break;
                    case 'u':
                        payload.append(QByteArray(mem.R1).leftJustified(parse.len, ' ', true));
                        break;
                    case 'U':
                        payload.append(QByteArray(mem.R1B).leftJustified(parse.len, ' ', true));
                        break;
                    case 'v':
                        payload.append(QByteArray(mem.R2).leftJustified(parse.len, ' ', true));
                        break;
                    case 'V':
                        payload.append(QByteArray(mem.R2B).leftJustified(parse.len, ' ', true));
                        break;
                    case 'w':                                                          // Tuning step
                        payload.append(quint8(mem.tuningStep != 0 ? 1 : 0));
                        payload.append(bcdEncodeChar(qMax(uchar(1), mem.tuningStep))); // 0 is invalid.
                        payload.append(bcdEncodeInt(mem.progTs));
                        break;
                    case 'x': // Attenuator & Preamp
                        payload.append(bcdEncodeChar(mem.atten));
                        payload.append(bcdEncodeChar(mem.preamp));
                        break;
                    case 'y': // Antenna
                        payload.append(bcdEncodeChar(mem.antenna));
                        break;
                    case '+': // IP Plus
                        payload.append(bcdEncodeChar(mem.ipplus));
                        break;
                    case 'z':
                        payload.append(QByteArray(mem.name).leftJustified(parse.len, ' ', true));
                        break;
                    case 'Z': // Mode-dependent extension block.
                    {
                        const auto modeIt = std::find_if(radioCaps.modes.cbegin(), radioCaps.modes.cend(),
                                                         [&mem](const ModeInfo& mode) { return mode.reg == mem.mode; });
                        if (modeIt != radioCaps.modes.cend())
                        {
                            switch (modeIt->mk)
                            {
                            case modeFM:
                                if (mem.tonemode)
                                {
                                    payload.append(bcdEncodeChar(mem.tonemode));
                                    payload.append(nul);
                                    appendToneByName(mem.tsql);
                                    payload.append(bcdEncodeChar(mem.dtcsp));
                                    payload.append(bcdEncodeInt(mem.dtcs));
                                }
                                break;
                            case modeDV:
                                if (mem.dsql)
                                {
                                    payload.append(bcdEncodeChar(2)); // IC-9700 expects 2 when enabling DV SQL.
                                    payload.append(bcdEncodeChar(mem.dvsql));
                                }
                                break;
                            default:
                                break;
                            }
                        }
                        break;
                    }
                    default:
                        break;
                    }
                    if (finished)
                    {
                        break;
                    }
                }
                qDebug(logRadio()) << "Writing memory location:" << payload.toHex(' ');
            }
            else if (valueHolds(qMetaTypeId<int>()) && (func == funcScopeRef))
            {
                bool isNegative = false;
                int level = value.value<int>();
                if (level < 0)
                {
                    isNegative = true;
                    level *= -1;
                }
                payload.append(bcdEncodeInt(quint16(level * 10)));
                payload.append(static_cast<quint8>(isNegative));
            }
            else if (valueHolds(qMetaTypeId<ModeInfo>()))
            {
                {
                    ModeInfo m = value.value<ModeInfo>();
                    if (func == funcDataModeWithFilter)
                    {
                        payload.append(bcdEncodeChar(m.data));
                        if (m.data != 0)
                        {
                            payload.append(m.filter);
                        }
                    }
                    else
                    {
                        payload.append(bcdEncodeChar(m.reg));
                        if (func == funcMode || func == funcSelectedMode || func == funcUnselectedMode)
                        {
                            payload.append(m.data);
                        }
                        if (!radioCaps.filters.empty() && m.mk != modeWFM)
                        {
                            payload.append(m.filter);
                        }
                        qDebug(logRadio()) << "Sending mode command" << funcString[func] << " mode:" << m.name
                                           << "data:" << m.data << "filter" << m.filter;
                    }
                }
            }
            else if (valueHolds(qMetaTypeId<Frequency>()))
            {
                if (func == funcSendFreqOffset)
                {
                    payload.append(makeFreqPayload(value.value<Frequency>()).mid(1, 3));
                }
                else
                {
                    payload.append(makeFreqPayload(value.value<Frequency>()));
                }
            }
            else if (valueHolds(qMetaTypeId<AntennaInfo>()))
            {
                payload.append(bcdEncodeChar(value.value<AntennaInfo>().antenna));
                if (radioCaps.commands.contains(funcRXAntenna))
                {
                    payload.append(value.value<AntennaInfo>().rx);
                }
            }
            else if (valueHolds(qMetaTypeId<radioInput>()))
            {
                payload.append(bcdEncodeChar(value.value<radioInput>().reg));
            }
            else if (valueHolds(qMetaTypeId<SpectrumBounds>()))
            {
                SpectrumBounds s = value.value<SpectrumBounds>();
                uchar range = 1;
                double lastRange = -1.0;
                auto band = radioCaps.bands.cend();
                while (band != radioCaps.bands.cbegin())
                {
                    band--;
                    if (band->range > s.start)
                    {
                        break;
                    }
                    else if (lastRange != band->range && band->range != 0.0 && band->range <= s.start)
                    {
                        range++;
                        lastRange = band->range;
                    }
                }
                payload.append(bcdEncodeChar(range));
                payload.append(bcdEncodeChar(s.edge));
                payload.append(makeFreqPayload(s.start));
                payload.append(makeFreqPayload(s.end));
            }
            else if (valueHolds(qMetaTypeId<duplexMode_t>()))
            {
                payload.append(static_cast<uchar>(value.value<duplexMode_t>()));
            }
            else if (valueHolds(qMetaTypeId<centerSpanData>()))
            {
                centerSpanData span = value.value<centerSpanData>();
                double freq = double(span.freq / 1000000.0);
                payload.append(makeFreqPayload(freq));
            }
            else if (valueHolds(qMetaTypeId<ToneInfo>()))
            {
                ToneInfo t = value.value<ToneInfo>();
                payload.append(encodeTone(t.tone, t.tinv, t.rinv));
            }
            else if (valueHolds(qMetaTypeId<DateKind>()))
            {
                DateKind d = value.value<DateKind>();
                qInfo(logRadio())
                    << QString("Sending new date: (MM-DD-YYYY) %1-%2-%3").arg(d.month).arg(d.day).arg(d.year);
                payload.append(convertNumberToHex(d.year / 100));                  // 20
                payload.append(convertNumberToHex(d.year - 100 * (d.year / 100))); // 21
                payload.append(convertNumberToHex(d.month));
                payload.append(convertNumberToHex(d.day));
            }
            else if (valueHolds(qMetaTypeId<TimeKind>()))
            {
                TimeKind t = value.value<TimeKind>();
                if (cmd.cmd == funcTime)
                {
                    qInfo(logRadio()) << QString("Sending new time: (HH:MM) %1:%2").arg(t.hours).arg(t.minutes);
                    payload.append(convertNumberToHex(t.hours));
                    payload.append(convertNumberToHex(t.minutes));
                }
                else if (cmd.cmd == funcUTCOffset)
                {
                    qInfo(logRadio()) << QString("Sending new UTC offset: %1%2:%3")
                                             .arg(t.isMinus ? "-" : "+")
                                             .arg(t.hours)
                                             .arg(t.minutes);
                    payload.append(convertNumberToHex(t.hours));
                    payload.append(convertNumberToHex(t.minutes));
                    payload.append((uchar)t.isMinus);
                }
            }
            else if (valueHolds(qMetaTypeId<RptrAccessData>()))
            {
                RptrAccessData r = value.value<RptrAccessData>();
                qDebug(logRadio()) << "Sending RptrAccessData Mode" << r.accessMode;
                payload.append(bcdEncodeChar(static_cast<uchar>(r.accessMode)));
            }
            else
            {
                qInfo(logRadio()) << funcString[func] << "Got unknown value type" << QString(value.typeName());
                return;
            }
        }
        else
        {
            if (!cmd.getCmd)
            {
                qDebug(logRadio()) << "Removing unsupported get command from queue" << funcString[func] << "VFO"
                                   << receiver;
                queue->del(func, receiver);
                return;
            }
        }
        if (!value.isValid() && cmd.getCmd)
        {
            rememberPendingReply(func, receiver);
        }
        else if (value.isValid() && cmd.setCmd &&
                 !(func == funcMemoryContents && value.metaType().id() == qMetaTypeId<uint>()))
        {
            rememberPendingSetCommand(func, payload, value, receiver, cmd);
        }
        // Register command correlation before emitting dataForComm. Most
        // production connections are queued across threads, but this ordering
        // also remains correct for direct test or diagnostic connections that
        // can deliver an immediate reply synchronously.
        prepDataAndSend(payload);
    }
    else
    {
        qDebug(logRadio()) << "CachingQueue(): unimplemented command" << funcString[func];
        queue->del(func, receiver);
    }
}

void Commander::receiveCommandNoReadback(Funcs func, QVariant value, uchar receiver)
{
    const bool previous = m_suppressReadbackForCurrentCommand;
    m_suppressReadbackForCurrentCommand = true;
    receiveCommand(func, value, receiver);
    m_suppressReadbackForCurrentCommand = previous;
}
