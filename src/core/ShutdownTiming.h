#pragma once

namespace sdr9700::shutdownTiming
{
// Unix signal shutdown first stops local audio and then gives the ordered LAN
// teardown one bounded opportunity to complete. The forced-exit alarm must
// exceed both synchronous handoff budgets plus an explicit scheduling margin;
// otherwise a stalled radio or VM can terminate the process before token
// removal and the control-port departure have been attempted.
inline constexpr int kStopLocalAudioTimeoutMs = 3000;
inline constexpr int kUdpHandlerShutdownTimeoutMs = 11000;
inline constexpr int kUdpThreadStopTimeoutMs = 3000;
inline constexpr int kUdpThreadInterruptTimeoutMs = 1000;
inline constexpr int kConnectionShutdownSchedulingMarginMs = 1000;
inline constexpr int kConnectionShutdownTimeoutMs = kUdpHandlerShutdownTimeoutMs + kUdpThreadStopTimeoutMs +
                                                    kUdpThreadInterruptTimeoutMs +
                                                    kConnectionShutdownSchedulingMarginMs;

// RadioBackend subsequently gives each of its radio-data and worker threads a
// 3-second orderly stop plus a 1-second interrupted stop. Those waits occur
// after shutdownConnection(), so the Unix forced-exit budget must include them
// as a separate sequential phase.
inline constexpr int kBackendThreadStopBudgetMs = 2 * (3000 + 1000);
inline constexpr int kForcedExitSchedulingMarginMs = 5000;
inline constexpr int kSignalForcedExitSeconds = 35;

static_assert(kSignalForcedExitSeconds * 1000 > kStopLocalAudioTimeoutMs + kConnectionShutdownTimeoutMs +
                                                    kBackendThreadStopBudgetMs + kForcedExitSchedulingMarginMs);
} // namespace sdr9700::shutdownTiming
