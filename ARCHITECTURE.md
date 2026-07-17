# SDR9700 Architecture

SDR9700 is a Qt 6 desktop application for controlling an Icom IC-9700 over the
radio LAN interface.

## High-Level Flow

```text
IC-9700 LAN
    |
    v
src/radio/        IC-9700 UDP, CI-V, audio, and scope protocol handling
    |
    v
src/backend/      RadioBackend adapts radio events to app-facing signals
    |
    v
src/models/       RadioModel, VfoModel, and SpectrumScopeModel hold UI state
    |
    v
src/gui/          MainWindow, dialogs, VFO display, spectrum, and waterfall
```

## Main Components

- `RadioBackend`: owns the radio commander bridge, worker thread, and audio
  device selection.
- `Commander` / `RadioCommander`: parse IC-9700 CI-V responses and dispatch
  radio commands.
- `UdpHandler`, `UdpCivData`, `UdpAudio`, `UdpBase`: handle IC-9700 LAN UDP
  control, CI-V data, audio, scope, and network status packets.
- `ScopeAdapter`: namespace that converts raw IC-9700 scope bytes (0–159) to
  calibrated dBm float values.
- `RadioModel`: app-level connection and radio state.
- `VfoModel`: active VFO state exposed to the UI.
- `SpectrumScopeModel`: spectrum range and waterfall/scope data exposed to the UI.
- `SpectrumWidget`: displays FFT/scope and waterfall data.
- `TitleBarWidget`: custom frameless window title bar housing the menu bar,
  volume/mute/lock controls, and window management buttons.
- `DtmfWidget`: floating DTMF send panel with PTT gating.
- `MemoryStore` / `MemoryWidget`: local SDR9700 memory channels stored outside
  the radio.
- `Rc28Manager` (optional, HAVE_HIDAPI): HID driver for the Icom RC-28 rotary
  controller; emits tuning step and button events, and accepts LED state.
- `AppSettings`: JSON-backed client settings at
  `~/.config/SDR9700/config.json`.

## Threading

- GUI and models live on the main thread.
- The radio commander runs on a worker thread owned by `RadioBackend`.
- Cross-thread communication should use queued Qt signals or
  `QMetaObject::invokeMethod` with `Qt::QueuedConnection`.
- Audio callbacks must not block on locks or perform expensive work.

## Radio Definitions

The IC-9700 capability definition is a compiled C++ capability table used by
the radio commander. Startup does not depend on a runtime `radios/` directory
or external radio definition files.

SDR9700 client settings use `AppSettings`; do not add app-owned `QSettings`
persistence.

## Spectrum Data

IC-9700 scope data is converted to display bins by `ScopeAdapter::toDbm` before
it is emitted to the models/UI. The IC-9700 encodes each point as a byte in the
range 0–159, where 0 maps to `minDbm` and 159 maps to `maxDbm`:

```cpp
bins[i] = minDbm + (byte / 159.0f) * (maxDbm - minDbm);
```

The UI treats those values as display-ready dBm bins for the current scope
range.

## Current Constraints

- The application is currently centered on one IC-9700 session and the active
  VFO flow.
- Imported design documents may describe features that do not exist in SDR9700.
  They are not architecture until they are validated and promoted.
- `resources/manuals/` is research material only.
