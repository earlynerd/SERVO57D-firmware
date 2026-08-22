# SERVO57D Open Firmware

Clean-sheet firmware for the N32L406CBL7-based Makerbase MKS SERVO57D RS-485
closed-loop stepper controller.

## Project ambition

This project is building a high-performance motor drive, not a permanently
derated commissioning demonstration. The goal is responsive, precise current,
velocity, and position control that makes useful use of the board's available
bus voltage, phase current, encoder, and 20 kHz control bandwidth. Safety comes
from independent measured limits, bounded authority, timing guarantees, and a
common immediate fault path—not from leaving the product constrained to tiny
current, voltage, speed, or motion ceilings.

Operating limits are expanded deliberately from bench evidence, and every
accepted point remains motor-, supply-, board-, cooling-, and enclosure-specific.
Makerbase advertises 12-24 V operation, 0-5200 mA current, 20 kHz current,
velocity, and position loops, 3000+ RPM, and up to 256 "subdivision" (16 by
default). Matching those capabilities—or identifying and fixing the concrete
board or control limitation that prevents each one—is an active product
requirement. The vendor figures are not yet verified safe operating limits, so
expansion remains measurement-gated, but the present low commissioning ceilings
are temporary and must not displace current, voltage, speed, and loop-rate work
behind unrelated feature development. Any architecture, estimator, timing,
power-stage, sensing, thermal, or test-fixture work needed to reach them is in
scope now.
The rotating-current operation is retained as a production motor-diagnostic
client of the same drive supervisor used by motion control. It is the measured
foundation used by the 0.23 aligned torque candidate, not a parallel authority
path or the final velocity/position interface.

## Run a test move and view plots

With the motor connected to a current-limited supply and the RS-485 adapter on
`COM14`, one command runs a bounded move, captures current and encoder telemetry,
analyzes the result, and opens a self-contained plot report:

```powershell
py tools/motor_test.py --port COM14 --current-ma 750 --rpm 24 --seconds 5
```

The firmware supervisor still owns readiness and bridge authority; the active
operation owns its duration/deadline, while the current backend independently
owns current, voltage, duty, fast-loop deadline, and fault limits. Press Ctrl+C
to send STOP. Each run is saved under ignored
`scratch/motor-runs/`, and the tool restores the preceding inactive test
configuration unless `--keep-config` is requested. Use `--no-open` to capture
without opening a browser or `--replot RUN_DIRECTORY` to reopen a saved run.

This interface currently commands a positive-frequency rotating current vector;
`--rpm` is a speed magnitude derived from the tested motor geometry, not yet a
closed-loop shaft-speed, signed-direction, or position command. Those controls
remain separate from this diagnostic: firmware 0.25.1 established the qualified
signed-velocity baseline, firmware 0.26.0 is the flashed relative-position
baseline, and firmware 0.26.1 adds an independent encoder-production liveness
guard through the same production actuator and fault path.

The 0.23 candidate adds the first production motion command: a signed,
calibrated q-current demand with firmware-reported current, slew, velocity,
acceleration, feedback-age, and duration bounds. After flashing, the initial
bench gate starts at 151.5 mA for 250 ms:

```powershell
py tools/mks57d_rs485.py --port COM14 torque-status
py tools/mks57d_rs485.py --port COM14 torque --current-ma 151.5 --duration-ms 250
```

The command uses `RUN` motion authority and the existing 20 kHz current backend;
it is torque-producing current, not a velocity request, so keep clear of the
shaft and use the current-limited supply procedure in
[the bring-up guide](docs/BRINGUP.md).

Firmware 0.26.0 expands the signed closed-loop velocity evaluation service to
±4 rev/s (±240 RPM), 4 rev/s² reference slew, and 100 current counts (about
606 mA). The independent observed-speed shutdown remains 5 rev/s. Inspect the
live policy after flashing before starting:

```powershell
py tools/mks57d_rs485.py --port COM14 velocity-status
py tools/mks57d_rs485.py --port COM14 velocity --rpm 60 --current-limit-ma 606 --duration-ms 3000
```

The command reports the acceleration-limited reference, measured speed,
requested and actually applied q-current, saturation, deadline, authority, and
fault state. Each run stores normalized metadata and CSV telemetry under
`scratch/velocity-runs/`; the terminal shows only a compact live status line.
Positive/negative deadline, explicit STOP, physical Right-button stop, and
hand-loaded saturation/recovery gates pass. Initial velocity qualification is
accepted; physical feedback-loss injection is deferred indefinitely on the
current board/motor assembly because the encoder is inaccessible.

The 0.26.0 / protocol 1.9 product image adds a relative-position command with
separate travel, trajectory-velocity, acceleration, current, start-speed,
following-error, feedback-age, settling, and deadline bounds. A conservative
first bench move after flashing is:

```powershell
py tools/mks57d_rs485.py --port COM14 position-status
py tools/mks57d_rs485.py --port COM14 position --revolutions 0.25 --max-rpm 30 --acceleration-rps2 1 --current-limit-counts 100 --duration-ms 3000
```

The move succeeds only with result `settled`; reaching its deadline releases
the motor but reports a non-success result. Ctrl+C, generic STOP, and the
physical Right button retain the same immediate release path. Position runs use
the velocity command's recording convention: normalized `metadata.json` and
`telemetry.csv` under `scratch/position-runs/`, with one compact live terminal
line instead of repeated nested JSON. Full JSONL remains opt-in.

The first 0.26.0 position hardware sub-gate passes. Mirrored ±0.25-revolution
moves settled in 1.29/1.35 seconds with -0.000427/+0.001465 revolution endpoint
error, used at most 32 of 100 current counts, stayed below 0.05 revolution
profile following error, and held captured encoder timing to 1000 us. A
scheduled generic STOP also cleared all current references and duties without
faults or calibration changes. Physical Right-button stop and loaded
following-error checks remain pending.

## Current operating envelope

Firmware 0.26.0 / protocol 1.9 is the current flashed product build. Its
mirrored relative-position and generic-STOP evidence is summarized above. The
earlier 0.25.1 velocity qualification remains accepted: mirrored ±0.1 rev/s,
25-count, two-second commands moved in the requested encoder coordinate,
completed at their deadlines, and returned to `ZERO` with no control, encoder,
current-loop, reset, or watchdog faults. The correction maps velocity effort
through the already persisted alignment direction without changing the
configuration schema or wire protocol. A 303 mA run accepted
generic STOP, a 606 mA / 1 rev/s run accepted the physical Right button, and
hand-loaded runs demonstrated bounded saturation/recovery without faults or
material overshoot. The physical feedback-loss gate is indefinitely deferred;
the fault/ZERO contract remains covered by host/native tests. Firmware
0.24.15 passed its ordinary hardware
smoke check. Firmware 0.24.14 retired
the local Left/Center phase selector and its direct fixed-duty PWM helper while
retaining the RS-485 rotating-current diagnostic through the product supervisor
and current backend. Firmware 0.24.15 makes the deterministic rotor runtime the
sole estimator owner and gives the not-yet-linked motion candidate an immutable,
timestamped position/velocity observation instead of raw encoder samples. The
0.25.1 / protocol 1.8 image closes the
first bounded signed mechanical-velocity loop on that observation. It applies
an acceleration-limited reference and PI current request, then commands only
the existing aligned-q-current actuator. The 0.26.0 / protocol 1.9 product
image reuses that controller and actuator beneath a bench-tested trapezoidal
relative-position profile; step/direction remains disconnected. The current product line provides bounded signed
encoder-aligned q-current through the same supervisor,
current backend, and ZERO-vector fault path, and accepts the full wrap-safe
finite-deadline range. Torque activation is seeded only by newly accepted
encoder feedback so command-processing latency is not mistaken for an active
feedback overrun. Firmware 0.26.1 additionally requires foreground evidence
that accepted encoder production has advanced within 3 ms. Loss of that
evidence removes idle readiness or forces every energized authority through
the common fault/`ZERO` path; protocol 1.9's estimator-ready bit now includes
this liveness condition without changing its wire layout. Firmware 0.22.0's
power-loss-safe dual-slot motor-
configuration storage remains accepted through first-save, unchanged-save,
power-cycle restore, persistent-clear, and no-restored-authority gates. The
firmware runs from the fitted
8 MHz crystal at a bench-proven 64 MHz system clock, closes independent A/B
winding-current loops at 20 kHz, releases timestamped encoder acquisition at
1 kHz from TIM6, transfers each frame through SPI1 DMA channels 2/3, defers
decode and rotor-runtime work through PendSV, updates the OLED, and serves native RS-485 telemetry,
automatic alignment, persistent configuration in protocol 1.6, and the bounded
20 kHz startup-trace service.
Two 757.4 mA automatic alignments and a generic-STOP abort passed on the tested
motor with clean authority release, zero faults, and no reset or retained panic. The 0.19.0
supervisor-authorized path is bench-proven at 303 mA / 5 Hz through normal
deadline release and at 151.5 mA / 5 Hz through an explicit STOP. The new 1 kHz
estimator is bench-proven at idle and during a 757 mA / 20 Hz bounded run;
physical readiness-loss fault injection is indefinitely deferred on this
assembly because the encoder cannot be accessed non-destructively.

The deterministic rotor-feedback path is bench-proven during a 606 mA aligned-
torque command lasting five seconds: it completed 100,000 current-loop updates,
held encoder intervals to 1000-1001 us, reported zero encoder, DMA, estimator,
backend, or control faults, and returned all references and bridge duties to
zero at the deadline.

The image preloads PA6/PA7/PB0/PB1 low and maps them to TIM3. Independent zero
calibration, initialized current control, and a healthy encoder move the product
supervisor from `DIAGNOSTIC` to `READY`. The transitional local Left/Center
phase selector has been retired; it no longer requests bridge authority. RS-485
can configure 1-495 ADC counts (about 6.06 mA-2.999 A)
and 0.001-250 Hz electrical frequency, start a 0.003-2,147,483.647 second run, stream current,
duty, fault, reset, and encoder state, or stop diagnostic or motion authority.
The physical Right button remains an immediate stop input. The
independent raw-current trip is about 3.635 A, phase voltage is limited to 70%
of the bus, and the timer guardian latches missed current-loop updates. Loss of
readiness before a run returns to `DIAGNOSTIC`; loss while energized stops the
backend and enters `FAULT`.

Those numeric ceilings are the present firmware operating contract, not a
claim about the board's physical capability. The project is replacing inherited
commissioning values with a traceable limit model: every retained bound must be
tied to hardware, motor configuration, measured qualification, or an explicit
diagnostic policy and must have an enforcement owner, telemetry, and a test.

The loop starts from all-low `ZERO` and uses low-zero sign-magnitude PWM: for
each winding, only the leg selected by the voltage-command sign switches while
the opposite leg remains low. Current is sampled at 80% of the carrier through
a 16 MHz, TIM2-released two-rank ADC/DMA sequence. The phase-specific bridge mapping
matches the asymmetric A+/B- shunt placement.

Each MCU bridge command drives tied EG3013 HIN/LIN inputs. Command low selects
the low-side FET and command high selects the high-side FET, so all-low is a
deterministic zero-voltage vector, not an all-FET-off state. All current-loop
faults converge on that vector. Reset/halt waveforms, thermal limits, and the
wider voltage/current/speed envelope remain engineering
work; they no longer block bounded motor operation on the tested board.

A separate general-motion candidate still exercises remote lease expiry,
step/direction behavior, bounded position trajectories, and fault recovery
against host-side deterministic plants. The hardware image owns the only angle
tracker and publishes validated mechanical position/velocity observations.
Firmware 0.26.0 promotes only focused relative position into the product;
step/direction, leases, and the candidate's broader motion shell remain
separately compiled and unlinked.

## Current project status

Bench-proven on the tested board:

- 64 MHz HSE/PLL clock operation and explicit APB/timer clock derivation;
- OLED, MT6816 encoder, local and isolated inputs, and RS-485 command/response;
- all four TIM3 AF2 bridge outputs and 20 kHz two-rank ADC/DMA current acquisition;
- independent startup zero calibration and signed-current conversion using the
  measured 3.3 V ADC reference, 6.65 sense gain, 20 mOhm shunts, and
  6.059 mA/count scale;
- delayed 80%-carrier ADC/DMA sampling and 100,000 consecutive fault-free
  current-loop updates during a five-second 757 mA motor run;
- correct A/B feedback and all four bridge-leg polarities;
- encoder-confirmed rotation at -5.97 RPM from a commanded 5 Hz electrical
  vector (6.00 RPM expected), with zero encoder, SPI, current-loop, or reset
  faults;
- measured two-phase stepper geometry at 757.5 mA: +90-degree electrical
  commands moved the encoder -84, -80, -81, and -81 counts, totaling -326
  counts versus -327.68 theoretical for 50 electrical cycles per mechanical
  revolution; this establishes negative encoder direction for positive
  electrical phase;
- firmware 0.18.2 proportional gain `Kp=2` with the integral gain retained at
  `1/64` per 20 kHz step: a 303 mA startup step has 6.53 ms 10-90% rise time,
  8% overshoot, and 14.0 mA tail RMS error; 606 mA / 15 Hz tracks -17.78 RPM
  versus -18 RPM commanded, and 757 mA / 20 Hz tracks 1.97 revolutions over
  five seconds versus 2.00 commanded with 25.2% peak voltage effort.

Host-, build-, and bench-validated in firmware 0.21.0: one product drive supervisor owns
`RESET_SAFE`/`DIAGNOSTIC`/`READY`/`ALIGN`/`RUN`/`FAULT`, separates diagnostic
from motion authority, gates energization on current-path and encoder readiness,
and deauthorizes every fault transition. The product build also contains the
measured 50-cycle alignment mapping, a bounded automatic alignment controller,
transactional calibration math, a microsecond timebase, and 1 kHz estimator
telemetry. The controller requests `ALIGN` motion authority, applies phase-zero
and quarter-phase current vectors through the production backend, validates
current tracking, encoder stability, geometry, and return closure, and commits
the new zero/direction only after the complete sequence passes.
On hardware, stationary polling produced no raw-count or velocity movement over
five seconds. During a 757 mA / 20 Hz run, sampled encoder intervals were
981-1001 us with a 5.450 ms cumulative worst case, estimator velocity averaged
-0.3953 revolution/s versus -0.4000 commanded, and no estimator fault occurred.

Successful/repeatable automatic alignment and explicit STOP are accepted on the
tested motor. The alignment-specific Right-button abort remains a physical
check. Induced readiness-loss testing is indefinitely deferred on the current
assembly while its common fault/ZERO behavior remains host/native tested. Firmware 0.22.0
reserves the final two 2 KiB Flash pages
for alternating versioned configuration records, validates schema, CRC-32,
generation, commit marker, and motor geometry at boot, automatically persists
alignment only after authority/backend release, and exposes status/save/clear
through the production command service. Interrupted-write fallback is host-
tested, and physical power-cycle restore plus persistent clear are bench-
accepted. The next functional gate is the physical Right-button and loaded
following-error position checks, followed by expansion of the 240 RPM velocity
evaluation envelope. Remaining characterization
includes expansion beyond the inherited 1 A firmware endpoint, enclosed thermal behavior, current-sense temperature and
unit-to-unit tolerance, bus-voltage protection, bootstrap/duty limits,
reset/halt waveforms, and timer capture for step/direction/enable. See
[the project plan](PLAN.md).

## Intended outcome and scope

The project is progressing toward reproducible open firmware with bounded
two-phase current, velocity, and position control;
step/direction and RS-485 interfaces; and documented calibration,
configuration, fault handling, and update procedures.

This is a clean-sheet implementation. It will not extract, disassemble, or
reproduce Makerbase firmware or its bootloader. The canonical interface is a
project-owned versioned protocol over a transport-independent command service;
Modbus RTU and publicly documented Makerbase commands are optional adapters.
The project does not redesign the PCB or make the controller suitable for
safety-critical machinery.

## Documentation

Use the [documentation index](docs/README.md) to select the subsystem material
needed for a task. The index routes to hardware evidence, architecture,
building, bench procedures, protocol details, and reference provenance.
`DECISIONS.md` remains the append-only architectural history; it is not routine
cover-to-cover reading.

## Repository layout

| Path | Purpose |
| --- | --- |
| `firmware/` | Embedded firmware and portable control/application code |
| `tests/` | Native tests and deterministic host plants |
| `docs/` | Routed architecture, hardware, bring-up, and protocol documentation |
| `tools/` | Build, programming, and reference-cache helpers |
| `reference/` | External-document catalog and ignored local cache |
| `vendor/` | Imported manufacturer-support manifest and required source subset |
| `DECISIONS.md` | Append-only architectural and behavioral history |

External manufacturer material remains under ignored `reference/local/` and
`vendor/local/` directories until its provenance and redistribution terms are
recorded.

## License

A project license has not been selected. See [LICENSE](LICENSE). Choose an
open-source license before publishing project-owned firmware.
