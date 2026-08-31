#pragma once

#include <QtGlobal>

namespace sdr9700::backend
{
// A receiver mode is band-dependent state. During a MAIN/SUB exchange, UDP
// delivery may present the mode before the frequency that establishes the
// receiver's new band. RadioState must invalidate old-band fields when that
// frequency arrives, so an early mode cannot satisfy exchange completion.
// Keeping this rule as a small policy function makes the required ordering
// explicit and permits high-volume deterministic testing without a radio.
constexpr bool exchangeModeMayConfirm(quint8 confirmations, quint8 receiverFrequencyConfirmation)
{
    return (confirmations & receiverFrequencyConfirmation) != 0;
}
} // namespace sdr9700::backend
