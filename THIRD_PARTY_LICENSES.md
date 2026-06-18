# SDR9700 Third-Party Licenses

SDR9700 incorporates or references the third-party work listed below. Each
component retains its original license.

This file is an attribution and license inventory.

## Active Protocol Attribution

### AetherSDR Project

SDR9700 benefited from AetherSDR as a source of design inspiration, operating
experience, and behavior comparison while SDR9700 was developed as its own
IC-9700-focused application.

- Reference project: AetherSDR

### Icom IC-9700 LAN Protocol Research

The IC-9700 LAN protocol stack in `src/radio/`, `src/audio/`, `src/core/`, and
`src/backend/` is maintained as SDR9700 project code. Protocol behavior and
compatibility were informed by public IC-9700 behavior, local testing, Icom
documentation, and prior open-source Icom LAN client work.

- Reference project: wfview
- Reference URL: <https://gitlab.com/eliggett/wfview>
- Copyright: Copyright (C) 2021-2025 Phil Taylor M0VSE and the wfview contributors
- Authors: Phil Taylor M0VSE, Elliott Liggett, and contributors
- License: GPL-2.0-or-later

## Build-Time/System Dependencies

The application links against system-provided libraries when available. These
are not vendored in this repository.

- Qt 6: Core, Widgets, Network, and Multimedia.
- XKB common: required by Qt GUI platform dependency discovery.
- Opus: required codec dependency discovered through `pkg-config`.
- SpeexDSP: required resampler dependency discovered through `pkg-config`.
- Eigen3: required for audio sample format conversion in `src/audio/AudioConverter`.
- HIDAPI (optional): used by `Rc28Manager` for Icom RC-28 HID control; linked
  when `libhidapi-hidraw` or `libhidapi` is detected at build time via
  `pkg-config`. Enables the `HAVE_HIDAPI` compile definition.

## Manual Material Not Distributed As SDR9700 Code

The `resources/manuals/` directory contains local IC-9700 manuals and related
research material for contributors and AI agents. It is not built into SDR9700
and must not be treated as SDR9700 source.

Imported notes, old docs, tests, and packaging work must be validated,
rewritten, or removed before they become shipping SDR9700 documentation or
code.
