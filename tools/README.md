# Project Tools

Project-owned host tools, probe firmware configuration, build helpers, and test utilities live here.

Manufacturer executables and archives belong under ignored `vendor/local/`, not in this directory.

## Production disposition

The motor tools below are product service and engineering-diagnostic tools, not
an alternate commissioning firmware stack. `motor_test.py`, the identity/status/
encoder/alignment/torque/velocity/position/STOP portions of `mks57d_rs485.py`, the
current trace, and both analyzers
are retained because they provide repeatable acceptance, tuning, and fault
evidence. Current-test wire names from native protocol 1.3 remain compatibility
labels; START is now a diagnostic request to the product drive supervisor and
cannot directly own the bridge. The historical commissioning-image and
bridge-characterization build aliases have been removed.

The transitional local phase selector and its unused OLED renderer have been
removed. The retained rotating-current workflow is an RS-485 production
diagnostic through the supervisor and current backend, not a second local
control path.

`motor_test.py` is the normal human-facing motor diagnostic and regression loop. It runs one
firmware-bounded rotating-current move, captures the diagnostic stream and
20 kHz startup trace, analyzes current tracking and encoder motion, writes a
self-contained HTML report with four plots, and opens it. On protocol 1.4+
also rejects an unready or faulted mechanical estimator and records the worst
observed estimator sample interval; saved protocol-1.3 runs remain supported:

```powershell
py tools/motor_test.py --port COM14 --current-ma 750 --rpm 24 --seconds 5
```

The equivalent frequency form is `--electrical-hz 20`. Press Ctrl+C to send
STOP. Results go to a timestamped directory under ignored
`scratch/motor-runs/`; the previous inactive current-test configuration is
restored by default. Add `--no-open` for capture-only use, or regenerate a
saved report without accessing the board:

```powershell
py tools/motor_test.py --replot scratch/motor-runs/RUN_DIRECTORY
```

On protocol 1.10 the report plots A/B reference and measured current, encoder
motion versus the expected movement, commanded carrier-average phase voltage
in volts versus its voltage ceiling, and the first 12.8 ms at 20 kHz. Raw
controller ratios remain in the saved JSON for diagnostic use. The present
`motor_test.py --rpm` input is a positive speed magnitude
converted using the tested motor's 50 electrical cycles per mechanical
revolution. It is not a closed-loop command; use the separate
`mks57d_rs485.py velocity` product service for signed, regulated speed.

`mks57d_tune.py` builds a comparison session from those same bounded product
diagnostics. Protocol 1.15 applies each Kp/Ki pair only as inactive volatile
configuration, runs fixed current/frequency points, restores the starting gains
and inactive diagnostic configuration even after an aborted trial, and never
persists a sweep result:

```powershell
py tools/mks57d_tune.py --port COM14 sweep --kp 2,3,4 --ki 0.015625 --electrical-hz 5,20,50,100,200 --current-ma 303 --seconds 2
```

Protocol 1.17 adds `--controller rotating` for a fixed-point d/q comparison;
omitting it keeps the established stationary A/B PI and legacy wire request.
Use identical current, gains, ramp, frequency, and hold time when comparing:

```powershell
py tools/mks57d_tune.py --port COM14 sweep --controller rotating --kp 9 --ki 0.5 --electrical-hz 5,20,50,100 --current-ma 303 --seconds 2
```

Each trial now ramps its rotating field from zero before the complete
`--seconds` hold/test window. The default is 50 electrical Hz/s (1 rev/s² for
the present 50-cycle/revolution motor), so a 200 Hz trial ramps for four seconds
and then holds for the requested two seconds. Override it with
`--ramp-electrical-hz-per-second RATE`, or use `0` only when an intentional
legacy frequency step is wanted. The high-resolution trace is armed after ramp
plus `--settle-seconds`; reports and CSV files retain the ramp duration
separately from the scored hold duration.

Each timestamped `scratch/tuning-runs/*-current-loop/` session contains
`session.json`, `summary.csv`, a self-contained `report.html`, and per-trial
telemetry/trace JSON lines plus `trial.json`. The report combines cross-trial
gain, phase, error, voltage, motion, and timing comparisons with each trial's
settled 20 kHz current and phase-voltage waveforms and per-run
missed-PWM-update evidence. It also transforms every trace into the commanded
frame and plots color-coded d/q current and voltage with mean, ripple RMS,
peak-to-peak, and d/q error metrics. Rebuild it without opening
the serial port with `--replot SESSION_DIRECTORY`. Promotion is deliberately
separate:

```powershell
py tools/mks57d_tune.py --port COM14 apply --kp 4 --ki 0.015625
py tools/mks57d_tune.py --port COM14 persist --confirm-save-active-configuration
```

The apply command changes RAM only. Persist uses the firmware's safe-state
dual-slot configuration transaction and should follow review of the saved
report; the sweep never chooses or saves a winner automatically.

`build.ps1` configures and builds the Arm firmware and/or the native unit tests. It does not flash hardware or execute anything from `vendor/local/`.

```powershell
pwsh -File tools/build.ps1 -Target all
```

`mks57d_rs485.py` is the lower-level native protocol product-service console. It uses PySerial,
prints machine-readable JSON, and can inspect the current loop before, during,
and after a duration-bounded remote run:

```powershell
py tools/mks57d_rs485.py list
py tools/mks57d_rs485.py --port COM14 identity
py tools/mks57d_rs485.py --port COM14 boot
py tools/mks57d_rs485.py --port COM14 encoder
py tools/mks57d_rs485.py --port COM14 alignment
py tools/mks57d_rs485.py --port COM14 configuration
py tools/mks57d_rs485.py --port COM14 align --current-ma 757.5 --interval 0.1
py tools/mks57d_rs485.py --port COM14 save-configuration
py tools/mks57d_rs485.py --port COM14 clear-calibration
py tools/mks57d_rs485.py --port COM14 torque-status
py tools/mks57d_rs485.py --port COM14 torque --current-ma 151.5 --duration-ms 250
py tools/mks57d_rs485.py --port COM14 velocity-status
py tools/mks57d_rs485.py --port COM14 velocity --rps 0.1 --acceleration-rps2 16 --current-limit-ma 151.5 --duration-ms 2000 --trace-at-seconds 1
py tools/mks57d_rs485.py --port COM14 position-status
py tools/mks57d_rs485.py --port COM14 position --revolutions 0.25 --max-rpm 30 --acceleration-rps2 1 --current-limit-ma 606 --duration-ms 3000
py tools/mks57d_rs485.py --port COM14 status
py tools/mks57d_rs485.py --port COM14 configure --current-ma 303 --frequency-hz 5
py tools/mks57d_rs485.py --port COM14 run --leg A1 --ramp-duration-ms 1000 --duration-ms 3000 --interval 0.1
py tools/mks57d_rs485.py --port COM14 trace --output scratch/current-trace.jsonl
py tools/mks57d_rs485.py --port COM14 stop
py tools/mks57d_rs485.py --port COM14 clear-faults
```

`encoder` reports the live 14-bit magnetic angle and health counters. On
protocol 1.4+ it also reports unwrapped mechanical position, filtered velocity,
estimator faults, alignment/electrical-phase validity, and the latest and
maximum observed sample intervals. The decoder remains compatible with the
shorter protocol-1.3 encoder schema. `watch` and `run` attach the same encoder
snapshot to every current-loop record so electrical commutation and physical
rotor motion can be compared directly.

On firmware 0.21.0 and later, `alignment` reads the production alignment state without
energizing the motor. `align` requests the bounded phase-zero/quarter/return
sequence, polls its structured result, and sends the generic drive STOP on
Ctrl+C. The recommended first bench command uses the already accepted current
point shown above. The status output includes the firmware-reported current,
timing, sample-count, stability, geometry, closure, and current-error policy;
the CLI preflights current against that contract before START. Calibration is
updated only if all of those runtime checks pass. On protocol 1.6, successful
alignment is automatically persisted after the backend stops and motion
authority is released. `configuration` compares the stored record with the
active alignment and reports generation, selected/valid slots, and the last
storage result. `save-configuration` is an idempotent explicit retry;
`clear-calibration` first persists the invalid state and then clears the live
calibration. Both write operations are rejected while any drive operation or
pending start/stop owns the safe-state boundary.

On firmware 0.23.2 and later / protocol 1.7, `torque-status` reads the complete aligned
q-current state and firmware-owned policy without energizing the bridge.
`torque` accepts signed counts or signed milliamperes, preflights the absolute
current and duration against that reported policy, starts the supervisor-owned
bounded operation, and streams state until deadline, STOP, or fault. Ctrl+C
sends generic STOP. The evaluation policy permits ±495 counts (±2.999 A nominal)
and accepts an explicit 3 ms through 2,147,483,647 ms finite deadline. The
upper duration is the wrap-safe timer representation limit, not a motor-current
or thermal rating. The current ceiling matches the attached motor's reported
3 A rating and deliberately opens evaluation above the 757.4 mA validated point;
it is not yet a qualified continuous-current rating. The hardware gate starts at
±25 counts (±151.5 mA) for 250 ms and advances from measured results. This
command produces torque and can accelerate the shaft; it does not request or
regulate velocity. The evaluation shutdown policy permits 20 rev/s (1,200 RPM),
1,000 rev/s² observed acceleration, and 10,000 counts/s current slew so poor
tracking and phase-prediction/estimator boundaries can be measured instead of preflighted
away.
On firmware 0.29.1 / protocol 1.12, `torque-status` also reports the last
predictor rejection reason and age, the maximum successful prediction age in
the run, and the configured 3,000 us horizon. The host remains compatible with
the shorter protocol-1.11/schema-1 response, where those fields are unavailable.

Firmware 0.26.0 / protocol 1.9 retains the qualified velocity service and adds
relative position. Firmware 0.26.1 keeps the same host protocol and makes the
encoder `estimator_ready` field clear if accepted sample production has not
advanced within 3 ms. Firmware 0.27.1 also keeps protocol 1.9 and advances
electrical phase/A-B references in the 20 kHz backend; existing torque, drive,
velocity, position, and encoder captures require no decoder changes. `velocity` accepts either `--rps` or `--rpm`, while
`velocity-status` is passive and reports target, acceleration, feedback-speed,
current, feedback-age, PI-gain, and deadline policy. `velocity` accepts a
signed mechanical target in revolutions per second, an explicit positive
reference acceleration (default 16 rev/s²), and a positive q-current limit in
counts or nominal milliamperes. It preflights the request
against the live firmware policy, starts through `RUN` motion authority, and
captures target, acceleration-limited reference, measured velocity, requested
and applied current, saturation, faults, and selected drive/encoder evidence.
By default each command creates a timestamped directory under
`scratch/velocity-runs/` containing `metadata.json` and `telemetry.csv`. Static
policy, identity, and configuration are stored once in metadata instead of
being repeated in every row. The terminal overwrites one concise live line at
about 5 Hz regardless of the capture interval, and prints the saved path plus a
final summary. Use `--jsonl` to additionally retain complete nested snapshots,
`--quiet` to suppress live refresh, or `--output-root PATH` to relocate the
captures. Ctrl+C sends generic STOP and finalizes the directory. For a
repeatable automated shutdown gate, `--stop-after-seconds SECONDS` sends the
same STOP over the capture's active serial connection before the firmware
deadline. On firmware 0.30.0 / protocol 1.13,
`--trace-at-seconds SECONDS` atomically re-arms the 256-sample one-shot during
motion and writes normalized `current_trace.csv` after authority ends. It
contains A/B tracking, predicted phase/age, 32 MHz carrier-timer trigger and
ADC/DMA timing, 64 MHz DWT ISR timing, and PWM preload margin. No trace bytes
are transported while the backend is active; `arm-trace` exposes the same
active-only operation for specialized clients. The evaluation envelope is ±16 rev/s (±960 RPM), a caller-selected
positive reference slew through 256 rev/s², and at most 495 counts (about
2.999 A nominal). The host and legacy-request default is 16 rev/s². The independent
observed-speed shutdown remains 20 rev/s. Positive-direction 0.27.1 captures
now exercise the predictor through a 12 rev/s request. At 24 V, +8 rev/s reaches
target without q-current clipping; +12 rev/s saturates q-demand and phase
voltage in most samples and plateaus near 10 rev/s. Full 12 V/24 V snapshots
show that current tracking of the high-frequency rotating reference, rather
than a host command ceiling, is the next control boundary. The expanded values remain bench-
evaluation permission, not a
performance claim. Accepted high-end requests may saturate,
stall, fault, heat the motor/drive, or produce unexpectedly energetic motion;
use bounded runs, a suitable fixture, and immediate STOP/supply-cutoff access.
Firmware 0.27.1 is flashed and passes identity, readiness, live-policy,
calibration restore, and bounded positive-velocity smoke confirmation. Firmware
0.26.0 remains the bench-qualified relative-position baseline; corrected-
position-cascade confirmation on 0.27.1 remains open.

Firmware 0.28.0 / protocol 1.10 appends validity-tagged PA3 VBUS telemetry to
commissioning status schema 3. `status`, velocity/position live lines, compact
CSV, metadata, and trace downloads report measured bus volts and commanded
carrier-average phase volts. Current commands and live output use
milliamperes/amperes first. Raw ADC counts, phase-command ratios, and duties
remain in machine-readable diagnostics for calibration and saturation analysis.
Current counts do not vary with input voltage; higher VBUS changes available
phase-voltage headroom and current tracking.

Firmware 0.29.0 / protocol 1.11 adds `clear-faults`. It sends the operator
acknowledgment, prints a typed list of cleared and remaining fault owners, and
returns exit code 3 only if the in-place reset transaction itself is blocked.
The firmware does not demand a healthy encoder/current/input observation as a
second acknowledgment. It establishes `ZERO`, rebuilds ADC/DMA and the
PWM/current backend, resets controller operation latches, and returns to
uncommanded `DIAGNOSTIC`; normal fresh samples restore `READY`, or a persistent
condition faults again. `stop` is deliberately separate and never clears a
fault latch.

Firmware 0.29.1 / protocol 1.12 keeps that recovery operation unchanged and
adds aligned-torque status schema 2. After a motion or predictor fault, run
`torque-status` before `clear-faults` to retain the typed rejection reason and
age; a successful recovery starts a fresh predictor evidence window.

`position-status` passively reports target/reference/measured position,
profile/corrected/measured velocity, requested/applied current, state, result,
faults, and the firmware travel/velocity/acceleration/following-error policy.
`position` accepts a signed relative displacement plus explicit positive
trajectory velocity, acceleration, current limit, and deadline. Firmware 0.27.1
permits up to 16 rev/s, 64 rev/s², and 495 counts while the inner velocity path
retains 256 rev/s² slew and a 17 rev/s corrected-target allowance. It preflights
the request against position and velocity policy and sends the 18-byte protocol
1.9 request. Like `velocity`, each run creates a timestamped directory under
`scratch/position-runs/`: static request/policy/identity/configuration and
initial/final snapshots go to `metadata.json`, while compact position,
velocity, current, drive, and encoder samples stream to `telemetry.csv`. The
terminal refreshes one concise target/reference/measured/error/current/fault
line at about 5 Hz. Use `--jsonl`, `--quiet`, `--output-root`, and
`--stop-after-seconds` exactly as for velocity captures. A deadline safely
releases the drive but returns a nonzero host result because the requested
position did not settle. Ctrl+C sends generic STOP and finalizes the capture;
the physical Right button uses the same firmware release path.

Protocol 1.3 and later firmware also records the first 256 current-loop samples
after each start. Run a nearly stationary reference for a clean startup step,
then read and analyze it after authority ends:

```powershell
py tools/mks57d_rs485.py --port COM14 configure --current-ma 303 --frequency-hz 0.001
py tools/mks57d_rs485.py --port COM14 run --leg A1 --duration-ms 100 --interval 0.02
py tools/mks57d_rs485.py --port COM14 trace --output scratch/current-trace-a.jsonl
py tools/analyze_current_trace.py scratch/current-trace-a.jsonl
```

`analyze_current_loop.py` summarizes the slower diagnostic stream's RMS
tracking error, gain, lag, commanded phase volts, raw voltage ratio, fault
state, and encoder RPM. `analyze_current_trace.py` reports 10-90% rise time,
overshoot, 5% settling, steady error, commanded phase volts, raw saturation,
and cross-axis coupling from the 20 kHz
startup trace. Generated captures belong under ignored `scratch/`.

`flash-jlink.ps1` builds and validates the current-regulated firmware image, then
programs and verifies it with SEGGER J-Link Commander using the exact
`N32L406CB` target. Without `-Yes`, it performs a dry run and does not access
the probe or target:

```powershell
pwsh -File tools/flash-jlink.ps1
pwsh -File tools/flash-jlink.ps1 -Yes
```

Use a current-limited supply appropriate for the intended run. The tested board
may be flashed with its motor connected; start a new or reworked bridge backend
unloaded. The script requires RDP L0 before programming and does not unlock
read protection or modify option bytes.

`reference_cache.py` verifies cataloged local PDFs, extracts searchable
page-level text, and creates provenance-tracked page renders on demand. Its
generated cache is ignored. See [the reference-cache workflow](../docs/REFERENCE_CACHE.md).

```powershell
python tools/reference_cache.py status
python tools/reference_cache.py build n32l40x-um-v2.6
python tools/reference_cache.py search "SRAM2 parity"
```
