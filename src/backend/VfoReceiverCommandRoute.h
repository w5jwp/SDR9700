#pragma once

#include "Vfo.h"

#include <QtGlobal>
#include <utility>

namespace sdr9700::backend
{
template <typename SelectReceiver, typename SendCommand>
void routeVfoReceiverCommand(Vfo targetVfo, Vfo restoreVfo, SelectReceiver&& selectReceiver, SendCommand&& sendCommand)
{
    selectReceiver(targetVfo);
    sendCommand(targetVfo == Vfo::Sub ? uchar{1} : uchar{0});
    if (targetVfo != restoreVfo)
    {
        selectReceiver(restoreVfo);
    }
}
} // namespace sdr9700::backend
