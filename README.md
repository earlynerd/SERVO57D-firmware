# SERVO57D Open Firmware

Feasibility-stage clean-sheet firmware project for the Makerbase MKS SERVO57D RS-485 closed-loop stepper controller.

A buildable bridge-safe diagnostic image now exists, but it has not been
flashed or tested on hardware and cannot drive a motor. It actively samples the
encoder while leaving every bridge control untouched. The repository also
organizes the hardware research, manufacturer support material, safety
constraints, and staged implementation plan needed to decide whether the
project is worth pursuing.

## Intended outcome

If the feasibility gates succeed, the project should provide:

- Open, reproducible firmware for the N32L406CBL7-based controller.
- Closed-loop two-phase stepper current, velocity, and position control.
- Step/direction and RS-485 command interfaces.
- Documented calibration, configuration, fault handling, and update procedures.
- A design that starts safely and keeps the power bridge disabled after resets or faults.

## Scope boundaries

This is a clean-sheet implementation. It will not depend on extracting, disassembling, or reproducing Makerbase firmware or its bootloader. Compatibility with Makerbase's command protocol may be considered later from public documentation, but byte-for-byte compatibility is not an initial requirement.

The project does not redesign the PCB and does not make the controller suitable for safety-critical machinery.

## What is already known

- MCU: Nations Technologies N32L406CBL7, Cortex-M4F, 128 KiB flash, with discontiguous 16 KiB SRAM1 and 8 KiB SRAM2 banks.
- The board exposes SWDIO and SWCLK, but not NRST, on its programming header.
- The MCU supports read-protection levels L0, L1, and irreversible L2.
- Manufacturer tools provide an L1-to-L0 unlock operation that mass-erases main flash.
- Nations supplies GCC startup code, linker support, peripheral drivers, examples, CMSIS-Pack data, J-Link loaders, and flash-algorithm source.
- The board has two external GS8632 current-sense amplifiers connected to the MCU's PA1 and PA2 ADC inputs.
- The display bus is provisionally I2C1 on PA4/PA5 with PB2 reset; an inactive, host-tested SSD1306-compatible 72-by-40 candidate layer is compiled but not enabled at boot.
- An inactive, bounded ADC layer preserves the provisional PA1 `currentB`, PA2 `currentA`, and PA3 `vBus` raw-sample contract without enabling GPIOA or HSI at boot.
- The published RS-485 V1.1 schematic routes an MT6816 encoder to SPI1 on PB3-PB6 and the blue status LED to PD0; PB9 is the Menu key, not the LED.
- A bounded mode-3 SPI1 reader now acquires coherent MT6816 register bursts at 100 Hz, validates parity, and reports raw angle and sensor/transport status without making encoder loss boot-fatal.
- The manufacturer SDK includes timer-synchronous ADC and motor-control-oriented PWM examples that closely match the required peripheral architecture.
- The safe bring-up image keeps the reset-default 4 MHz MSI, initializes SRAM2 parity without allocating from it, leaves every bridge-control pin untouched, runs a seven-gate boot self-test and foreground-supervised independent watchdog, and publishes a versioned debugger diagnostic record.

See [hardware notes](docs/HARDWARE.md), [peripheral bring-up](docs/PERIPHERALS.md),
[MT6816 encoder bring-up](docs/ENCODER.md), [passive ADC bring-up](docs/ADC.md),
[architecture](docs/ARCHITECTURE.md), [real-time architecture](docs/REALTIME_ARCHITECTURE.md),
[watchdog policy](docs/WATCHDOG.md), [boot self-test](docs/BOOT_SELF_TEST.md),
[debugger diagnostics](docs/DIAGNOSTICS.md), and the [project plan](PLAN.md) for
the details and remaining unknowns.

## Safety status

The firmware is not ready to drive a motor or energize the bridge. The active
peripheral image is still unverified on hardware. Initial work must be
performed with the motor disconnected, a current-limited supply, and the
bridge held disabled. See [bring-up procedure](docs/BRINGUP.md).

## Repository layout

| Path | Purpose |
| --- | --- |
| `firmware/` | Project-owned embedded source code when implementation begins |
| `tools/` | Project-owned host utilities and build/programming helpers |
| `docs/` | Architecture, hardware, bring-up, and research documentation |
| `reference/` | Manifest for external schematics, datasheets, manuals, and CMSIS packs |
| `vendor/` | Manifest for manufacturer SDKs, algorithms, and programming tools |
| `scratch/` | Ignored generated images and temporary analysis material |
| `DECISIONS.md` | Append-only architectural and behavioral decision log |

External manufacturer material is retained under ignored `reference/local/` and `vendor/local/` directories. It is deliberately not staged for publication until redistribution rights and canonical download URLs are documented.

## Project status

The immediate decision is whether to proceed past feasibility. The first meaningful milestone is not motor movement: it is proving that a purchased board can be unlocked, erased, programmed, reset, and debugged reliably.

## License

A project license has not been selected. See [LICENSE](LICENSE). Choose an open-source license before publishing project-owned firmware.
