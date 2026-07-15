#include "RadioRouter.h"

#include "CachingQueue.h"
#include "LogCategories.h"

#include <QtGlobal>

namespace
{
constexpr uchar kMainReceiver = 0;
}

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
        qWarning(logRadio()) << "modeInfoToString: unrecognised mode" << mode << "reg" << mi.reg
                             << "- defaulting to FM";
        return QStringLiteral("FM");
    }
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
        static constexpr double kS0Dbm = -147.0;
        static constexpr double kS9Dbm = -93.0;
        static constexpr double kS9p60Dbm = -33.0;
        static constexpr double kS9MeterFraction = 4.0 / 7.0;

        const double dbm = item.value.toDouble();
        double fraction = 0.0;
        if (dbm <= kS0Dbm)
        {
            fraction = 0.0;
        }
        else if (dbm <= kS9Dbm)
        {
            fraction = kS9MeterFraction * (dbm - kS0Dbm) / (kS9Dbm - kS0Dbm);
        }
        else
        {
            fraction = kS9MeterFraction + (1.0 - kS9MeterFraction) * (dbm - kS9Dbm) / (kS9p60Dbm - kS9Dbm);
        }

        const int value = qBound(0, static_cast<int>(fraction * 255.0 + 0.5), 255);
        if (item.receiver == kMainReceiver)
        {
            emit radioValueUpdated(item.command, QVariant(value), kMainReceiver);
            emit smeterChanged(value);
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
