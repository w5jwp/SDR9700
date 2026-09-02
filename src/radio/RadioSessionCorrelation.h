#pragma once

#include <QtGlobal>

namespace sdr9700
{
struct RadioSessionRequest
{
    quint16 innerSequence{0};
    quint16 tokenRequest{0};
    quint32 token{0};
    bool pending{false};

    void begin(quint16 sequence, quint16 request, quint32 authenticationToken)
    {
        innerSequence = sequence;
        tokenRequest = request;
        token = authenticationToken;
        pending = true;
    }

    void clear() { pending = false; }

    [[nodiscard]] bool matches(quint16 sequence, quint16 request, quint32 authenticationToken) const
    {
        return pending && matchesIdentity(sequence, request, authenticationToken);
    }

    [[nodiscard]] bool matchesIdentity(quint16 sequence, quint16 request, quint32 authenticationToken) const
    {
        return sequence == innerSequence && request == tokenRequest && authenticationToken == token;
    }

    [[nodiscard]] bool matchesLogin(quint16 sequence, quint16 request) const
    {
        return pending && sequence == innerSequence && request == tokenRequest;
    }

    // Authentication responses can replace the six-byte authentication
    // identifier, so their returned token fields are response data rather than
    // correlation keys.
    [[nodiscard]] bool matchesAuthenticationResponse(quint16 sequence) const
    {
        return pending && sequence == innerSequence;
    }
};

[[nodiscard]] inline bool matchesRadioSessionEnvelope(quint32 sender, quint32 recipient, quint32 radioId,
                                                      quint32 clientId)
{
    return sender == radioId && recipient == clientId;
}

[[nodiscard]] inline bool shouldResetReissuedTokenAfterStreamRejection(bool retainedRecoveryInProgress,
                                                                       bool retainedTokenResetAttempted,
                                                                       quint32 streamError)
{
    return retainedRecoveryInProgress && !retainedTokenResetAttempted && streamError != 0;
}
} // namespace sdr9700
