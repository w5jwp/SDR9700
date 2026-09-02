#pragma once

#include "Types.h"

namespace sdr9700
{
inline bool isAutomaticReconnectError(ErrorCode code)
{
    return code == ErrorCode::ConnectionFailed || code == ErrorCode::Disconnected ||
           code == ErrorCode::PortReservationFailed;
}

inline bool shouldRetryRadioConnection(bool hadRadioUiReady, bool connectionAttemptFailed, bool userDisconnected,
                                       bool credentialFailure, bool profileAvailable)
{
    return (hadRadioUiReady || connectionAttemptFailed) && !userDisconnected && !credentialFailure && profileAvailable;
}
} // namespace sdr9700
