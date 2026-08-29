#include "RadioRouter.h"

#include "CachingQueue.h"
#include "LogCategories.h"

#include <QtGlobal>

namespace
{
constexpr uchar kMainReceiver = 0;
constexpr uchar kSubReceiver = 1;
} // namespace

RadioRouter::RadioRouter(QObject* parent) : QObject(parent) {}

bool RadioRouter::isReplaceable(const CacheItem& item)
{
    switch (item.command)
    {
    case funcSMeter:
    case funcSWRMeter:
    case funcPowerMeter:
    case funcALCMeter:
    case funcCompMeter:
    case funcVdMeter:
    case funcIdMeter:
        return true;
    case funcScopeWaveData:
        return item.value.canConvert<ScopeData>() && item.value.value<ScopeData>().valid;
    default:
        return false;
    }
}

quint64 RadioRouter::beginQueueSession()
{
    std::lock_guard locker(m_pendingMutex);
    m_pendingItems.clear();
    m_drainScheduled = false;
    m_scopeReceiver.store(kMainReceiver, std::memory_order_release);
    return ++m_queueSession;
}

void RadioRouter::cancelQueueSession(quint64 session)
{
    std::lock_guard locker(m_pendingMutex);
    if (session == 0 || session != m_queueSession)
    {
        return;
    }
    m_pendingItems.clear();
    m_drainScheduled = false;
    ++m_queueSession;
}

void RadioRouter::enqueueBatch(const QVector<CacheItem>& items, quint64 session)
{
    bool scheduleDrain = false;
    quint64 scheduledSession = 0;
    {
        std::lock_guard locker(m_pendingMutex);
        if (session != 0 && session != m_queueSession)
        {
            return;
        }
        for (const CacheItem& item : items)
        {
            bool replaced = false;
            if (isReplaceable(item))
            {
                for (auto pending = m_pendingItems.end(); pending != m_pendingItems.begin();)
                {
                    --pending;
                    if (!isReplaceable(*pending))
                    {
                        break;
                    }
                    const bool sameKey = pending->command == item.command &&
                                         (item.command == funcScopeWaveData || pending->receiver == item.receiver);
                    if (sameKey)
                    {
                        *pending = item;
                        ++m_coalescedItems;
                        replaced = true;
                        break;
                    }
                }
            }
            if (!replaced)
            {
                m_pendingItems.append(item);
            }
        }
        m_pendingHighWaterMark = qMax(m_pendingHighWaterMark, m_pendingItems.size());
        if (!m_drainScheduled && !m_pendingItems.isEmpty())
        {
            m_drainScheduled = true;
            scheduleDrain = true;
            scheduledSession = m_queueSession;
        }
    }

    if (scheduleDrain)
    {
        QMetaObject::invokeMethod(
            this, [this, scheduledSession]() { drainPendingBatch(scheduledSession); }, Qt::QueuedConnection);
    }
}

void RadioRouter::drainPendingBatch(quint64 session)
{
    QVector<CacheItem> items;
    {
        std::lock_guard locker(m_pendingMutex);
        if (session != m_queueSession)
        {
            return;
        }
        items.swap(m_pendingItems);
        m_drainScheduled = false;
        ++m_drainEvents;
    }
    routeBatch(items);
}

RadioRouterQueueDiagnostics RadioRouter::queueDiagnostics() const
{
    std::lock_guard locker(m_pendingMutex);
    return {m_pendingItems.size(), m_pendingHighWaterMark, m_coalescedItems, m_drainEvents};
}

void RadioRouter::routeBatch(const QVector<CacheItem>& items)
{
    for (const CacheItem& item : items)
    {
        route(item);
    }
}

QString RadioRouter::modeInfoToString(const ModeInfo& mi) const
{
    radioMode_t mode = mi.mk;
    if (mode == modeUnknown && mi.reg != 0xff)
    {
        mode = static_cast<radioMode_t>(mi.reg);
    }

    switch (mode)
    {
    case modeUSB:
        return QStringLiteral("USB");
    case modeLSB:
        return QStringLiteral("LSB");
    case modeAM:
        return QStringLiteral("AM");
    case modeFM:
        return QStringLiteral("FM");
    case modeCW:
        return QStringLiteral("CW");
    case modeCW_R:
        return QStringLiteral("CW-R");
    case modeRTTY:
        return QStringLiteral("RTTY");
    case modeRTTY_R:
        return QStringLiteral("RTTY-R");
    case modeDV:
        return QStringLiteral("DV");
    case modeDD:
        return QStringLiteral("DD");
    default:
        qWarning(logRadio()).noquote() << "modeInfoToString: unrecognised mode" << mode << "reg" << mi.reg
                                       << "- defaulting to FM";
        return QStringLiteral("FM");
    }
}

bool RadioRouter::toneRegisterIsDisplayed(Funcs command, uchar receiver) const
{
    const rptAccessTxRx_t toneAccessMode = m_toneAccessModes[receiver == kSubReceiver ? 1 : 0];
    if (isDtcsToneMode(toneAccessMode) || toneAccessMode == ratrNN)
    {
        return false;
    }

    const bool displayRxTone = toneAccessMode == ratrNT || toneAccessMode == ratrDT;
    return displayRxTone ? command == funcTSQLFreq : command == funcToneFreq;
}

void RadioRouter::route(const CacheItem& item)
{
    switch (item.command)
    {
    case funcFreqGet:
    case funcFreqSet:
    case funcSelectedFreq:
    case funcUnselectedFreq:
    {
        if (item.command == funcUnselectedFreq)
        {
            break;
        }

        emit radioValueUpdated(item.command, item.value, item.receiver);
        const auto frequency = item.value.value<Frequency>();
        if (item.receiver == kMainReceiver && frequency.Hz > 0)
        {
            emit frequencyReported(frequency.Hz);
        }
        break;
    }
    case funcModeGet:
    case funcModeSet:
    case funcSelectedMode:
    case funcUnselectedMode:
    {
        if (item.command == funcUnselectedMode)
        {
            break;
        }

        emit radioValueUpdated(item.command, item.value, item.receiver);
        if (item.receiver != kMainReceiver)
        {
            break;
        }
        const auto mode = item.value.value<ModeInfo>();
        emit modeReported(modeInfoToString(mode), mode.filter);
        break;
    }
    case funcSplitStatus:
        emit radioValueUpdated(item.command, item.value, item.receiver);
        emit duplexModeChanged(item.value.value<duplexMode_t>());
        break;
    case funcVFOBandMS:
        emit radioValueUpdated(item.command, item.value, kMainReceiver);
        break;
    case funcVFODualWatch:
        emit radioValueUpdated(item.command, item.value, kMainReceiver);
        break;
    case funcScopeMainSub:
        m_scopeReceiver.store(item.value.toBool() ? kSubReceiver : kMainReceiver, std::memory_order_release);
        emit radioValueUpdated(item.command, item.value, kMainReceiver);
        break;
    case funcReadFreqOffset:
        emit radioValueUpdated(item.command, item.value, item.receiver);
        emit repeaterOffsetChanged(item.value.value<Frequency>().Hz);
        break;
    case funcToneSquelchType:
        emit radioValueUpdated(item.command, item.value, item.receiver);
        m_toneAccessModes[item.receiver == kSubReceiver ? 1 : 0] = item.value.value<RptrAccessData>().accessMode;
        if (item.receiver == kMainReceiver)
        {
            emit toneAccessModeChanged(m_toneAccessModes[0]);
        }
        break;
    case funcToneFreq:
    case funcTSQLFreq:
        emit radioValueUpdated(item.command, item.value, item.receiver);
        if (item.receiver == kMainReceiver && toneRegisterIsDisplayed(item.command, item.receiver))
        {
            emit toneFrequencyChanged(item.value.value<ToneInfo>().tone);
        }
        break;
    case funcDTCSCode:
        emit radioValueUpdated(item.command, item.value, item.receiver);
        emit dtcsCodeChanged(item.value.value<ToneInfo>().tone);
        break;
    case funcMemoryContents:
        emit radioValueUpdated(item.command, item.value, item.receiver);
        emit radioMemoryReceived(item.value.value<MemoryType>());
        break;
    case funcSMeter:
    {
        const int rawValue = qBound(0, item.value.toInt(), 255);
        qDebug(logRadioTraffic()).noquote().nospace() << "S meter receiver=" << item.receiver << " raw=" << rawValue;
        emit radioValueUpdated(item.command, QVariant(rawValue), item.receiver);
        if (item.receiver == kMainReceiver)
        {
            emit smeterChanged(rawValue);
        }
        break;
    }
    case funcNoiseReduction:
        emit radioValueUpdated(item.command, item.value, item.receiver);
        if (item.receiver == kMainReceiver)
            emit nrChanged(item.value.toBool());
        break;
    case funcNRLevel:
        emit nrLevelChanged(qBound(0, item.value.toInt(), 15));
        break;
    case funcNoiseBlanker:
        emit radioValueUpdated(item.command, item.value, item.receiver);
        if (item.receiver == kMainReceiver)
            emit nbChanged(item.value.toBool());
        break;
    case funcNBLevel:
        emit nbLevelChanged(qBound(0, item.value.toInt(), 10));
        break;
    case funcPreamp:
    {
        const int level = qBound(0, item.value.toInt(), 3);
        emit radioValueUpdated(item.command, QVariant(level), item.receiver);
        if (item.receiver == kMainReceiver)
        {
            emit preampLevelChanged(level);
            emit preampChanged(level != 0);
        }
        break;
    }
    case funcAttenuator:
        emit radioValueUpdated(item.command, item.value, item.receiver);
        if (item.receiver == kMainReceiver)
            emit attenuatorChanged(item.value.toInt() != 0);
        break;
    case funcAutoNotch:
        emit radioValueUpdated(item.command, item.value, item.receiver);
        if (item.receiver == kMainReceiver)
            emit autoNotchChanged(item.value.toBool());
        break;
    case funcManualNotch:
        emit radioValueUpdated(item.command, item.value, item.receiver);
        if (item.receiver == kMainReceiver)
            emit manualNotchChanged(item.value.toBool());
        break;
    case funcCompressor:
        emit compressorChanged(item.value.toBool());
        break;
    case funcCompressorLevel:
        emit compressorLevelChanged(qBound(0, item.value.toInt(), 255));
        break;
    case funcXFCStatus:
        emit xfcChanged(item.value.toBool());
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
        emit radioValueUpdated(item.command, QVariant(idx), item.receiver);
        if (item.receiver == kMainReceiver)
            emit agcModeChanged(QString::fromLatin1(kAgcModes[idx]));
        break;
    }
    case funcRfGain:
        emit radioValueUpdated(item.command, item.value, item.receiver);
        if (item.receiver == kMainReceiver)
            emit rfGainChanged(qBound(0, item.value.toInt(), 255));
        break;
    case funcRFPower:
    {
        const int level = qBound(0, item.value.toInt(), 255);
        emit radioValueUpdated(item.command, QVariant(level), item.receiver);
        emit txPowerChanged(level);
        break;
    }
    case funcDATAOffMod:
    {
        if (item.value.userType() == qMetaTypeId<radioInput>())
        {
            emit dataOffModChanged(item.value.value<radioInput>());
        }
        emit radioValueUpdated(item.command, item.value, item.receiver);
        break;
    }
    case funcDATA1Mod:
    {
        if (item.value.userType() == qMetaTypeId<radioInput>())
        {
            emit data1ModChanged(item.value.value<radioInput>());
        }
        emit radioValueUpdated(item.command, item.value, item.receiver);
        break;
    }
    case funcSquelch:
    {
        const int level = qBound(0, item.value.toInt(), 255);
        emit radioValueUpdated(item.command, QVariant(level), item.receiver);
        if (item.receiver == kMainReceiver)
            emit squelchChanged(level > 0, level);
        break;
    }
    case funcSWRMeter:
        emit radioValueUpdated(item.command, item.value, item.receiver);
        emit swrMeterChanged(item.value.toDouble());
        break;
    case funcPowerMeter:
        emit radioValueUpdated(item.command, item.value, item.receiver);
        emit powerMeterChanged(item.value.toDouble());
        break;
    case funcALCMeter:
        emit radioValueUpdated(item.command, item.value, item.receiver);
        emit alcChanged(item.value.toDouble());
        break;
    case funcCompMeter:
        emit radioValueUpdated(item.command, item.value, item.receiver);
        emit compressionMeterChanged(item.value.toDouble());
        break;
    case funcVdMeter:
        emit radioValueUpdated(item.command, item.value, item.receiver);
        emit voltageMeterChanged(item.value.toDouble());
        break;
    case funcIdMeter:
        emit radioValueUpdated(item.command, item.value, item.receiver);
        emit currentMeterChanged(item.value.toDouble());
        break;
    case funcTransceiverStatus:
        emit pttChanged(item.value.toBool());
        break;
    case funcScopeWaveData:
    {
        const ScopeData data = item.value.value<ScopeData>();
        const uchar expectedReceiver = m_scopeReceiver.load(std::memory_order_acquire);
        if (data.receiver != expectedReceiver)
        {
            qDebug(logSpectrumScope()).noquote()
                << "Ignoring scope frame for inactive receiver" << data.receiver << "expected" << expectedReceiver;
            break;
        }
        emit scopeDataReady(data);
        break;
    }
    default:
        break;
    }
}
