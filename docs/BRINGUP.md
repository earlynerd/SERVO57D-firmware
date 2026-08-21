# Bench Bring-up Procedure

This document covers both first bring-up of an unverified board and routine
motor development on the tested revision. Stages 0-5 are the evidence ladder
for new hardware or a changed bridge/timing backend. The tested board has
already passed functional bridge mapping, current regulation, and
encoder-confirmed rotation, so ordinary firmware iterations can begin at
Stage 6 within the measured operating envelope.

## Equipment

- Current-limited bench supply suitable for the controller input voltage.
- Multimeter.
- Oscilloscope with appropriately rated probes.
- Logic analyzer where useful.
- J-Link or another N32L406-compatible SWD probe; a Pico CMSIS-DAP probe is an alternative.
- USB-to-RS-485 adapter.
- Temporary fine wire or probe for NRST if required.

## Stage 0 — Unpowered inspection

1. Record board revision, MCU marking, encoder marking, gate-driver marking, MOSFET marking, and visible rework.
2. Photograph both sides at sufficient resolution to trace nets.
3. Confirm J4 ground, target 3.3 V, SWDIO, and SWCLK against the schematic.
4. Locate NRST and confirm it is not exposed on J4.
5. Check for shorts between input power, 5 V, 3.3 V, and ground.
6. For first power on an unknown revision, leave the motor connector open until
   supply rails and bridge idle state are known.

## Stage 1 — Power only

1. Set a supply current limit appropriate for the board and planned test.
2. Power the board without a programmer or motor.
3. Verify input current is plausible and stable.
4. Measure 5 V, 3.3 V, MCU reset level, bridge enable, and all four phase-control signals.
5. Stop immediately for unexpected heating, rail collapse, oscillation, or excessive current.

## Stage 2 — Debug attachment and protection

1. Connect the selected SWD probe to GND, SWDIO, and SWCLK; connect target
   voltage sensing if the probe requires it.
2. Use a conservative SWD rate, beginning around 200 kHz.
3. Attempt a non-destructive debug-port connection and identify the Cortex-M4.
4. If connection is unreliable, add NRST and use connect-under-reset or coordinate connection with controller power-up.
5. Determine the protection state before starting an unlock.
6. Record L0, L1, or L2 and all tool messages.
7. If L1, acknowledge that release to L0 irreversibly erases Makerbase firmware, configuration, and calibration.
8. Perform the unlock only on a board designated for replacement-firmware work.
9. Power-cycle and verify repeatable attachment.

## Stage 3 — Minimal replacement image

1. Flash a minimal vector table, clock setup, safe GPIO initialization, and LED heartbeat.
2. Confirm the bridge command remains in its reset or all-low state before,
   during, and after programming.
3. Exercise reset, power-cycle, watchdog, and debugger halt.
4. Confirm SWD remains recoverable after every case.
5. Use the guarded J-Link wrapper to build, program, independently verify,
   reset, and start the image.
6. Confirm an unserviced running image resets near the measured IWDG interval and exposes `RCC_CTRLSTS_IWDGRSTF` in `g_platform_boot_diagnostics.reset_flags` after reboot.
7. Confirm a debugger halt does not pause IWDG, TIM2, or TIM3 in firmware 0.19.0; capture PA6/PA7/PB0/PB1, gate outputs, and the watchdog reset transition.
8. Load the matching ELF symbols and verify `g_diagnostics` has magic `0x4D4B5335`, schema `5`, size `240`, firmware version `0.19.0`, and an even stable sequence.
9. Scope PD0 and confirm the active-high heartbeat without button contention on PB9.
10. Scope PB3-PB6 and confirm the bounded MT6816 mode-3 burst described in [encoder bring-up](ENCODER.md), including non-fatal no-magnet behavior.
11. Compare the diagnostic reset and retained-panic fields across power-on, NRST, software panic, and IWDG reset cases.
12. Verify the required/passed self-test masks are both `0x7F`, the failed mask is zero, and the reported board-safety result agrees with scoped PA6/PA7/PB0/PB1/PB7 levels.

## Stage 4 — Passive inputs

Use the passive diagnostic path or perform these checks before the current
backend takes ownership of PA6/PA7/PB0/PB1:

1. Read current-sense zero offsets and bus voltage repeatedly.
2. Read encoder position through complete mechanical revolutions.
3. Test buttons, display, isolated inputs, and RS-485.
4. Confirm every pin assignment against observation or continuity rather than relying solely on names.
5. Record ADC noise and encoder noise at multiple supply voltages.

## Stage 5 — Bridge characterization for new hardware

Use this stage for a new board revision, changed gate-control mapping, or a new
PWM/ADC timing backend. It need not be repeated for application-only changes on
the already-tested board.

1. Scope gate-driver inputs before enabling any output.
2. Apply a low-frequency, low-duty diagnostic pattern.
3. Observe high-side and low-side gate signals and bridge switch nodes.
4. Test reset, watchdog, firmware fault, breakpoint, and communications loss while the pattern is active.
5. Confirm the all-low zero-vector fault path acts immediately in every case;
   this PCB has no defined software-commanded all-FET-off state.
6. Characterize minimum pulse width, dead time, propagation delay, and bootstrap behavior.

## Stage 6 — Current-regulated motor operation

Firmware 0.19.0 operates an attached two-phase stepper through independent
20 kHz winding-current loops. Local Enter remains hold-to-run; release or Menu
commands `ZERO`. Both local and RS-485 operations request diagnostic authority
from the product drive supervisor after current-path and encoder readiness.
RS-485 provides configurable current amplitude, electrical
frequency, initial phase, run duration, STOP, and live current plus encoder
telemetry. Timeout, Menu, transport failure, or STOP ends authority.

Use a current-limited bench supply and connect the motor normally. The accepted
run used `COM14`; replace it with the port reported by `list` when necessary.
For ordinary tuning, run the complete move, capture, analysis, and plotting loop
with one command:

```powershell
py tools/motor_test.py --port COM14 --current-ma 750 --rpm 24 --seconds 5
```

The command reports live current, voltage effort, and encoder angle; STOP is
sent on Ctrl+C. It then saves `telemetry.jsonl`, `trace.jsonl`, `summary.json`,
and a self-contained `report.html` under ignored `scratch/motor-runs/`, opens
the report, and restores the prior inactive test configuration. Use
`--no-open` when no browser is wanted and `--replot RUN_DIRECTORY` to rebuild a
saved report without touching the motor.

Use the lower-level console when inspecting individual protocol operations:

```powershell
py tools/mks57d_rs485.py list
py tools/mks57d_rs485.py --port COM14 identity
py tools/mks57d_rs485.py --port COM14 boot
py tools/mks57d_rs485.py --port COM14 encoder
py tools/mks57d_rs485.py --port COM14 status
py tools/mks57d_rs485.py --port COM14 configure --counts 50 --frequency-hz 5
py tools/mks57d_rs485.py --port COM14 run --leg A1 --duration-ms 3000 --interval 0.1
```

The lower-level `run` streams current-loop and encoder snapshots until authority ends. At the
tested 12 V / 50-count point, current is 303 mA from the measured
6.059 mA/count scale, the motor rotates at
about 6 RPM, and the current loop should complete about 20,000 samples per
second without a fault. The original accepted firmware 0.17.8 run produced 59,905 loop
samples and -5.97 RPM measured by the encoder over three seconds.

Use the telemetry to evaluate each run directly:

1. `reference_counts` and `measured_counts` should have the same signs and
   remain close throughout the electrical cycle.
2. `sample_count` should advance continuously near 20 kHz and `faults` should
   remain empty.
3. Encoder angle should change smoothly and monotonically. For the tested
   1.8-degree stepper, mechanical RPM is approximately electrical hertz times
   `1.2`; a 5 Hz command therefore predicts 6 RPM.
4. A1/A2 and B1/B2 duties should alternate with phase sign while the opposing
   leg remains at zero.
5. After timeout or STOP, authority flags clear and the hardware returns to
   the all-low zero vector.

The current interface accepts 1-165 counts, 0.001-50 Hz, and 0.1-60 second
runs. Expand current, speed, bus voltage, and duration deliberately while
recording encoder tracking, current error, duty, supply current, and
temperature. An explicit `stop` command is available from another terminal or
after an interrupted watch.

`motor_test.py --rpm` is only a convenient positive speed-magnitude conversion
for this open-loop rotating vector. It does not yet regulate mechanical speed,
select direction, or command position; use encoder agreement and the generated
plots as the acceptance evidence until the aligned outer loops exist.

Firmware 0.19.0 retains the 0.18.2 `Kp=2` with `Ki=1/64` per 20 kHz step. A 303 mA startup
step has 6.53 ms rise time and 8% overshoot. The tested motor tracked 606 mA /
15 Hz at -17.78 RPM and completed 1.97 revolutions during a five-second 757 mA /
20 Hz run, with zero faults and 25.2% peak voltage effort. Operation through
757 mA / 20 Hz is accepted on this motor; stage the untested 1 A / 50 Hz endpoints
separately. The 256-sample startup trace is available after a run with
`trace`; analyze saved JSON lines with `tools/analyze_current_trace.py`.

The tuning sweep is not an enclosed thermal qualification. Before permanently
closing the housing, repeat the longest intended bounded duty cycle while
recording supply current and board, driver, and motor temperature, then inspect
the idle `status` and reset record again.

Firmware 0.19.0 displays a current-loop shutdown as persistent `F####`. The
number is the one-based fault-bit position: `F0002`/`F0003` are A/B raw
overcurrent, `F0017` ADC/DMA, `F0018` PWM, `F0019` deadline, and `F0020`
internal backend failure.

## Stage 7 — Toward useful closed-loop motion

The rotating-current test proves the inverter, current loop, motor geometry,
and encoder direction. The next implementation sequence is:

1. Measure the encoder offset for known electrical phase states and formalize
   the 50-electrical-cycles-per-revolution relationship.
2. Integrate the existing portable angle unwrapping and velocity estimator
   with timestamped hardware encoder samples.
3. Add an aligned torque/current command and close the velocity loop.
4. Add position trajectories, following-error detection, step/direction
   capture, and native motion commands.
5. Characterize the useful current, speed, acceleration, bus-voltage, and
   thermal envelope with encoder tracking as the acceptance measure.

## Stop conditions during motor development

End the active run and inspect telemetry for any of the following:

- Unexpected supply current or rapid heating.
- Any half-bridge commanding both MOSFETs on simultaneously.
- ADC clipping, current-sign disagreement, or repeated saturation at the
  configured voltage bound.
- Encoder discontinuity, loss of magnet status, or motion inconsistent with
  electrical frequency.
- Any latched ADC, PWM, deadline, overcurrent, reset, or communications fault.

## Suggested test record

For each session record:

- Date, board serial/revision, and firmware commit.
- Supply voltage and current limit.
- Motor/load connection state.
- Programmer, SWD speed, and reset wiring.
- Instrument models and probe points.
- Expected result, observed result, and captured waveforms.
- Decision to proceed, repeat, or stop.
