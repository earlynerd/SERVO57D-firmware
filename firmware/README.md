# Firmware

This directory contains a buildable N32L406CBL7 bridge-characterization image.
It deliberately drives gate-control GPIO but is not motor-driving firmware.
Display, encoder, RS-485, and passive ADC behavior have been exercised on the
tested board. The local, auxiliary, and isolated step/direction/enable input
mappings are confirmed.

## Current safety boundary

- Startup verifies the reset-default 4 MHz MSI, then enables the fitted 8 MHz HSE and PLL x8 for 64 MHz HCLK. PCLK2 is 32 MHz, PCLK1 is 16 MHz, and TIM3 receives the doubled 32 MHz APB1 timer clock.
- The initial stack and runtime data use SRAM1 only. SRAM2 is initialized for parity but unavailable to the linker until bench validation.
- The active-high status LED is PD0; PB8/PB9/PA15 and PB12/PB13 are bench-proven active-low monitored inputs.
- PA6, PA7, PB0, and PB1 begin high impedance/no-pull, then firmware 0.15.0 preloads all four low and assigns TIM3 channels 1-4 on AF2. Each signal directly drives tied EG3013 HIN/LIN inputs, so low selects the low-side FET and high selects the high-side FET. Firmware 0.15.0 suppresses bridge switching until a bounded current-loop backend owns bridge authority.
- SPI1 on PB3-PB6 performs bounded mode-3 MT6816 reads every 10 ms in foreground; parity, no-magnet, over-speed, and transport state are published in diagnostics.
- USART1 AF4 on PA9/PA10 receives continuously through DMA channel 4. DMA
  channel 5 provides bounded TX, and PC13 returns low only after final line
  completion. A foreground COBS/CRC parser replies only to valid address-1
  ping, identity, and capability requests; no bytes are transmitted
  automatically.
- A bounded 333.3 kHz I2C1 PA4/PA5 transport updates the fitted SSD1306-compatible 72-by-40 panel. The characterizer refreshes its two-page view at 5 Hz.
- TIM3 update triggers a 20 kHz PA1/PA2 `currentB`/`currentA` ADC sequence captured by circular DMA channel 1. The OLED alternates stable raw A/B samples during `RUN`; synchronous capture must be healthy before PWM can start. Acquisition failure keeps diagnostics alive and appears as numeric status `A####`. The earlier PA3 `vBus` polling path is not active in this image.
- All eight passive inputs are sampled every 10 ms with independent three-sample debounce. The OLED shows the PA0/PA8/PB7 raw levels as `S D E`; this validates static pin/polarity mapping and does not count step pulses.
- Next selects A1/A2/B1/B2 on the OLED. Holding Enter applies edge-aligned 20 kHz, 50% hardware PWM to the selected leg; raw Enter release or Menu returns all four commands low.
- The tied HIN/LIN topology has no defined all-FET-off command. `board_bridge_force_low_zero()` is the common deterministic software-fault state, not electrical disconnect.
- Core exceptions and every unclaimed interrupt record a panic code and halt.
- The firmware sets and verifies four NVIC preemption bits with no subpriorities; SysTick runs at priority 15.
- Sticky reset flags are captured and cleared at boot for debugger-visible reset-cause diagnostics.
- A nominal one-second independent watchdog is serviced only by the foreground liveness supervisor; no interrupt or subsystem has a raw-feed API.
- IWDG continues during debugger halt and resets the characterization image if foreground service stops.
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
| `src/board/` | Board-specific I/O and bridge-characterization GPIO ownership |
| `src/platform/` | Startup-adjacent runtime, timebase, TIM3 PWM, panic handling, and IWDG access |
| `src/protocol/` | Foreground framing and wire-protocol adapters |
| `src/safety/` | Hardware-independent fault state and watchdog liveness policy |
| `linker/` | Exact N32L406CBL7 memory layout |
| `vendor/nations/` | Minimal, license-preserving CMSIS/device subset |

See [building instructions](../docs/BUILDING.md), the [MT6816 encoder
contract](../docs/ENCODER.md), the [USART1 / RS-485 contract](../docs/RS485.md),
the [command protocol](../docs/PROTOCOL.md),
the [ADC contract](../docs/ADC.md), the
[watchdog policy](../docs/WATCHDOG.md), the [boot self-test](../docs/BOOT_SELF_TEST.md),
and the [debugger diagnostic record](../docs/DIAGNOSTICS.md). Motor-control code
remains gated by scoped power-stage validation in `PLAN.md`.
