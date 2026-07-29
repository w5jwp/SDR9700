#pragma once

namespace sdr9700
{
inline bool shouldRetryRadioConnection(bool hadRadioUiReady, bool connectionAttemptFailed, bool userDisconnected,
                                       bool credentialFailure, bool profileAvailable)
{
    return (hadRadioUiReady || connectionAttemptFailed) && !userDisconnected && !credentialFailure && profileAvailable;
}
} // namespace sdr9700
