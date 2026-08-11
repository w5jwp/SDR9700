#pragma once

#include "TransmitFrequencyPolicy.h"

#include <optional>

namespace sdr9700
{
class TransmitConfigurationPolicy
{
  public:
    void reset() { *this = TransmitConfigurationPolicy{}; }

    void requestFrequency(quint64 hz)
    {
        m_requestedFrequencyHz = hz;
        m_frequencyPending = true;
    }
    void requestDuplexMode(duplexMode_t mode)
    {
        m_requestedDuplexMode = mode;
        m_duplexPending = true;
    }
    void requestOffset(quint64 hz)
    {
        m_requestedOffsetHz = hz;
        m_offsetPending = true;
    }

    void confirmFrequency(quint64 hz)
    {
        m_frequencyHz = hz;
        m_frequencyPending = m_frequencyPending && hz != m_requestedFrequencyHz;
    }
    void confirmDuplexMode(duplexMode_t mode)
    {
        m_duplexMode = mode;
        m_duplexPending = m_duplexPending && mode != m_requestedDuplexMode;
    }
    void confirmOffset(quint64 hz)
    {
        m_offsetHz = hz;
        m_offsetPending = m_offsetPending && hz != m_requestedOffsetHz;
    }

    bool confirmationPending() const { return m_frequencyPending || m_duplexPending || m_offsetPending; }
    bool transmitFrequencyAllowed() const
    {
        if (confirmationPending())
        {
            return false;
        }
        const std::optional<quint64> transmitHz = duplexTransmitFrequency(m_frequencyHz, m_duplexMode, m_offsetHz);
        return transmitHz.has_value() && sdr9700::transmitFrequencyAllowed(m_frequencyHz, *transmitHz);
    }

  private:
    quint64 m_frequencyHz{0};
    duplexMode_t m_duplexMode{dmSimplex};
    quint64 m_offsetHz{0};
    quint64 m_requestedFrequencyHz{0};
    duplexMode_t m_requestedDuplexMode{dmSimplex};
    quint64 m_requestedOffsetHz{0};
    bool m_frequencyPending{false};
    bool m_duplexPending{false};
    bool m_offsetPending{false};
};
} // namespace sdr9700
