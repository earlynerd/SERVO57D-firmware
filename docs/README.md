# Documentation Index

- [Project plan](../PLAN.md) — go/no-go gates and implementation sequence
- [Building](BUILDING.md) — reproducible firmware and host-test commands
- [Architecture](ARCHITECTURE.md) — proposed firmware layering, timing, and state model
- [Real-time architecture](REALTIME_ARCHITECTURE.md) — interrupt priorities, loop ownership, control data flow, and timing questions
- [Watchdog policy](WATCHDOG.md) — IWDG timing, foreground ownership, reset diagnostics, and debugger behavior
- [Debugger diagnostics](DIAGNOSTICS.md) — versioned RAM record, consistent-read procedure, and field definitions
- [MT6816 encoder](ENCODER.md) — active bounded SPI acquisition, frame validation, diagnostics, and bench proof
- [USART1 / RS-485](RS485.md) — DMA transport, direction turnaround, diagnostics, and bench proof
- [Passive boot self-test](BOOT_SELF_TEST.md) — startup gates, latched failure ledger, and board invariant checks
- [Clock bring-up](CLOCKS.md) — current 4 MHz policy and deferred 64 MHz PLL plan
- [Memory map](MEMORY.md) — split SRAM layout and SRAM2 parity-initialization contract
- [Hardware](HARDWARE.md) — known board architecture, pin map, and unresolved questions
- [Peripheral bring-up](PERIPHERALS.md) — evidence-ranked I2C, OLED, ADC, SPI, RS-485, and PWM sequence
- [Passive ADC bring-up](ADC.md) — active raw monitoring, schematic-derived scaling, bench results, and remaining calibration
- [Bring-up](BRINGUP.md) — staged bench procedure and abort conditions
- [Reference inventory](REFERENCE_INVENTORY.md) — external package versions, hashes, and publication policy
- [Decision log](../DECISIONS.md) — append-only record of structural decisions
