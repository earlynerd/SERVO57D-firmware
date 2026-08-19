# Project Plan

This plan is organized around go/no-go gates. Completing a phase authorizes investigation of the next phase; it does not automatically authorize energizing the power stage.

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

## Phase 1 — Establish destructive debug access

Goal: prove that a retail controller can accept replacement firmware.

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

Go criterion: at least one board can be repeatedly erased, programmed, reset, and debugged.

No-go criterion: all available boards are irreversibly L2-protected, or reliable reset/debug access cannot be established without unacceptable board modification.

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
- [ ] Document flash commands after the pyOCD target and unlock path are proven on hardware.

Software status: firmware 0.15.0 builds, runs from the bench-proven 8 MHz HSE
through PLL at 64 MHz with explicit APB and timer clocks, runs a seven-gate boot self-test,
samples the encoder and runs bench-proven TIM3-synchronous two-channel current acquisition, performs independent startup zero calibration, updates the fitted OLED with both signed currents in milliamperes, and serves the
read-only native protocol over RS-485. The 184-byte schema-4 RAM diagnostic
record remains ABI-checked. The retained 20 kHz TIM3 PWM characterizer
preloads and returns to the all-low vector, but bridge switching remains
inhibited until a bounded current-loop backend owns it. Display operation, encoder motion, RS-485
command/response, and stable bridge-disabled ADC readings are bench-proven.
Reset waveforms, SRAM2 and IWDG details, debugger-visible diagnostics,
exception behavior, and physical bridge safety still require explicit hardware
validation.

Go criterion: a clean checkout builds, flashes, boots, and reports its version from a defined board state.

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
- [ ] Measure ADC reference accuracy, amplifier settling, gain/sign, clipping, and supply-range behavior.
- [x] Retire the post-peripheral permanent no-drive invariant after completing passive input validation; retain the reset-safe initial board-state gate.

Go criterion: all passive inputs required by the first RS-485-controlled bridge
tests are understood and repeatable, and the step/direction/enable pin mapping
has been checked. Gate-control polarity, reset/fault waveforms, and hardware
inhibit behavior move into Phase 4 because they cannot be established without
driving the bridge interface. Deferred step/direction capture and operating
semantics are not a gate for Phase 4.

## Phase 4 — Power-stage timing without a motor

Goal: validate the output waveform before energy is applied to a winding.

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

Go criterion: scoped gate and bridge-node waveforms remain safe under normal operation, reset, watchdog, breakpoint, and deliberately injected faults.

## Phase 5 — Two-phase current regulation

Goal: regulate winding current before attempting position control.

- [ ] Trigger ADC conversions at deterministic quiet points in the PWM cycle.
- [x] Calibrate PA1/PA2 offsets independently at every safe startup using 32 bridge-zeroed snapshots.
- [x] Convert ADC readings to signed milliamperes from the 20 mOhm shunt and 6.65 amplifier gain; nominal 3.3 V reference is used until reference accuracy is measured.
- [ ] Implement hard clamps independent of requested current.
- [ ] Implement the A/B winding current controllers and anti-windup behavior.
- [ ] Test first into a non-motor load or at very low bus voltage when practical.
- [ ] Verify sine/cosine current commands at progressively higher current.
- [ ] Characterize current-loop bandwidth, noise, saturation, and thermal behavior.

Go criterion: both winding currents track bounded references stably across electrical angle and expected supply voltage.

## Phase 6 — Encoder alignment and servo control

Goal: close the mechanical loop incrementally.

- [ ] Determine encoder-to-electrical-angle alignment and motor pole/step geometry.
- [ ] Add a controlled alignment/calibration procedure.
- [ ] Implement robust angle unwrapping and velocity estimation.
- [ ] Add a bounded torque/current command layer.
- [ ] Close the velocity loop at low gains and limited current.
- [ ] Close the position loop with explicit acceleration, velocity, and following-error limits.
- [ ] Define stall, encoder-loss, overcurrent, and runaway detection.
- [ ] Persist calibration using a versioned, CRC-protected configuration record.

Go criterion: controlled moves and disturbances remain stable, faults shut down safely, and power cycling preserves valid calibration without preserving unsafe state.

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

- [ ] Add host unit tests for control math, protocol parsing, and configuration migration.
- [ ] Add hardware-in-the-loop tests for reset, brownout, watchdog, communications, and fault shutdown.
- [ ] Measure CPU load, ISR latency, stack use, flash use, and worst-case loop timing.
- [ ] Test multiple board revisions and motors.
- [ ] Document thermal and electrical operating limits.
- [ ] Add reproducible release artifacts and checksums.
- [ ] Complete license and third-party attribution review.

## Candidate reuse strategy

The Nations SDK should supply low-level MCU support. Existing motor-control projects, including SimpleFOC, may be useful as references or sources of tested control math, but integration should be evaluated only after the board timers, ADC timing, current sensing, and shutdown behavior are proven. The two-phase bridge and N32-specific peripheral layer are sufficiently specialized that forcing an early framework integration could obscure the highest-risk work.

## Principal risks

| Risk | Mitigation or decision gate |
| --- | --- |
| Retail MCU is RDP L2 | Determine protection before investing in firmware architecture |
| No NRST on the programming header | Add a temporary reset lead and design firmware to preserve SWD recovery |
| Schematic differs from purchased revision | Photograph, trace, and continuity-check actual hardware |
| TIM3 mapping or its ADC-trigger timing cannot provide deterministic sampling | Verify alternate functions and evaluate edge-aligned, dual-sample, or synchronized auxiliary-timer strategies |
| Current measurement is too noisy or poorly timed | Timer-synchronous sampling, offset calibration, scope measurements |
| Gate-driver behavior is incompletely documented | Bench validation before motor connection |
| 128 KiB flash / 24 KiB SRAM becomes restrictive | Begin bare-metal and measure resource use continuously |
| Public protocol compatibility expands scope | Isolate Modbus RTU and Makerbase compatibility behind optional adapters to one command service |
| Third-party material cannot be redistributed | Publish source URLs/manifests; keep local caches ignored |
