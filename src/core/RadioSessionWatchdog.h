#pragma once

#include <QtGlobal>

class RadioSessionWatchdog
{
  public:
    enum class Action
    {
        None,
        RestartCiv,
        Disconnect
    };

    static constexpr qint64 kCivSilenceBeforeRecoveryMs = 2000;
    static constexpr qint64 kCivRecoveryIntervalMs = 1000;
    static constexpr int kMaxCivRecoveryAttempts = 3;
    static constexpr qint64 kControlSilenceBeforeDisconnectMs = 5000;
    static constexpr qint64 kAudioSilenceDiagnosticMs = 3000;
    static constexpr qint64 kControlFreshnessMs = 1500;

    static bool isHealthy(qint64 controlSilenceMs, qint64 civSilenceMs)
    {
        return controlSilenceMs < kControlFreshnessMs && civSilenceMs < kCivSilenceBeforeRecoveryMs;
    }

    Action evaluate(qint64 controlSilenceMs, qint64 civSilenceMs)
    {
        if (controlSilenceMs >= kControlSilenceBeforeDisconnectMs)
        {
            return Action::Disconnect;
        }

        if (civSilenceMs < kCivSilenceBeforeRecoveryMs)
        {
            m_civRecoveryAttempts = 0;
            m_nextCivRecoveryAtMs = kCivSilenceBeforeRecoveryMs;
            return Action::None;
        }

        if (m_civRecoveryAttempts < kMaxCivRecoveryAttempts && civSilenceMs >= m_nextCivRecoveryAtMs)
        {
            ++m_civRecoveryAttempts;
            m_nextCivRecoveryAtMs += kCivRecoveryIntervalMs;
            return Action::RestartCiv;
        }

        if (m_civRecoveryAttempts >= kMaxCivRecoveryAttempts && civSilenceMs >= m_nextCivRecoveryAtMs)
        {
            return Action::Disconnect;
        }

        return Action::None;
    }

    void reset()
    {
        m_civRecoveryAttempts = 0;
        m_nextCivRecoveryAtMs = kCivSilenceBeforeRecoveryMs;
    }

    int civRecoveryAttempts() const { return m_civRecoveryAttempts; }

  private:
    int m_civRecoveryAttempts = 0;
    qint64 m_nextCivRecoveryAtMs = kCivSilenceBeforeRecoveryMs;
};
