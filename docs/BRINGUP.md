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

The current interface accepts 1-495 counts, 0.001-250 Hz, and
0.003-2,147,483.647 second
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
757 mA / 20 Hz is accepted on this motor; stage the enabled 1.503 A, 2.25 A,
2.999 A, and 50-250 Hz evaluation points separately. The 256-sample startup trace is available after a run with
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
and encoder direction. Firmware 0.20.0 bench-validated the timestamped
mechanical estimator. Firmware 0.21.0 adds the production automatic-alignment
operation; it must pass the gate below before aligned torque or an outer loop
uses electrical phase. Firmware 0.22.0 adds automatic persistence after the
backend and motion authority are released.

Initial result on the tested motor: accepted. Two 757.4 mA runs each measured
`9302 → 9222 → 9302`, an -80-count quarter step versus 82 expected, direction
`-1`, and zero-count closure. Both completed in 2.55 seconds and released
authority without a current-loop, encoder, reset, or panic fault. A third run
stopped at 113 ms reported `aborted`, cleared backend/authority, and preserved
the accepted zero/direction. Menu and induced readiness-loss injection remain.

Use the already accepted motor, 12 V supply, and a current-limited supply
setting appropriate for the 757 mA test point. Confirm the flashed identity is
0.22.0 / protocol 1.6 and that the drive is `READY` with no faults:

```powershell
py tools/mks57d_rs485.py --port COM14 identity
py tools/mks57d_rs485.py --port COM14 status
py tools/mks57d_rs485.py --port COM14 encoder
py tools/mks57d_rs485.py --port COM14 alignment
py tools/mks57d_rs485.py --port COM14 configuration
```

Keep clear of the shaft: alignment deliberately steps the rotor between known
electrical states. Run the bounded sequence at the previously accepted current:

```powershell
py tools/mks57d_rs485.py --port COM14 align --current-ma 757.5 --interval 0.1
```

Acceptance requires all of the following:

1. State advances through phase-zero settle/sample, quarter settle/sample, and
   return settle/sample, then reports `complete` / `success` within four seconds.
2. Current/backend/authority flags remain active only during the operation and
   all clear afterward; current-loop and supervisor fault fields remain zero.
3. The observed quarter step is near 82 counts with direction `-1`; the initial
   software tolerance is ±12 counts. Each sampling window spans no more than
   8 raw counts and return closure is within 12 counts.
4. A following `encoder` query reports alignment and electrical phase valid,
   and a following `status` reports `READY` with the bridge backend inactive.
5. Run a second sequence and confirm repeatable zero, direction, quarter step,
   closure, and clean release. Do not promote the initial timing/tolerance
   candidates without recording the observed distributions.
6. `configuration` reports `record_valid`, `stored_calibration_valid`,
   `active_calibration_valid`, and `active_matches_record`; last result is
   `ok`, the selected slot is 0 or 1, and stored/active zero and direction are
   identical. Repeating an identical alignment must leave the generation and
   selected slot unchanged, proving the wear-avoidance path.

After ordinary and STOP alignment behavior pass, validate persistence without
reflashing:

1. Record `configuration`, `encoder`, `status`, and `boot` output.
2. Reset the controller, then repeat all four queries. The same zero and
   direction must load before motion, while supervisor authority and backend
   activity remain clear.
3. Power the controller off fully, wait for rails to discharge, restore power,
   and repeat the queries. Stored and active calibration must still match with
   no new fault or retained panic.
4. With the bridge inactive, issue the persistent clear and power-cycle:

   ```powershell
   py tools/mks57d_rs485.py --port COM14 clear-calibration
   ```

   `configuration`, `alignment`, and `encoder` must all report calibration
   invalid after the clear and after the power cycle. No authority or backend
   activity may appear.
5. Run the bounded alignment once more to restore the accepted calibration and
   verify a final power cycle.

The two slots protect runtime updates against an interrupted erase/program.
Preservation across a firmware flash depends on the programming tool's erase
policy and is not part of this gate; do not substitute reflashing for reset or
power-cycle testing.

Persistence result on the tested board: accepted. A 757.4 mA alignment saved
`9302 / -1 / 80` in slot 0 at generation 1; an unchanged explicit save left the
generation unchanged. A complete power cycle restored the same calibration
with authority, backend, references, duties, faults, and retained panic clear.
Persistent clear selected slot 1 at generation 2, remained invalid across a
second power cycle, and did not restore authority. A final 757.4 mA alignment
saved the accepted `9301 / -1 / 79` result in slot 0 at generation 3.

Validate stop separately by starting `align` and pressing Ctrl+C during the
first settle interval. The CLI sends the same generic command directly; the
standalone equivalent is:

```powershell
py tools/mks57d_rs485.py --port COM14 stop
```

The alignment result must become `aborted`, the prior accepted calibration must
remain valid and unchanged, and authority/backend activity must clear. Repeat
with Menu and then with an induced encoder-readiness loss; Menu is an orderly
abort, while readiness loss during authority must enter the common fault path.
Do not perform the readiness-loss injection until an immediate supply cutoff is
available and the ordinary/STOP runs have passed.

### Aligned q-current hardware gate

Firmware 0.23.1 connects signed torque-producing current to the calibrated
electrical phase through the production `RUN`/motion-authority path. This is not
a speed or position command: an unloaded shaft can accelerate. Keep clear of
the motor, use 12 V and the current-limited supply, start at or below the accepted
757 mA envelope, and keep generic STOP plus immediate supply cutoff available.

Confirm 0.23.1 / protocol 1.7, restored alignment, `READY`, and the complete
firmware policy before energizing:

```powershell
py tools/mks57d_rs485.py --port COM14 identity
py tools/mks57d_rs485.py --port COM14 configuration
py tools/mks57d_rs485.py --port COM14 status
py tools/mks57d_rs485.py --port COM14 torque-status
```

The initial status must report alignment valid, no active authority/backend,
±495 counts maximum current, 10,000 counts/s slew, 5 rev/s velocity, 1,000 rev/s²
acceleration, a 3 ms minimum duration, and a 2,147,483,647 ms maximum
duration. That maximum comes from wrap-safe 32-bit deadline arithmetic and is
not a thermal or motor limit; the caller still selects a finite interval for
every operation. The remaining values are independently enforced 0.23
candidate values, not physical capability claims. The 495-count current ceiling
is a 2.999 A evaluation point matching the attached motor's reported 3 A rating,
while the separately reported 600-count raw trip is about 3.635 A.

Begin with 5 counts (about 30.3 mA) for a conservative 100 ms, then inspect final
drive, encoder, torque, fault, reset, and panic state:

```powershell
py tools/mks57d_rs485.py --port COM14 torque --counts 5 --duration-ms 100 --interval 0.02
py tools/mks57d_rs485.py --port COM14 status
py tools/mks57d_rs485.py --port COM14 encoder
py tools/mks57d_rs485.py --port COM14 torque-status
py tools/mks57d_rs485.py --port COM14 boot
```

Acceptance requires `ramping`/`holding` samples followed by `complete` with
result `deadline`; motion authority and backend are active only during the run,
the A/B reference rotates with calibrated electrical phase, and the terminal
state is supervisor `READY` with zero backend/torque/estimator faults and no new
panic or reset. A safety result such as `overacceleration` is a successful
shutdown-path observation but does not pass the normal-run gate; preserve its
telemetry and tune only from measured evidence.

If clean, repeat at `--counts -5` and confirm the q-current and mechanical
response reverse. Then progress through ±25 counts (151.5 mA) and ±50 counts
(303 mA) at 100 ms. Do not advance after an unexpected fault, implausible phase
reference, encoder discontinuity, heating, or supply-current step. Reconfirm the
existing 757 mA point before evaluating 1.503 A (248 counts), 2.25 A (371 counts),
and finally 2.999 A (495 counts). Use a restrained or appropriately loaded shaft
for high-current torque evaluation so the independent velocity/acceleration
guards do not substitute an overspeed test for a current-loop test.

Validate multi-second duration and explicit STOP separately by starting a
5,000 ms, 5-count `torque` run
and pressing Ctrl+C while it is active. The CLI sends the same generic STOP as
the standalone `py tools/mks57d_rs485.py --port COM14 stop` command.
The result must become `stopped`, backend and authority must clear immediately,
and alignment/configuration must remain unchanged. Repeat Menu and induced
encoder/readiness-loss tests only after ordinary/deadline and STOP behavior pass;
the latter must enter the common fault/ZERO path.

After that gate, the next implementation sequence is:

1. Close the velocity loop at low gains and explicit current/velocity/acceleration bounds.
2. Add position trajectories, following-error detection, step/direction
   capture, and native motion commands.
3. Characterize the useful current, speed, acceleration, bus-voltage, and
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
