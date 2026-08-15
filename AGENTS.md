# Repository Guidance

Read `DECISIONS.md`, `PLAN.md`, and `README.md` before making project changes. `DECISIONS.md` is the most recent source of architectural truth.

## Project state

This is a feasibility-stage clean-sheet firmware project for an N32L406CBL7-based Makerbase MKS SERVO57D RS-485 controller. There is no safe motor-driving firmware yet.

## Working rules

- Do not execute binaries from `vendor/local/` unless the user explicitly requests it.
- Do not extract, disassemble, or attempt to reproduce Makerbase firmware or its bootloader.
- Keep the bridge disabled during initial firmware and passive-peripheral bring-up.
- Treat pin assignments from schematics as provisional until checked against a physical board revision.
- Preserve Nations copyright, redistribution conditions, and disclaimers in imported or modified vendor source files.
- Import only vendor source files actually needed by the firmware; do not copy the entire SDK into project-owned source.
- Keep third-party archives, tools, PDFs, packs, and generated analysis material in ignored local directories. Record public source URLs and versions in `docs/REFERENCE_INVENTORY.md` before adding redistributable dependencies.
- Record structural architecture, protocol, pin, timing, and safety-contract changes in `DECISIONS.md`.
- Do not add a motor-control framework until timer mapping, ADC timing, shutdown behavior, and current-sense scaling are proven on hardware.

## Safety invariants

- Reset and uninitialized GPIO states must not energize the bridge.
- Every fault path must converge on one immediate bridge-disable mechanism.
- Debugger halt, watchdog reset, malformed communications, and invalid configuration must fail safe.
- Current, duty cycle, velocity, acceleration, and following error must have independent bounds.
- First bridge tests use a current-limited supply with the motor disconnected and are observed on an oscilloscope.

