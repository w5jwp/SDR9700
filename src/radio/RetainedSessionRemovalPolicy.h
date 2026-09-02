#pragma once

namespace sdr9700
{

// Orders predecessor-token removal ahead of replacement authentication. The
// IC-9700 may retain a crashed client's token after its UDP transports have
// disappeared. Starting another login before removal is acknowledged creates
// overlapping authentication generations and can yield a stream that appears
// open while CI-V remains silent.
class RetainedSessionRemovalPolicy
{
  public:
    static constexpr int kMaxAttempts = 8;

    void begin()
    {
        m_pending = true;
        m_attempts = 0;
    }

    [[nodiscard]] bool takeAttempt()
    {
        if (!m_pending || m_attempts >= kMaxAttempts)
        {
            return false;
        }
        ++m_attempts;
        return true;
    }

    [[nodiscard]] bool acknowledge()
    {
        if (!m_pending)
        {
            return false;
        }
        m_pending = false;
        return true;
    }

    void reset()
    {
        m_pending = false;
        m_attempts = 0;
    }

    [[nodiscard]] bool pending() const { return m_pending; }
    [[nodiscard]] int attempts() const { return m_attempts; }
    [[nodiscard]] bool exhausted() const { return m_pending && m_attempts >= kMaxAttempts; }

  private:
    bool m_pending{false};
    int m_attempts{0};
};

} // namespace sdr9700
