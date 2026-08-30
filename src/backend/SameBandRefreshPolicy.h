#pragma once

#include <QtTypes>

namespace sdr9700
{

class SameBandRefreshPolicy
{
  public:
    bool observe(quint64 mainFrequencyHz, quint64 subFrequencyHz)
    {
        const bool sameFrequency = mainFrequencyHz != 0 && mainFrequencyHz == subFrequencyHz;

        if (!sameFrequency)
        {
            m_sameBandObserved = false;
            return false;
        }
        if (m_sameBandObserved)
        {
            return false;
        }

        m_sameBandObserved = true;
        return true;
    }

    void reset() { m_sameBandObserved = false; }

  private:
    bool m_sameBandObserved{false};
};

} // namespace sdr9700
