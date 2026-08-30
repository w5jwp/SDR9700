#pragma once

#include <QtGlobal>
#include <algorithm>
#include <cmath>

class CivRttEstimator
{
  public:
    void reset()
    {
        m_estimateMs = 0.0;
        m_jitterMs = 0.0;
        m_sampleCount = 0;
    }

    void observe(quint32 rttMs)
    {
        if (rttMs == 0)
        {
            return;
        }
        const double sample = static_cast<double>(rttMs);
        if (m_sampleCount == 0)
        {
            m_estimateMs = sample;
            m_jitterMs = sample * 0.5;
        }
        else
        {
            const double error = std::abs(sample - m_estimateMs);
            m_jitterMs = m_jitterMs * 0.75 + error * 0.25;
            m_estimateMs = m_estimateMs * 0.875 + sample * 0.125;
        }
        ++m_sampleCount;
    }

    qint64 resolvedDrainMs() const
    {
        if (m_sampleCount == 0)
        {
            return 50;
        }
        return std::clamp<qint64>(static_cast<qint64>(std::ceil(m_estimateMs * 0.5 + m_jitterMs * 2.0)), 20, 250);
    }

    qint64 abandonedDrainMs() const
    {
        if (m_sampleCount == 0)
        {
            return 500;
        }
        return std::clamp<qint64>(static_cast<qint64>(std::ceil(m_estimateMs + m_jitterMs * 4.0)), 100, 2000);
    }

    qint64 replyTimeoutMs() const
    {
        if (m_sampleCount == 0)
        {
            return 1000;
        }
        // A reply timeout must tolerate ordinary jitter without retaining a
        // lost receiver-less correlation for several seconds. Eight jitter
        // widths gives VM scheduling spikes substantial room while keeping
        // recovery bounded on a healthy local connection.
        return std::clamp<qint64>(static_cast<qint64>(std::ceil(m_estimateMs + m_jitterMs * 8.0)), 300, 3000);
    }

    quint64 sampleCount() const { return m_sampleCount; }

  private:
    double m_estimateMs{0.0};
    double m_jitterMs{0.0};
    quint64 m_sampleCount{0};
};
