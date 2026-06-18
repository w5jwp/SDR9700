# Contributing to SDR9700

Thanks for helping with SDR9700. The project is early-stage, so the most useful
contributions are focused, verifiable changes that move the IC-9700 client
toward a clean first release.

## Before You Start

1. Read `README.md` for project scope.
2. Read `CONVENTIONS.md` for coding rules.
3. Read `AGENTS.md` if you are using an AI coding assistant.
4. Check whether the file you want to use is imported reference material.
   Rewrite and validate it before promoting it to active docs.

## Good First Contributions

- Remove stale references to other projects from active SDR9700 files.
- Replace copied documentation with IC-9700-specific documentation.
- Fix build warnings or obvious memory/resource ownership issues.
- Add narrow tests for protocol parsing or settings behavior.
- Improve accessibility names/descriptions on existing widgets.

## Development Workflow

```bash
make
./src/build/bin/SDR9700
```

Use `src/build` for all local builds. Do not create agent-specific build
directories such as `build-codex`, `build-claude`, or similar variants. If
`src/build` was configured with a different CMake generator, clear and
reconfigure `src/build`.

Use `make debug` for developer builds that need debug symbols. Runtime logging
is controlled separately in release builds. Debug builds default to `--log=all`:

```bash
./src/build/bin/SDR9700 --log=radio,udp,ci-v --log-file=/tmp/sdr9700.log
```

Other CMake build types are rejected by CMake.

## Change Guidelines

- Keep each change focused on one problem.
- Preserve user changes already present in the working tree.
- Do not copy material from `resources/manuals/` into `src/`.
- Do not promote imported material to active docs without rewriting it for
  SDR9700 and the IC-9700.
- Do not add new app-owned `QSettings` usage. Use `AppSettings`.

## Bug Reports

Useful bug reports include:

- SDR9700 commit or version.
- Linux distribution and Qt version.
- IC-9700 firmware version.
- Connection type and radio LAN settings.
- Exact steps to reproduce.
- Logs or packet captures when protocol behavior is involved.

## Pull Requests

Before opening a pull request, run the automated checks from the project root:

**clang-format** — apply in-place and confirm no files changed:
```bash
find src -name '*.cpp' -o -name '*.h' | xargs clang-format -i
git diff --stat
```

**cppcheck** — pedantic static analysis:
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
  -I src src
```

Any findings not already suppressed should be addressed or explained in the PR description.

Then:

- Build locally.
- Run any relevant tests.
- Explain what changed and how it was verified.
- Note any behavior that needs validation against real IC-9700 hardware.

Root documentation should stay short and accurate. Long design notes, imported
plans, and future feature concepts should stay out of root docs until ready.
