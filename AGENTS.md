# SDR9700 — Project Context for AI Agents

SDR9700 is a Linux-native Qt/C++ GUI client for controlling an Icom IC-9700
amateur radio transceiver over the radio's LAN interface.

## Project Goal

Build an open-source Linux application that gives IC-9700 operators a
maintainable native GUI for everyday control, spectrum/waterfall display,
audio routing, and station workflows.

## Current Scope

- Radio connection profiles for IC-9700 LAN control.
- UDP/TCP radio control through the IC-9700 LAN ports.
- VFO frequency and mode control with configurable step sizes.
- Scope/waterfall display.
- RX/TX audio routing through Qt Multimedia.
- AF/RF/TX gain, squelch, PTT, and DTMF send with PTT gating.
- Local SDR9700 memory channels stored outside the radio.
- Main-window lock mode.
- Icom RC-28 rotary controller support for step tuning and button mapping
  (optional, requires libhidapi at build time).

## Agent Guidelines

- Read `CONVENTIONS.md` before writing code.
- Prefer C++20 and Qt 6 idioms where the surrounding code supports them.
- Keep classes small and single-purpose.
- Use RAII and Qt parent ownership; avoid raw owning `new`/`delete`.
- Use Qt signals/slots for cross-object communication.
- Keep IC-9700 protocol decisions grounded in logs, packet captures, or radio
  behavior; ask for captures when behavior is uncertain.
- Use `AppSettings` for SDR9700 client settings. Do not add new app-owned
  `QSettings` persistence.
- Radio capability definitions are compiled into the application; do not add
  runtime radio definition files.
- Do not copy code from other projects into SDR9700 source files without an
  explicit decision and license review.
- The `resources/manuals/` directory contains local copies of IC-9700 manuals
  and related research material. Do not treat it as SDR9700 source code.
- Do not remove or rewrite user changes from the working tree unless the user
  explicitly asks.

## Source Code Review

When an AI agent is asked to perform a top-down and bottom-up review of the
source code, the expected result is a detailed, nit-picky review of every file
within the source tree. Findings should be identified for a maintainer to review
and confirm; do not automatically correct issues found during this review unless
the maintainer explicitly asks for fixes.

A code review must always include these automated checks, run from the project
root, before reporting findings:

**clang-format** — apply in-place and report any files changed:
```bash
find src -name '*.cpp' -o -name '*.h' | xargs clang-format -i
git diff --stat
```

**cppcheck** — pedantic mode, using the project suppressions file:
```bash
cppcheck --enable=all --inconclusive --std=c++20 \
  --library=qt \
  --suppress=missingIncludeSystem \
  --suppress=missingInclude \
  --suppress=unknownMacro \
  --suppress=noValidConfiguration \
  --suppress=toomanyconfigs \
  --suppress=preprocessorErrorDirective \
  --suppressions-list=.cppcheck-suppressions \
  -I src -i src/build src
```

Do not scan `src/build` with cppcheck. It contains generated CMake, Qt MOC,
and resource files, which wastes review time and obscures source findings.

Any findings from these tools that are not already suppressed must be included
in the review report.

Items to look for include, but are not limited to:

- Adherence to SDR9700 principles, coding requirements, style, syntax practices,
  and formatting rules.
- Security risks, unsafe assumptions, input validation gaps, and resource
  handling problems.
- Leftover code, build paths, assumptions, or branches intended for operating
  systems other than Linux.
- Opportunities to refactor, simplify, optimize, or improve maintainability,
  regardless of size or apparent material benefit.
- Comments that lack detail, completeness, or useful context. During review,
  prefer flagging comments that would benefit from more explicit explanation.

## C++ Style

The canonical coding rules live in `CONVENTIONS.md`. Do not duplicate them
here; update `CONVENTIONS.md` when a coding rule changes.

## Build

```bash
make release
./src/build/bin/SDR9700
```

Always use `src/build` for local builds. Do not create agent-specific build
directories such as `build-codex`, `build-claude`, or similar variants. Always
do a clean build for verification: remove `src/build`, reconfigure it, then
build. The root `Makefile` release and debug targets perform this clean rebuild.
Only `Release` and `Debug` are supported CMake build types. Use `make release`
for production behavior and normal verification. Use `make debug` when debug
symbols or debugger-friendly builds are required. Runtime logging is controlled
with `--log=radio,udp,ci-v`, `--log=all`, and optional `--log-file=<path>`.
Debug builds default to `--log=all` when no log option is supplied.

## Settings

Client-side settings are stored by `AppSettings` at:

```text
~/.config/SDR9700/config.json
```

The file is JSON. Boolean settings are stored as `"True"` / `"False"` strings
for compatibility with existing code. Prefer PascalCase keys such as
`AutoConnect`, `AudioInputDeviceId`, and `RadioProfiles`.

## Architecture

- `src/gui/`: Qt widgets and dialogs.
- `src/models/`: UI-facing radio and VFO models.
- `src/backend/`: bridge between models and the IC-9700 radio stack.
- `src/radio/`: IC-9700 LAN/CI-V radio protocol implementation.
- `src/audio/`: Qt Multimedia audio handlers and conversion utilities.
- `src/core/`: shared types, settings, profile storage, logging, and queues.
