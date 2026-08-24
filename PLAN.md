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

- Firmware 0.30.3 / protocol 1.13 is flashed. Firmware 0.30.1 corrected the
  predictor's nominal 7 us lead to the measured 55 us DMA-to-application
  interval. Matched +8 rev/s Kp=2/3/4 bursts then reduced velocity RMS error
  from 0.797 to 0.621 to 0.460 rev/s and A/B current RMS error from about 146
  to 100 to 87 counts without a control, timing, encoder, backend, reset, or
  panic fault. Kp=4 remains the active bench candidate, not a universal motor
  default.
- Firmware 0.33.0 / protocol 1.15 is the performance/tuning source candidate.
  It retains 0.31.0's safe-state volatile current-gain apply/revert,
  active/stored/default status, schema-1 migration, explicit persistence, and
  guided sweep. It also stages an 8 MHz MT6816 transport and deterministic
  4 kHz rotor release, timestamps observations at CS assertion, preserves the
  filter and settling time contracts, and enables `-O2` deferred control plus
  Cortex-M4F single-precision hardware. An informal 4 kHz run is encouraging,
  not yet qualification evidence. Normal fast control now leaves optional
  trace timing dormant, and the rotor/foreground publication path uses compact
  4 kHz progress plus 100 Hz/event-driven full snapshots and 1 ms safety
  housekeeping without changing the immediate ISR/runtime fault paths. Its
  rotating-current diagnostic can ramp to each target before the full tuning
  hold window, avoiding an instantaneous speed step at higher-frequency points.
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
- Firmware 0.29.3 / protocol 1.12 is flashed. It closes
  the remaining non-tuning project-review items: a traceable independent
  acceleration shutdown, explicit cascade deadline ordering, wide-range slew
  arithmetic, shared control-math semantics, and generated-artifact hygiene.
  A bounded 0.1 rev/s smoke reported the 512 rev/s² policy, completed 40,001
  current-loop updates, released all outputs, preserved calibration, and left
  all fault/reset/panic channels clear.
- The project-owned timer, ADC, modulation, current backend, drive supervisor,
  and direct-GPIO all-low `ZERO` mechanism remain the only bridge-authority
  path.

The exact numeric envelope and next evidence for each limit are maintained in
[the operating-limit inventory](docs/OPERATING_LIMITS.md).

## Active outcomes

Work is ordered approximately by current engineering priority. Reorder this
list only when measurements or a newly discovered prerequisite justify it.

- [ ] Capture formal firmware 0.33.0 evidence and bench-validate 8 MHz SPI
  integrity, 4 kHz sample/acquisition timing and noise, PendSV/current-ISR preemption and stack margin, controller
  numerical behavior, current-gain status, idle-only volatile apply/revert,
  sweep-abort restoration, explicit save, schema-2 generation advance, and
  power-cycle restoration without disturbing alignment or authority.
- [ ] Run the guided fixed-current/frequency sweep across conservative staged
  amplitudes and the live diagnostic frequency range; accept a current-loop
  profile from tracking, phase, overshoot, voltage headroom, timing, motion, and
  fault evidence rather than velocity RMS alone.
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
- Absolute position, homing, persistent communications leases, general
  multi-source arbitration, and step/direction integration remain outside the
  current focused product slice until they justify fresh implementation through
  the product supervisor/runtime. Relative position remains the supported
  position operation. The standalone step/direction decoder stays retained and
  tested; PMSM-oriented d/q control remains a later project.
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
