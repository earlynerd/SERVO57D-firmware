# Firmware

This directory contains a buildable, bridge-safe N32L406CBL7 diagnostic image.
It actively reads low-energy peripherals but is not motor-driving firmware and
has not been run on physical hardware.

## Current safety boundary

- The reset-default 4 MHz MSI is verified and retained; HSI, HSE, and PLL remain disabled by the boot path.
- The initial stack and runtime data use SRAM1 only. SRAM2 is initialized for parity but unavailable to the linker until bench validation.
- The active-high status LED is PD0; PB9 is left as the `KEY_MENU` input.
- PA6, PA7, PB0, PB1, and provisional PB7 `nEN` remain input/no-pull while other GPIOA/GPIOB pins may serve low-energy peripherals.
- SPI1 on PB3-PB6 performs bounded mode-3 MT6816 reads every 10 ms in foreground; parity, no-magnet, over-speed, and transport state are published in diagnostics.
- USART1 AF4 on PA9/PA10 receives continuously through DMA channel 4. DMA
  channel 5 provides bounded TX, and PA8 returns low only after final line
  completion. A foreground COBS/CRC parser replies only to valid address-1
  ping, identity, and capability requests; no bytes are transmitted
  automatically.
- A bounded I2C1 PA4/PA5 transport and configurable SSD1306-compatible panel layer compile but are not called by the passive boot path.
- A bounded PA1/PA2/PA3 raw ADC layer compiles but is not called by the diagnostic boot path; its optional HSI timing source, ADC, and analog-pin setup remain inactive.
- No bridge-control interface exists in project-owned code.
- Core exceptions and every unclaimed interrupt record a panic code and halt.
- The firmware sets and verifies four NVIC preemption bits with no subpriorities; SysTick runs at priority 15.
- Sticky reset flags are captured and cleared at boot for debugger-visible reset-cause diagnostics.
- A nominal one-second independent watchdog is serviced only by the foreground liveness supervisor; no interrupt or subsystem has a raw-feed API.
- IWDG pauses on debugger halt only in this passive, bridge-incapable image.
- The foreground loop publishes a versioned `g_diagnostics` RAM record with
  encoder, RS-485 transport, and native-protocol state; the record is not the
  on-wire payload layout.
- A seven-gate boot ledger latches startup failures, publishes progress, and gates watchdog health.
- The application can transition only from reset-safe to diagnostic operation, or from any modeled state to fault.

## Layout

| Path | Purpose |
| --- | --- |
| `include/mks57d/` | Project-owned public interfaces |
| `src/app/` | Application state transitions |
| `src/board/` | Board-specific safe I/O and bridge-pin invariants |
| `src/platform/` | Startup-adjacent runtime, timebase, panic handling, and IWDG access |
| `src/protocol/` | Foreground framing and wire-protocol adapters |
| `src/safety/` | Hardware-independent fault state and watchdog liveness policy |
| `linker/` | Exact N32L406CBL7 memory layout |
| `vendor/nations/` | Minimal, license-preserving CMSIS/device subset |

See [building instructions](../docs/BUILDING.md), the [MT6816 encoder
contract](../docs/ENCODER.md), the [USART1 / RS-485 contract](../docs/RS485.md),
the [command protocol](../docs/PROTOCOL.md),
the [passive ADC contract](../docs/ADC.md), the
[watchdog policy](../docs/WATCHDOG.md), the [boot self-test](../docs/BOOT_SELF_TEST.md),
and the [debugger diagnostic record](../docs/DIAGNOSTICS.md). Motor-control code
remains gated by scoped power-stage validation in `PLAN.md`.
