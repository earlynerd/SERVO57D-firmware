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

Current product motion operates an attached two-phase stepper through a 20 kHz
fixed-point rotating d/q PI for encoder-aligned current. Static vectors and
alignment retain independent stationary A/B PI, and the bounded rotating-current
diagnostic may select either controller for comparison. The retired local Left/Center selector
cannot request bridge authority. RS-485 requests diagnostic authority from the
product drive supervisor after current-path and encoder readiness and provides
configurable current amplitude, electrical
frequency, initial phase, run duration, STOP, and live current plus encoder
telemetry. Timeout, the physical Right button, transport failure, or STOP ends authority.

Use a current-limited bench supply and connect the motor normally. The accepted
run used `COM14`; replace it with the port reported by `list` when necessary.
For ordinary tuning, run the complete move, capture, analysis, and plotting loop
with one command:

```powershell
py tools/motor_test.py --port COM14 --current-ma 750 --rpm 24 --seconds 5
```

On protocol 1.10 the command reports live current, measured bus volts,
commanded carrier-average phase volts, and encoder angle; STOP is sent on
Ctrl+C. It then saves `telemetry.jsonl`, `trace.jsonl`, `summary.json`,
and a self-contained `report.html` under ignored `scratch/motor-runs/`, opens
the report, and restores the prior inactive test configuration. Use
`--no-open` when no browser is wanted and `--replot RUN_DIRECTORY` to rebuild a
saved report without touching the motor.

On protocol 1.16 or later, use the production tuner to compare PI candidates under the
same fixed current and electrical-frequency points. The sweep applies gains only
while inactive, runs each point through the same supervisor/current backend,
aborts on fault or abnormal terminal state, and restores the starting gains and
diagnostic configuration even after interruption:

```powershell
py tools/mks57d_tune.py --port COM14 sweep --kp 2,3,4 --ki 0.015625 --electrical-hz 5,20,50,100,200 --current-ma 303 --seconds 2
```

Protocol 1.17 retains stationary A/B PI as the default and adds
`--controller rotating`. For the first rotating-frame gate, keep the known
303 mA and Kp=9/Ki=0.5 point, stage only through 100 Hz, inspect normal release,
faults, missed PWM updates, and minimum preload margin, and only then repeat at
150/200 Hz:

```powershell
py tools/mks57d_tune.py --port COM14 sweep --controller rotating --kp 9 --ki 0.5 --electrical-hz 5,20,50,100 --current-ma 303 --seconds 2
```

The tuner defaults to a 50 electrical-Hz/s field ramp before every trial's
full `--seconds` hold window. For the present 50-cycle/revolution motor that is
1 rev/s²; a 200 Hz point therefore ramps for four seconds before its two-second
window. Set `--ramp-electrical-hz-per-second` deliberately for a different
fixture, or set it to zero only to reproduce the legacy instantaneous frequency
step. Trace capture and scored encoder motion begin after ramp plus the requested
settling interval.

Review `session.json`, `summary.csv`, and `report.html` under the printed
`scratch/tuning-runs/` session. The report includes both cross-trial comparisons
and the settled 20 kHz current/voltage waveform for every completed trial, plus
color-coded d/q current and voltage plots and the per-run total and maximum-
consecutive missed PWM update counts. Both counts should remain zero. Compare
controller modes only at identical current, gains, ramp, frequency, and hold
time; lower q-current error and better d-current tracking do not compensate for
lost timing margin or abnormal motion.
Replotting is offline. The sweep never persists a candidate. Apply the selected
gains in RAM first, repeat a bounded validation, then promote them explicitly
only after reviewing voltage headroom, overshoot, tracking, encoder motion,
timing, and faults:

```powershell
py tools/mks57d_tune.py --port COM14 apply --kp 4 --ki 0.015625
py tools/mks57d_tune.py --port COM14 persist --confirm-save-active-configuration
```

After persistence, power-cycle and confirm configuration generation, stored and
active gains, alignment, readiness, zero outputs, and fault/reset/panic state
before another motor command. Automatic alignment save and calibration clear
must not promote an unsaved volatile tuning trial.

Use the lower-level console when inspecting individual protocol operations:

```powershell
py tools/mks57d_rs485.py list
py tools/mks57d_rs485.py --port COM14 identity
py tools/mks57d_rs485.py --port COM14 boot
py tools/mks57d_rs485.py --port COM14 encoder
py tools/mks57d_rs485.py --port COM14 status
py tools/mks57d_rs485.py --port COM14 configure --current-ma 303 --frequency-hz 5
py tools/mks57d_rs485.py --port COM14 run --leg A1 --ramp-duration-ms 1000 --duration-ms 3000 --interval 0.1
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

The current interface accepts 1-495 counts, 0.001-1,000 Hz, and a
0.003-2,147,483.647 second hold. An optional nonnegative frequency ramp runs
before that hold, with ramp plus hold constrained to the same wrap-safe maximum.
Expand current, speed, bus voltage, and duration deliberately while
recording encoder tracking, current error, duty, supply current, and
temperature. An explicit `stop` command is available from another terminal or
after an interrupted watch.

`motor_test.py --rpm` is only a convenient positive speed-magnitude conversion
for this open-loop rotating vector. It does not yet regulate mechanical speed,
select direction, or command position; use encoder agreement and the generated
plots as the acceptance evidence until the aligned outer loops exist.

Firmware 0.19.0 used the 0.18.2 `Kp=2` with `Ki=1/64` per 20 kHz step. A 303 mA startup
step has 6.53 ms rise time and 8% overshoot. The tested motor tracked 606 mA /
15 Hz at -17.78 RPM and completed 1.97 revolutions during a five-second 757 mA /
20 Hz run, with zero faults and 25.2% peak voltage effort. Operation through
757 mA / 20 Hz is accepted on this motor; stage the enabled 1.503 A, 2.25 A,
2.999 A, and 50-1,000 Hz evaluation points separately. The upper frequency is
an implementation search limit for other motors, not evidence for this motor.
The 256-sample startup trace is available after a run with
`trace`; analyze saved JSON lines with `tools/analyze_current_trace.py`.
Firmware 0.30.3 bench-tested Kp=4 at +8 rev/s without changing Ki or any
electrical bound; firmware 0.31.0 makes both gains volatile-first product
configuration so a different motor or supply can be measured rather than
inheriting that candidate blindly.

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
the accepted zero/direction. The alignment-specific Right-button test remains.
Physical readiness-loss injection is indefinitely deferred on this assembly.

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
with the Right button, which is an orderly abort. Physical encoder/readiness-loss
injection is indefinitely deferred on the current assembly because the encoder
is inaccessible without risking damage. Reinstate that physical test only if a
future assembly or fixture provides a non-destructive injection mechanism; keep
the automated common fault/ZERO tests active.

### Aligned q-current hardware gate

Firmware 0.23.2 connects signed torque-producing current to the calibrated
electrical phase through the production `RUN`/motion-authority path. This is not
a speed or position command: an unloaded shaft can accelerate. Keep clear of
the motor, use 12 V and the current-limited supply, start at or below the accepted
757 mA envelope, and keep generic STOP plus immediate supply cutoff available.
Firmware 0.36.0 changes the aligned-q inner controller from stationary A/B PI
to rotating d/q PI but retains this authority and safety procedure. Treat its
first flash as a new controller gate inside the already proven bridge/timing
backend: repeat both signs at 30.3 mA before increasing current or speed.

Confirm 0.23.2 / protocol 1.7, restored alignment, `READY`, and the complete
firmware policy before energizing:

```powershell
py tools/mks57d_rs485.py --port COM14 identity
py tools/mks57d_rs485.py --port COM14 configuration
py tools/mks57d_rs485.py --port COM14 status
py tools/mks57d_rs485.py --port COM14 torque-status
```

The initial status must report alignment valid, no active authority/backend,
±2.999 A nominal maximum current (495 raw counts), 60.59 A/s nominal slew
(10,000 raw counts/s), 20 rev/s observed velocity, 1,000 rev/s²
acceleration, a 3 ms minimum duration, and a 2,147,483,647 ms maximum
duration. That maximum comes from wrap-safe 32-bit deadline arithmetic and is
not a thermal or motor limit; the caller still selects a finite interval for
every operation. The remaining values are independently enforced 0.23
candidate values, not physical capability claims. The 2.999 A current ceiling
matches the attached motor's reported 3 A rating,
while the separately reported 600-count raw trip is about 3.635 A.

Begin with 5 counts (about 30.3 mA) for a conservative 100 ms, then inspect final
drive, encoder, torque, fault, reset, and panic state:

```powershell
py tools/mks57d_rs485.py --port COM14 torque --current-ma 30.3 --duration-ms 100 --interval 0.02
py tools/mks57d_rs485.py --port COM14 status
py tools/mks57d_rs485.py --port COM14 encoder
py tools/mks57d_rs485.py --port COM14 torque-status
py tools/mks57d_rs485.py --port COM14 boot
```

Acceptance requires `ramping`/`holding` samples followed by `complete` with
result `deadline`; motion authority and backend are active only during the run,
the A/B reference rotates with calibrated electrical phase, and the terminal
state is supervisor `READY` with zero backend/torque/estimator faults and no new
panic or reset. Torque activation waits for a newly accepted encoder sample;
the following sample is the first active feedback interval, so foreground
serial-service time before authority cannot create an immediate timing fault.
A safety result such as `overacceleration` is a successful
shutdown-path observation but does not pass the normal-run gate; preserve its
telemetry and tune only from measured evidence.

If clean, repeat at `--current-ma -30.3` and confirm the q-current and mechanical
response reverse. Then progress through ±25 counts (151.5 mA) and ±50 counts
(303 mA) at 100 ms. Do not advance after an unexpected fault, implausible phase
reference, encoder discontinuity, heating, or supply-current step. Firmware
already permits 1.503 A (248 counts), 2.25 A (371 counts), and 2.999 A
(495 counts); using the existing 757 mA point first provides a same-bench
comparison rather than unlocking those requests. Use a restrained or appropriately loaded shaft
for high-current torque evaluation so the independent velocity/acceleration
guards do not substitute an overspeed test for a current-loop test.

Validate multi-second duration and explicit STOP separately by starting a
5,000 ms, 5-count `torque` run
and pressing Ctrl+C while it is active. The CLI sends the same generic STOP as
the standalone `py tools/mks57d_rs485.py --port COM14 stop` command.
The result must become `stopped`, backend and authority must clear immediately,
and alignment/configuration must remain unchanged. Repeat with the Right button
only after ordinary/deadline and STOP behavior pass. Physical encoder/readiness-
loss injection is indefinitely deferred on this assembly; its common fault/ZERO
contract remains an automated regression.

### Low-speed velocity hardware gate

> **Evaluation warning:** live policy reports permission, not guaranteed
> performance. Accepted commands can saturate current, track poorly, stall,
> fault, heat the motor/drive, or produce unexpectedly energetic motion. Use a
> suitable fixture and current-limited supply, bound every run, and keep the
> physical Right-button stop and supply cutoff immediately available.

Firmware 0.25.1 / protocol 1.8 closes the first product velocity loop on the
authoritative 1 kHz rotor observation. It commands only the existing bounded
aligned-q-current actuator and applies the direction already measured and
persisted by alignment; it does not add a PWM or bridge-authority path. No new
alignment is required when upgrading from a valid stored configuration.
The candidate permits targets through ±1 rev/s, slews the reference at
1 rev/s², and requires each command to state a positive current limit no higher
than 100 counts (about 606 mA). Use a free, observable shaft initially; keep
clear of it and retain current-limited supply cutoff plus generic STOP.

After flashing, confirm the exact identity, restored alignment, no faults, and
the live velocity policy before energizing:

```powershell
py tools/mks57d_rs485.py --port COM14 identity
py tools/mks57d_rs485.py --port COM14 configuration
py tools/mks57d_rs485.py --port COM14 status
py tools/mks57d_rs485.py --port COM14 velocity-status
```

The first run uses 0.1 rev/s (6 RPM), a 25-count limit (about 151.5 mA), and a
two-second deadline:

```powershell
py tools/mks57d_rs485.py --port COM14 velocity --rps 0.1 --current-limit-ma 151.5 --duration-ms 2000 --interval 0.02
```

Acceptance requires ramping then tracking telemetry, a reference slope no
greater than 1 rev/s², measured velocity approaching the signed target without
oscillation or runaway, requested/applied current staying within 25 counts,
and terminal `complete` / `deadline`. Afterward supervisor state must be
`READY`; authority, backend, actuator, and active flags must be clear; all
current, velocity, torque, estimator, reset, and panic faults must remain clear.
The CLI saves every run under `scratch/velocity-runs/` by default. Preserve the
run directory: `metadata.json` contains the request, identity, configuration,
policy, endpoints, and summary; `telemetry.csv` contains the compact time
series used for the first rise, overshoot, steady-error, saturation, and current
decision. The terminal refreshes one concise status line at about 5 Hz rather
than printing each nested snapshot. Add `--jsonl` only when full protocol-level
snapshots are needed, or `--output-root PATH` to relocate the run directories.

If clean, repeat at `--rps -0.1` and verify direction reverses. Then run a
five-second command and press Ctrl+C during tracking; status must retain
`stopped`, with immediate authority/backend release and unchanged calibration.
For deterministic capture without an interactive terminal, use a longer
firmware deadline and schedule the same generic STOP on the active connection:

```powershell
py tools/mks57d_rs485.py --port COM14 velocity --rps 0.1 --current-limit-ma 303 --duration-ms 5000 --stop-after-seconds 2 --interval 0.02
```

Repeat the STOP check with the physical Right button. Those checks establish a
known comparison point; firmware permission is not conditional on completing
them. Current-limit saturation is
an intentional test: confirm `current_at_limit`, bounded recovery, and no
integrator-driven overshoot while selecting the next request from the live
2.999 A nominal policy.
Physical encoder/readiness-loss injection is indefinitely deferred on the
current board/motor assembly. Its common fault/ZERO contract remains covered by
host/native regression tests and should be physically re-opened only when a
non-destructive injection mechanism exists.

Bench status on 2026-08-21: firmware 0.25.1 passed the mirrored ±0.1 rev/s,
25-count, two-second deadline gates. Both directions followed the requested raw
encoder coordinate, stayed below 25 counts, held sampled encoder intervals to
1,000 us, reported no faults, and released all current references and bridge
duties at deadline. A 50-count/303 mA command accepted generic STOP after two
seconds with clean release. A 1 rev/s, 100-count/606 mA command accepted the
physical Right button. Across three captures, including two hand-loaded shaft
disturbances, the controller
reported 29, 11, and 23 current-limit samples; two runs returned to about
0.99 rev/s with no faults and no material overshoot, while the first ended still
current-limited and is not counted as a recovery. The reported disturbances
are qualitative rather than calibrated torque measurements; a guarded brake or
load fixture is preferred for repeatable measurements. Induced encoder/readiness-
loss injection is indefinitely deferred on this assembly. With that explicit
scope deferral, initial velocity qualification is accepted.

After that gate, the next implementation sequence is:

1. Bench-qualify the focused relative-position command through settle, STOP,
   Right-button stop, and following-error behavior.
2. Stage the expanded velocity envelope through 2, 4, 5, 8, 12, and 16 rev/s in both
   signs while preserving the capture directories.
3. Add step/direction capture and the broader motion/application commands.
4. Characterize the useful current, speed, acceleration, bus-voltage, and
   thermal envelope with encoder tracking as the acceptance measure.

### Relative-position hardware gate

Firmware 0.26.0 / protocol 1.9 adds a bounded relative-position trajectory to
the production rotor runtime. The position controller generates a trapezoidal
reference and a bounded position correction, then changes only the target of
the existing velocity PI and aligned-q-current actuator. The command has
separate relative-travel, velocity, acceleration, current, start-speed,
following-error, feedback-age, settling, and duration limits. It starts only
when measured speed is at most 0.1 rev/s. No new alignment is required when the
stored calibration is valid.

After flashing, confirm firmware `0.26.0`, protocol `1.9`, restored calibration,
zero faults, and the passive position policy:

```powershell
py tools/mks57d_rs485.py --port COM14 identity
py tools/mks57d_rs485.py --port COM14 configuration
py tools/mks57d_rs485.py --port COM14 status
py tools/mks57d_rs485.py --port COM14 position-status
```

Begin with a quarter revolution at 30 RPM maximum, 1 rev/s², 100 counts, and a
three-second deadline:

```powershell
py tools/mks57d_rs485.py --port COM14 position --revolutions 0.25 --max-rpm 30 --acceleration-rps2 1 --current-limit-ma 606 --duration-ms 3000 --interval 0.02
```

Acceptance requires `moving`, then `settling`, then `complete` / `settled`;
profile and corrected velocity must remain within the requested bounds,
requested/applied current must stay within 100 counts, and the final measured
position must be within 0.002 revolution at no more than 0.02 rev/s for 50
consecutive samples. After completion, supervisor state returns to `READY`,
authority/backend/actuator activity clears, all current references and bridge
duties return to zero, and every position, velocity, actuator, encoder,
current-loop, reset, and watchdog fault remains clear. A terminal `deadline`
is a safe release but not a successful position move.

Repeat with `--revolutions -0.25`, then with a long deadline issue generic
STOP and repeat using the physical Right button. Apply shaft load during a
separate 100-count move and confirm either bounded recovery or a following-error
failure at 0.25 revolution; a failure must enter the common fault/`ZERO` path
without exceeding the current limit. Preserve each generated directory under
`scratch/position-runs/`: `metadata.json` retains the request, policies,
endpoints, and summary, while `telemetry.csv` contains the compact trajectory,
error, current, drive, and encoder time series. Add `--jsonl` only when complete
nested protocol snapshots are needed.

Bench status on 2026-08-22: firmware 0.26.0 passed mirrored ±0.25-revolution
moves at 0.5 rev/s maximum, 1 rev/s², and 100 current counts. The positive move
settled in about 1.29 s with -0.000427 revolution endpoint error; the negative
move settled in about 1.35 s with +0.001465 revolution error. Both used at most
32 current counts, kept maximum profile following error below 0.05 revolution,
held captured encoder intervals to 1000 us, and reported no faults. A separate
+0.5-revolution move accepted scheduled generic STOP at 0.5 s, finalized its
capture, cleared current references and bridge duties, and coasted to zero
measured speed without changing generation-3 calibration. Physical Right-button
stop and loaded following-error behavior remain pending.

### Firmware 0.27.1 liveness, phase-prediction, and motion-policy smoke gate

After flashing 0.27.1, confirm identity still reports protocol 1.9 and encoder
schema 2. Before any motor command, sample `encoder` repeatedly: accepted count
must advance, `estimator_ready` must remain true, and the latest interval should
remain near 1000 us. Then repeat one already-qualified low-speed velocity or
±0.25-revolution position command and require normal completion, zero terminal
references/duties, and no new encoder, estimator, backend, reset, or panic
fault. The new 3 ms total-production deadline is host-tested; do not disturb the
inaccessible sensor on this assembly solely to inject the fault. Reopen physical
injection only with a non-destructive scheduler/test-point fixture.

Use the same already-qualified low-speed move as the first predictor gate. The
aligned-torque status A/B fields now report the references actually used by the
20 kHz backend; they should advance between 1 kHz encoder samples while q-current
is nonzero. Require zero prediction/backend faults and clean terminal A/B
references. Firmware 0.30.0 measured the DMA-to-PWM application interval and
firmware 0.30.1 compensates it with a 55 us lead. Also
measure or bound the MT6816 angle-acquisition instant relative to the timestamp
published at completion of its four-byte SPI transaction; the predictor cannot
remove an uncharacterized constant sensor/transport phase bias.

Confirm `velocity-status` reports 16 rev/s, 256 rev/s², 495 counts, and a
20 rev/s observed-speed boundary. Confirm `position-status` reports 16 rev/s,
64 rev/s², and 495 counts. A position capture may show a corrected velocity
target above the requested profile speed, through 17 rev/s; this is intentional
servo headroom, not a protocol-policy mismatch.

Accepted 2026-08-22 result: firmware 0.27.1 reported the expected identity and
live policy, restored generation-3 calibration, advanced encoder samples at
nominal 1 kHz, and completed bounded positive-velocity requests through
12 rev/s with clean terminal references/duties and no predictor, encoder,
backend, current-loop, supervisor, reset, or panic fault. Active captured
encoder intervals remained 998-1,002 us. The physical sensor-loss injection
remains deferred as described above.

### Firmware 0.28.0 physical electrical-units and VBUS gate

After flashing 0.28.0, confirm identity reports protocol 1.10 and passive
`status` reports commissioning schema 3 with `vbus_snapshot_valid`, a steadily
advancing VBUS sample count, and a bus-voltage value consistent with the supply
or a DMM. With the bridge inactive at the present 24 V supply setting, stop and
inspect wiring/scaling if the reported value is outside 22-26 V; record the DMM
comparison before assigning a tighter production tolerance. Current commands
and motion live lines should use amperes, measured VBUS, commanded phase volts,
and the phase-voltage limit in volts. Raw counts and ratios remain diagnostic
fields, not the primary command/display units.

Then prove the automatic-injected conversion did not disturb the 20 kHz regular
DMA deadline with an already bounded low-energy motion run:

```powershell
py tools/mks57d_rs485.py --port COM14 status
py tools/mks57d_rs485.py --port COM14 velocity --rpm 60 --current-limit-ma 606 --duration-ms 1000 --interval 0.02 --jsonl
```

Require VBUS validity/sample advancement throughout, phase commands no greater
than the reported voltage limit, normal deadline release, terminal zero
references/duties, and no ADC, deadline, predictor, backend, supervisor, reset,
or panic fault. This active run is the timing confirmation; the inactive voltage
comparison alone does not validate automatic-injected conversion under the
20 kHz schedule.

This gate passes on the tested board. Identity reported firmware 0.28.0 and
protocol 1.10. Inactive schema-3 status reported 23.829 V at the 24 V supply
setting with all bridge duties zero. Across all 22 samples from the documented
one-second run, VBUS held 23.776-23.815 V and phase command peaked at 2.88 V against a 16.66 V
reported limit, encoder samples remained at 1,000 us in the live display, and
the run completed 20,001 current-loop updates. Post-run status reported
23.815 V, an advanced VBUS accepted-sample count, zero current references and
bridge duties, no authority, and no ADC, deadline, predictor, encoder, backend, supervisor,
reset, or panic fault.

### Firmware 0.29.2 coherent-timebase gate

Firmware 0.29.1 proved following-error acknowledgment, calibration
preservation, no-reset recovery, propagated backend-fault recovery, and
`no_fault` idempotence. One later move ran for its complete three-second
deadline, but the following mirrored move retained predictor age
`0xFFFFFE58`: unsigned -424 us while encoder intervals remained 1,000-1,014 us.
This identifies the preempted-SysTick epoch race rather than true stale
feedback. After flashing 0.29.2, confirm identity reports protocol 1.12 and the
drive returns to `READY` with generation-3 calibration and no fault/reset/panic
evidence.

Run three alternating pairs through the same low-energy position path that
exposed the race, preserving every automatic capture:

```powershell
py tools/mks57d_rs485.py --port COM14 position --revolutions 0.25 --max-rps 0.5 --acceleration-rps2 1 --current-limit-counts 100 --duration-ms 5000 --interval 0.05 --jsonl
py tools/mks57d_rs485.py --port COM14 torque-status
py tools/mks57d_rs485.py --port COM14 position --revolutions -0.25 --max-rps 0.5 --acceleration-rps2 1 --current-limit-counts 100 --duration-ms 5000 --interval 0.05 --jsonl
py tools/mks57d_rs485.py --port COM14 torque-status
```

Each run must end with ordinary authority/current/duty release, no
predictor/backend/encoder/supervisor fault, no reset or panic, and preserved
calibration. Schema 2 must report rejection reason `none` and maximum successful
prediction age no greater than the configured 3,000 us. Settled completion is
useful position evidence but is not required to prove clock coherence; a clean
finite deadline is acceptable. If any prediction fault occurs, preserve
`torque-status` before `clear-faults`, because recovery clears per-run predictor
evidence. Do not increase the predictor horizon in response to another
unsigned future-age value.

This gate passes on the tested board. Firmware 0.29.2 completed three
alternating +0.25/-0.25-revolution pairs at 0.5 rev/s, 1 rev/s², and 100 current
counts. All six retained predictor rejection reason `none`; maximum successful
age was 1,435-1,483 us against the unchanged 3,000 us limit. Every run ended
with zero applied current, zero phase references, zero bridge duties, no
predictor/backend/encoder/supervisor fault, no reset or panic, and preserved
generation-3 calibration. The moves reached their finite deadline with
repeatable endpoint errors of approximately -0.0021 to -0.00275 revolution in
the positive direction and +0.0025 to +0.0027 revolution in the negative
direction. That offset remains position-tuning evidence; it does not invalidate
the coherent-timebase result.

### Expanded velocity evaluation gate

Firmware 0.27.1 permits a direct velocity target through 16 rev/s, inner
reference slew through 256 rev/s², and per-command current through 2.999 A
nominal (495 raw counts).
The independent estimator/observed-speed shutdown is 20 rev/s. Positive
operation has now reached target through 8 rev/s at 24 V and exposed a
q-current/phase-voltage-saturated boundary near 10 rev/s during a 12 rev/s
request. The larger command space remains permission to find poor tracking,
saturation, torque ripple, heating, or another real boundary rather than having
software preflight hide it. At 16 mechanical
rev/s on the 50-cycle/revolution motor, firmware advances phase on 25 current-
loop events per electrical cycle and receives 1.25 encoder observations per
electrical cycle.

Use direct velocity commands to characterize the phase predictor without the
position following-error policy. Run both signs and retain each automatic
capture directory. Positive-direction captures currently cover 1, 2, 4, 5, 6,
8, and a 12 rev/s request; repeat the informative points in the negative sign
while improving the measured current-tracking boundary. The sequence is a
comparison plan, not a firmware unlock condition:

```powershell
py tools/mks57d_rs485.py --port COM14 velocity --rpm 120 --current-limit-ma 1503 --duration-ms 3000 --interval 0.02
py tools/mks57d_rs485.py --port COM14 velocity --rpm 240 --current-limit-ma 3000 --duration-ms 3000 --interval 0.02
py tools/mks57d_rs485.py --port COM14 velocity --rpm 300 --current-limit-ma 3000 --duration-ms 3000 --interval 0.02
py tools/mks57d_rs485.py --port COM14 velocity --rpm 360 --current-limit-ma 3000 --duration-ms 3000 --interval 0.02 --jsonl
py tools/mks57d_rs485.py --port COM14 velocity --rpm 480 --current-limit-ma 3000 --duration-ms 3000 --interval 0.02 --jsonl
py tools/mks57d_rs485.py --port COM14 velocity --rpm 720 --current-limit-ma 3000 --duration-ms 3000 --interval 0.02 --jsonl
```

On firmware 0.30.0 or newer, add a steady-state timing burst
to the retained +8 rev/s and +12 rev/s boundary runs. The arm request is sent on
the active serial connection at one second; after STOP/deadline release the host
downloads the 256 samples to `current_trace.csv` beside the ordinary compact
telemetry and metadata:

```powershell
py tools/mks57d_rs485.py --port COM14 velocity --rps 8 --current-limit-ma 3000 --duration-ms 3000 --interval 0.02 --trace-at-seconds 1
py tools/mks57d_rs485.py --port COM14 velocity --rps 12 --current-limit-ma 3000 --duration-ms 3000 --interval 0.02 --trace-at-seconds 1
```

Stop on any abnormal tracking, supply, mechanical, thermal, reset, panic, or
fault evidence. For each accepted burst record the minimum/maximum TIM2 ADC
trigger phase, trigger-to-DMA microseconds, DWT DMA-to-preload and
DMA-to-trace-record microseconds, TIM3 preload margin, prediction age, phase,
and A/B tracking error. The nominal trigger compare is 80% of the 50 us
carrier; the trace is measurement evidence, not permission to change sampling,
duty, voltage, or prediction lead.

The first +8 rev/s firmware 0.30.0 burst completed normally with no faults. It
measured a 41.094 us trigger, 3.938 us trigger-to-DMA interval, and
20.578-21.141 us DMA-to-stage path. The staging crossed the 50 us update and
left 33.031-33.594 us to the 100 us application boundary, proving that the old
7 us predictor lead was one carrier short. Firmware 0.30.1 corrects that lead
to 55 us without changing the ADC/PWM schedule or electrical bounds. After
flashing 0.30.1, repeat only the +8 rev/s command first; accept normal release,
stable timing, improved alignment, and clear faults before the +12 rev/s run.

Use the Saleae only on the 3.3 V MCU-side PWM/control signals (PA6, PA7, PB0,
PB1 or another verified low-voltage test point) with a common ground. Do not
connect a 5 V-limited logic input to the 12-24 V motor-phase outputs. Capture
carrier period, duty transitions, and the update boundary while the internal
burst is armed; use a properly rated differential scope probe if motor-terminal
waveforms are needed. The logic capture validates external edge timing, while
the SRAM burst separates ADC/DMA latency, computation, and preload margin.

At each step record sign, the bounded 256 rev/s² reference slope, overshoot,
current saturation, encoder/control/current/backend faults, and release state.
Persistent saturation, poor tracking, objectionable torque ripple, excessive
voltage use, supply instability, or heating is a valid boundary measurement;
end that run, preserve its capture, and use it as the next engineering input.

The accepted captures identify the present boundary. At 12 V and +6 rev/s,
commanded phase voltage reached the nominal 8.4 V limit in 27/62 active samples
and measured-current-vector magnitude peaked near 0.90 A against a 2.999 A
request;
bench motion appeared smooth, the supply did not enter current limit, and no
temperature rise was perceptible. Repeating at 24 V eliminated q-demand and
phase-voltage clipping, reduced RMS velocity error from 1.197 to 0.638 rev/s,
and required at most 1.81 A q-current. At 24 V and +8 rev/s the motor
reached/passed target, q-demand peaked at 2.41 A, and phase voltage touched
the clamp in 2/61 active samples. A +12 rev/s request then saturated q-demand in
46/61 samples and the nominal 16.8 V phase ceiling in 45/61 samples, plateauing
near 9.1-9.9 rev/s.
The next speed work is high-electrical-frequency current bandwidth/phase
tracking and predictor timing, not another +16 rev/s saturated run.

Normal deadline, generic STOP, and Right-button release end in the same all-low
zero-voltage vector used by the safe-state backend. With the tied EG3013 inputs,
this selects both low-side FETs across each winding and dynamically brakes the
motor. The board exposes no proven all-FET-off state and therefore has no true
coast mode. A future controlled deceleration may soften normal stops, but it is
not coast and must not replace immediate all-low braking on faults.

For position tests, the maximum profile acceleration is 64 rev/s² while the
inner velocity reference has 256 rev/s² slew and the corrected target has
1 rev/s of speed headroom. Raising profile acceleration moves the ideal
trajectory away faster and therefore does not cure a genuine torque-limited
following error. If the 0.25-revolution fault remains, inspect
`current_at_limit`: saturation means the selected current/plant cannot deliver
the requested trajectory, while a fault without saturation points to loop
tuning, estimator lag, phase quality, or mechanics.

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
