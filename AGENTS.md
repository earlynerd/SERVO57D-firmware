# Repository Guidance

Read the current operating envelope and project status in `README.md` before making
project changes. Use `docs/README.md` to select only the documentation relevant
to the task.

Additional reading is conditional:

- Read the last 10 entries in `DECISIONS.md` before changing architecture,
  protocol, pins, timing, safety contracts, or project scope. Read the full log
  only for audits, reversals, or unresolved historical conflicts.
- Read `PLAN.md` before changing milestones or scope.
- Read `docs/BRINGUP.md` before bench procedures or hardware tests.

The newest applicable entry in `DECISIONS.md` is the architectural source of
truth when documents conflict.

## Project state

This is an active clean-sheet firmware project for an N32L406CBL7-based
Makerbase MKS SERVO57D RS-485 controller. Firmware 0.29.0 / protocol 1.11 is
the current source candidate and adds explicit operator-acknowledged in-place
fault recovery; firmware 0.28.0 / protocol 1.10 is the flashed hardware
baseline. Its 20 kHz two-phase current loop, 1 kHz deterministic rotor
service, persisted alignment, aligned torque, velocity, and relative-position
stack are bench-proven. At 24 V, +8 rev/s reaches target; a +12 rev/s request
reaches the 2.999 A nominal q-demand and 70%-of-bus phase-voltage ceilings and
plateaus near 10 rev/s without a control, encoder, reset, or panic fault.
It adds automatic-injected VBUS telemetry plus physical amperes/volts in the
host interface. Inactive status measured 23.829 V at the 24 V supply setting;
a one-second 1 rev/s / 606 mA regression completed 20,001 current-loop updates
with advancing VBUS samples, zero terminal duties, and no ADC, deadline,
encoder, backend, reset, or panic fault. The product ambition is a high-performance motor drive: expand
useful current, voltage, speed, and motion from measurements rather than
treating low commissioning ceilings as permanent design targets. The next
control objective is improved high-electrical-frequency current tracking and
predictor timing, followed by continued signed speed/position evaluation.

## Working rules

- Do not execute binaries from `vendor/local/` unless the user explicitly requests it.
- Do not extract, disassemble, or attempt to reproduce Makerbase firmware or its bootloader.
- Route bridge switching through the project-owned current/motion authority
  path; application code must not bypass its current, voltage, duty, duration,
  deadline, and fault contracts.
- Treat pin assignments from schematics as provisional until checked against a physical board revision.
- Preserve Nations copyright, redistribution conditions, and disclaimers in imported or modified vendor source files.
- Import only vendor source files actually needed by the firmware; do not copy the entire SDK into project-owned source.
- Keep third-party archives, tools, PDFs, packs, and generated analysis material in ignored local directories. Record public source URLs and versions in `docs/REFERENCE_INVENTORY.md` before adding redistributable dependencies.
- Record structural architecture, protocol, pin, timing, and safety-contract changes in `DECISIONS.md`.
- Build motor-control features on the proven project-owned timer, ADC,
  modulation, and shutdown backend. External frameworks may contribute control
  algorithms, but they do not own bridge registers or fast-loop timing.

## Safety invariants

- Reset and uninitialized GPIO states must not energize the bridge.
- Every fault path that can affect bridge authority must converge on one
  immediate bridge-safe-state mechanism. On the current board that mechanism
  is the all-low `ZERO` vector, not an all-FET-off electrical disconnect.
- Debugger halt, watchdog reset, malformed communications, and invalid configuration must fail safe.
- Current, duty cycle, velocity, acceleration, and following error must have independent bounds.
- Motor tests use a current-limited supply and explicit current, voltage,
  duration, and motion bounds. A new board revision or changed bridge/timing
  backend returns to unloaded waveform characterization before expanding its
  operating envelope.
