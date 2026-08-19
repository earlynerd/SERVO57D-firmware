# Documentation Index

The [root README](../README.md) is the concise current-state and safety
snapshot. Use this index to read only the material relevant to the task; the
documents below are not a single prerequisite reading list.

## Planning and history

- [Project plan](../PLAN.md) — go/no-go gates, scope, and implementation sequence; read for milestone or gate changes.
- [Decision log](../DECISIONS.md) — append-only structural history; read the latest entries for structural work and the full log only for audits or conflicts.
- [Debug log](../DEBUG_LOG.md) — resolved and active bench problems; read when a reported hardware symptom may recur.

## Architecture and interfaces

- [Architecture](ARCHITECTURE.md) — firmware layering, implemented foundation, and state model.
- [Real-time architecture](REALTIME_ARCHITECTURE.md) — interrupt priorities, ownership, control data flow, and timing budgets.
- [Command protocol](PROTOCOL.md) — native protocol, command service, and optional compatibility adapters.
- [Debugger diagnostics](DIAGNOSTICS.md) — versioned RAM record and consistent-read procedure.
- [Watchdog policy](WATCHDOG.md) — IWDG ownership, reset diagnostics, and debugger behavior.
- [Boot self-test](BOOT_SELF_TEST.md) — startup gates and latched failure ledger.
- [Memory map](MEMORY.md) — split SRAM layout and SRAM2 initialization contract.
- [Clock configuration](CLOCKS.md) — bench-proven 64 MHz HSE/PLL tree and startup contract.

## Hardware and bench work

- [Hardware](HARDWARE.md) — evidence-ranked board architecture, pin map, and unresolved questions.
- [Peripheral bring-up](PERIPHERALS.md) — OLED, ADC, encoder, inputs, RS-485, and PWM evidence.
- [ADC](ADC.md) — synchronous acquisition, scaling, bench results, and remaining calibration.
- [MT6816 encoder](ENCODER.md) — SPI transaction, validation, diagnostics, and bench proof.
- [USART1 / RS-485](RS485.md) — DMA transport, direction turnaround, and bench proof.
- [Bench bring-up](BRINGUP.md) — staged hardware procedure and abort conditions; required before bench tests.

## Build and provenance

- [Building and flashing](BUILDING.md) — reproducible firmware/host builds and guarded J-Link workflow.
- [Reference inventory](REFERENCE_INVENTORY.md) — external versions, sources, hashes, and publication policy.
- [Reference cache](REFERENCE_CACHE.md) — local page-addressable PDF cache workflow.
- [Firmware directory guide](../firmware/README.md) — current hardware-image boundary and source layout.
- [Tool guide](../tools/README.md) — project-owned host utilities.
