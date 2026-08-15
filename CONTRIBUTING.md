# Contributing

The project is still deciding whether a full firmware implementation is practical. Contributions are most useful when they reduce a named uncertainty in `PLAN.md`.

## Before proposing code

- Read `README.md`, `PLAN.md`, `DECISIONS.md`, and `docs/BRINGUP.md`.
- State which plan gate or risk the change addresses.
- Distinguish measured hardware facts from schematic-derived assumptions.
- Preserve third-party license headers and identify the source and version of imported code.
- Do not submit extracted Makerbase firmware, disassembly-derived code, or undocumented proprietary material.

## Firmware expectations

- Safe bridge-disable behavior is more important than feature completeness.
- Timing-sensitive code should document its execution context and worst-case assumptions.
- Protocol parsers and control math should be testable on the host where practical.
- Hardware changes should include board revision, test setup, instruments, and observed results.

## Licensing

The project-owned-code license is still pending. Significant outside contributions should wait until that choice is made so contributors can agree to clear terms.

