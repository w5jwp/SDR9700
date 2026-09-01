#pragma once

namespace sdr9700
{
// Records whether this process has received a successful stream grant from the
// radio. Authentication alone is deliberately insufficient: the IC-9700 can
// issue a login token and subsequently reject the stream because another LAN
// client owns it. Only a granted stream authorizes token removal, stream-close,
// or control-departure packets during shutdown.
class RadioSessionOwnership
{
  public:
    void acquire() { m_owned = true; }
    void release() { m_owned = false; }
    bool permitsRadioTeardown() const { return m_owned; }

  private:
    bool m_owned{false};
};
} // namespace sdr9700
