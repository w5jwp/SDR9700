#pragma once

#include "RadioCapabilities.h"
#include "Types.h"

#include <limits>
#include <optional>

namespace sdr9700
{
inline std::optional<quint64> duplexTransmitFrequency(quint64 receiveHz, duplexMode_t mode, quint64 offsetHz)
{
    switch (mode)
    {
    case dmDupMinus:
        if (offsetHz > receiveHz)
        {
            return std::nullopt;
        }
        return receiveHz - offsetHz;
    case dmDupPlus:
        if (offsetHz > std::numeric_limits<quint64>::max() - receiveHz)
        {
            return std::nullopt;
        }
        return receiveHz + offsetHz;
    default:
        return receiveHz;
    }
}

inline bool transmitFrequencyAllowed(quint64 receiveHz, quint64 transmitHz)
{
    const availableBands receiveBand = radioBandForFrequency(receiveHz);
    quint64 bandStart = 0;
    quint64 bandEnd = 0;
    return receiveBand != bandUnknown && radioBandEdges(receiveBand, &bandStart, &bandEnd) && transmitHz >= bandStart &&
           transmitHz <= bandEnd;
}
} // namespace sdr9700
