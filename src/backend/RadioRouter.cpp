#include "RadioRouter.h"

#include "CachingQueue.h"
#include "LogCategories.h"

#include <QtGlobal>

namespace
{
constexpr uchar kMainReceiver = 0;
} // namespace

RadioRouter::RadioRouter(QObject* parent) : QObject(parent) {}

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

bool RadioRouter::toneRegisterIsDisplayed(Funcs command) const
{
    if (isDtcsToneMode(m_toneAccessMode) || m_toneAccessMode == ratrNN)
    {
        return false;
    }

    const bool displayRxTone = m_toneAccessMode == ratrNT || m_toneAccessMode == ratrDT;
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
        if (item.command == funcUnselectedFreq || item.receiver != kMainReceiver)
        {
            break;
        }

        emit radioValueUpdated(item.command, item.value, kMainReceiver);
        const auto frequency = item.value.value<Frequency>();
        if (frequency.Hz > 0)
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
        if (item.command == funcUnselectedMode || item.receiver != kMainReceiver)
        {
            break;
        }

        emit radioValueUpdated(item.command, item.value, kMainReceiver);
        const auto mode = item.value.value<ModeInfo>();
        emit modeReported(modeInfoToString(mode), mode.filter);
        break;
    }
    case funcSplitStatus:
        emit duplexModeChanged(item.value.value<duplexMode_t>());
        break;
    case funcVFOBandMS:
        emit radioValueUpdated(item.command, QVariant::fromValue<bool>(false), kMainReceiver);
        if (item.value.toBool())
        {
            emit vfoBandMSRequested();
        }
        break;
    case funcReadFreqOffset:
        emit repeaterOffsetChanged(item.value.value<Frequency>().Hz);
        break;
    case funcToneSquelchType:
        emit radioValueUpdated(item.command, item.value, item.receiver);
        m_toneAccessMode = item.value.value<RptrAccessData>().accessMode;
        emit toneAccessModeChanged(m_toneAccessMode);
        break;
    case funcToneFreq:
    case funcTSQLFreq:
        if (toneRegisterIsDisplayed(item.command))
        {
            emit toneFrequencyChanged(item.value.value<ToneInfo>().tone);
        }
        break;
    case funcDTCSCode:
        emit dtcsCodeChanged(item.value.value<ToneInfo>().tone);
        break;
    case funcMemoryContents:
        emit radioValueUpdated(item.command, item.value, item.receiver);
        emit radioMemoryReceived(item.value.value<MemoryType>());
        break;
    case funcSMeter:
    {
        const int rawValue = qBound(0, item.value.toInt(), 255);
        qDebug(logRadioTraffic()).noquote().nospace() << "S meter raw=" << rawValue;
        if (item.receiver == kMainReceiver)
        {
            emit radioValueUpdated(item.command, QVariant(rawValue), kMainReceiver);
            emit smeterChanged(rawValue);
        }
        break;
    }
    case funcNoiseReduction:
        emit nrChanged(item.value.toBool());
        break;
    case funcNRLevel:
        emit nrLevelChanged(qBound(0, item.value.toInt(), 15));
        break;
    case funcNoiseBlanker:
        emit nbChanged(item.value.toBool());
        break;
    case funcNBLevel:
        emit nbLevelChanged(qBound(0, item.value.toInt(), 10));
        break;
    case funcPreamp:
    {
        const int level = qBound(0, item.value.toInt(), 3);
        emit preampLevelChanged(level);
        emit preampChanged(level != 0);
        break;
    }
    case funcAttenuator:
        emit attenuatorChanged(item.value.toInt() != 0);
        break;
    case funcAutoNotch:
        emit autoNotchChanged(item.value.toBool());
        break;
    case funcManualNotch:
        emit manualNotchChanged(item.value.toBool());
        break;
    case funcCompressor:
        emit compressorChanged(item.value.toBool());
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
        emit agcModeChanged(QString::fromLatin1(kAgcModes[idx]));
        break;
    }
    case funcRfGain:
        emit radioValueUpdated(item.command, item.value, item.receiver);
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
        emit scopeDataReady(item.value.value<ScopeData>());
        break;
    default:
        break;
    }
}
