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

- Firmware 0.38.0 / protocol 1.19 is the current source and flashed baseline.
  It runs the fixed-point rotating d/q controller in the production aligned-q
  torque, velocity, and position path, retains stationary A/B control for
  alignment and static vectors, and preserves the project-owned 20 kHz current
  backend, deterministic 4 kHz rotor service, supervisor authority, finite
  deadlines, independent electrical/motion limits, and common `ZERO` release.
- Firmware 0.37.1 moves telemetry-only aligned A/B reference reconstruction
  out of the ordinary pre-PWM 20 kHz deadline path. Status remains refreshed by
  each accepted 4 kHz observation, and an armed trace reconstructs its exact
  per-event reference after PWM staging. Native/Python tests, clean Arm builds,
  generated instruction ordering, and bounded hardware regression pass. Five
  sequential arms retained 1,280 consecutive 20 kHz samples, 20.72-21.66 us
  DMA-to-PWM time, at least 31.09 us preload margin, and zero missed updates or
  faults. Scheduled generic STOP also released normally.
- Firmware 0.38.0 adds a trace-gated 256-release aggregate profile for pend
  latency, deferred dispatch/copy, encoder decode, estimation, active control,
  publication, total PendSV work, foreground work, and intervening 20 kHz
  current completions. A simultaneous profile/current-trace +4 rev/s gate
  completed all 256 releases with 2.89/99.77 us average/maximum pend latency,
  144.09/336.61 us average/maximum PendSV work, 29.59 us minimum PWM-preload
  margin, zero missed updates/faults, normal `ZERO`, and preserved calibration.
- The bench configuration retains generation-3 alignment and ended with its
  stored Kp=4/Ki=1/64 current gains active and no dirty state. Kp=9/Ki=0.5
  remains a volatile development profile used only for same-condition captures;
  final gain selection and persistence remain deferred.
- Firmware 0.37.1 has clean signed production-motion evidence. Signed 30.3 mA,
  100 ms torque pulses released normally. Matched +4 rev/s for 2 s and -4 rev/s
  for 3 s at 16 rev/s² and a 606 mA permission measured 0.1015 and 0.1437 rev/s
  RMS error without current limiting or faults. A +151.5 mA open-torque pulse
  on 0.37.0 reached about 5.25 rev/s within 80 ms, so higher direct-torque points
  require restraint or a suitable load rather than current-rating permission
  alone.
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
- Torque, velocity, and position commands now use timestamped normalized host
  captures with compact live status, metadata, telemetry CSV, deterministic
  scheduled STOP coverage, and operation-appropriate optional trace evidence.
- The optional RP2040/NAU7802 force stream is integrated into torque captures.
  Firmware 0.2.0 on COM30 passed standalone no-load acquisition and a combined
  COM14 1-count/10 ms torque-capture smoke with contiguous samples, all motor
  timeline markers, clean drain/accounting, and safe motor deadline release.
  The pulse did not produce a reportable applied-q sample, and the absent load
  cell and fixture leave physical calibration and force evidence open.

The exact numeric envelope and next evidence for each limit are maintained in
[the operating-limit inventory](docs/OPERATING_LIMITS.md).

## Active outcomes

Work is ordered approximately by current engineering priority. Reorder this
list only when measurements or a newly discovered prerequisite justify it.

- [ ] Use the firmware 0.38.0 stage breakdown to continue optimizing the
  ordinary 20 kHz fixed-point
  rotating-current path and 4 kHz rotor/control chain before selecting permanent
  gains. Examine remaining redundant prediction, transform, publication, and
  snapshot work; require bounded numerical equivalence plus unchanged raw-current,
  voltage, duty, timing, authority, deadline, fault, and `ZERO` enforcement.
- [ ] Extend repeatable firmware-performance acceptance for each candidate:
  clean native/Python/Arm builds, 8 MHz SPI integrity, 4 kHz acquisition timing
  and noise, aggregate PendSV/current-ISR preemption under representative
  command/telemetry/display loads, stack high-water margin, armed and disarmed
  current-loop timing, bounded motion, and clean STOP/deadline release without
  reset or panic evidence.
- [ ] Improve the high-electrical-frequency current-control architecture using
  the accepted 200 electrical-Hz diagnostic and ±4/+8/+12 rev/s captures as
  regression baselines. Prioritize algorithm, dataflow, phase prediction, and
  model/feedforward work before a final gain sweep, then stage the remaining
  signed velocity envelope through ±5, ±8, ±12, and ±16 rev/s.
- [ ] Extend estimator, phase prediction, and outer-loop scheduling toward the
  advertised 3000+ RPM and 20 kHz velocity/position claims, preserving
  independently enforced motion and timing bounds.
- [ ] Characterize bus protection, bootstrap refresh, minimum/maximum duty,
  power-on/reset/debugger-halt/watchdog bridge waveforms, and injectable
  current/bus-voltage trip behavior before expanding the qualified electrical
  envelope.
- [ ] Define and implement product policy for mechanical stall, partial encoder
  degradation, protection-grade overcurrent, and runaway detection beyond the
  existing total encoder-production, raw-current, and observed-speed guards.
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

- Torque-reaction dyno construction, physical load-cell calibration, restrained
  direct-torque qualification, the 1.503/2.25/2.999 A production-current gates,
  and the later 5.2 A board characterization are deferred until the current,
  estimator, prediction, and outer-loop architecture is stable enough that the
  resulting force and thermal evidence will remain useful. The passive force
  instrument and synchronized capture path remain available.
- Final current-gain selection, its guided sweep/save/power-cycle acceptance,
  low-speed position tuning, and mechanically loaded following-error testing
  are deferred for the same reason. Volatile Kp=9/Ki=0.5 remains a development
  profile rather than a persisted product setting. Physical Right-button STOP
  coverage does not require the dyno and remains eligible for bounded testing.
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
