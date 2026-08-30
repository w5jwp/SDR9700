#pragma once

#include <QtGlobal>

namespace sdr9700
{
class DualWatchTransitionPolicy
{
  public:
    static constexpr quint8 kStateConfirmed = 0x01;
    static constexpr quint8 kSubFrequencyConfirmed = 0x02;
    static constexpr quint8 kSubModeConfirmed = 0x04;

    bool request(bool enabled)
    {
        if (m_pending)
        {
            return false;
        }
        m_pending = true;
        m_requestedEnabled = enabled;
        m_confirmations = 0;
        return true;
    }

    bool observeState(bool enabled)
    {
        if (!m_pending || enabled != m_requestedEnabled)
        {
            return false;
        }
        m_confirmations |= kStateConfirmed;
        return true;
    }

    void observeSubFrequency()
    {
        if (m_pending && m_requestedEnabled)
        {
            m_confirmations |= kSubFrequencyConfirmed;
        }
    }

    void observeSubMode()
    {
        if (m_pending && m_requestedEnabled)
        {
            m_confirmations |= kSubModeConfirmed;
        }
    }

    bool complete() const { return m_pending && missingConfirmations() == 0; }
    bool pending() const { return m_pending; }
    bool requestedEnabled() const { return m_requestedEnabled; }
    quint8 confirmations() const { return m_confirmations; }

    quint8 missingConfirmations() const
    {
        const quint8 required = m_requestedEnabled
                                    ? static_cast<quint8>(kStateConfirmed | kSubFrequencyConfirmed | kSubModeConfirmed)
                                    : kStateConfirmed;
        return static_cast<quint8>(required & ~m_confirmations);
    }

    void reset()
    {
        m_pending = false;
        m_requestedEnabled = false;
        m_confirmations = 0;
    }

  private:
    bool m_pending{false};
    bool m_requestedEnabled{false};
    quint8 m_confirmations{0};
};
} // namespace sdr9700
