# Documentation Index

Use this page as a router, not as a prerequisite list. Start with the
[current operating snapshot](../README.md#current-operating-snapshot), then
read only the rows relevant to the task.

## Minimum reading by task

| Task | Read |
| --- | --- |
| Ordinary implementation or review | Current operating snapshot, then the owning subsystem document below |
| Architecture, timing, pins, protocol, safety contract, or scope | Owning document plus the latest applicable three to five entries found by searching [DECISIONS.md](../DECISIONS.md) |
| Build or flash | Relevant section of [BUILDING.md](BUILDING.md) |
| Bench or hardware test | Safety warning in the root README, stop conditions in [BRINGUP.md](BRINGUP.md#stop-conditions-during-motor-development), and the applicable bring-up stage only |
| Milestone or priority change | [PLAN.md](../PLAN.md) |
| Historical audit or reversal | Relevant canonical documents, decision history, recent commits, and debug history as needed |

Do not read all subsystem documents, all bring-up stages, or either historical
log by default.

## Source-of-truth ownership

| Information | Canonical owner |
| --- | --- |
| Current source candidate, flashed baseline, next control objective | [Root README](../README.md) |
| Active incomplete outcomes and explicit deferrals | [PLAN.md](../PLAN.md) |
| Numeric limits, their class, enforcement owner, and next evidence | [OPERATING_LIMITS.md](OPERATING_LIMITS.md) |
| Structural choices and rationale | [DECISIONS.md](../DECISIONS.md) |
| Durable bug resolutions and genuinely new unresolved evidence | [DEBUG_LOG.md](../DEBUG_LOG.md) |
| Firmware layering and state/authority ownership | [ARCHITECTURE.md](ARCHITECTURE.md) |
| Interrupt ownership, control data flow, and timing budgets | [REALTIME_ARCHITECTURE.md](REALTIME_ARCHITECTURE.md) |
| Commands, payloads, compatibility, and protocol versions | [PROTOCOL.md](PROTOCOL.md) |
| Bench safety and executable procedures | [BRINGUP.md](BRINGUP.md) |
| External-source versions, URLs, hashes, and redistribution status | [REFERENCE_INVENTORY.md](REFERENCE_INVENTORY.md) |

Other documents should link to these owners instead of repeating volatile
versions, numeric envelopes, validation narratives, or active backlog state.

## Architecture and interfaces

- [Architecture](ARCHITECTURE.md) — firmware layering, implemented foundation,
  drive-supervisor ownership, and state model.
- [Real-time architecture](REALTIME_ARCHITECTURE.md) — interrupt priorities,
  execution ownership, control data flow, and timing budgets.
- [Motor-drive operating limits](OPERATING_LIMITS.md) — hard, validated,
  evaluation, configured, and implementation limits.
- [Command protocol](PROTOCOL.md) — native protocol, command service, and
  optional compatibility adapters.
- [Debugger diagnostics](DIAGNOSTICS.md) — versioned RAM record and
  consistent-read procedure.
- [Watchdog policy](WATCHDOG.md) — IWDG ownership, reset diagnostics, and
  debugger behavior.
- [Boot self-test](BOOT_SELF_TEST.md) — startup gates and latched failure
  ledger.
- [Memory map](MEMORY.md) — split SRAM layout and SRAM2 initialization
  contract.
- [Clock configuration](CLOCKS.md) — bench-proven 64 MHz HSE/PLL tree and
  startup contract.

## Hardware and bench work

- [Hardware](HARDWARE.md) — evidence-ranked board architecture, pin map, and
  unresolved questions.
- [Peripheral bring-up](PERIPHERALS.md) — OLED, ADC, encoder, inputs, RS-485,
  and PWM evidence.
- [ADC](ADC.md) — synchronous acquisition, scaling, bench results, and
  remaining calibration.
- [MT6816 encoder](ENCODER.md) — SPI transaction, validation, diagnostics, and
  bench proof.
- [USART1 / RS-485](RS485.md) — DMA transport, direction turnaround, and bench
  proof.
- [Bench bring-up](BRINGUP.md) — staged procedures for new hardware,
  current-regulated operation, closed-loop motion, and recovery.

## Build, tools, and provenance

- [Building and flashing](BUILDING.md) — reproducible firmware/host builds and
  guarded J-Link workflow.
- [Firmware directory guide](../firmware/README.md) — hardware-image boundary
  and source layout.
- [Tool guide](../tools/README.md) — host utilities, capture formats, and
  operator commands.
- [Reference inventory](REFERENCE_INVENTORY.md) — external versions, sources,
  hashes, and publication policy.
- [Reference cache](REFERENCE_CACHE.md) — ignored local page-addressable PDF
  cache workflow.
