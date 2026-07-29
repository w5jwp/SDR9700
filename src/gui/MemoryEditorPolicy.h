#pragma once

#include "MainWindowHelpers.h"
#include "MemoryConstants.h"
#include "MemoryRecordHelpers.h"

#include <QSize>

namespace sdr9700::memory
{
using namespace sdr9700::ui::main_window;
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


inline bool modeSupportsMemoryOffset(int mode)
{
    return mode == modeFM || mode == modeDV || mode == modeDD;
}

constexpr int radioMemorySyncTimeoutMs()
{
    return (kRadioMemorySyncTotal * kRadioMemoryRefreshIntervalMs) + kRadioMemorySyncReplyGraceMs +
           kRadioMemorySyncSafetyMarginMs;
}

} // namespace sdr9700::memory
