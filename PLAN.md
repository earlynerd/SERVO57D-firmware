# Project Plan

This plan is organized around measured capability milestones. The tested board
has passed current-regulated motor rotation; completed milestones are normal
development foundations rather than restrictions that must be repeated for
every firmware iteration.

The product ambition is a high-performance motor drive with responsive current,
velocity, and position control. Commissioning limits are temporary measured
envelopes, not product targets: expand useful voltage, current, speed, and motion
while retaining independent bounds, explicit authority, deterministic timing,
and immediate fault convergence.

## Phase 0 — Repository and evidence base

Status: substantially complete.

- [x] Collect the published Makerbase schematics and manuals.
- [x] Collect the N32L40x reference manual and CMSIS device pack.
- [x] Collect the current Nations peripheral library, GCC guidance, flash-algorithm source, and programming support.
- [x] Record the initial hardware and protection findings.
- [x] Separate local third-party artifacts from project-owned files.
- [x] Add a reproducible, ignored, page-addressable cache for repeatedly used PDF references.
- [ ] Record canonical source URLs and redistribution terms for every external package.
- [ ] Select the project name and open-source license.

Exit criterion: the public repository can explain where every dependency came from without relying on undocumented binary blobs.

## Phase 1 — Establish replacement-firmware access

Goal: prove that a retail controller can repeatedly accept project-owned firmware.

- [ ] Photograph both sides of each board and record its exact revision and component markings.
- [ ] Build a Raspberry Pi Pico CMSIS-DAP v1/HID probe.
- [ ] Add a temporary NRST connection if practical; the stock SWD header omits reset.
- [ ] Power the controller without a motor from a current-limited supply.
- [ ] Verify the 5 V and 3.3 V rails before attaching SWD.
- [ ] Connect at a conservative SWD clock and identify the Cortex-M4 debug port.
- [ ] Determine whether the device reports RDP L0, L1, or L2.
- [ ] If L1, perform the documented L1-to-L0 mass erase.
- [ ] Power-cycle and confirm repeatable SWD attachment.
- [ ] Program and verify a minimal image using both the Nations tool and pyOCD/CMSIS-Pack path.

Milestone result: achieved. The tested board is repeatedly built, flashed,
reset, and operated with project-owned firmware through the J-Link workflow.

## Phase 2 — Minimal board-support package

Goal: produce a small, auditable project-owned firmware base.

- [x] Import only the required Nations CMSIS and device-support files, preserving their license headers.
- [x] Add the N32L406CBL7 startup file and an exact 128 KiB flash / split 16 KiB SRAM1 + 8 KiB SRAM2 linker layout with link-time guards.
- [x] Keep the first image on the reset-default 4 MHz MSI.
- [x] Promote the functional image to the board's 8 MHz HSE through PLL at 64 MHz, with HCLK/APB/timer clocks explicitly derived and bench-proven.
- [x] Establish deterministic reset behavior and route core faults and unclaimed interrupts to one panic path.
- [x] Add a monotonic timebase.
- [x] Define the initial interrupt-priority, execution-ownership, and multi-rate control architecture.
- [x] Define and implement the foreground-owned independent-watchdog policy.
- [x] Implement the schematic-correct PD0 LED heartbeat without changing bridge-control or `nEN` pins from their safe state.
- [x] Add a versioned debugger-readable RAM diagnostic channel independent of the on-wire protocol.
- [x] Document reproducible firmware and host-test build commands.
- [x] Document the bench-proven guarded J-Link build, program, verify, reset, and start workflow.

Software status: firmware 0.20.0 builds on the bench-proven 8 MHz HSE
through PLL at 64 MHz with explicit APB and timer clocks, runs a seven-gate boot self-test,
samples the encoder and runs bench-proven TIM3-synchronous two-channel current acquisition, performs independent startup zero calibration, updates the fitted OLED with both signed currents in milliamperes, and serves the
native product diagnostic service over RS-485. The 240-byte schema-5 RAM diagnostic
record remains ABI-checked. The 20 kHz TIM3 backend preloads and returns to the
all-low vector. After current-path and encoder readiness, the product drive
supervisor can grant distinct diagnostic or motion authority. The retained
hold-to-run diagnostic requests a bounded fixed-point A/B current loop with low-zero
sign-magnitude modulation, raw-count overcurrent trips, and a timer deadline
guardian. Display operation, continuous encoder telemetry, RS-485
command/response, current regulation, all four bridge polarities, and
encoder-confirmed 5.97 RPM motor rotation are bench-proven. The tested board's
3.3 V ADC reference, 6.65 current-sense gain, and 6.059 mA/count conversion are
verified. Reset/halt waveforms, SRAM2 and IWDG details, production current-sense
tolerance, the broader speed/current/thermal envelope, and production fault
coverage remain open. The 0.18.2 current path is bench-proven through 757 mA /
20 electrical Hz. The 0.19.0 supervisor path has passed healthy boot/readiness,
303 mA / 5 Hz deadline release, 151.5 mA / 5 Hz explicit STOP, configuration
restoration, and no-reset/no-panic checks. The 0.20.0 build adds a 1 kHz
timestamped mechanical estimator and protocol-1.4 telemetry. Its idle and
757 mA / 20 Hz schedule, noise, direction, and velocity regression passes;
readiness-loss fault injection remains.

Milestone result: achieved. A clean checkout builds, flashes, boots, and
reports its version from a defined board state.

## Phase 3 — Passive peripheral bring-up

Goal: understand every input without commanding motor current.

- [ ] Verify all pin assignments against continuity measurements on the actual board.
- [x] Add debounced monitoring for the three local keys and M_IN1/M_IN2.
- [x] Verify all five monitored input levels and active-low behavior on the board.
- [x] Add a passive static monitor for schematic candidates PA0 `nSTP`, PA8 `nDIR`, and PB7 `nEN` without pull resistors or bridge authority.
- [x] Verify those three physical pin mappings and active-low electrical behavior on the board.
- [ ] **Deferred:** Implement timer capture, pulse-rate validation, and step/direction/enable operating semantics.
- [x] Confirm the SSD1306-compatible 72-by-40 display profile on the fitted panel.
- [x] Add an active, bounded I2C1 transport and host-tested configurable SSD1306-compatible display layer.
- [x] Add an active, bounded PA1/PA2/PA3 polling ADC transport and host-tested raw-sample contract.
- [x] Add an active, bounded SPI1 transport and host-tested MT6816 coherent-burst decoder with foreground diagnostics.
- [x] Add an active, receive-first USART1 transport with circular RX DMA, bounded foreground draining, DMA TX, and line-complete PC13 turnaround.
- [x] Read the fitted magnetic encoder through the MT6816-compatible SPI protocol.
- [x] Verify stable rest readings, consistent shaft response, and repeatable once-per-revolution wraparound.
- [ ] Quantify encoder noise and determine mechanical/electrical zero offset.
- [x] Bring up RS-485 receive/transmit and direction control with an external adapter.
- [x] Sample bus voltage and both current-sense outputs with the bridge disabled.
- [x] Measure one-board zero-current offsets and short-term raw ADC noise at 12 V input.
- [x] Encode the schematic-derived current-sense and bus-divider conversion formulas.
- [x] Measure the tested board's 3.3 V ADC reference and verify the 6.65 current-sense gain/sign.
- [ ] Characterize amplifier settling, clipping, supply-range behavior, and temperature/unit-to-unit tolerance.
- [x] Retire the post-peripheral permanent no-drive invariant after completing passive input validation; retain the reset-safe initial board-state gate.

Milestone result: achieved. All inputs required by RS-485 current operation are
understood and repeatable, the step/direction/enable pin mapping is checked,
and subsequent phases have proven gate-control polarity and current response.
Timer capture and step/direction operating semantics remain a separate feature.

## Phase 4 — Power-stage characterization

Goal: establish the bridge mapping, modulation, timing, and common fault state.

- [x] Remove the post-peripheral invariant that permanently required PA6/PA7/PB0/PB1 to remain non-driving, while retaining reset-safe startup verification.
- [x] Add a minimal button-held bridge-characterization backend with explicit all-low initialization and one common deterministic zero-vector path.
- [ ] Scope PA6/PA7/PB0/PB1 and any candidate inhibit signal through power-on, reset, debugger halt, watchdog reset, and ordinary firmware startup.
- [x] Map PA6/PA7/PB0/PB1 to TIM3 channels 1-4 on AF2 using the Nations 2.3.0 four-channel PWM example; no timer synchronization is required.
- [x] Confirm from the EG3013 documentation that HIN is active-high, LIN is active-low, and nominal dead time is 120 ns; record the tied-input topology and undefined floating state.
- [x] Generate a button-held 500 Hz single-leg GPIO test pattern. With a 12 V bus and no motor, bench DMM measurements show 0 V differential across both phases in `ZERO` and approximately 6 V average across a selected phase during `RUN`; scoped waveform and dead-time measurements remain pending.
- [x] Verify ordinary 500 Hz bridge switching produces no detectable increase in supply current, supporting adequate fixed EG3013 dead time and no gross cross-conduction. Startup, reset, watchdog, and debugger-halt transitions remain separate measurements.
- [x] Replace the foreground-timed pattern with edge-aligned 20 kHz, 50% TIM3 PWM using preloaded compare registers. Bench testing confirms A1/A2/B1/B2 selection drives the expected phase and polarity while the other phase remains at zero, proving all four TIM3 AF2 outputs on the tested board.
- [x] Trigger a two-rank `currentB`/`currentA` ADC sequence from every TIM3 update and capture it on circular DMA channel 1; bench validation confirms stable target A/B acquisition without DMA errors.
- [x] Route every running software panic to the all-low zero vector; a true all-FET-off hardware path remains unresolved.
- [ ] Confirm current-limit and bus-voltage trip handling with injected test signals where possible.
- [ ] Verify bootstrap refresh and minimum/maximum duty-cycle constraints for the timer-PWM implementation.

Milestone result: functional bridge mapping, 20 kHz PWM, all-low fault control,
and ordinary switching are proven on the tested board. The remaining scope,
bootstrap, reset/halt, and injected-fault measurements are hardening work and
do not block the measured low-current operating envelope.

## Phase 5 — Two-phase current regulation

Goal: regulate winding current before attempting position control.

- [x] Bench-validate the 80%-phase TIM2 compare-ISR path with a 16 MHz ADC under switched current through sustained fault-free loop operation.
- [ ] Quantify switching-edge contamination and ISR/preload timing on an oscilloscope.
- [x] Calibrate PA1/PA2 offsets independently at every safe startup using 32 bridge-zeroed snapshots.
- [x] Convert ADC readings to signed milliamperes from the measured 3.3 V reference, 20 mOhm shunts, and verified 6.65 amplifier gain (6.059 mA/count on the tested board).
- [x] Implement independent requested-current, raw overcurrent, phase-voltage, and absolute-duty bounds.
- [x] Implement fixed-point A/B winding PI controllers with conditional anti-windup and low-zero sign-magnitude modulation.
- [x] Run the controller from every completed two-rank DMA sequence and latch ADC, invalid-output, PWM-write, and missed-update faults to the common all-low bridge path.
- [x] Verify sine/cosine current commands in all quadrants at 151.5 mA and 303 mA.
- [x] Drive the attached motor from a rotating current vector and confirm motion independently with the encoder.
- [x] Capture 20 kHz startup traces and characterize the 303 mA rotating-vector loop from 5-20 Hz with Kp=2.
- [x] Expand the tested motor envelope to 757 mA / 20 electrical Hz for five seconds with encoder tracking and no fault.
- [x] Add a one-command bounded move, telemetry capture, analysis, and plot-report workflow with inactive-configuration restoration.
- [ ] Complete scope-based bandwidth/noise work and characterize higher-current and enclosed thermal behavior.

Milestone result: achieved through the present 12 V, 757 mA / 20 Hz operating point. Both
winding currents track across electrical angle and the rotor follows at
-23.7 RPM versus 24.0 RPM expected. Firmware 0.18.2 Kp=2 used 25.2% peak
phase-voltage effort against the 70% ceiling. The 1 A / 50 Hz endpoints remain
available but unqualified. Scope and enclosed-thermal characterization continue
while encoder alignment and outer-loop integration begin.

## Phase 6 — Encoder alignment and servo control

Goal: close the mechanical loop incrementally.

- [x] Converge the hardware image on one product drive supervisor with explicit
  readiness, diagnostic/motion authority, and fault deauthorization; route the
  existing bounded current diagnostic through it.
- [x] Determine motor geometry and direction: 50 electrical cycles per
  mechanical revolution and decreasing raw encoder count for increasing
  commanded electrical phase, confirmed by a 757.5 mA cardinal-vector sequence.
- [x] Add a controlled alignment/calibration procedure with transactional
  acceptance, bounded production current authority, generic STOP, and native
  status telemetry.
- [x] Bench-validate successful and repeatable automatic alignment at the
  accepted 757 mA point, including exact return closure, generic STOP, retained
  prior calibration, clean authority release, and reset/fault health.
- [ ] Exercise physical Menu abort and induced invalid-encoder/readiness-loss
  shutdown with an immediate supply cutoff available.
- [x] Integrate the existing host-tested angle unwrapping and velocity estimator
  with the on-board encoder on a timestamped 1 kHz foreground schedule without
  enabling the outer servo or importing host-test motion limits.
- [x] Bench-validate estimator sample interval, jitter, stationary noise,
  direction, and velocity during a bounded run; move acquisition to a
  timer-released SPI/DMA path if later outer-loop load makes the initially
  accepted foreground schedule unfit for purpose.
- [ ] Connect the existing bounded torque/current command layer to the proven phase-current backend.
- [ ] Close the velocity loop at low gains and limited current.
- [ ] Close the position loop with explicit acceleration, velocity, and following-error limits.
- [ ] Define stall, encoder-loss, overcurrent, and runaway detection.
- [ ] Inventory every production limit with units, basis, enforcement owner,
  reporting, and test; remove, configure, or replace inherited commissioning
  ceilings and host-test defaults that have no defensible product basis.
- [ ] Persist calibration using a versioned, CRC-protected configuration record.

Milestone target: controlled moves and disturbances remain stable, faults
return the bridge to its defined state, and power cycling preserves valid
calibration without preserving active authority.

## Phase 7 — User interfaces and protocol

- [x] Choose and document a transport-independent command architecture with native, Modbus RTU, and Makerbase compatibility roles.
- [x] Specify and freeze the native RS-485 v1 base frame, default address, CRC, read-only command IDs, and initial error contract.
- [ ] Define address provisioning and recovery after a lost address.
- [x] Define and implement transport-independent duplicate-request, retry,
  control-lease, and motion-completion semantics.
- [x] Retain Modbus RTU as an optional standards-oriented integration profile.
- [x] Implement portable step/direction behavior and edge-rate limits.
- [ ] **Deferred:** Map step/direction capture to verified timer and physical pins.
- [ ] Define configuration, status, telemetry, and fault registers or messages.
- [ ] Add protocol fuzz and malformed-frame tests.
- [x] Graduate the host-side bounded move/capture/analysis/report workflow into
  a supervisor-authorized product motor diagnostic and regression tool.
- [x] Remove the commissioning-only firmware build personality and historical
  bridge-characterization/current-loop-commissioning target aliases.
- [ ] Replace or remove the transitional local phase-selector/OLED diagnostic
  after the aligned motor service interface exists.
- [ ] Add a host-side configuration and firmware-update workflow if needed.
- [x] Treat useful publicly documented Makerbase commands as optional compatibility aliases rather than the canonical API.

See [protocol architecture](docs/PROTOCOL.md) for the adopted boundaries,
safety rules, staged implementation order, and remaining wire-format decisions.

## Phase 8 — Hardening and release

Software note: native tests now exercise the initial encoder estimator,
trajectory limits, PI anti-windup, cascaded outer loop, d/q transforms,
voltage-vector saturation, deterministic mechanical plant, and two-axis RL
current plant. They also exercise one-source motion authority, explicit remote
heartbeats and lease expiry, controlled stop/disable, idempotent recent
requests, retained command outcomes, cumulative step/direction input, and
application-level fault recovery. Configuration migration, modulation,
replay/fuzz coverage, measured timing, and hardware-in-the-loop coverage remain
open.

- [x] Add host unit tests for control math and protocol parsing.
- [ ] Add host tests for versioned configuration migration.
- [ ] Add hardware-in-the-loop tests for reset, brownout, watchdog, communications, and fault shutdown.
- [ ] Measure CPU load, ISR latency, stack use, flash use, and worst-case loop timing.
- [ ] Test multiple board revisions and motors.
- [ ] Document thermal and electrical operating limits.
- [ ] Add reproducible release artifacts and checksums.
- [ ] Complete license and third-party attribution review.

## Candidate reuse strategy

The Nations SDK supplies low-level MCU support. Existing motor-control projects,
including SimpleFOC, may be useful as references or sources of tested control
math. Any reuse should sit above the now-proven project-owned timer, ADC,
current-loop, modulation, and shutdown backend; replacing that backend with a
generic hardware abstraction would discard the board-specific work already
validated.

## Principal risks

| Risk | Mitigation or engineering response |
| --- | --- |
| Retail MCU is RDP L2 | Determine protection before investing in firmware architecture |
| No NRST on the programming header | Add a temporary reset lead and design firmware to preserve SWD recovery |
| Schematic differs from purchased revision | Photograph, trace, and continuity-check actual hardware |
| TIM3 mapping or its ADC-trigger timing cannot provide deterministic sampling | Verify alternate functions and evaluate edge-aligned, dual-sample, or synchronized auxiliary-timer strategies |
| Current measurement is too noisy or poorly timed | Timer-synchronous sampling, offset calibration, scope measurements |
| Gate-driver behavior is incompletely documented | Continue waveform/bootstrap characterization while expanding the measured motor operating envelope |
| 128 KiB flash / 24 KiB SRAM becomes restrictive | Begin bare-metal and measure resource use continuously |
| Public protocol compatibility expands scope | Isolate Modbus RTU and Makerbase compatibility behind optional adapters to one command service |
| Third-party material cannot be redistributed | Publish source URLs/manifests; keep local caches ignored |
