# IC-9700 Hardware Stress Tools

These scripts exercise a running SDR9700 instance against a physical IC-9700
through the opt-in local automation bridge. Start the application explicitly
with automation enabled:

```bash
./src/build/bin/SDR9700 --enable-automation --log=radio,udp,ci-v
```

Run the tools from the repository root with Python 3:

```bash
python3 resources/tools/ic9700_vfo_hardware_stress.py
python3 resources/tools/ic9700_control_matrix.py
python3 resources/tools/ic9700_shared_value_sweep.py
```

The VFO stress tool performs repeated selection, band rotation, MAIN/SUB
exchange, Dual Watch, and busy-gate pressure tests. Pass `--diagnostic` for a
short transition run, `--dual-only` to isolate Dual Watch, `--skip-band` to
skip the 500-transition band phase, or `--list-ui` to inspect the available UI
controls.

The control matrix tunes multiple frequencies upward and downward on 2 m,
70 cm, and 23 cm for both receivers. It also verifies independent MAIN and SUB
operation of AGC, ATT, NB, NOTCH, NR, P.AMP, SQL, and RFG. The shared-value
tool sweeps AF gain, LAN modulation, and transmit-power settings through their
available range and restores their original values.

## Safety

The tools require the automation discovery record to declare
`transmitAllowed: false`, continuously reject a transmitting state, and never
request PTT or DTMF Send. Transmit-power and LAN-modulation values may be
changed for wiring verification, but the radio is never keyed. Each value
sweep restores the value observed before the sweep.

These are hardware-integration tools, not CTest tests. Run them only when the
radio may be retuned and its receive-side controls may be changed repeatedly.
Review the application and radio logs after each stress run; a script pass does
not by itself prove that the radio emitted no warnings or recovery events.
