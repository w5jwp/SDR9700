#pragma once

#include "MainWindowHelpers.h"
#include "MemoryStore.h"
#include "RadioCapabilities.h"
#include "Types.h"
#include "UiTheme.h"

#include <QApplication>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QStyle>

#include <cstring>

using namespace sdr9700::ui::main_window;

namespace
{
constexpr quint16 kRadioMemoryFirstGroup = 1;
constexpr quint16 kRadioMemoryLastGroup = 3;
constexpr quint16 kRadioMemoryFirstChannel = 1;
constexpr quint16 kRadioMemoryLastChannel = 99;
constexpr int kRadioMemorySyncTotal =
    (kRadioMemoryLastGroup - kRadioMemoryFirstGroup + 1) * (kRadioMemoryLastChannel - kRadioMemoryFirstChannel + 1);
constexpr int kRadioMemoryRefreshIntervalMs = 25;
constexpr int kRadioMemorySyncReplyGraceMs = 1000;
constexpr int kRadioMemorySyncSafetyMarginMs = 5000;
constexpr int kRadioMemoryInitialSyncRetryDelayMs = 2000;
constexpr int kRadioMemoryWriteIntervalMs = 100;
constexpr int kRadioMemoryWriteReadbackTimeoutMs = 3000;
constexpr int kRadioMemoryNameMaxChars = 16;
constexpr int kMemoryEditorPaneWidth = 420;
constexpr int kMemoryEditorFieldHeight = 30;
constexpr int kMemoryEditorGutter = 10;
constexpr int kMemoryEditorLabelFieldSpacing = 6;
constexpr int kMemoryFooterTopPadding = 8;
constexpr int kMemoryFooterBottomPadding = 10;
constexpr int kMemoryFooterTextLeftPadding = 6;
constexpr int kMemoryToneCellTextPadding = 8;
constexpr int kMemoryToneTypeSectionWidth = 62;
constexpr int kMemoryToneTypeRole = Qt::UserRole + 1;
constexpr int kMemoryToneRxRole = Qt::UserRole + 2;
constexpr int kMemoryToneTxRole = Qt::UserRole + 3;
constexpr auto kMemoryFileFilter = "SDR9700 Memories (*.csv);;CSV Files (*.csv);;All Files (*)";

enum MemoryToneFamily
{
    MemoryToneOff = 0,
    MemoryToneTone,
    MemoryToneDtcs
};

class ToneCellDelegate : public QStyledItemDelegate
{
  public:
    explicit ToneCellDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const QString type = index.data(kMemoryToneTypeRole).toString();
        if (type.isEmpty() || type == QLatin1String("OFF"))
        {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionViewItem itemOption(option);
        initStyleOption(&itemOption, index);
        itemOption.text.clear();
        const QWidget* widget = itemOption.widget;
        QStyle* style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &itemOption, painter, widget);

        const QString rx = index.data(kMemoryToneRxRole).toString();
        const QString tx = index.data(kMemoryToneTxRole).toString();
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const QColor textColor =
            selected ? option.palette.color(QPalette::HighlightedText) : option.palette.color(QPalette::Text);
        QRect rect = option.rect.adjusted(5, 0, -5, 0);
        if (rect.width() < 24 || rect.height() < 8)
        {
            return;
        }

        const int typeWidth = qMin(kMemoryToneTypeSectionWidth, rect.width() / 3);
        const int valueWidth = (rect.width() - typeWidth) / 2;
        const QRect typeRect(rect.left(), rect.top(), typeWidth, rect.height());
        const QRect txRect(typeRect.right() + 1, rect.top(), valueWidth, rect.height());
        const QRect rxRect(txRect.right() + 1, rect.top(), rect.right() - txRect.right(), rect.height());

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(textColor);
        painter->drawText(typeRect.adjusted(kMemoryToneCellTextPadding, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                          type);
        painter->drawText(txRect.adjusted(kMemoryToneCellTextPadding, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("TX: %1").arg(tx.isEmpty() ? QStringLiteral("OFF") : tx));
        painter->drawText(rxRect.adjusted(kMemoryToneCellTextPadding, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("RX: %1").arg(rx.isEmpty() ? QStringLiteral("OFF") : rx));
        painter->restore();
    }
};

inline QSize memoryManagerWindowSize()
{
    return QSize(kMemoryWindowSize.width() + kMemoryEditorPaneWidth + kMemoryEditorGutter + (kMemoryPanelSpacing * 2) +
                     1,
                 kMemoryWindowSize.height());
}

inline MemoryToneFamily memoryToneFamilyForMode(rptAccessTxRx_t mode)
{
    if (isDtcsToneMode(mode))
    {
        return MemoryToneDtcs;
    }
    if (mode == ratrTN || mode == ratrNT || mode == ratrTT || mode == ratrTD)
    {
        return MemoryToneTone;
    }
    return MemoryToneOff;
}

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

quint64 defaultOffsetForModeAndHz(duplexMode_t mode, quint64 hz)
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

quint64 normalizedOffsetForModeAndHz(duplexMode_t mode, quint64 rawOffsetHz, quint64 receiveHz)
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

quint8 radioDuplexFromRecord(int duplexMode)
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

inline QString dtcsMemoryValue(ushort code, int polarity)
{
    return QStringLiteral("%1%2").arg(dtcsCodeLabel(code), polarity == 3 ? QStringLiteral("R") : QStringLiteral("N"));
}

inline QString memoryToneTypeLabel(const MemoryRecord& memory)
{
    const auto toneMode = static_cast<rptAccessTxRx_t>(memory.toneMode);
    if (toneMode == ratrNN)
    {
        return QStringLiteral("OFF");
    }
    return isDtcsToneMode(toneMode) ? QStringLiteral("DTCS") : QStringLiteral("TONE");
}

inline QString memoryToneRxLabel(const MemoryRecord& memory)
{
    const auto toneMode = static_cast<rptAccessTxRx_t>(memory.toneMode);
    if (toneMode == ratrNN || toneMode == ratrTN || toneMode == ratrDN)
    {
        return QStringLiteral("OFF");
    }
    if (isDtcsToneMode(toneMode))
    {
        return dtcsMemoryValue(memory.dtcsB, memory.dtcsPolarityB);
    }
    return memory.tsql.isEmpty() ? memory.tone : memory.tsql;
}

inline QString memoryToneTxLabel(const MemoryRecord& memory)
{
    const auto toneMode = static_cast<rptAccessTxRx_t>(memory.toneMode);
    if (toneMode == ratrNN || toneMode == ratrNT)
    {
        return QStringLiteral("OFF");
    }
    if (isDtcsToneMode(toneMode))
    {
        return dtcsMemoryValue(memory.dtcs, memory.dtcsPolarity);
    }
    return memory.tone.isEmpty() ? memory.tsql : memory.tone;
}

inline QString memoryToneTableLabel(const MemoryRecord& memory)
{
    const QString type = memoryToneTypeLabel(memory);
    if (type == QLatin1String("OFF"))
    {
        return QStringLiteral("OFF");
    }
    return QStringLiteral("%1: %2/%3").arg(type, memoryToneTxLabel(memory), memoryToneRxLabel(memory));
}

inline QString memoryFilterLabel(int filter)
{
    if (filter >= 1 && filter <= 3)
    {
        return QStringLiteral("FIL%1").arg(filter);
    }
    return QString::number(filter);
}

inline bool modeSupportsMemoryOffset(int mode)
{
    return mode == modeFM || mode == modeDV || mode == modeDD;
}

constexpr int radioMemorySyncTimeoutMs()
{
    return (kRadioMemorySyncTotal * kRadioMemoryRefreshIntervalMs) + kRadioMemorySyncReplyGraceMs +
           kRadioMemorySyncSafetyMarginMs;
}
} // namespace
