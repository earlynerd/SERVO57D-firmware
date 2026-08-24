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

- Firmware 0.37.0 / protocol 1.18 is the current source and flashed baseline.
  It runs the fixed-point rotating d/q controller in the production aligned-q
  torque, velocity, and position path, retains stationary A/B control for
  alignment and static vectors, and preserves the project-owned 20 kHz current
  backend, deterministic 4 kHz rotor service, supervisor authority, finite
  deadlines, independent electrical/motion limits, and common `ZERO` release.
- The active bench configuration retains generation-3 alignment and uses
  volatile Kp=9/Ki=0.5 current gains. Stored gains remain Kp=4/Ki=1/64, so the
  configuration is intentionally dirty and persistence across a power cycle
  remains an explicit open gate.
- Firmware 0.37.0 has clean initial signed production-motion evidence. Signed
  30.3 mA, 100 ms torque pulses reversed q demand and released normally. Paired
  ±4 rev/s launches at 16 rev/s² and a 606 mA permission measured 0.073 and
  0.132 rev/s RMS error. The negative run's armed trace retained 256 consecutive
  20 kHz samples, 4.25-4.31 us trigger-to-DMA time, 22.47-22.97 us
  DMA-to-PWM time, at least 29.78 us preload margin, and zero missed updates or
  faults. A +151.5 mA open-torque pulse reached about 5.25 rev/s within 80 ms
  on the unloaded shaft, so higher direct-torque points require restraint or a
  suitable load rather than current-rating permission alone.
- Mirrored 0.25-revolution moves at 0.5 rev/s, 1 rev/s², and a 606 mA
  permission settled at +0.00067/-0.00085 revolution; one earlier positive move
  ended safely at deadline with -0.00482 revolution error and did not reproduce.
  Scheduled generic STOP, a 6.1 mA following-error injection, in-place recovery,
  and a post-recovery move all converged through `ZERO`, preserved alignment,
  and left fault/reset/panic evidence clear.
- The retained rotating-current diagnostic is bench-proven at Kp=9/Ki=0.5
  through 200 electrical Hz and 303 mA. At 200 Hz, rotating mode reduced lag
  from 39.09 to -0.01 degrees and RMS current error from 149.4 to 7.9 mA with
  zero missed updates or faults.
- At 24 V, positive velocity reaches target through +8 rev/s. A +12 rev/s
  request reaches the 2.999 A nominal q-demand and 70%-of-bus phase-voltage
  ceilings and exposes the next high-frequency current-tracking boundary without
  a control, encoder, backend, reset, or panic fault.
- The project-owned timer, ADC, modulation, current backend, drive supervisor,
  and direct-GPIO all-low `ZERO` mechanism remain the only bridge-authority
  path.

The exact numeric envelope and next evidence for each limit are maintained in
[the operating-limit inventory](docs/OPERATING_LIMITS.md).

## Active outcomes

Work is ordered approximately by current engineering priority. Reorder this
list only when measurements or a newly discovered prerequisite justify it.

- [ ] Finish firmware 0.37.0 aligned-q current qualification on a restrained or
  suitably loaded fixture, beginning with mirrored 151.5/303/606 mA direct-torque
  points and then 1.503 A. Require d-current rejection, tracking, timing margin,
  STOP/release, predictor, encoder, supply, thermal, mechanical, and fault
  evidence before expanding current or speed.
- [ ] Capture the remaining formal firmware 0.37.0 evidence and bench-validate 8 MHz SPI
  integrity, 4 kHz sample/acquisition timing and noise, PendSV/current-ISR preemption and stack margin, controller
  numerical behavior, current-gain status, idle-only volatile apply/revert,
  sweep-abort restoration, explicit save, schema-2 generation advance, and
  power-cycle restoration without disturbing alignment or authority.
- [ ] Run the guided fixed-current/frequency sweep across conservative staged
  amplitudes and the live diagnostic frequency range; accept a current-loop
  profile from tracking, phase, overshoot, voltage headroom, timing, motion, and
  fault evidence rather than velocity RMS alone.
- [ ] Improve high-electrical-frequency current tracking from those measurements,
  then stage the remaining signed velocity envelope through ±5, ±8, ±12,
  and ±16 rev/s, retaining the accepted ±4 rev/s / 16 rev/s² comparison, with
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
- [ ] Tune low-speed position to remove the occasional approximately
  0.005-revolution deadline outlier, then complete the physical Right-button
  position stop and a mechanically loaded following-error event with bounded
  current, common `ZERO` convergence, and clean post-fault evidence.
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
  tested.
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
