# Active Project Plan

This file contains current incomplete outcomes and explicit deferrals only. It
is not a historical completion log. Completed phase checklists remain
recoverable from git history; structural rationale and durable bench/debug
evidence remain in `DECISIONS.md` and `DEBUG_LOG.md`.

The product objective is a high-performance motor drive with responsive current,
velocity, and position control. Makerbase's advertised 12–24 V, 0–5200 mA,
20 kHz loop, 3000+ RPM, and 256-subdivision endpoints are active engineering
targets, not verified safe operating limits. Current work must either advance
those targets or resolve a measured prerequisite while retaining independent
current, voltage, duty, timing, speed, acceleration, following-error, authority,
and fault bounds.

## Accepted baseline

- Firmware 0.29.2 / protocol 1.12 is flashed. Its inherited drive baseline is
  bench-proven for the current
  20 kHz two-phase current backend, 1 kHz deterministic rotor service, persisted
  alignment, bounded torque, signed velocity, relative position, and physical
  VBUS/phase-voltage telemetry.
- At 24 V, positive velocity reaches target through +8 rev/s. A +12 rev/s
  request reaches the 2.999 A nominal q-demand and 70%-of-bus phase-voltage
  ceilings and exposes the next high-frequency current-tracking boundary without
  a control, encoder, backend, reset, or panic fault.
- Firmware 0.29.2 clears following-error and propagated current-backend fault
  chains in place without an MCU reset and reconciles the preempted-SysTick
  epoch race without raising SysTick above the current loop. Six alternating
  signed position moves produced no future-age rejection; maximum successful
  prediction age remained 1,435-1,483 us against the 3,000 us contract, with
  preserved generation-3 calibration and no reset.
- Firmware 0.29.3 / protocol 1.12 is the current source candidate. It closes
  the remaining non-tuning project-review items: a traceable independent
  acceleration shutdown, explicit cascade deadline ordering, wide-range slew
  arithmetic, shared control-math semantics, and generated-artifact hygiene.
- The project-owned timer, ADC, modulation, current backend, drive supervisor,
  and direct-GPIO all-low `ZERO` mechanism remain the only bridge-authority
  path.

The exact numeric envelope and next evidence for each limit are maintained in
[the operating-limit inventory](docs/OPERATING_LIMITS.md).

## Active outcomes

Work is ordered approximately by current engineering priority. Reorder this
list only when measurements or a newly discovered prerequisite justify it.

- [ ] Flash firmware 0.29.3 and repeat one already bounded motion command.
  Confirm identity, the reported 512 rev/s² aligned-actuator acceleration
  boundary, ordinary release, preserved calibration, and zero new faults.
- [ ] Measure the current-loop and predictor timing on hardware: ADC acquisition
  instant, configured output lead, worst-case ISR/preload duration, switching
  contamination, and remaining phase error at the 8–12 rev/s boundary.
- [ ] Improve high-electrical-frequency current tracking from those measurements,
  then stage signed velocity through ±2, ±4, ±5, ±8, ±12, and ±16 rev/s with
  current, voltage, prediction, supply, thermal, mechanical, and release
  evidence at each retained point.
- [ ] Validate the production current path at 1.503 A, 2.25 A, and the enabled
  2.999 A motor-rated evaluation point, including tracking error, phase-voltage
  effort, supply behavior, winding/power-stage temperature, STOP, and fault
  health.
- [ ] After the attached motor's 3 A gate, characterize the board-level 5.2 A
  claim with an appropriate motor/load and thermal fixture; quantify
  current-sense settling, clipping, temperature drift, and unit-to-unit
  tolerance.
- [ ] Characterize bus protection, bootstrap refresh, minimum/maximum duty,
  power-on/reset/debugger-halt/watchdog bridge waveforms, and injectable
  current/bus-voltage trip behavior before expanding the qualified electrical
  envelope.
- [ ] Resolve the repeatable approximately ±0.0025-revolution low-speed endpoint
  offset, then bench-validate the physical Right-button position stop and a
  loaded following-error event with bounded current, common `ZERO` convergence,
  and clean post-fault evidence.
- [ ] Define and implement product policy for mechanical stall, partial encoder
  degradation, protection-grade overcurrent, and runaway detection beyond the
  existing total encoder-production, raw-current, and observed-speed guards.
- [ ] Extend estimator, phase prediction, and outer-loop scheduling toward the
  advertised 3000+ RPM and 20 kHz velocity/position claims, preserving
  independently enforced motion and timing bounds.
- [ ] Replace remaining alignment-policy constants that lack a defensible
  product basis with measured or configured motor/application values.
- [ ] Complete actual-board pin/revision evidence and map step/direction/enable
  capture to verified timer resources with pulse-rate validation and bounded
  operating semantics.
- [ ] Define native address provisioning and lost-address recovery; add
  malformed-frame, replay, and protocol fuzz coverage.
- [ ] Decide and implement the required host configuration/firmware-update
  workflow, including configuration migration tests when a new persisted schema
  is introduced.
- [ ] Add hardware-in-the-loop reset, brownout, watchdog, communications-loss,
  and fault-shutdown coverage; measure CPU load, ISR latency, stack use, flash
  use, and worst-case loop timing.
- [ ] Test additional board revisions, motors, supplies, and cooling conditions,
  then publish qualified thermal and electrical operating limits.
- [ ] Finish release readiness: project license, third-party source/redistribution
  inventory, reproducible artifacts, checksums, and publication review.

## Explicit deferrals

- Physical encoder/no-magnet injection is deferred indefinitely on the current
  board/motor assembly because the sensor cannot be disturbed non-destructively.
  Automated invalid/stale/total-silence fault convergence remains required.
- Absolute position, homing, and the broader general-motion shell remain outside
  the current focused product slice until the active current/speed/timing work
  justifies their priority. Relative position remains the supported position
  operation.
- Modbus RTU and publicly documented Makerbase command compatibility remain
  optional adapters. They do not replace the project-owned native command
  service or block core drive performance work.

## Planning hygiene

- Keep one independently completable outcome per checkbox. Split an item when
  part of it is accepted rather than accumulating status prose beneath an open
  checkbox.
- Put measured numeric status in `docs/OPERATING_LIMITS.md`, executable bench
  steps in `docs/BRINGUP.md`, protocol details in `docs/PROTOCOL.md`, and
  structural rationale in `DECISIONS.md`.
- Remove completed items from this active plan after their canonical evidence is
  recorded. Do not copy their validation narrative into multiple documents.

## Reuse constraint

The Nations SDK supplies low-level MCU support. External motor-control projects
may contribute control algorithms or reference math, but they do not own bridge
registers, fast-loop timing, modulation, ADC acquisition, or shutdown. Replacing
the proven project-owned backend with a generic hardware abstraction is outside
the current architecture.
