#pragma once

namespace sdr9700
{
class MainSubExchangePolicy
{
  public:
    enum class State
    {
        Idle,
        AwaitingRadio,
        AwaitingScope
    };

    bool request()
    {
        if (m_state != State::Idle)
        {
            return false;
        }
        m_state = State::AwaitingRadio;
        return true;
    }

    bool confirmRadio()
    {
        if (m_state != State::AwaitingRadio)
        {
            return false;
        }
        m_state = State::AwaitingScope;
        return true;
    }

    bool confirmScope()
    {
        if (m_state != State::AwaitingScope)
        {
            return false;
        }
        m_state = State::Idle;
        return true;
    }

    void reset() { m_state = State::Idle; }
    bool pending() const { return m_state != State::Idle; }
    State state() const { return m_state; }

  private:
    State m_state{State::Idle};
};
} // namespace sdr9700
