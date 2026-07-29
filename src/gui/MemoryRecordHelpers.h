#pragma once

#include "MemoryConstants.h"
#include "MainWindowHelpers.h"
#include "MemoryStore.h"
#include "RadioCapabilities.h"

#include <cstring>

namespace sdr9700::memory
{
using namespace sdr9700::ui::main_window;
inline quint32 radioMemoryKey(quint16 group, quint16 channel)
{
    return (static_cast<quint32>(group) << 16) | static_cast<quint32>(channel);
}

inline QString radioMemoryId(quint16 group, quint16 channel)
{
    return QStringLiteral("radio:%1:%2").arg(group).arg(channel, 3, 10, QLatin1Char('0'));
}

inline QString radioMemoryName(const MemoryType& memory)
{
    int length = 0;
    while (length < static_cast<int>(sizeof(memory.name)) && memory.name[length] != '\0')
    {
        ++length;
    }
    return QString::fromLatin1(memory.name, length).trimmed();
}

inline QString memoryCharField(const char* field, int size)
{
    int length = 0;
    while (length < size && field[length] != '\0')
    {
        ++length;
    }
    return QString::fromLatin1(field, length).trimmed();
}

inline bool radioMemoryIsStored(const MemoryType& memory)
{
    return !memory.del && memory.frequency.Hz > 0;
}

inline quint16 radioMemoryGroupForHz(quint64 hz)
{
    const availableBands band = sdr9700::radioBandForFrequency(hz);
    if (const sdr9700::RadioBandDef* def = sdr9700::radioBandDefinition(band))
    {
        if (def->memGroup >= kRadioMemoryFirstGroup && def->memGroup <= kRadioMemoryLastGroup)
        {
            return static_cast<quint16>(def->memGroup);
        }
    }
    return kRadioMemoryFirstGroup;
}

inline int recordDuplexModeFromRadio(quint8 duplex)
{
    switch (duplex)
    {
    case 1:
        return dmDupMinus;
    case 2:
        return dmDupPlus;
    default:
        return dmSimplex;
    }
}

inline quint64 defaultOffsetForModeAndHz(duplexMode_t mode, quint64 hz)
{
    const QVector<OffsetPreset> presets = offsetPresetsForHz(hz);
    const auto preset = std::find_if(presets.cbegin(), presets.cend(),
                                     [mode](const OffsetPreset& option) { return option.mode == mode; });
    if (preset != presets.cend())
    {
        return preset->hz;
    }
    return 0;
}

inline quint64 normalizedOffsetForModeAndHz(duplexMode_t mode, quint64 rawOffsetHz, quint64 receiveHz)
{
    if (mode != dmDupMinus && mode != dmDupPlus)
    {
        return 0;
    }

    for (const OffsetPreset& preset : offsetPresetsForHz(receiveHz))
    {
        if (preset.mode != mode)
        {
            continue;
        }
        if (rawOffsetHz == preset.hz || rawOffsetHz * 100ULL == preset.hz)
        {
            return preset.hz;
        }
    }
    if (rawOffsetHz == 0)
    {
        return defaultOffsetForModeAndHz(mode, receiveHz);
    }
    return rawOffsetHz;
}

inline quint8 radioDuplexFromRecord(int duplexMode)
{
    switch (static_cast<duplexMode_t>(duplexMode))
    {
    case dmDupMinus:
        return 1;
    case dmDupPlus:
        return 2;
    default:
        return 0;
    }
}

inline ushort toneValueFromRadioText(const QString& text)
{
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok)
    {
        return 0;
    }
    return static_cast<ushort>(value * 10.0 + 0.5);
}

inline QString normalizedToneText(QString text)
{
    text = text.trimmed();
    if (text.isEmpty())
    {
        return QString();
    }

    const ushort value = toneValueFromRadioText(text);
    return value > 0 ? toneFrequencyLabel(value) : text;
}

inline bool memoryToneModeUsesTxTone(rptAccessTxRx_t toneMode)
{
    return toneMode == ratrTN || toneMode == ratrTT || toneMode == ratrTD;
}

inline bool memoryToneModeUsesRxTone(rptAccessTxRx_t toneMode)
{
    return toneMode == ratrNT || toneMode == ratrTT || toneMode == ratrTD || toneMode == ratrDT;
}

inline int modeRegisterFromLabel(const QString& mode)
{
    if (mode == QLatin1String("LSB"))
    {
        return modeLSB;
    }
    if (mode == QLatin1String("USB"))
    {
        return modeUSB;
    }
    if (mode == QLatin1String("AM"))
    {
        return modeAM;
    }
    if (mode == QLatin1String("CW"))
    {
        return modeCW;
    }
    if (mode == QLatin1String("DV"))
    {
        return modeDV;
    }
    return modeFM;
}

inline MemoryRecord recordFromRadioMemory(const MemoryType& radioMemory)
{
    MemoryRecord memory;
    memory.id = radioMemoryId(radioMemory.group, radioMemory.channel);
    memory.group = radioMemory.group;
    memory.channel = radioMemory.channel;
    memory.name = radioMemoryName(radioMemory);
    if (memory.name.isEmpty())
    {
        memory.name = memoryFrequencyLabel(radioMemory.frequency.Hz);
    }
    memory.receiveHz = radioMemory.frequency.Hz;
    memory.mode = radioMemory.mode;
    memory.filter = radioMemory.filter;
    memory.dataMode = radioMemory.datamode;
    memory.scan = radioMemory.scan;
    memory.band = sdr9700::radioBandShortLabel(sdr9700::radioBandForFrequency(memory.receiveHz));
    memory.bandKey = memoryBandKeyForHz(memory.receiveHz);
    memory.duplexMode = recordDuplexModeFromRadio(radioMemory.duplex);
    memory.offsetHz = normalizedOffsetForModeAndHz(static_cast<duplexMode_t>(memory.duplexMode),
                                                   radioMemory.duplexOffset.Hz, memory.receiveHz);
    memory.shift = offsetModeLabel(static_cast<duplexMode_t>(memory.duplexMode), memory.offsetHz);
    memory.toneMode = radioMemory.tonemode;
    memory.toneOption = toneOptionLabel(static_cast<rptAccessTxRx_t>(memory.toneMode));
    memory.tone = radioMemory.tone;
    memory.tsql = radioMemory.tsql;
    memory.dsql = radioMemory.dsql;
    memory.dtcs = radioMemory.dtcs;
    memory.dtcsPolarity = radioMemory.dtcsp;
    memory.dtcsB = radioMemory.dtcsB;
    memory.dtcsPolarityB = radioMemory.dtcspB;
    memory.dvSql = radioMemory.dvsql;
    memory.urCall = memoryCharField(radioMemory.UR, sizeof radioMemory.UR);
    memory.r1Call = memoryCharField(radioMemory.R1, sizeof radioMemory.R1);
    memory.r2Call = memoryCharField(radioMemory.R2, sizeof radioMemory.R2);
    if (isDtcsToneMode(static_cast<rptAccessTxRx_t>(memory.toneMode)))
    {
        memory.toneValue = memory.dtcs;
    }
    else if (memory.toneMode != ratrNN)
    {
        const auto toneMode = static_cast<rptAccessTxRx_t>(memory.toneMode);
        memory.toneValue = toneValueFromRadioText(toneMode == ratrNT || toneMode == ratrDT ? memory.tsql : memory.tone);
    }
    memory.toneFrequency = memoryToneFrequencyLabel(static_cast<rptAccessTxRx_t>(memory.toneMode), memory.toneValue);
    return memory;
}

inline MemoryType radioMemoryFromRecord(const MemoryRecord& memory, quint16 group, quint16 channel)
{
    MemoryType radioMemory;
    radioMemory.group = group;
    radioMemory.channel = channel;
    radioMemory.scan = static_cast<quint8>(qBound(0, memory.scan, 3));
    radioMemory.frequency.Hz = memory.receiveHz;
    radioMemory.frequency.VFO = activeVFO;
    radioMemory.mode = static_cast<quint8>(memory.mode);
    radioMemory.filter = static_cast<quint8>(qBound(1, memory.filter, 3));
    radioMemory.datamode = static_cast<quint8>(qBound(0, memory.dataMode, 1));
    radioMemory.duplex = radioDuplexFromRecord(memory.duplexMode);
    radioMemory.tonemode = static_cast<quint8>(memory.toneMode);
    radioMemory.dsql = static_cast<quint8>(qBound(0, memory.dsql, 2));
    radioMemory.dtcsp = static_cast<quint8>(qBound(0, memory.dtcsPolarity, 3));
    radioMemory.dtcspB = static_cast<quint8>(qBound(0, memory.dtcsPolarityB, 3));
    radioMemory.dvsql = static_cast<quint8>(qBound(0, memory.dvSql, 99));
    radioMemory.duplexOffset.Hz = memory.offsetHz;
    radioMemory.duplexOffset.VFO = activeVFO;
    const auto toneMode = static_cast<rptAccessTxRx_t>(memory.toneMode);
    if (isDtcsToneMode(toneMode))
    {
        radioMemory.dtcs = memory.dtcs;
        radioMemory.dtcsB = memory.dtcsB;
    }
    // Mixed tone modes share payload fields: ratrDT writes DTCS for transmit
    // and TSQL for receive. Keep those decisions explicit so a later tone-mode
    // cleanup cannot accidentally drop half of the radio memory definition.
    if (memoryToneModeUsesTxTone(toneMode))
    {
        // CSV import and the editor validate required tone fields before a write
        // is queued. Keep the radio payload faithful to those explicit fields so
        // bad memory data fails at the schema boundary instead of being repaired
        // here with hidden inferred values.
        radioMemory.tone = normalizedToneText(memory.tone);
    }
    if (memoryToneModeUsesRxTone(toneMode))
    {
        radioMemory.tsql = normalizedToneText(memory.tsql);
    }
    const QByteArray name = memory.name.toLatin1().left(kRadioMemoryNameMaxChars);
    std::copy(name.cbegin(), name.cend(), radioMemory.name);
    const QByteArray ur = memory.urCall.toLatin1().left(sizeof radioMemory.UR);
    const QByteArray r1 = memory.r1Call.toLatin1().left(sizeof radioMemory.R1);
    const QByteArray r2 = memory.r2Call.toLatin1().left(sizeof radioMemory.R2);
    std::copy(ur.cbegin(), ur.cend(), radioMemory.UR);
    std::copy(r1.cbegin(), r1.cend(), radioMemory.R1);
    std::copy(r2.cbegin(), r2.cend(), radioMemory.R2);
    return radioMemory;
}

inline MemoryType deletedRadioMemory(quint16 group, quint16 channel)
{
    MemoryType memory;
    memory.group = group;
    memory.channel = channel;
    memory.del = true;
    return memory;
}

inline QVector<MemoryType> deletedUserRadioMemories()
{
    QVector<MemoryType> deletes;
    deletes.reserve((kRadioMemoryLastGroup - kRadioMemoryFirstGroup + 1) * kRadioMemoryLastChannel);
    for (quint16 group = kRadioMemoryFirstGroup; group <= kRadioMemoryLastGroup; ++group)
    {
        for (quint16 channel = kRadioMemoryFirstChannel; channel <= kRadioMemoryLastChannel; ++channel)
        {
            deletes.append(deletedRadioMemory(group, channel));
        }
    }
    return deletes;
}

inline bool radioMemoryReadbackMatches(const MemoryType& expected, const MemoryType& actual)
{
    if (expected.group != actual.group || expected.channel != actual.channel)
    {
        return false;
    }
    if (expected.del)
    {
        return !radioMemoryIsStored(actual);
    }
    if (!radioMemoryIsStored(actual) || expected.frequency.Hz != actual.frequency.Hz || expected.mode != actual.mode ||
        expected.filter != actual.filter || expected.datamode != actual.datamode || expected.scan != actual.scan ||
        expected.duplex != actual.duplex || expected.duplexOffset.Hz != actual.duplexOffset.Hz ||
        expected.tonemode != actual.tonemode || expected.dsql != actual.dsql || expected.dvsql != actual.dvsql ||
        radioMemoryName(expected) != radioMemoryName(actual) ||
        memoryCharField(expected.UR, sizeof expected.UR) != memoryCharField(actual.UR, sizeof actual.UR) ||
        memoryCharField(expected.R1, sizeof expected.R1) != memoryCharField(actual.R1, sizeof actual.R1) ||
        memoryCharField(expected.R2, sizeof expected.R2) != memoryCharField(actual.R2, sizeof actual.R2))
    {
        return false;
    }

    const auto toneMode = static_cast<rptAccessTxRx_t>(expected.tonemode);
    if (memoryToneModeUsesTxTone(toneMode) && normalizedToneText(expected.tone) != normalizedToneText(actual.tone))
    {
        return false;
    }
    if (memoryToneModeUsesRxTone(toneMode) && normalizedToneText(expected.tsql) != normalizedToneText(actual.tsql))
    {
        return false;
    }
    if (isDtcsToneMode(toneMode) &&
        (expected.dtcs != actual.dtcs || expected.dtcsp != actual.dtcsp ||
         (toneMode == ratrDD && (expected.dtcsB != actual.dtcsB || expected.dtcspB != actual.dtcspB))))
    {
        return false;
    }
    return true;
}


} // namespace sdr9700::memory
