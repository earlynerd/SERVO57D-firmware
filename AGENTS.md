# Repository Guidance

## Start with the smallest relevant context

Before changing the project:

- Read the current operating snapshot in `README.md` and the routing table in
  `docs/README.md`.
- Follow the routing table to only the subsystem documents needed for the task.
- For architecture, protocol, pins, timing, safety contracts, or project-scope
  work, search `DECISIONS.md` by subsystem and read the latest applicable
  entries, normally no more than three to five. Read the full log only for an
  explicit audit, reversal, or unresolved historical conflict.
- Read `PLAN.md` only when changing priorities, milestones, or scope.
- Before bench work, read the safety prerequisites and the applicable stage in
  `docs/BRINGUP.md`; unrelated stages are not prerequisite reading.

The newest applicable decision is authoritative when historical documents
conflict. Steady-state facts belong in the canonical document listed below,
not in repeated session summaries.

## Parallel work

For tasks with two or more independent workstreams, use subagents when parallel
execution materially improves speed or review coverage. Give each editing agent
exclusive ownership of named files or modules, and keep shared integration
files with the root agent. The root agent synthesizes the results, resolves
conflicts, and runs end-to-end validation. Prefer one agent for small or
sequential tasks and work dominated by shared mutable state.

## Documentation ownership

- `README.md`: concise current source/flashed baseline, safety warning, and
  entry points.
- `PLAN.md`: active incomplete outcomes and explicit deferrals only.
- `DECISIONS.md`: short append-only records of structural choices and why
  they were made.
- `DEBUG_LOG.md`: durable resolved bugs or genuinely new unresolved evidence,
  not routine test narration.
- `docs/OPERATING_LIMITS.md`: numeric operating envelopes, classification,
  enforcement owner, and next evidence.
- `docs/PROTOCOL.md`: wire formats, commands, compatibility, and version
  semantics.
- `docs/ARCHITECTURE.md` and `docs/REALTIME_ARCHITECTURE.md`: ownership,
  layering, control flow, and timing contracts.
- `docs/BRINGUP.md`: bench safety and executable hardware procedures.

Update only the canonical documents made stale by a change. Prefer links over
duplicating versions, limits, validation results, or historical narrative
across many files.

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
