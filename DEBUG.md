# Debugging SDR9700

Use the debug build when you need debug symbols or debugger-friendly behavior:

```bash
make debug
./src/build/bin/SDR9700
```

`make debug` cleans and reconfigures `src/build` with CMake's `Debug` build
type. Debug builds default to `--log=all` when no explicit log option is
supplied.

Runtime logging can also be enabled in release builds:

```bash
./src/build/bin/SDR9700 --log=radio,udp,ci-v
./src/build/bin/SDR9700 --log=all --log-file=/tmp/sdr9700.log
```

Supported logging categories:

- `all`
- `audio`
- `audioconverter`
- `ci-v`
- `gui`
- `icom-rc-28`
- `radio`
- `repeater`
- `sdr9700-radio-control`
- `system`
- `udp`

`--log-file=<path>` appends the same formatted console output to a file.
Each line includes a conventional severity name: `DEBUG`, `INFO`, `WARN`,
`ERROR`, or `FATAL`.
