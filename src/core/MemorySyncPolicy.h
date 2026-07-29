#pragma once

#include <QSet>
#include <QtGlobal>
#include <algorithm>

namespace sdr9700
{
inline int clampMemoryPollIntervalSeconds(int seconds)
{
    return qBound(30, seconds, 3600);
}

inline bool memorySyncComplete(const QSet<quint32>& expected, const QSet<quint32>& received)
{
    return !expected.isEmpty() &&
           std::all_of(expected.cbegin(), expected.cend(), [&received](quint32 key) { return received.contains(key); });
}

inline bool shouldRetryIncompleteMemoryOperationSync(bool operationPending, bool completedPollPass,
                                                     bool receivedAllExpected, int attempt, int maximumAttempts)
{
    return operationPending && completedPollPass && !receivedAllExpected && attempt < maximumAttempts;
}

inline int memorySyncProgressIndex(quint16 group, quint16 channel, quint16 firstGroup, quint16 firstChannel,
                                   quint16 lastChannel)
{
    return (group - firstGroup) * (lastChannel - firstChannel + 1) + (channel - firstChannel + 1);
}

inline void advanceMemorySyncSlot(quint16& group, quint16& channel, quint16 firstChannel, quint16 lastChannel)
{
    ++channel;
    if (channel > lastChannel)
    {
        channel = firstChannel;
        ++group;
    }
}

inline bool memoryReadbackExpected(bool waiting, quint32 expectedKey, quint32 receivedKey)
{
    return waiting && expectedKey != 0 && expectedKey == receivedKey;
}
} // namespace sdr9700
