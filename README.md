# SERVO57D Open Firmware

Feasibility-stage clean-sheet firmware project for the Makerbase MKS SERVO57D RS-485 closed-loop stepper controller.

A bridge-characterization image now deliberately owns the four gate-driver
commands but is not motor-control firmware. It holds all four commands low as
a deterministic zero-voltage vector and, while Enter is held, toggles one
selected leg with edge-aligned 20 kHz, 50% TIM3 PWM for hardware validation. It also samples the encoder
and ADC, drives the fitted OLED, and serves a read-only native RS-485 protocol
while the characterizer is idle. A separate portable
application and control build exercises motion ownership, remote lease expiry,
step/direction input, bounded trajectory and servo behavior, and fault recovery
against deterministic host plants; it is deliberately not linked into the
characterization image.

## Intended outcome

If the feasibility gates succeed, the project should provide:

- Open, reproducible firmware for the N32L406CBL7-based controller.
- Closed-loop two-phase stepper current, velocity, and position control.
- Step/direction and RS-485 command interfaces.
- Documented calibration, configuration, fault handling, and update procedures.
- A design with documented reset/fault bridge behavior and bounded current,
  recognizing that this PCB exposes no defined all-FET-off command through its
  four tied gate-driver inputs.

## Scope boundaries

This is a clean-sheet implementation. It will not depend on extracting,
disassembling, or reproducing Makerbase firmware or its bootloader. The
canonical interface is a project-owned, versioned protocol over a
transport-independent command service. Modbus RTU and useful publicly
documented Makerbase commands are optional compatibility adapters to that same
service, not the internal API; byte-for-byte Makerbase compatibility is not a
requirement.

The project does not redesign the PCB and does not make the controller suitable for safety-critical machinery.

## What is already known

- MCU: Nations Technologies N32L406CBL7, Cortex-M4F, 128 KiB flash, with discontiguous 16 KiB SRAM1 and 8 KiB SRAM2 banks.
- The board exposes SWDIO and SWCLK, but not NRST, on its programming header.
- The MCU supports read-protection levels L0, L1, and irreversible L2.
- Manufacturer tools provide an L1-to-L0 unlock operation that mass-erases main flash.
- Nations supplies GCC startup code, linker support, peripheral drivers, examples, CMSIS-Pack data, J-Link loaders, and flash-algorithm source.
- Firmware 0.15.0 runs the fitted 8 MHz crystal through PLL at the N32L406's 64 MHz maximum. HCLK is 64 MHz, PCLK2 is 32 MHz, PCLK1 is 16 MHz, and the APB1 timer multiplier gives TIM3 32 MHz. Every active driver receives its actual bus or timer clock; the new default clock is bench-proven.
- The board has two external GS8632 current-sense amplifiers connected to the MCU's PA1 and PA2 ADC inputs.
- The fitted 72-by-40 display responds to the SSD1306-compatible profile at address `0x3C`. Firmware uses I2C1 on PA4/PA5 at 333.3 kHz and PB2 active-low reset. Sustained 50 Hz two-page updates are bench-proven; the current characterizer refreshes at 5 Hz.
- Firmware 0.15.0 uses the bench-proven target 20 kHz TIM3-triggered two-rank PA1 `currentB` / PA2 `currentA` acquisition. ADC/DMA are completely configured and armed before TIM3 starts. After 32 bridge-zeroed startup snapshots, the OLED shows both independently zero-calibrated signed currents as `A+#####mA` and `B+#####mA`; the nominal scale is approximately 6.06 mA/count. Bench readings dither near zero and remain within approximately +/-12 mA. Acquisition failure still shows its numeric status.
- Firmware 0.15.0 continues to monitor the bench-proven PB8 Enter, PB9 Menu,
  PA15 Next, PB13 M_IN1, and PB12 M_IN2 inputs and adds passive, no-pull
  monitoring of PA0 `nSTP`, PA8 `nDIR`, and PB7
  `nEN`. All three isolated inputs are bench-proven in the expected locations,
  and the 30 ms debounce is intentionally not a pulse counter.
- The published RS-485 V1.1 schematic routes an MT6816 encoder to SPI1 on PB3-PB6 and the blue status LED to PD0; PB9 is the Menu key, not the LED.
- A bounded mode-3 SPI1 reader acquires coherent MT6816-compatible register bursts at 100 Hz. Bench testing found stable position at rest, consistent response to shaft motion, and repeatable wraparound once per revolution.
- An active USART1 transport receives through a 256-byte circular DMA buffer,
  uses PC13 for final-stop-bit direction turnaround, and responds only to
  complete, CRC-valid native requests addressed to the board. Command/response
  operation is proven on the tested board through the physical `485_A2`/`485_B2`
  connector; the first read-only commands provide ping, identity, and capabilities.
- Schematic-derived ADC conversion uses a 20 mOhm shunt, 6.65 differential
  gain with unity mid-rail bias gain, and a 15.4 kOhm/1 kOhm bus divider. Actual
  ADC reference and independent A/B zero counts remain runtime calibration data.
- The manufacturer SDK includes timer-synchronous ADC and motor-control-oriented PWM examples that closely match the required peripheral architecture.
- Each EG3013 has HIN active-high and LIN active-low with nominal 120 ns dead
  time. On this board each MCU leg command is wired directly to both inputs, so
  command low selects the low-side FET and command high selects the high-side
  FET. Opposing internal input biases make a floating tied input undefined;
  there is no controlled all-FET-off state through these four signals.
- The retained bridge implementation preloads the
  all-low vector and maps PA6/PA7/PB0/PB1 to TIM3 channels 1-4 on AF2. Next
  selects A1/A2/B1/B2; holding Enter applies edge-aligned 20 kHz, 50% PWM;
  raw Enter release, Menu, and software panic
  converge on the all-low `ZERO` vector. IWDG continues during debugger halt.

See [hardware notes](docs/HARDWARE.md), [peripheral bring-up](docs/PERIPHERALS.md),
[MT6816 encoder bring-up](docs/ENCODER.md), [USART1 / RS-485 bring-up](docs/RS485.md),
[passive ADC bring-up](docs/ADC.md),
[architecture](docs/ARCHITECTURE.md), [real-time architecture](docs/REALTIME_ARCHITECTURE.md),
[command protocol](docs/PROTOCOL.md),
[watchdog policy](docs/WATCHDOG.md), [boot self-test](docs/BOOT_SELF_TEST.md),
[debugger diagnostics](docs/DIAGNOSTICS.md), and the [project plan](PLAN.md) for
the details and remaining unknowns.

## Safety status

The firmware is not ready to drive a motor or regulate current. Firmware 0.15.0
still deliberately inhibits bridge switching while the first real current-loop
backend is developed. Reset, halt, gate, and switch-node behavior remain to be measured,
and a true all-FET-off hardware mechanism has not been identified.

## Repository layout

| Path | Purpose |
| --- | --- |
| `firmware/` | Project-owned embedded source code when implementation begins |
| `tools/` | Project-owned host utilities and build/programming helpers |
| `docs/` | Architecture, hardware, bring-up, and research documentation |
| `reference/` | Catalog for external documents plus ignored originals and searchable local cache |
| `vendor/` | Manifest for manufacturer SDKs, algorithms, and programming tools |
| `scratch/` | Ignored generated images and temporary analysis material |
| `DECISIONS.md` | Append-only architectural and behavioral decision log |

External manufacturer material is retained under ignored `reference/local/` and `vendor/local/` directories. It is deliberately not staged for publication until redistribution rights and canonical download URLs are documented.

## Project status

No motor-control path has been commissioned on a board. The local buttons,
isolated auxiliary inputs, and isolated PA0/PA8/PB7 step/direction/enable
inputs are bench-proven. Implementing pulse capture and a step/direction
operating mode remains deferred. The bounded TIM3 characterizer is implemented;
the next gate is confirming the AF2 20 kHz carrier on PA6/PA7/PB0/PB1 before
adding timer-synchronous current acquisition.

## License

A project license has not been selected. See [LICENSE](LICENSE). Choose an open-source license before publishing project-owned firmware.
