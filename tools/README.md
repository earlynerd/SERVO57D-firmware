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

The report plots A/B reference and measured current, encoder motion versus the
expected movement, phase-voltage use versus its ceiling, and the first 12.8 ms
at 20 kHz. The present `motor_test.py --rpm` input is a positive speed magnitude
converted using the tested motor's 50 electrical cycles per mechanical
revolution. It is not a closed-loop command; use the separate
`mks57d_rs485.py velocity` product service for signed, regulated speed.

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
py tools/mks57d_rs485.py --port COM14 velocity --rps 0.1 --current-limit-ma 151.5 --duration-ms 2000
py tools/mks57d_rs485.py --port COM14 position-status
py tools/mks57d_rs485.py --port COM14 position --revolutions 0.25 --max-rpm 30 --acceleration-rps2 1 --current-limit-counts 100 --duration-ms 3000
py tools/mks57d_rs485.py --port COM14 status
py tools/mks57d_rs485.py --port COM14 configure --counts 50 --frequency-hz 5
py tools/mks57d_rs485.py --port COM14 run --leg A1 --duration-ms 3000 --interval 0.1
py tools/mks57d_rs485.py --port COM14 trace --output scratch/current-trace.jsonl
py tools/mks57d_rs485.py --port COM14 stop
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
regulate velocity. The evaluation shutdown policy permits 5 rev/s (300 RPM),
1,000 rev/s² observed acceleration, and 10,000 counts/s current slew so poor
tracking and phase-refresh boundaries can be measured instead of preflighted
away.

Firmware 0.26.0 / protocol 1.9 retains the qualified velocity service and adds
relative position. Firmware 0.26.1 keeps the same host protocol and makes the
encoder `estimator_ready` field clear if accepted sample production has not
advanced within 3 ms. `velocity` accepts either `--rps` or `--rpm`, while
`velocity-status` is passive and reports target, acceleration, feedback-speed,
current, feedback-age, PI-gain, and deadline policy. `velocity` accepts a
signed mechanical target in revolutions per second plus an explicit positive
q-current limit in counts or nominal milliamperes. It preflights the request
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
deadline. The 0.26.0 evaluation envelope is ±4 rev/s (±240 RPM), 4 rev/s²
reference slew, and at most 100 counts (about 606 mA). The independent
observed-speed shutdown remains 5 rev/s. The velocity evidence remains
qualified only through 1 rev/s; the expanded values are bench-evaluation
permission, not a performance claim. Firmware 0.26.0 is the flashed baseline
and its first relative-position gate passed; 0.26.1 still needs a normal flash
and smoke check.

`position-status` passively reports target/reference/measured position,
profile/corrected/measured velocity, requested/applied current, state, result,
faults, and the firmware travel/velocity/acceleration/following-error policy.
`position` accepts a signed relative displacement plus explicit positive
trajectory velocity, acceleration, current limit, and deadline. It preflights
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
py tools/mks57d_rs485.py --port COM14 configure --counts 50 --frequency-hz 0.001
py tools/mks57d_rs485.py --port COM14 run --leg A1 --duration-ms 100 --interval 0.02
py tools/mks57d_rs485.py --port COM14 trace --output scratch/current-trace-a.jsonl
py tools/analyze_current_trace.py scratch/current-trace-a.jsonl
```

`analyze_current_loop.py` summarizes the slower diagnostic stream's RMS
tracking error, gain, lag, voltage use, fault state, and encoder RPM.
`analyze_current_trace.py` reports 10-90% rise time, overshoot, 5% settling,
steady error, voltage saturation, and cross-axis coupling from the 20 kHz
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
