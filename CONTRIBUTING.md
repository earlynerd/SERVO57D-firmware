# Contributing

The project has proven replacement firmware, two-phase current regulation, and
encoder-confirmed motor rotation on hardware. Contributions are most useful
when they advance the next named milestone in `PLAN.md` or widen the measured
operating envelope.

## Before proposing code

- Read the current operating envelope and project status in `README.md`, then use
  `docs/README.md` to select the subsystem documentation relevant to the change.
- Read `PLAN.md` for milestone or scope changes; read the last 10 entries
  in `DECISIONS.md` for structural changes; read `docs/BRINGUP.md` for hardware
  tests or bench procedures.
- State which milestone, capability, or risk the change addresses.
- Distinguish measured hardware facts from schematic-derived assumptions.
- Preserve third-party license headers and identify the source and version of imported code.
- Do not submit extracted Makerbase firmware, disassembly-derived code, or undocumented proprietary material.

## Firmware expectations

- Preserve the proven bridge-authority, current-limit, deadline, and all-low
  fault contracts while adding useful motion features.
- Timing-sensitive code should document its execution context and worst-case assumptions.
- Protocol parsers and control math should be testable on the host where practical.
- Hardware changes should include board revision, test setup, instruments, and observed results.

## Licensing

The project-owned-code license is still pending. Significant outside contributions should wait until that choice is made so contributors can agree to clear terms.
