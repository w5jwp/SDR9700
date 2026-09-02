#pragma once

namespace sdr9700
{
// Bounds the special bootstrap path used when LAN authentication succeeds but
// the IC-9700's CI-V command plane remains silent. A fresh-session retry comes
// first because a retained LAN session can produce the same symptom; only a
// second silent session is treated as evidence that the radio may be asleep.
class StandbyWakePolicy
{
  public:
    enum class Action
    {
        Continue,
        RetrySession,
        Wake,
        Fail
    };

    Action commandPlaneReady()
    {
        m_complete = true;
        return Action::Continue;
    }

    Action commandPlaneUnavailable()
    {
        if (m_complete)
        {
            return Action::Continue;
        }
        if (!m_sessionRetryAttempted && m_wakeAttempts == 0)
        {
            m_sessionRetryAttempted = true;
            return Action::RetrySession;
        }
        if (m_wakeAttempts < kMaxWakeAttempts)
        {
            ++m_wakeAttempts;
            return Action::Wake;
        }
        return Action::Fail;
    }

    void reset()
    {
        m_sessionRetryAttempted = false;
        m_wakeAttempts = 0;
        m_complete = false;
    }

    [[nodiscard]] bool complete() const { return m_complete; }
    [[nodiscard]] int wakeAttempts() const { return m_wakeAttempts; }

  private:
    static constexpr int kMaxWakeAttempts = 2;
    bool m_sessionRetryAttempted{false};
    int m_wakeAttempts{0};
    bool m_complete{false};
};
} // namespace sdr9700
