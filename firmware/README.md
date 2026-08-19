# Firmware

This directory contains the buildable N32L406CBL7 current-regulated motor
image. Firmware 0.18.2 closes both winding-current loops at 20 kHz and has
driven the attached stepper at an encoder-confirmed 23.7 RPM. Display, encoder,
RS-485, ADC/DMA, all four bridge legs, and local/isolated input mappings are
bench-proven on the tested board.

## Current operating contract

- Startup verifies the reset-default 4 MHz MSI, then enables the fitted 8 MHz HSE and PLL x8 for 64 MHz HCLK. PCLK2 is 32 MHz, PCLK1 is 16 MHz, and TIM3 receives the doubled 32 MHz APB1 timer clock.
- The initial stack and runtime data use SRAM1 only. SRAM2 is initialized for parity but unavailable to the linker until bench validation.
- The active-high status LED is PD0; PB8/PB9/PA15 and PB12/PB13 are bench-proven active-low monitored inputs.
- PA6, PA7, PB0, and PB1 begin high impedance/no-pull, then firmware 0.18.2 preloads all four low and assigns TIM3 channels 1-4 on AF2. Each signal directly drives tied EG3013 HIN/LIN inputs, so low selects the low-side FET and high selects the high-side FET.
- SPI1 on PB3-PB6 performs bounded mode-3 MT6816 reads every 10 ms in foreground, including while the motor runs; angle, parity, no-magnet, over-speed, and transport state are available through diagnostics and native protocol 1.3.
- USART1 AF4 on PA9/PA10 receives continuously through DMA channel 4. DMA
  channel 5 provides bounded TX, and PC13 returns low only after final line
  completion. A foreground COBS/CRC parser replies only to valid address-1
  discovery, telemetry, and current-loop requests; no bytes are transmitted
  automatically. Status and STOP remain available while a test is active.
- A bounded 333.3 kHz I2C1 PA4/PA5 transport updates the fitted SSD1306-compatible 72-by-40 panel. The current-loop display refreshes its two-page view at 5 Hz.
- TIM2 resets from each TIM3 update and raises a compare interrupt at 80% of
  the carrier; that bounded ISR software-starts a 16 MHz, 7.5-cycle PA1/PA2
  `currentB`/`currentA` ADC sequence captured as one complete DMA pair. After
  independent startup zero calibration, the OLED shows both signed currents
  in milliamperes. Acquisition failure appears as numeric status `A####`; a
  current-loop shutdown latches `F####`, where the number is the one-based
  position of the first set fault bit. The earlier PA3 `vBus` polling path is
  not active in this image.
- All eight passive inputs are sampled every 10 ms with independent three-sample debounce. The OLED shows the PA0/PA8/PB7 raw levels as `S D E`; this validates static pin/polarity mapping and does not count step pulses.
- Earlier characterization builds used Next to select A1/A2/B1/B2 and Enter to apply edge-aligned 20 kHz, 50% hardware PWM. Firmware 0.18.2 uses Next to choose the rotating reference's initial phase and requires Enter to be released once, then held continuously, before the current loop runs. Raw release or Menu returns to `ZERO`. RS-485 can configure 1-165 counts and 0.001-50 electrical Hz, then start a 0.1-60 second run; timeout, Menu, transport failure, or STOP returns it to `ZERO`.
- DMA completion runs fixed-point A/B PI controllers and stages low-zero sign-magnitude TIM3 preloads. Positive A voltage drives A2 and positive B voltage drives B1, matching the board's asymmetric shunt placement; the opposite signs drive A1/B2. Raw overcurrent, invalid references or outputs, DMA/PWM failures, and two consecutive carrier updates without a new control output latch the common all-low fault path.
- Firmware 0.18.2 uses `Kp=2`, retains `Ki=1/64` per 20 kHz step, and records the first 256 successful loop outputs for post-run tuning analysis. At 12 V, a 303 mA startup step has 6.53 ms 10-90% rise time, 8% overshoot, and 14.0 mA tail RMS error. A 606 mA / 15 Hz run tracked -17.78 RPM versus -18 RPM commanded. A 757 mA / 20 Hz, five-second run completed 100,000 loop updates and 1.97 revolutions versus 2.00 commanded with no fault or reset and 252-permille peak voltage effort against the 700-permille ceiling.
- The tied HIN/LIN topology has no defined all-FET-off command. `board_bridge_force_low_zero()` is the common deterministic software-fault state, not electrical disconnect.
- Core exceptions and every unclaimed interrupt record a panic code and halt.
- The firmware sets and verifies four NVIC preemption bits with no subpriorities; SysTick runs at priority 15.
- Sticky reset flags are captured and cleared at boot for debugger-visible reset-cause diagnostics.
- A nominal one-second independent watchdog is serviced only by the foreground liveness supervisor; no interrupt or subsystem has a raw-feed API.
- IWDG continues during debugger halt and resets the firmware if foreground service stops.
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
| `src/board/` | Board-specific I/O and bridge GPIO ownership |
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
and the [debugger diagnostic record](../docs/DIAGNOSTICS.md). The next hardware
integration step is encoder/electrical alignment followed by the existing
portable velocity and position control modules.
