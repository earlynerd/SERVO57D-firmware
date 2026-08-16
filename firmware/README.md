# Firmware

This directory contains a buildable, passive N32L406CBL7 diagnostic image. It is not motor-driving firmware and has not been run on physical hardware.

## Current safety boundary

- The reset-default 4 MHz MSI is verified and retained; HSI, HSE, and PLL operation are deferred.
- The initial stack and runtime data use SRAM1 only. SRAM2 is initialized for parity but unavailable to the linker until bench validation.
- Only provisional status LED PB9 is configured as an output.
- PA6, PA7, PB0, and PB1 remain in their reset configuration.
- No bridge-control interface exists in project-owned code.
- Core exceptions and every unclaimed interrupt record a panic code and halt.
- The firmware sets and verifies four NVIC preemption bits with no subpriorities; SysTick runs at priority 15.
- Sticky reset flags are captured and cleared at boot for debugger-visible reset-cause diagnostics.
- A nominal one-second independent watchdog is serviced only by the foreground liveness supervisor; no interrupt or subsystem has a raw-feed API.
- IWDG pauses on debugger halt only in this passive, bridge-incapable image.
- The foreground loop publishes a versioned `g_diagnostics` RAM record without configuring a serial peripheral.
- The application can transition only from reset-safe to diagnostic operation, or from any modeled state to fault.

## Layout

| Path | Purpose |
| --- | --- |
| `include/mks57d/` | Project-owned public interfaces |
| `src/app/` | Application state transitions |
| `src/board/` | Board-specific passive I/O |
| `src/platform/` | Startup-adjacent runtime, timebase, panic handling, and IWDG access |
| `src/safety/` | Hardware-independent fault state and watchdog liveness policy |
| `linker/` | Exact N32L406CBL7 memory layout |
| `vendor/nations/` | Minimal, license-preserving CMSIS/device subset |

See [building instructions](../docs/BUILDING.md), the [watchdog policy](../docs/WATCHDOG.md), and the [debugger diagnostic record](../docs/DIAGNOSTICS.md). Motor-control code remains gated by passive bring-up and scoped power-stage validation in `PLAN.md`.
