#pragma once

#include "Types.h"
#include <QtGlobal>

namespace sdr9700::memory
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
constexpr int kRadioMemoryOperationSyncMaxAttempts = 3;
constexpr int kRadioMemoryInitialSyncRetryDelayMs = 2000;
constexpr int kRadioMemoryWriteIntervalMs = 100;
constexpr int kRadioMemoryWriteReadbackRetryMs = 500;
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


} // namespace sdr9700::memory
