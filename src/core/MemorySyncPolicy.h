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
} // namespace sdr9700
