# Firmware

This directory contains the buildable N32L406CBL7 current-regulated product
image. Firmware 0.22.0 closes both winding-current loops at 20 kHz through the
authoritative drive supervisor and adds a bounded automatic alignment service
to the product's timestamped 1 kHz mechanical estimator and measured
50-electrical-cycle alignment geometry. It also adds versioned, CRC-protected,
dual-slot persistence for the accepted alignment. Two successful 757.4 mA alignments and
a generic-STOP abort are bench-proven on the tested motor with zero faults,
reset, or retained panic. The
0.19.0 supervisor path is bench-proven through a
303 mA, 5 electrical Hz, two-second deadline-bounded run and a separate
151.5 mA explicit-STOP run. Both released diagnostic authority, returned the
bridge to `ZERO`, and preserved reset/panic health. Display, encoder, RS-485,
ADC/DMA, all four bridge legs, and local/isolated input mappings are
bench-proven on the tested board. Encoder-loss fault injection and local-button
authority remain to be exercised. The 0.20.0 estimator schedule passed its
initial idle and active hardware regression. Firmware 0.21.0 automatic alignment
and generic STOP are bench-proven; Menu and readiness-loss injection remain to
be exercised.

The 0.22.0 storage and protocol implementation passes host failure-injection
tests and Debug/Release Arm builds. First-save, unchanged-save, power-cycle
restore, persistent clear, and no-restored-authority behavior pass on COM14.

## Current operating contract

- Startup verifies the reset-default 4 MHz MSI, then enables the fitted 8 MHz HSE and PLL x8 for 64 MHz HCLK. PCLK2 is 32 MHz, PCLK1 is 16 MHz, and TIM3 receives the doubled 32 MHz APB1 timer clock.
- The initial stack and runtime data use SRAM1 only. SRAM2 is initialized for parity but unavailable to the linker until bench validation.
- The active-high status LED is PD0; PB8/PB9/PA15 and PB12/PB13 are bench-proven active-low monitored inputs.
- PA6, PA7, PB0, and PB1 begin high impedance/no-pull, then firmware 0.22.0 preloads all four low and assigns TIM3 channels 1-4 on AF2. Each signal directly drives tied EG3013 HIN/LIN inputs, so low selects the low-side FET and high selects the high-side FET.
- SPI1 on PB3-PB6 performs bounded mode-3 MT6816 reads on a 1 kHz foreground schedule, including while the motor runs. Accepted samples receive microsecond timestamps and feed the shared unwrap/velocity estimator. Native protocol 1.6 reports raw sensor health, unwrapped mechanical position, filtered velocity, current and maximum observed sample intervals, estimator faults, alignment validity, automatic-alignment progress/results, and persistent configuration state. The foreground rate is a hardware-validation candidate; a future timer-released SPI/DMA path remains available if measured jitter requires it.
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
- Earlier characterization builds used Next to select A1/A2/B1/B2 and Enter to apply edge-aligned 20 kHz, 50% hardware PWM. Firmware 0.21.0 retains Next only as a product-diagnostic initial-phase selector and requires Enter to be released once, then held continuously, before it requests diagnostic authority from the drive supervisor. Raw release or Menu returns to `ZERO`. RS-485 can configure 1-165 counts and 0.001-50 electrical Hz, then request a 0.1-60 second diagnostic run; timeout, Menu, transport failure, or STOP returns it to `ZERO`.
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
- The drive supervisor owns `RESET_SAFE`, `DIAGNOSTIC`, `READY`, `ALIGN`, `RUN`,
  and `FAULT`, with distinct diagnostic and motion authority. It admits
  energization only from `READY`, clears authority on every fault, and keeps
  expected fault reporting alive under the watchdog.
- Automatic alignment is a production motion-authority client. It drives
  phase zero, positive quarter phase, and phase zero again through the same
  current backend; checks current tracking, encoder stability, expected
  quarter-step geometry, closure, and deadline; and commits calibration only
  after the complete observation passes. STOP, Menu, transport loss, encoder
  loss, current-loop failure, or readiness loss stop the backend and converge
  on the supervisor's normal release/fault path.
- The final two 2 KiB Flash pages are alternating motor-configuration slots.
  A successful alignment is automatically saved only after backend and motion
  authority release. Boot restores alignment only after schema, length,
  generation, CRC-32, commit marker, semantic bounds, and motor geometry all
  validate; it never restores authority, pending work, or current-sensor zeros.
  Explicit status/save/clear operations are available only through the same
  safe-state production command service.

## Layout

| Path | Purpose |
| --- | --- |
| `include/mks57d/` | Project-owned public interfaces |
| `src/app/` | Application state transitions |
| `src/board/` | Board-specific I/O and bridge GPIO ownership |
| `src/platform/` | Startup-adjacent runtime, timebase, TIM3 PWM, panic handling, and IWDG access |
| `src/protocol/` | Foreground framing and wire-protocol adapters |
| `src/safety/` | Hardware-independent fault state and watchdog liveness policy |
| `src/services/` | Versioned product configuration storage |
| `linker/` | Exact N32L406CBL7 memory layout |
| `vendor/nations/` | Minimal, license-preserving CMSIS/device subset |

See [building instructions](../docs/BUILDING.md), the [MT6816 encoder
contract](../docs/ENCODER.md), the [USART1 / RS-485 contract](../docs/RS485.md),
the [command protocol](../docs/PROTOCOL.md),
the [ADC contract](../docs/ADC.md), the
[watchdog policy](../docs/WATCHDOG.md), the [boot self-test](../docs/BOOT_SELF_TEST.md),
and the [debugger diagnostic record](../docs/DIAGNOSTICS.md). The next control
integration step is the aligned torque/current interface and existing portable
velocity and position modules; Menu and readiness-loss fault injection remain
parallel hardware checks.
