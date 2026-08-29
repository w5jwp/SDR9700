#pragma once

#include "Vfo.h"

#include <QtGlobal>
#include <utility>

namespace sdr9700::backend
{
constexpr uchar receiverForVfo(Vfo vfo)
{
    return vfo == Vfo::Sub ? uchar{1} : uchar{0};
}

constexpr Vfo meterPollTarget(Vfo activeVfo, bool dualWatchEnabled, int pollTick)
{
    if (!dualWatchEnabled || pollTick % 5 != 4)
    {
        return activeVfo;
    }
    return activeVfo == Vfo::Main ? Vfo::Sub : Vfo::Main;
}

constexpr bool receiverMeterPollAllowed(bool radioReady, bool pttTransitionActive, bool mainSubExchangePending,
                                        bool tuningHoldoffActive)
{
    return radioReady && !pttTransitionActive && !mainSubExchangePending && !tuningHoldoffActive;
}

template <typename SelectReceiver, typename SendCommand>
void routeVfoReceiverCommand(Vfo targetVfo, Vfo restoreVfo, SelectReceiver&& selectReceiver, SendCommand&& sendCommand)
{
    selectReceiver(targetVfo);
    sendCommand(receiverForVfo(targetVfo));
    if (targetVfo != restoreVfo)
    {
        selectReceiver(restoreVfo);
    }
}
} // namespace sdr9700::backend
