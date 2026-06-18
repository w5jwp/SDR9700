## Summary

Describe the change and why it is needed.

## Type of change

- [ ] Bug fix
- [ ] Feature or workflow improvement
- [ ] Radio/protocol behavior change
- [ ] UI/accessibility change
- [ ] Documentation
- [ ] Build/packaging
- [ ] Refactor/cleanup

## Verification

- [ ] Built with `cmake --build src/build -j$(nproc)`
- [ ] Ran relevant tests, if any
- [ ] Manually tested the affected SDR9700 workflow
- [ ] Verified against a real IC-9700, if this changes radio behavior
- [ ] Not applicable / documentation only

## Radio Impact

If this changes IC-9700 LAN, CI-V, scope/waterfall, RX/TX audio, PTT, offset, tone, or memory behavior, describe:

- IC-9700 firmware/version used for testing:
- Band/mode tested:
- Logs, manual references, packet notes, or captures used:
- Known behavior that still needs hardware validation:

## Security and Privacy

- [ ] No credentials, tokens, private station details, or unredacted packet captures are included.
- [ ] No new app-owned `QSettings` persistence was added.
- [ ] No third-party code was copied into `src/` without license review.

## Checklist

- [ ] I read `CONTRIBUTING.md`.
- [ ] I followed `CONVENTIONS.md`.
- [ ] I kept the change focused.
- [ ] I updated documentation when behavior changed.
