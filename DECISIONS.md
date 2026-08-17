# Decision Log

Forward-facing, append-only record of architectural and behavioral decisions for this project. Read this file at the start of each session — it is more reliable than memory and supersedes any conflicting claims in AGENTS.md or README.

When a decision is reversed or superseded, append a new entry rather than rewriting the old one.

## 2026-08-14 — Use a clean-sheet implementation

- **Decision:** Replacement firmware will be implemented from public hardware documentation and manufacturer MCU support, without extracting or reproducing Makerbase firmware or its bootloader.
- **Why:** The goal is a maintainable open implementation, not recovery or emulation of undocumented proprietary code.
- **Supersedes:** (initial)
- **Affects:** Project scope, firmware architecture, protocol compatibility work

## 2026-08-14 — Treat the repository as a gated feasibility project

- **Decision:** Work proceeds through explicit debug-access, passive-bring-up, power-stage, current-loop, and servo-control gates; motor output is not an initial milestone.
- **Why:** MCU protection and safe bridge behavior must be proven before substantial control-software investment.
- **Supersedes:** (initial)
- **Affects:** `PLAN.md`, `docs/BRINGUP.md`, firmware milestone ordering

## 2026-08-14 — Keep external binary material local by default

- **Decision:** Manufacturer archives, programming executables, schematics, manuals, and generated analysis files live under ignored `vendor/local/`, `reference/local/`, and `scratch/` directories.
- **Why:** The future public repository should not redistribute external binaries or documents until provenance and redistribution rights are recorded.
- **Supersedes:** (initial)
- **Affects:** `.gitignore`, `vendor/`, `reference/`, repository layout

## 2026-08-14 — Use CMSIS-DAP as the baseline programming path

- **Decision:** The initial programming/debug path is a Raspberry Pi Pico running CMSIS-DAP v1/HID, paired with the Nations utility for recovery and pyOCD plus the N32 CMSIS pack for development.
- **Why:** It supports the vendor's full programming workflow, is reproducible, and avoids the J-Link EDU Mini commercial-use restriction.
- **Supersedes:** (initial)
- **Affects:** Phase 1 bring-up, host tooling, programmer documentation

## 2026-08-14 — Sample the board's external current amplifiers directly

- **Decision:** The initial design will sample `currentB` on PA1 and `currentA` on PA2 directly rather than enabling the MCU's internal op-amps.
- **Why:** The PCB already provides GS8632 current amplifiers, while the internal op-amp pin routing conflicts with the two existing current-sense nets.
- **Supersedes:** (initial)
- **Affects:** ADC configuration, current-loop design, hardware assumptions

## 2026-08-15 — Use CMake and Arm GNU for the bare-metal firmware build

- **Decision:** Build the N32L406CBL7 image with CMake presets and `arm-none-eabi-gcc`, using only the required Nations CMSIS/device subset and a corrected 128 KiB flash / 24 KiB SRAM linker layout.
- **Why:** The installed toolchain is reproducible and auditable, while the generic vendor linker script incorrectly exposes 32 KiB of SRAM for this part.
- **Supersedes:** (initial)
- **Affects:** `CMakePresets.json`, `cmake/`, `firmware/CMakeLists.txt`, `firmware/linker/`, `firmware/vendor/nations/`

## 2026-08-15 — Make the first image passive by construction

- **Decision:** The initial firmware configures only the provisional PB9 status LED and exposes no bridge-control API; PA6, PA7, PB0, and PB1 remain in their reset configuration.
- **Why:** Bridge polarity, disable behavior, and the purchased PCB revision are not yet verified, so an unused motor-output interface is safer than an unproven software disable flag.
- **Supersedes:** (initial)
- **Affects:** `firmware/src/board/`, application state model, Phase 2 bring-up

## 2026-08-15 — Keep hardware-independent logic host-testable

- **Decision:** Application-state and fault-latch logic build as native C tests in a compiler configuration separate from the Arm firmware image.
- **Why:** Safety transitions and pure logic should be testable before hardware arrives without weakening or emulating the embedded peripheral layer.
- **Supersedes:** (initial)
- **Affects:** `tests/`, `tools/build.ps1`, CMake presets, future control/protocol modules

## 2026-08-15 — Treat N32L406xB SRAM as two discontiguous banks

- **Decision:** Place the initial stack, `.data`, `.bss`, and `.noinit` entirely in the 16 KiB SRAM1 bank at `0x20000000`. Treat `0x20004000`–`0x20005FFF` as unmapped, initialize all 8 KiB of SRAM2 at `0x20006000` with stores before checking its parity status, and reject project allocations in SRAM2 until this behavior is verified on hardware.
- **Why:** N32L40x User Manual V2.6 Table 2-9 describes a bank gap and requires SRAM2 initialization before use. The vendor SDK linker and CMSIS pack metadata describe incompatible contiguous layouts that would place the initial stack in the gap or expose nonexistent RAM.
- **Supersedes:** The contiguous 24 KiB SRAM layout in “Use CMake and Arm GNU for the bare-metal firmware build.”
- **Affects:** `firmware/linker/n32l406cbl7.ld`, `firmware/src/platform/system.c`, post-link verification, SRAM2 allocation policy

## 2026-08-15 — Keep the first image on reset-default 4 MHz MSI

- **Decision:** Use project-owned minimal clock startup that verifies and retains the reset-default 4 MHz MSI. Do not change the voltage range, enable HSI/HSE/PLL, or use the vendor system-clock source in the first hardware image.
- **Why:** The vendor clock source contains voltage-register operations and frequency options that conflict with the N32L406 documentation, and the SDK clock example contains a silicon-revision workaround for direct HSI/HSE selection. MSI is the least disruptive path for the first flash.
- **Supersedes:** The 16 MHz HSI clock-selection portion of “Use CMake and Arm GNU for the bare-metal firmware build.”
- **Affects:** `firmware/src/platform/system.c`, `firmware/CMakeLists.txt`, SysTick rate, first-flash procedure, deferred 64 MHz work

## 2026-08-15 — Route every unclaimed interrupt to the common panic path

- **Decision:** Make the vendor startup file's default exception/IRQ handler tail-call the project panic handler, which disables interrupts, records the reason in `.noinit`, and halts.
- **Why:** A silent infinite loop loses the diagnostic cause and creates a separate failure behavior. Initial firmware should have one deterministic terminal path for all unexpected exceptions and interrupts.
- **Supersedes:** The unmodified vendor `Default_Handler` loop.
- **Affects:** `firmware/vendor/nations/device/startup/startup_n32l40x_gcc.s`, `firmware/src/platform/panic.c`, fault diagnostics

## 2026-08-15 — Use a priority-partitioned bare-metal control architecture

- **Decision:** Use all four implemented NVIC priority bits for preemption and no subpriorities. Reserve the highest software priorities for terminal fault shutdown, control-deadline supervision, and synchronous current control; use interrupts only for bounded hardware-event work, and run outer control, protocol parsing, configuration, and diagnostics cooperatively outside the hard-real-time path. Share a host-testable `alpha/beta` and `d/q` current-control core between motor personalities while keeping phase adaptation, modulation, PWM/ADC timing, and shutdown project-owned.
- **Why:** The current loop requires deterministic latency, while communications and services must not interfere with it. A single-writer data model and explicit priority classes make nesting, deadlines, and fault authority auditable without adding an RTOS before measurements justify one.
- **Supersedes:** The less-specific candidate timing domains in `docs/ARCHITECTURE.md`.
- **Affects:** `docs/REALTIME_ARCHITECTURE.md`, NVIC configuration, ISR contracts, shared-data publication, control-loop boundaries, future stepper and PMSM/BLDC personalities

## 2026-08-15 — Give watchdog service to one foreground supervisor

- **Decision:** Configure IWDG for a nominal one-second timeout and expose no raw-feed API; one foreground supervisor may reload it every 100 ms only while application health passes and foreground polling stays within 250 ms. Capture and clear sticky reset flags at boot. Pause IWDG on debugger halt only in the passive, bridge-incapable image.
- **Why:** Interrupt-owned or subsystem-owned reloads could hide a stalled foreground or failed control domain. The initial debug exemption preserves first-board recovery without weakening bridge safety because the bridge pins remain untouched.
- **Supersedes:** (initial)
- **Affects:** `firmware/src/platform/watchdog.c`, `firmware/src/safety/watchdog_policy.c`, `firmware/src/main.c`, `docs/WATCHDOG.md`, future bridge-capable debug policy

## 2026-08-15 — Make the first diagnostic channel a versioned RAM record

- **Decision:** Export a 52-byte schema-versioned `g_diagnostics` record located through ELF symbols and published only by foreground with an odd/even sequence protocol; report firmware `0.1.0`, capabilities, boot/reset state, retained panic, application state, uptime, heartbeat, and watchdog health without assigning a fixed SRAM address or configuring serial hardware.
- **Why:** Early bring-up needs machine-readable observability before the RS-485 pin map and direction behavior are proven, while a versioned record provides an auditable compatibility boundary for debugger and future telemetry consumers.
- **Supersedes:** (initial)
- **Affects:** `firmware/include/mks57d/diagnostics.h`, `firmware/src/app/diagnostics.c`, `docs/DIAGNOSTICS.md`, Phase 2 diagnostic milestone

## 2026-08-15 — Require a monotonic passive boot self-test before watchdog service

- **Decision:** Track seven required startup gates in passed/failed masks, latch every failure until reset, publish each transition, and permit watchdog service only after all gates pass. Append the three masks to diagnostic schema 1, growing its record from 52 to 64 bytes without moving the original prefix.
- **Why:** Successful control flow alone does not expose which safety assumption was established or failed. A monotonic ledger makes boot progress auditable and prevents later activity from hiding a failed passive invariant.
- **Supersedes:** The 52-byte size in “Make the first diagnostic channel a versioned RAM record”; its original field layout and ownership remain unchanged.
- **Affects:** `firmware/src/safety/boot_self_test.c`, `firmware/src/board/board.c`, `firmware/src/main.c`, `docs/BOOT_SELF_TEST.md`, `docs/DIAGNOSTICS.md`

## 2026-08-15 — Stage passive I2C and OLED support before activation

- **Decision:** Compile a bounded 100 kHz I2C1 transport for provisional PA4/PA5 and a configurable SSD1306-compatible `0x3C` panel profile, but leave both uncalled until the purchased board and display profile are bench-verified.
- **Why:** The display is a useful low-risk peripheral for diagnostics, while keeping activation separate preserves the current GPIOA-clock-gated proof that bridge pins PA6/PA7 remain untouched.
- **Supersedes:** (initial)
- **Affects:** `firmware/src/platform/i2c1.c`, `firmware/src/drivers/ssd1306.c`, `docs/PERIPHERALS.md`, future passive-board self-test and diagnostics schema

## 2026-08-16 — Stage sequential raw ADC sampling before synchronous acquisition

- **Decision:** Compile an inactive, bounded ADC path that reads PA1 `currentB`, PA2 `currentA`, and PA3 `vBus` as three independent software-triggered 12-bit conversions. Use HSI only for the required 1 MHz ADC timing clock and a synchronous HCLK-derived sampling clock at or below 2 MHz; defer scan/DMA, interrupts, scaling, offsets, and PWM triggering.
- **Why:** Sequential reads preserve every result in the single regular-data register without prematurely committing to DMA or real-time timing. Keeping the path uncalled preserves the boot image's GPIOA-clock-gated bridge proof until the board, ADC power-up sequence, reference, gains, and settling can be measured.
- **Supersedes:** (initial)
- **Affects:** `firmware/src/platform/adc1.c`, `firmware/src/drivers/adc_sample.c`, `docs/ADC.md`, `docs/PERIPHERALS.md`, future passive-board self-test and diagnostics schema

## 2026-08-16 — Correct the RS-485 V1.1 LED, encoder, enable, and key mapping

- **Decision:** Use PD0/BOOT0 for the active-high blue status LED; use SPI1 on PB3 SCK, PB4 MISO, PB5 MOSI, and software-controlled PB6 CS for the MT6816; reserve PB7 as provisional `nEN`, PB8 as `KEY_ENTER`, PB9 as `KEY_MENU`, and PA15 as `KEY_NEXT`.
- **Why:** A visual pin-by-pin trace of the published RS-485 V1.1 schematic, corroborated by the N32 pin data and independent board code, found that the previous map was shifted by one pin and incorrectly drove the Menu key as the heartbeat LED.
- **Supersedes:** The PB9 LED assignment in “Make the first image passive by construction” and the earlier PB4-PB7 encoder mapping.
- **Affects:** `firmware/src/board/board.c`, SPI1 configuration, `docs/HARDWARE.md`, boot invariants, physical bring-up checklist

## 2026-08-16 — Activate bounded low-energy peripherals without exposing the bridge

- **Decision:** Safe peripherals may initialize and operate during normal diagnostic boot as their drivers mature. The MT6816 candidate now receives a bounded, read-only, mode-3 SPI burst every 10 ms in foreground after a 20 ms power-up delay; transport/parity failures and sensor warnings are diagnostic rather than boot-fatal. PA6/PA7/PB0/PB1 and provisional PB7 `nEN` remain untouched, and no bridge-control API exists.
- **Why:** The firmware needs to progress toward normal operation before bench access is available, while the material hazard is the unproven power bridge rather than read-only low-energy peripheral traffic. Separate pre/post peripheral invariants preserve an auditable bridge boundary.
- **Supersedes:** The general inactive/no-output posture of “Make the first image passive by construction”; its prohibition on bridge control remains in force. It does not by itself activate the staged I2C/OLED or ADC paths.
- **Affects:** `firmware/src/main.c`, `firmware/src/platform/spi1.c`, `firmware/src/drivers/mt6816.c`, board invariants, peripheral bring-up policy, diagnostics

## 2026-08-16 — Append encoder state in diagnostic schema 2

- **Decision:** Bump firmware to `0.2.0` and diagnostic schema to 2, preserving the original 64-byte prefix and appending MT6816 status, SPI status, raw angle, sensor flags, accepted/error counters, and last-attempt time for a total of 92 bytes.
- **Why:** Active encoder acquisition needs debugger-visible success and failure evidence, and the capability/size change is an explicit compatibility boundary rather than an undocumented ABI expansion.
- **Supersedes:** Diagnostic schema 1 and the 64-byte size in “Require a monotonic passive boot self-test before watchdog service.”
- **Affects:** `CMakeLists.txt`, `firmware/include/mks57d/diagnostics.h`, `firmware/src/app/diagnostics.c`, post-link checks, host ABI tests, `docs/DIAGNOSTICS.md`

## 2026-08-16 — Reserve DMA for deterministic peripheral movement

- **Decision:** Treat the eight-channel DMA controller as a budgeted `RUN` resource: prioritize complete synchronous ADC sample capture, then encoder SPI and USART transfers; keep I2C DMA out of `RUN` because of the documented concurrency erratum, and begin PWM control with direct validated preload stores rather than autonomous DMA bursts.
- **Why:** DMA is most valuable when it removes polling and per-byte interrupts, while indiscriminate use adds AHB contention, channel pressure, errata exposure, and stale-output failure modes without offloading control math.
- **Supersedes:** (initial)
- **Affects:** `docs/REALTIME_ARCHITECTURE.md`, future DMA driver, synchronous ADC, encoder acquisition, USART1 transport, display scheduling, PWM backend

## 2026-08-16 — Make USART1 receive-first and silent with reserved DMA channels

- **Decision:** Activate USART1 on PA9/PA10 at 115200 8N1 with PA8 low for receive; assign circular RX to DMA channel 4 and bounded frame TX to channel 5. DMA handlers publish only bounded transfer state, foreground owns received-byte consumption and future framing, and PA8 returns low only after USART transmission-complete proves the final stop bit has left the shifter. The image never transmits unsolicited traffic.
- **Why:** Continuous RX and frame TX remove polling and per-byte interrupt load while preserving multidrop-bus behavior and a clean protocol boundary. The published SP485E circuit ties active-low `/RE` to active-high `DE`, making low receive/high transmit unambiguous; its 10 kohm direction pull-up creates a separate reset-time bus-state measurement that firmware cannot eliminate.
- **Supersedes:** The unimplemented RS-485 transport state; the Phase 3 electrical-validation gate remains open.
- **Affects:** `firmware/src/platform/usart1_rs485.c`, DMA channels 4/5, PA8-PA10, board invariants, `docs/RS485.md`, `docs/REALTIME_ARCHITECTURE.md`

## 2026-08-16 — Append RS-485 state in diagnostic schema 3

- **Decision:** Bump firmware to `0.3.0` and diagnostics to schema 3, preserve the complete 92-byte schema-2 prefix, and append 44 bytes of RS-485 status, RX progress/error/overrun evidence, last received byte, and TX completion state for a 136-byte record.
- **Why:** DMA and half-duplex turnaround need debugger-visible evidence before an on-wire protocol exists, while preserving the old prefix keeps earlier readers able to consume fields they understand.
- **Supersedes:** Diagnostic schema 2 as the current producer format.
- **Affects:** `CMakeLists.txt`, diagnostics ABI/tests/post-link checks, `docs/DIAGNOSTICS.md`

## 2026-08-16 — Use a verified local cache for PDF reference access

- **Decision:** Catalog repeatedly used PDFs by stable ID, version, page count, and SHA-256, then generate ignored UTF-8 page text and provenance-tracked page renders under `reference/cache/`; keep the original PDF authoritative for layout-sensitive evidence.
- **Why:** Page-addressable text makes technical references quickly searchable without repeatedly rasterizing whole documents, while source verification and on-demand renders preserve traceability and visual review.
- **Supersedes:** The ad hoc generated-image portion of “Keep external binary material local by default”; its local-only and redistribution restrictions remain in force.
- **Affects:** `reference/sources.json`, `reference/cache/`, `tools/reference_cache.py`, `docs/REFERENCE_CACHE.md`, reference-review workflow

## 2026-08-16 — Separate canonical commands from wire compatibility

- **Decision:** Define one transport-independent, foreground-owned command service; make a versioned native RS-485 protocol its canonical wire interface, and implement Modbus RTU plus publicly documented Makerbase commands only as optional adapters to the same validation and dispatch path.
- **Why:** The Makerbase manuals reuse one command vocabulary across custom RS-485, sparse Modbus register mappings, and CAN, so an adapter boundary preserves useful interoperability without making implicit frame lengths, an additive checksum, or nonstandard register behavior part of the project's core contract.
- **Supersedes:** The unresolved protocol-architecture choices in Phase 7 of `PLAN.md`.
- **Affects:** `docs/PROTOCOL.md`, future protocol framing and dispatch modules, Modbus register map, Makerbase compatibility profile, host tools and protocol tests

## 2026-08-16 — Freeze the native v1 discovery frame

- **Decision:** Native v1 uses COBS-delimited, big-endian frames with an explicit device address, 16-bit sequence and command, 8-bit payload length capped at 64 bytes, and CRC-16/CCITT-FALSE; address 1 serves only `PING`, `GET_IDENTITY`, and `GET_CAPABILITIES`, while address 0 and all malformed or non-request traffic remain silent. Firmware 0.4.0 appends native parser evidence to the preserved diagnostics prefix as 184-byte schema 4.
- **Why:** A small read-only vertical slice proves framing, resynchronization, common dispatch, and DMA-backed replies without exposing configuration or motor authority, while dedicated counters keep upcoming electrical tests diagnosable.
- **Supersedes:** The open native-frame details in “Separate canonical commands from wire compatibility” and diagnostic schema 3 as the current producer format.
- **Affects:** `firmware/src/protocol/native_protocol.c`, `firmware/src/app/command_service.c`, `firmware/src/main.c`, diagnostic ABI/tests, `docs/PROTOCOL.md`, host and bench protocol tests

## 2026-08-16 — Develop the production servo core against host simulation

- **Decision:** Preserve the embedded `mks57d` executable as the bridge-incapable passive bring-up image while implementing production-owned estimation, trajectory, control, limits, and application contracts as portable modules exercised first by deterministic host simulation.
- **Why:** Most application behavior can advance before hardware arrives without committing to unverified PWM, ADC-trigger, bridge-polarity, current-scaling, or numerical-backend details.
- **Supersedes:** (initial)
- **Affects:** `firmware/src/control/`, `firmware/include/mks57d/`, `tests/`, firmware target composition, future hardware backends
