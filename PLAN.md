# Project Plan

This plan is organized around go/no-go gates. Completing a phase authorizes investigation of the next phase; it does not automatically authorize energizing the power stage.

## Phase 0 — Repository and evidence base

Status: substantially complete.

- [x] Collect the published Makerbase schematics and manuals.
- [x] Collect the N32L40x reference manual and CMSIS device pack.
- [x] Collect the current Nations peripheral library, GCC guidance, flash-algorithm source, and programming support.
- [x] Record the initial hardware and protection findings.
- [x] Separate local third-party artifacts from project-owned files.
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
- [x] Keep the first image on the reset-default 4 MHz MSI; defer HSI, HSE, and PLL operation.
- [x] Establish deterministic reset behavior and route core faults and unclaimed interrupts to one panic path.
- [x] Add a monotonic timebase.
- [x] Define the initial interrupt-priority, execution-ownership, and multi-rate control architecture.
- [x] Define and implement the foreground-owned independent-watchdog policy.
- [x] Implement the provisional PB9 LED heartbeat without changing bridge-control pins from their safe state.
- [x] Add a versioned debugger-readable RAM diagnostic channel; defer serial transport until pin bring-up.
- [x] Document reproducible firmware and host-test build commands.
- [ ] Document flash commands after the pyOCD target and unlock path are proven on hardware.

Software status: the passive image builds, initializes and verifies the documented NVIC grouping and SysTick priority, publishes firmware version and boot/runtime diagnostics through a sequence-protected RAM record, passes post-link memory/vector/diagnostic-symbol checks, and its host-testable state, fault, watchdog-liveness, diagnostic-ABI, and priority-contract checks pass native tests. Reset behavior and cause capture, MSI/LSI clock assumptions, SRAM2 initialization, IWDG timing, LED polarity, bridge safety, exception handling, priority register behavior, and debugger record visibility remain unverified on hardware.

Go criterion: a clean checkout builds, flashes, boots, and reports its version while the bridge remains disabled.

## Phase 3 — Passive peripheral bring-up

Goal: understand every input without commanding motor current.

- [ ] Verify all pin assignments against continuity measurements on the actual board.
- [ ] Read buttons and isolated step/direction/enable inputs.
- [ ] Bring up the display only if it is useful for diagnostics.
- [ ] Identify and read the magnetic encoder over SPI.
- [ ] Characterize encoder noise, wraparound, direction, and zero-offset behavior.
- [ ] Bring up RS-485 receive/transmit and direction control in loopback or with an external adapter.
- [ ] Sample bus voltage and both current-sense outputs with the bridge disabled.
- [ ] Measure current-sense zero offsets, noise, ADC reference behavior, and amplifier settling.
- [ ] Determine the safe polarity and reset state of every gate-control and enable signal.

Go criterion: all control-relevant inputs are understood and repeatable, and the bridge remains disabled through resets, debugger attachment, and firmware faults.

## Phase 4 — Power-stage timing without a motor

Goal: validate the output waveform before energy is applied to a winding.

- [ ] Map the four bridge-control pins to available timer channels and synchronize the timers if more than one timer is required.
- [ ] Confirm the EG3013 input truth table and dead-time behavior from datasheets and measurements.
- [ ] Generate a low-duty test pattern while observing driver and MOSFET gate waveforms.
- [ ] Verify there is no shoot-through command during startup, shutdown, timer updates, or debugger halts.
- [ ] Implement a single, immediate bridge-disable path used by every fault.
- [ ] Confirm current-limit and bus-voltage trip handling with injected test signals where possible.
- [ ] Verify bootstrap refresh and minimum/maximum duty-cycle constraints.

Go criterion: scoped gate and bridge-node waveforms remain safe under normal operation, reset, watchdog, breakpoint, and deliberately injected faults.

## Phase 5 — Two-phase current regulation

Goal: regulate winding current before attempting position control.

- [ ] Trigger ADC conversions at deterministic quiet points in the PWM cycle.
- [ ] Calibrate PA1/PA2 offsets at every safe startup.
- [ ] Convert ADC readings to amperes from measured shunt and amplifier gain.
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

- [ ] Define a documented RS-485 framing and addressing model.
- [ ] Decide whether Modbus RTU compatibility is valuable.
- [ ] Implement step/direction behavior and edge-rate limits.
- [ ] Define configuration, status, telemetry, and fault registers or messages.
- [ ] Add protocol fuzz and malformed-frame tests.
- [ ] Add a host-side configuration and firmware-update workflow if needed.
- [ ] Decide whether any publicly documented Makerbase commands merit compatibility aliases.

## Phase 8 — Hardening and release

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
| Public protocol compatibility expands scope | Treat compatibility as a later optional layer |
| Third-party material cannot be redistributed | Publish source URLs/manifests; keep local caches ignored |
