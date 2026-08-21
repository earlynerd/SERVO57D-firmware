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

## 2026-08-16 — Give motion authority to one source with controlled lease expiry

- **Decision:** The portable motion manager grants one source exclusive setpoint authority, reports acceptance separately from completion, treats matching retries as idempotent, always permits valid stop/disable requests, and converts remote-lease expiry into a bounded trajectory stop followed by disable.
- **Why:** Native, Modbus, Makerbase, local, and step/direction adapters need one deterministic arbitration and timeout contract without writing control state directly.
- **Supersedes:** The unresolved lease and motion-completion decisions in “Freeze the native v1 discovery frame.”
- **Affects:** `firmware/src/app/motion_manager.c`, command adapters, application state, protocol status, motion tests

## 2026-08-16 — Model step/direction as cumulative timer counts

- **Decision:** The portable step/direction service consumes timestamped signed cumulative-step snapshots, explicitly re-anchors on enable transitions, rejects implausible rates, and produces position targets without requiring an application callback per input edge.
- **Why:** Hardware timer counting can support high pulse rates deterministically while keeping polarity, pin mapping, and counter-extension details in the future board backend.
- **Supersedes:** (initial)
- **Affects:** `firmware/src/app/step_direction.c`, future timer/input-capture backend, input-mode configuration, host tests

## 2026-08-16 — Refresh remote authority only with explicit new commands

- **Decision:** A newly accepted owner command or explicit `KEEPALIVE`
  refreshes the remote motion lease. A duplicate request remains an idempotent
  retry and does not refresh it. Motion status retains the two newest terminal
  command results, including source identity, so a stop or disable result does
  not immediately erase the interrupted move's outcome.
- **Why:** A long move needs an unambiguous heartbeat that cannot be synthesized
  by replaying stale traffic, while interrupted command results must remain
  observable long enough for a transport adapter to report them.
- **Supersedes:** The implicit lease-refresh and single-result details in “Give
  motion authority to one source with controlled lease expiry.”
- **Affects:** `firmware/src/app/motion_manager.c`, future native/Modbus/Makerbase
  motion adapters, and status/telemetry mappings

## 2026-08-17 — Define passive bridge pins by electrical behavior

- **Decision:** Passive bring-up accepts each bridge-control pin in analog or floating digital-input mode, while rejecting output, alternate-function, or pull-enabled configurations.
- **Why:** Both accepted modes are high impedance; requiring analog mode alone incorrectly rejected the N32L406 reset-default input state and stopped otherwise safe firmware at boot.
- **Supersedes:** The exact-zero PMODE assumption in the passive bridge invariant.
- **Affects:** `firmware/src/board/board.c`, passive boot self-test, bridge-safety contract

## 2026-08-17 — Test PC13 as RS-485 transceiver direction control

- **Decision:** Drive the tied RS-485 DE/RE control from PC13, low for receive and high for transmit, while retaining USART1 TX on PA9 and RX on PA10; leave PA8 untouched by the RS-485 transport.
- **Why:** PA8 is labeled `nDIR` on the schematic, while PC13 is labeled `RE1` and was traced by the user to the populated RS-485 transceiver. A diagnostic beacon using PC13 direction control was subsequently received on the board's working RS-485 connector.
- **Supersedes:** The PA8 direction assignment in “Activate bounded receive-first USART1 transport.”
- **Affects:** `firmware/src/platform/usart1_rs485.c`, `firmware/include/mks57d/rs485.h`, PC13, PA8, RS-485 bring-up documentation

## 2026-08-17 — Identify the working RS-485 connector by bench test

- **Decision:** Use the physical connector labeled `485_A2`/`485_B2` in the published schematic for RS-485 bring-up on the tested board; do not assume the connector labeled `485_A`/`485_B` is electrically usable without a continuity check.
- **Why:** The user received the 115200 8N1 diagnostic beacon cleanly only from the `485_A2`/`485_B2` connector. This contradicts the apparent schematic net connectivity, where the populated transceiver is shown on `485_A`/`485_B` and the `_2` labels appear isolated.
- **Supersedes:** The assumption that either published connector, or specifically the schematic-connected `485_A`/`485_B` connector, is the verified RS-485 bench port.
- **Affects:** Bench wiring, connector identification, `docs/BRINGUP.md`, `docs/RS485.md`, and hardware-revision verification

## 2026-08-18 — Activate bounded passive display bring-up

- **Decision:** Firmware 0.4.1 holds unbiased PB2 active-low display reset from passive board initialization, activates 100 kHz I2C1 on PA4/PA5, then makes one non-fatal SSD1306-compatible `0x3C` initialization and bordered `MKS57D` frame attempt while continuing to enforce every bridge-pin invariant.
- **Why:** The user checked the schematic mapping, 4.7 kOhm bus pull-ups, lack of PB2 bias, and absence of bridge sharing; a readable static pattern is the next low-energy hardware discriminator after native RS-485 command/response success.
- **Supersedes:** “Stage passive I2C and OLED support before activation.”
- **Affects:** `firmware/src/board/board.c`, `firmware/src/platform/i2c1.c`, `firmware/src/drivers/display_test_pattern.c`, `firmware/src/main.c`, passive boot behavior, and display capability reporting

## 2026-08-18 — Show encoder position with bounded fast-mode display updates

- **Decision:** Run I2C1 in fast mode at the 4 MHz clock's conservative 333.3 kHz divider and refresh a five-digit raw encoder position at 50 Hz using only two SSD1306 pages; display failures remain non-fatal and never affect bridge safety or control state.
- **Why:** The fitted panel is bench-proven, and partial updates provide a smooth visual encoder check without attempting the panel's out-of-spec 1 MHz rate or monopolizing the polling foreground.
- **Supersedes:** The static test-frame behavior and 100 kHz bus rate in “Activate bounded passive display bring-up.”
- **Affects:** `firmware/src/platform/i2c1.c`, `firmware/src/drivers/encoder_display.c`, `firmware/src/drivers/ssd1306.c`, `firmware/src/main.c`, passive display timing and behavior

## 2026-08-18 — Activate sequential passive ADC validation

- **Decision:** Firmware 0.6.0 initializes the staged PA1 `currentB`, PA2 `currentA`, and PA3 `vBus` polling ADC path, samples all three raw channels at 20 Hz, and cycles labeled `A`, `B`, and `V` values on the OLED once per second; sampling and display failures remain non-fatal.
- **Why:** Encoder and display behavior are bench-proven, making bridge-disabled raw current-sense and bus-voltage observation the next passive hardware gate without introducing scaling, DMA, interrupts, or control authority.
- **Supersedes:** The inactive status in “Stage sequential raw ADC sampling before synchronous acquisition” and the encoder-only screen behavior in “Show encoder position with bounded fast-mode display updates.”
- **Affects:** `firmware/src/platform/adc1.c`, `firmware/src/drivers/adc_display.c`, `firmware/src/main.c`, capability reporting, passive boot behavior and OLED contents

## 2026-08-18 — Encode schematic-derived ADC engineering scaling

- **Decision:** Convert calibrated ADC samples with `Vout = Vref + 6.65 * I * 0.020 ohm` for each current channel and a `(15.4 kohm + 1 kohm) / 1 kohm = 16.4` VBus ratio; supply actual ADC reference voltage and independently measured A/B zero counts at runtime rather than hard-coding nominal midscale or this board's observed offsets.
- **Why:** The Kelvin shunt network gives the mid-rail reference unity gain and the differential shunt voltage 6.65 gain, while bench readings establish stable channel-specific zero offsets but not universal production calibration values.
- **Supersedes:** The deferred engineering-unit scaling portion of “Stage sequential raw ADC sampling before synchronous acquisition.”
- **Affects:** `firmware/include/mks57d/adc_calibration.h`, `firmware/src/drivers/adc_calibration.c`, future current limits, telemetry, startup offset calibration and bus-voltage checks

## 2026-08-18 — Maintain durable documentation, omit ephemeral bench narration

- **Decision:** Keep the README, plan, and subsystem documentation synchronized with durable implementation and hardware findings, while not maintaining instructions or narration useful only during the current one-off bench session.
- **Why:** Stale project facts are harmful, but disposable bring-up notes do not justify ongoing documentation work once the present board has passed that step.
- **Supersedes:** (initial)
- **Affects:** `README.md`, `PLAN.md`, `docs/`, future bring-up work

## 2026-08-18 — Activate debounced passive input monitoring

- **Decision:** Firmware 0.7.0 configures PB8 `KEY_ENTER`, PB9 `KEY_MENU`, PA15 `KEY_NEXT`, PB13 `M_IN1`, and PB12 `M_IN2` as pulled-up inputs, debounces each independently over three 10 ms samples, and shows their raw active-low levels as `E M N 1 2` on the OLED at 50 Hz.
- **Why:** These local and opto-isolated signals are the final mapped low-energy inputs and can be validated without granting any bridge authority; showing raw levels avoids assuming application semantics during electrical bring-up.
- **Supersedes:** The unimplemented buttons/isolated-input portion of Phase 3; the ADC remains sampled but no longer owns the current OLED view.
- **Affects:** `firmware/src/board/board_inputs.c`, `firmware/src/drivers/user_inputs.c`, `firmware/src/drivers/input_display.c`, `firmware/src/main.c`, diagnostics capability bitmap, passive GPIO invariants

## 2026-08-18 — Confirm physical key layout and auxiliary inputs

- **Decision:** Treat the tested board's physical key order as left `KEY_NEXT`/N, center `KEY_ENTER`/E, right `KEY_MENU`/M; accept the active-low operation of all three keys plus M_IN1 and M_IN2 as bench-proven.
- **Why:** The firmware 0.7.0 OLED monitor changed independently and correctly for every physical button and both isolated auxiliary inputs.
- **Supersedes:** The unverified hardware status in “Activate debounced passive input monitoring.”
- **Affects:** `PLAN.md`, `README.md`, `docs/HARDWARE.md`, `docs/PERIPHERALS.md`, physical UI mapping

## 2026-08-18 — Defer external step/direction/enable mode

- **Decision:** Defer tracing and implementing the isolated step/direction/enable interface; it is not a prerequisite for the first bridge and current-control milestones, and provisional PB7 `nEN` remains untouched and must not be treated as a proven hardware bridge-disable signal.
- **Why:** The native RS-485 path is sufficient for controlled bring-up, while step/direction mode adds a separate input-capture and polarity-validation track that does not reduce the immediate power-stage risk.
- **Supersedes:** The Phase 3 requirement to bring up isolated step/direction/enable before entering power-stage characterization.
- **Affects:** `PLAN.md`, `README.md`, `docs/PERIPHERALS.md`, Phase 3 exit scope, future step/direction backend

## 2026-08-18 — Validate pulse-interface pins before deferring the mode

- **Decision:** Firmware 0.8.0 passively samples schematic candidates PA0 `nSTP`, PA8 `nDIR`, and PB7 `nEN` as high-impedance, no-pull inputs and shows their debounced raw electrical levels as `S D E` on the OLED. Use this only to validate pin mapping and active-low behavior; defer timer capture, pulse-rate validation, motion authority, enable semantics, and the step/direction operating mode.
- **Why:** Implementing step/direction control is not required for the first RS-485-controlled bridge tests, but confirming the physical input mapping now prevents a known schematic assumption from becoming stale or silently trusted later.
- **Supersedes:** The pin-tracing deferral in “Defer external step/direction/enable mode”; that entry's implementation deferral remains in force.
- **Affects:** `firmware/src/board/board_inputs.c`, `firmware/src/drivers/pulse_input_display.c`, `firmware/src/main.c`, `PLAN.md`, `README.md`, `docs/HARDWARE.md`, `docs/PERIPHERALS.md`

## 2026-08-18 — Confirm isolated pulse-interface mapping

- **Decision:** Accept PA0 `nSTP`, PA8 `nDIR`, and PB7 `nEN` as bench-proven active-low isolated inputs in their expected physical locations on the tested board; retain their passive read-only treatment and defer pulse capture and operating semantics.
- **Why:** Exercising Step, Direction, and Enable through the physical interface changed only the corresponding `S`, `D`, and `E` OLED indications with the expected polarity.
- **Supersedes:** The unverified hardware status in “Validate pulse-interface pins before deferring the mode.”
- **Affects:** `PLAN.md`, `README.md`, `firmware/README.md`, `docs/HARDWARE.md`, `docs/PERIPHERALS.md`, physical input pin map

## 2026-08-18 — Retire the permanent bridge-pin no-drive invariant

- **Decision:** Firmware 0.9.0 removes the post-peripheral `board_bridge_invariants_hold()` panic gate that permanently required PA6/PA7/PB0/PB1 to remain non-driving; retain the early `board_passive_invariants_hold()` reset-safe state check until an explicit bridge backend takes ownership.
- **Why:** The passive-peripheral phase is complete, and the four gate-control signals cannot be mapped or characterized without deliberately driving the bridge interface.
- **Supersedes:** The permanent post-initialization no-drive contract in “Allow safe peripherals after passive boot” and subsequent bring-up entries; reset-safe startup and common immediate-off requirements remain.
- **Affects:** `firmware/src/board/board.c`, `firmware/include/mks57d/board.h`, `firmware/src/main.c`, boot self-test meaning, Phase 3 exit, Phase 4 bridge characterization

## 2026-08-18 — Characterize tied EG3013 inputs from an all-low zero vector

- **Decision:** Firmware 0.10.0 preloads and drives PA6, PA7, PB0, and PB1 low, then permits one manually selected leg to toggle at 500 Hz only while Enter is held. Next selects A1/A2/B1/B2; raw Enter release, Menu, and the software panic path return all four commands low. The IWDG is not paused during debugger halt.
- **Why:** Each board command is tied directly to EG3013 HIN and LIN. HIN is active-high, LIN is active-low, and the driver inserts nominal 120 ns dead time, so one GPIO selects exactly one FET in its half bridge. Opposing input bias networks make a floating GPIO undefined. All-low is a deterministic zero differential-voltage vector, not an all-FET-off state; the missing coast state does not prevent two-level PWM or FOC, but it materially changes reset, fault, and current-decay behavior.
- **Supersedes:** The assumption that a common software-commanded all-FET-off state is available, and earlier bridge-disable wording that described all-low as off.
- **Affects:** `firmware/src/board/board.c`, `firmware/src/drivers/bridge_characterizer.c`, `firmware/src/platform/panic.c`, watchdog/debug policy, power-stage validation, future PWM modulation and fault handling

## 2026-08-18 — Move bridge characterization to TIM3 hardware PWM

- **Decision:** Firmware 0.11.0 maps TIM3 channels 1-4 to PA6, PA7, PB0, and PB1 on AF2 and uses an edge-aligned 20 kHz carrier. `RUN` applies an exact 50% compare value to one selected channel while the other three remain at zero. Compare and auto-reload preloads are enabled, and start/stop writes are committed with a forced update event. The direct-GPIO all-low path remains authoritative for panic and initialization failure.
- **Why:** The 500 Hz foreground pattern proved bridge polarity and ordinary commutation, but current regulation requires hardware-owned edge timing. Nations Library 2.3.0 contains a four-channel TIM3 PWM example using this exact pin and AF mapping. At the retained 4 MHz timer clock, 200 edge-aligned counts produce exactly 20 kHz and 100 counts produce exactly 50% duty.
- **Supersedes:** The foreground-timed 500 Hz `RUN` behavior in “Characterize tied EG3013 inputs from an all-low zero vector”; its button gating, selected-leg UI, and all-low zero/fault contract remain in force.
- **Affects:** `firmware/src/platform/tim3_bridge_pwm.c`, `firmware/src/board/board.c`, bridge-characterizer state, PWM/ADC timing validation, firmware version 0.11.0

## 2026-08-18 — Capture both current channels from the TIM3 carrier boundary

- **Decision:** Firmware 0.12.0 configures TIM3 update as `TRGO` and uses it to trigger one ADC regular sequence per 20 kHz PWM period. ADC scan mode sequences `currentB` then `currentA`; DMA channel 1 stores complete 16-bit results in a circular buffer without interrupts. Foreground accepts only a stable complete pair, inhibits PWM until one pair exists, and sends acquisition faults to the common all-low panic path. While `RUN` is held, the OLED alternates the raw A/B samples.
- **Why:** Current regulation needs hardware-synchronous acquisition rather than unrelated 20 Hz polling. At the retained 2 MHz ADC clock, two 28.5-cycle samples require approximately 44 microseconds including conservative single-sequence overhead and fit within the 50 microsecond carrier period. The update boundary is deterministic and sufficient to validate trigger/DMA behavior, but its proximity to switching edges means it is not yet accepted as the production quiet sampling point.
- **Supersedes:** The current-channel portion of the polling-only acquisition in “Activate raw ADC sampling and display”; polling `vBus` is temporarily displaced in this characterization image.
- **Affects:** `firmware/src/platform/adc1.c`, `firmware/src/platform/tim3_bridge_pwm.c`, DMA channel 1 ownership, OLED characterizer behavior, Phase 5 sampling validation, firmware version 0.12.0

## 2026-08-18 — Keep diagnostics alive when synchronous acquisition is unavailable

- **Decision:** Firmware 0.12.1 treats current acquisition as mandatory for PWM authority but not for diagnostic liveness. ADC initialization or DMA acquisition failure keeps the bridge at the all-low vector, rejects `RUN`, and leaves the heartbeat, protocol, and OLED operational. The OLED renders the numeric `adc1_status_t` value as `A####` whenever acquisition is unavailable.
- **Why:** Firmware 0.12.0 cleared the OLED and then entered the terminal panic path when initial synchronous acquisition setup failed, leaving no heartbeat or visible indication of which new peripheral gate failed. Loss of current data must inhibit the bridge, but killing independent diagnostics makes bring-up unnecessarily opaque.
- **Supersedes:** The requirement in “Capture both current channels from the TIM3 carrier boundary” that every acquisition setup/runtime fault enter terminal panic. Invalid acquisition still removes PWM authority immediately.
- **Affects:** `firmware/src/main.c`, bridge enable gating, OLED error reporting, diagnostic availability, firmware version 0.12.1

## 2026-08-18 — Isolate the manufacturer ADC-DMA example before restoring PWM synchronization

- **Decision:** Firmware 0.12.2 temporarily replaces the failing two-rank TIM3-triggered acquisition with Nations SDK 2.3.0's one-channel software-triggered continuous ADC-DMA pattern on PA2 `currentA`. DMA channel 1 uses a fixed one-halfword circular destination, and the OLED displays that value as `A####`. Bridge switching is suppressed for this isolation image.
- **Why:** Firmware 0.12.1 reported DMA channel-1 `ERRF`. The user manual and SDK confirm the selected channel, request, peripheral address, and halfword widths, so the next test removes TIM3 triggering, multi-rank scan, memory increment, and the ring buffer without introducing undocumented alternatives.
- **Supersedes:** The active acquisition behavior in “Capture both current channels from the TIM3 carrier boundary”; that design remains the target after the basic ADC-DMA path is proven.
- **Affects:** `firmware/src/platform/adc1.c`, `firmware/src/main.c`, bridge authority, OLED behavior, firmware version 0.12.2

## 2026-08-18 — Restore the ADC DMA ring before restoring scan or timer trigger

- **Decision:** Firmware 0.12.3 keeps the proven PA2 software-triggered conversion and manufacturer initialization order, but restores DMA memory increment, transfer count 64, and the 64-halfword circular destination. The OLED publishes the latest stable ring sample, and bridge switching remains suppressed.
- **Why:** Firmware 0.12.2 produced stable 2039-2044 readings, proving the basic ADC-to-DMA path. Changing only the destination behavior determines whether the original `ERRF` was caused by memory increment or the larger circular transfer before multi-rank scan and TIM3 trigger are reintroduced.
- **Supersedes:** The fixed one-halfword destination in “Isolate the manufacturer ADC-DMA example before restoring PWM synchronization”; the one-channel software-triggered conversion remains in force.
- **Affects:** `firmware/src/platform/adc1.c`, ADC DMA snapshot logic, OLED behavior, firmware version 0.12.3

## 2026-08-18 — Collapse the remaining ADC tests into the target synchronous path

- **Decision:** Firmware 0.13.0 restores the target two-rank `currentB/currentA` sequence triggered by 20 kHz TIM3 update, but configures and arms ADC/DMA before TIM3 initialization. The OLED alternates stable B/A pairs, and bridge switching remains suppressed until this target feedback path is bench-proven.
- **Why:** Both the manufacturer fixed-destination pattern and the 64-halfword incrementing ring produced stable PA2 samples. The user chose to stop testing scan and trigger separately; reversing the original timer-first startup order while restoring the final configuration gives one directly useful acceptance test.
- **Supersedes:** “Restore the ADC DMA ring before restoring scan or timer trigger”; the diagnostic ladder ends and target acquisition resumes.
- **Affects:** `firmware/src/platform/adc1.c`, `firmware/src/main.c`, ADC/TIM3 initialization order, OLED behavior, bridge authority, firmware version 0.13.0

## 2026-08-18 — Accept synchronous acquisition and zero-calibrate each phase independently

- **Decision:** Accept the 20 kHz TIM3-triggered `currentB/currentA` DMA acquisition as bench-proven. Firmware 0.14.0 averages 32 foreground snapshots while the bridge remains at the all-low zero vector, stores independent B/A zero offsets, converts both channels using the schematic-derived `6.65 * 20 mOhm` transfer and nominal 3.3 V ADC reference, and displays simultaneous signed milliamperes as compact `A+#####mA` / `B+#####mA` rows. Raw counts remain the underlying acquisition representation.
- **Why:** The target sequence is as stable as the isolated DMA tests and no longer reports `ERRF`. The tested board's B offset differs visibly from A, so a shared nominal midscale would create a false current. At the nominal reference, one ADC count is approximately 6.06 mA; absolute scale accuracy remains limited by the unmeasured ADC reference and analog tolerances.
- **Supersedes:** The pending bench-acceptance state in “Collapse the remaining ADC tests into the target synchronous path” and the raw alternating OLED view.
- **Affects:** `firmware/src/main.c`, `firmware/src/drivers/adc_calibration.c`, `firmware/src/drivers/adc_display.c`, startup current calibration, OLED behavior, Phase 5 current scaling, firmware version 0.14.0

## 2026-08-18 — Promote the board's 8 MHz HSE through PLL to 64 MHz

- **Decision:** Firmware 0.15.0 starts from the verified reset MSI, enables the fitted 8 MHz HSE, selects undivided HSE times eight for a 64 MHz PLL system clock, uses one Flash wait state, HCLK 64 MHz, PCLK2 32 MHz, and PCLK1 16 MHz. APB1 timers receive the documented doubled 32 MHz clock. Drivers receive explicit HCLK, APB, or timer clocks instead of assuming `SystemCoreClock` is universal.
- **Why:** The 4 MHz first-image clock leaves no practical execution budget after the 44 microsecond current ADC sequence in a 50 microsecond PWM period. The N32L40x manual documents the 64 MHz maximum, HSE x8 encoding, Flash latency, APB limits, and timer multiplier; firmware 0.15.0 then passed bench boot and peripheral checks.
- **Supersedes:** “Keep the first image on reset-default 4 MHz MSI.” MSI remains only the reset/startup source and safe pre-PLL state.
- **Affects:** `firmware/src/platform/system.c`, platform clock API, SysTick, ADC, TIM3, I2C1, SPI1, USART1, diagnostics, firmware version 0.15.0

## 2026-08-18 — Route documentation by task instead of requiring full-history reading

- **Decision:** Keep `README.md` as the concise current safety/status snapshot, use `docs/README.md` to route task-specific reading, require only the latest 10 decisions for structural work, and reserve full `DECISIONS.md`, `PLAN.md`, and `docs/BRINGUP.md` reads for audits/conflicts, gate or scope changes, and bench work respectively.
- **Why:** The blanket prerequisite had grown beyond 9,000 words and duplicated live status across several files, already leaving a stale next-gate claim in the README.
- **Supersedes:** The universal pre-change reading rule in `AGENTS.md` and `CONTRIBUTING.md`.
- **Affects:** `AGENTS.md`, `CONTRIBUTING.md`, `README.md`, `docs/README.md`, documentation maintenance and onboarding

## 2026-08-18 — Add a bounded interrupt-owned current-loop commissioning path

- **Decision:** Firmware 0.16.0 links fixed-point A/B PI control into the hardware image, triggers two 7.5-cycle current samples from TIM2 CC2 at 58% of each TIM3 period, stages symmetric four-leg PWM, and grants authority only through released-then-held Enter with independent reference, raw-current, phase-voltage, duty, DMA/PWM, and missed-update bounds converging on the direct-GPIO all-low path.
- **Why:** The bench-proven carrier, channel order, DMA transport, and zero calibration are sufficient to commission a deliberately low-energy loop without placing floating-point transforms or foreground work in the fast path; the delayed low-zero sample and conservative 150 mA nominal rotating reference make the remaining waveform and feedback-sign questions explicit bench gates.
- **Supersedes:** The active carrier-boundary sampling contract in “Capture both current channels from the TIM3 carrier boundary” and firmware 0.15.0's permanent `RUN` suppression; the earlier acquisition remains historical bench evidence.
- **Affects:** `firmware/src/platform/adc1.c`, `tim2_current_trigger.c`, `tim3_bridge_pwm.c`, `current_loop_backend.c`, `firmware/src/control/phase_current_loop.c`, `firmware/src/main.c`, Phase 5 commissioning and safety documentation

## 2026-08-18 — Append current-loop state to diagnostic schema 5

- **Decision:** Preserve the complete 184-byte schema-4 prefix and append 56 bytes of current-loop readiness, authority, fault, sample, reference, feedback, voltage, and four-duty state for a 240-byte schema-5 `g_diagnostics` record.
- **Why:** The first switched-current commissioning path needs debugger-visible evidence that can be correlated with scope captures without adding unsolicited traffic or reading ISR-owned objects inconsistently.
- **Supersedes:** Diagnostic schema 4 as the current producer format; its prefix and native-protocol fields remain unchanged.
- **Affects:** `firmware/include/mks57d/diagnostics.h`, `firmware/src/app/diagnostics.c`, host/post-link ABI checks, `docs/DIAGNOSTICS.md`

## 2026-08-18 — Enable the internal TIM2 CC2 event source

- **Decision:** Firmware 0.16.1 enables TIM2 capture/compare channel 2 in frozen output-compare mode while leaving its GPIO unconfigured. The counter remains reset from TIM3 ITR2 and the CC2 match at 58% remains the ADC regular trigger.
- **Why:** Firmware 0.16.0 selected TIM2 CC2 in the ADC but left `TIM2_CCEN.CC2EN` clear, so the newly introduced delayed trigger produced no accepted DMA snapshots on hardware and the OLED remained at `A-------mA`. The N32L40x output-compare setup requires the selected compare channel to be enabled even when it is used only as an internal event.
- **Supersedes:** The incomplete TIM2 setup in “Add a bounded interrupt-owned current-loop commissioning path”; its trigger phase, controller bounds, and authority contract are unchanged.
- **Affects:** `firmware/src/platform/tim2_current_trigger.c`, synchronous ADC startup, firmware version 0.16.1

## 2026-08-18 — Separate proven ADC startup from switched-current sample timing

- **Decision:** Firmware 0.16.3 retains the bench-proven DMA-before-ADC and ADC-before-PWM initialization order, but does not use TIM3 update as the switched-current conversion trigger. TIM2 resets from TIM3 update and compares at 65% of the carrier; its bounded interrupt software-starts the two-rank ADC sequence with 7.5-cycle apertures. A current-loop shutdown is latched visibly as `F####`, using the one-based position of the first set fault bit.
- **Why:** Restoring the entire firmware 0.13.0 acquisition configuration in 0.16.2 recovered ADC samples after the failed direct TIM2_CC2 trigger, but it also restored sampling exactly at the switching boundary. That timing had been accepted only for bridge-disabled acquisition proof. The compare-ISR path keeps the proven ADC/DMA construction, bypasses the internal trigger route that produced no samples in both 0.16.0 and 0.16.1, and places both sample apertures outside the loop's bounded 45%-55% switching window.
- **Supersedes:** The active direct TIM3-update trigger restored in firmware 0.16.2 and the direct TIM2_CC2-to-ADC route in “Add a bounded interrupt-owned current-loop commissioning path” / “Enable the internal TIM2 CC2 event source.” Historical bench evidence for the TIM3-triggered passive acquisition remains valid.
- **Affects:** `firmware/src/platform/adc1.c`, `firmware/src/platform/tim2_current_trigger.c`, `firmware/src/main.c`, OLED fault reporting, PWM/ADC timing contract, firmware version 0.16.3

## 2026-08-18 — Add a duration-bounded RS-485 current-loop commissioning console

- **Decision:** Firmware 0.17.0 / native protocol 1.1 adds status, configure, start, and stop commissioning commands. The schema-1 status response serializes ADC readiness/raw/zero state, raw and debounced inputs, authority source, backend activity, faults, loop sample count, references, measurements, voltage commands, four duties, configured bounds, and remaining remote-run time. Foreground RS-485 parsing remains active during bridge authority. Remote runs are bounded to 0.1-60 seconds and stop on deadline, raw Menu, transport failure, or explicit STOP; STOP may also end local authority.
- **Why:** The immediate-current-loop dropout is faster than the OLED refresh and the existing RS-485 service deliberately stopped parsing while RUN was active, leaving no way to distinguish an authority failure, inactive backend, absent ADC interrupts, zero references, saturation, or a latched fault from the bench. A polled binary snapshot provides the missing time-correlated internal evidence and duration-bounded authority makes it observable without relying on button timing.
- **Supersedes:** The read-only native-command slice and the bridge-active RX/parser suspension. It is a commissioning-only exception and does not define the production motion protocol or remove the existing current/reference/voltage/duty/fault bounds.
- **Affects:** `firmware/include/mks57d/command_service.h`, `firmware/include/mks57d/native_protocol.h`, `firmware/src/app/command_service.c`, `firmware/src/protocol/native_protocol.c`, `firmware/src/main.c`, `tools/mks57d_rs485.py`, RS-485 authority and observability contracts, firmware version 0.17.0

## 2026-08-18 — Preserve current-loop faults instead of converting them into watchdog resets

- **Decision:** Firmware 0.17.1 treats a failed foreground reference update with an already-latched backend fault as a normal fault stop: authority is revoked, the backend is stopped in `ZERO`, re-arming is inhibited, and fault telemetry remains queryable. Phase overcurrent paths preserve the fault-causing measured A/B counts. Commissioning status schema 2 fills the two remaining native-v1 payload bytes with retained panic code and watchdog-reset indication.
- **Why:** The first RS-485-controlled run proved authority and the backend started with a 25-count reference but no successful sample before the board reset. The backend's immediate safe-state fault was being followed by a foreground invariant panic one millisecond later, causing IWDG reset and erasing the exact volatile fault before the next query. Expected control faults must remain evidence, while genuine fault-free API failures still panic.
- **Supersedes:** The periodic current-reference path's unconditional `PANIC_BRIDGE_CHARACTERIZER_INIT` response and commissioning status schema 1. It does not weaken the ISR-owned immediate all-low shutdown or make a latched loop fault recoverable without reset.
- **Affects:** `firmware/src/main.c`, `firmware/src/platform/current_loop_backend.c`, `firmware/src/control/phase_current_loop.c`, commissioning status serialization and CLI, firmware version 0.17.1

## 2026-08-18 — Expose complete boot/reset evidence through native RS-485

- **Decision:** Firmware 0.17.2 adds `GET_BOOT_STATUS` (`0x0104`) returning a schema byte, the complete captured RCC reset-flag mask, retained panic code, and current uptime. The host console decodes the individual reset causes with `boot`.
- **Why:** Firmware 0.17.1 still reset immediately after bridge authority, but the post-boot commissioning summary showed neither retained panic nor IWDG reset, and the 1 A bench supply never reached constant-current mode. A one-bit watchdog summary cannot distinguish pin, power-on/brownout, software, RAM/MMU, window-watchdog, or low-power reset, and guessing among those would drive the next electrical or firmware change in different directions.
- **Supersedes:** The commissioning status watchdog-reset summary as the only on-wire reset evidence; that field remains for convenient correlation.
- **Affects:** Transport-independent command service, native command `0x0104`, `tools/mks57d_rs485.py`, reset-cause commissioning workflow, firmware version 0.17.2

## 2026-08-18 — Start the current loop from low-zero sign-magnitude PWM

- **Decision:** Firmware 0.17.3 replaces centered four-leg modulation with low-zero sign-magnitude modulation. Current-loop start stages `{0,0,0,0}`. A positive phase-voltage command PWM-drives leg 1 while leg 2 remains low; a negative command drives leg 2 while leg 1 remains low. Zero command remains the established all-low `ZERO` vector. The existing phase-voltage bound limits the active duty, and `duty_margin_permille` reserves only the upper end of its range because zero duty is now intentional.
- **Why:** Firmware 0.17.2 accepted authority and staged `{500,500,500,500}`, then reset through the external reset-pin class before completing one ADC/current-loop sample. Full boot telemetry reported only `PINRSTF`, with no panic, watchdog, software, or power-on reset flag, and the 1 A bench supply remained in constant-voltage mode. The earlier characterizer had already run a selected leg while holding the other three low. Restoring that proven low-zero switching shape removes the simultaneous four-leg 50% transition and sharply reduces the common-mode edge activity most closely correlated with the reset; hardware confirmation remains required.
- **Supersedes:** The symmetric four-leg modulation and 50% neutral startup in “Add a bounded interrupt-owned current-loop commissioning path.” All reference, raw-current, voltage, deadline, transport, and all-low fault bounds remain unchanged.
- **Affects:** `firmware/src/control/phase_current_loop.c`, `firmware/src/platform/current_loop_backend.c`, phase-loop duty semantics and tests, ADC quiet-window assumptions, commissioning waveform expectations, firmware version 0.17.3

## 2026-08-18 — Separate the NRST fault from the modulation change

- **Decision:** Retain firmware 0.17.3's low-zero sign-magnitude modulation because it matches the proven single-leg bridge behavior and gives the 65% ADC trigger a larger quiet window, but do not treat it as the NRST-reset fix. Remove or otherwise eliminate the temporary NRST lead as the next isolated bench variable before making another firmware change.
- **Why:** With the debugger fully disconnected, firmware 0.17.3 repeated the exact failure: START was accepted with authority/backend active and zero completed samples, the first 200 ms query timed out, and the reboot record contained only `PINRSTF` with no panic, watchdog, software, or power-on reset. The reset therefore persists without the probe and without the simultaneous four-leg 50% waveform.
- **Supersedes:** Only the causal hypothesis in “Start the current loop from low-zero sign-magnitude PWM.” Its modulation contract remains active.
- **Affects:** Current-loop fault diagnosis, NRST bench wiring, the next commissioning test; no firmware behavior change

## 2026-08-18 — Give the delayed current sample a complete control-compute window

- **Decision:** Firmware 0.17.5 moves the TIM2 current trigger from 65% to 30% of the 20 kHz carrier and compiles the ADC/DMA/current-loop/PWM interrupt path at `-O2` in Debug builds. The one-missed-update deadline limit is unchanged.
- **Why:** Firmware 0.17.4 preserved `F0019` with zero completed loop outputs. Startup calibration proves the same TIM2-software-triggered ADC/DMA path is running before authority; the failure begins when the carrier deadline guardian is enabled. Low-zero modulation finishes every permitted PWM edge by 10%, so sampling at 30% retains a quiet interval while returning most of the period for conversion and fixed-point control instead of beginning at 65%.
- **Supersedes:** The 65% trigger timing in “Separate proven ADC startup from switched-current sample timing”; its DMA-before-ADC order, delayed software trigger, low-zero modulation, and fault-preservation contracts remain active.
- **Affects:** `firmware/include/mks57d/tim2_current_trigger.h`, `firmware/CMakeLists.txt`, current-loop real-time timing, firmware version 0.17.5

## 2026-08-18 — Align phase-voltage signs with the asymmetric shunt placement

- **Decision:** Firmware 0.17.7 defines positive A current as A- to A+ and positive B current as B+ to B-. Low-zero modulation therefore maps positive A voltage to `phaseA2`, negative A voltage to `phaseA1`, positive B voltage to `phaseB1`, and negative B voltage to `phaseB2`. The selected-leg test phases are updated so `A1` and `A2` still name the leg driven first.
- **Why:** The schematic and physical tracing place the low-side shunts under the A+ and B- half-bridges. On firmware 0.17.6, an A1-positive command completed 75 loop samples but produced -101 A counts and tripped overcurrent, directly demonstrating that the prior uniform leg-1-positive mapping made the A controller positive feedback. The measured-current signs remain unchanged because they already match the physical winding conventions.
- **Supersedes:** The uniform positive-command-to-leg-1 mapping in “Start the current loop from low-zero sign-magnitude PWM.” Low-zero modulation and all existing current, voltage, duty, deadline, and fault bounds remain unchanged.
- **Affects:** `firmware/src/control/phase_current_loop.c`, `firmware/src/main.c`, selected-leg semantics, phase-current convention, firmware version 0.17.7

## 2026-08-19 — Expose encoder motion during current-loop commissioning

- **Decision:** Firmware 0.17.8 / native protocol 1.2 keeps the existing 100 Hz foreground encoder acquisition active during bridge authority and adds `GET_ENCODER_STATUS` (`0x0105`) with the latest 14-bit angle, encoder/SPI health, sensor flags, accepted and rejected sample counts, and last-attempt time. The host `encoder` command reads it directly, while `watch` and `run` attach an encoder snapshot to every current-loop record.
- **Why:** Firmware 0.17.7 proved approximately 100,000 fault-free current-loop updates and a rotating current vector, but the RS-485 record could not establish that the rotor followed because encoder data existed only in debugger RAM. Commissioning telemetry must distinguish electrical commutation from mechanical motion without requiring a person at the shaft.
- **Supersedes:** The commissioning console's current-only observability boundary in “Add a duration-bounded RS-485 current-loop commissioning console.” Existing command encodings and the full schema-2 current status remain unchanged.
- **Affects:** foreground encoder scheduling, command service, native command `0x0105`, protocol minor version 1.2, `tools/mks57d_rs485.py`, firmware version 0.17.8

## 2026-08-19 — Promote current-regulated motor operation to a development foundation

- **Decision:** Treat firmware 0.17.8's bounded motor-connected current loop as the tested board's normal development foundation. Motor-disconnected waveform staging remains appropriate for a new board revision or changed bridge/timing backend, but it is no longer a prerequisite for every firmware iteration. The next functional milestone is encoder/electrical alignment followed by velocity and position control.
- **Why:** At 12 V and nominal 300 mA, the 20 kHz loop completed 59,905 fault-free samples while the encoder moved monotonically at -5.97 RPM versus 6.00 RPM expected. Current sign, four-leg modulation, ADC timing, RS-485 observability, and rotor following are now hardware evidence rather than feasibility assumptions.
- **Supersedes:** The project-wide no-motor and uncommissioned-current-loop posture retained in early bring-up documentation. Independent current, voltage, duty, duration, deadline, encoder-health, and all-low fault bounds remain active.
- **Affects:** `AGENTS.md`, `README.md`, `PLAN.md`, `docs/BRINGUP.md`, architecture and subsystem status documentation, future motor-control development scope

## 2026-08-19 — Add a bounded 20 kHz startup trace for current-loop tuning

- **Decision:** Firmware 0.18.0 / native protocol 1.3 records the first 256 successful current-loop outputs after every backend start and exposes them by index through `GET_CURRENT_TRACE` (`0x0106`) only after authority ends. Each entry contains the loop sample number, A/B references, A/B measurements, and A/B voltage commands. The fixed buffer consumes 4096 bytes of SRAM1 and stops writing after 12.8 ms; it does not stream from interrupt context or change any bridge limit.
- **Why:** The existing polled RS-485 status averages about 68 snapshots per second, or roughly one observation per 294 controller updates. It proves low-frequency tracking and rotor following but cannot measure a 20 kHz loop's rise time, overshoot, settling, or fast-loop saturation. A bounded startup record captures the necessary transient without allowing telemetry traffic to own fast-loop timing.
- **Supersedes:** The commissioning console's status-snapshot-only tuning visibility. Existing status, encoder, STOP, current, voltage, duty, duration, deadline, fault, and all-low `ZERO` contracts are unchanged.
- **Affects:** `firmware/src/platform/current_loop_backend.c`, command service, native command `0x0106`, protocol minor version 1.3, `tools/mks57d_rs485.py`, current-loop tuning workflow, firmware version 0.18.0
- **Validation:** Host protocol tests and Debug/Release Arm builds pass. Firmware 0.18.0 returned complete 256-sample traces for bounded A1/A2/B1/B2 runs at 20 kHz without a deadline, current-loop, ADC, reset, or protocol fault; SRAM1 use remains 5472 of 16384 bytes.

## 2026-08-19 — Accept Kp=2 for the 20 kHz phase-current loops

- **Decision:** Firmware 0.18.1 doubles each phase controller's proportional gain from `Kp=1` to `Kp=2` voltage-per-current-count while retaining `Ki=1/64` per 20 kHz step, the 100-permille phase-voltage ceiling, the 100-count raw-current trip, and every existing authority, deadline, and all-low fault contract. Treat 5-10 Hz electrical operation at 303 mA as the well-controlled commissioning band, 15 Hz as a characterized degraded edge, and 20 Hz as voltage-headroom limited at the present 12 V bus and 10% phase-voltage bound.
- **Why:** Free-rotor single-phase startup traces were mechanically contaminated and did not consistently favor either gain. The representative rotating-vector test did: at approximately 5 Hz, Kp=2 reduced combined vector RMS error from 19.70 mA to 19.28 mA and average gain error from +2.61% to +1.70%, with phase lag increasing only from 4.11 to 4.54 degrees and peak voltage falling from 50 to 48 permille. A fault-free Kp=2 sweep remained unsaturated at 10 and 15 Hz; at 20 Hz it reached the unchanged voltage ceiling briefly, so additional PI gain cannot recover that operating point without changing voltage headroom.
- **Supersedes:** The provisional `Kp=1` tuning in the firmware 0.18.0 comparison image. It does not expand the current, voltage, duration, duty, thermal, or motion envelope.
- **Affects:** `firmware/src/main.c`, firmware version 0.18.1, current-loop commissioning envelope and tuning documentation
- **Validation:** Firmware 0.18.1 completed all four 151.5 mA polarity-balanced startup traces without fault. At 303 mA, the 5/10/15/20 Hz rotating-vector runs reported respectively 19.28/47.81/83.78/124.47 mA vector RMS error, 4.54/11.81/21.06/33.72 degrees phase lag, and 48/69/85/100 permille peak voltage command. The 20 Hz run alone touched saturation, for 0.785% of observations. Every run remained free of current-loop, ADC, encoder, reset, and protocol faults.

## 2026-08-19 — Stage a one-amp current and 20%-bus voltage envelope

- **Decision:** Firmware 0.18.2 raises the configurable phase-current ceiling from 50 to 165 ADC counts (303 mA to 999.8 mA), the independent raw-current trip from 100 to 200 counts (606 mA to 1.212 A), and the phase-voltage ceiling from 100 to 200 permille (10% to 20% of the bus). The 20 Hz frequency, 60 second duration, duty-margin, Kp=2, Ki=1/64, deadline, authority, and all-low fault contracts remain unchanged. Initial bench runs must climb through bounded intermediate currents before commanding the new ceiling.
- **Why:** The 0.18.1 sweep reached the old 10%-bus voltage ceiling at 20 Hz, and the user confirmed 1 A is below the attached motor's current limit. Raising only the reference would command current the existing voltage authority may be unable to establish. The 20% voltage ceiling still completes every permitted low-zero PWM edge by 20% of the carrier, preserving a 10%-of-period (5 microsecond) quiet interval before the established 30% current-sampling trigger.
- **Supersedes:** The 303 mA commissioning-reference, 606 mA raw-trip, and 10%-bus phase-voltage ceilings in the firmware 0.18.1 operating envelope. The expanded values are a bench candidate until staged motor, current, supply, encoder, fault, and thermal observations accept them.
- **Affects:** `firmware/src/main.c`, firmware version 0.18.2, current/voltage commissioning safety envelope
- **Validation:** Host and target build validation pending; hardware validation pending flash and staged runs.

## 2026-08-19 — Target a high-performance motor-drive envelope

- **Decision:** Treat high-performance motor control as the steady-state product goal. Safety comes from independently enforced current, voltage, duty, duration, timing, motion, thermal, and fault bounds whose values are expanded from measurements, not from permanently low commissioning ceilings. Firmware 0.18.2 therefore keeps the one-amp reference and 1.21 A raw trip, raises the phase-voltage ceiling to 700 permille, moves the zero-vector current sample to 80% of the carrier, raises the ADC conversion clock from 2 to 16 MHz, and opens commissioning frequency to 50 electrical Hz. The 20 kHz PWM rate, 200-permille bootstrap/duty reserve, Kp=2, Ki=1/64, deadline guardian, and all-low fault path remain unchanged.
- **Why:** A 10-20% bus-voltage ceiling materially prevents useful stepper torque and speed. The asymmetric low-side shunts require sampling after the active PWM edge for both current signs, so high bus utilization must be enabled by faster conversion and later zero-vector sampling rather than by allowing PWM edges to cross the old 30% ADC aperture. The N32L40x manual permits ADC clocks up to 64 MHz and quotes 4.57 MSPS at 12 bits; 16 MHz with the existing 7.5-cycle apertures completes the two-rank sequence in approximately 2.7 microseconds. At 70% maximum duty and an 80% trigger, this preserves 5 microseconds of post-edge settling and approximately 7 microseconds for conversion-complete control and preload staging.
- **Supersedes:** “Stage a one-amp current and 20%-bus voltage envelope” and the assumption that deliberately tiny commissioning ceilings are the project's long-term operating model. Expansion remains staged and evidence-driven; this does not authorize bypassing any safety authority or fault contract.
- **Affects:** project performance target, `firmware/include/mks57d/adc1.h`, `firmware/include/mks57d/tim2_current_trigger.h`, `firmware/src/platform/adc1.c`, `firmware/src/main.c`, firmware version 0.18.2, current/voltage/speed characterization strategy
- **Validation:** Host and target builds pending. Because ADC/PWM timing changed, the next hardware gate is unloaded timing/acquisition verification before staged motor runs.

### Validation follow-up

- Host unit tests pass. Debug and Release Arm builds pass; Debug uses 25,992 bytes of Flash and 5,472 bytes of SRAM1, while Release uses 23,824 bytes of Flash and the same SRAM1. Firmware 0.18.2 booted with every readiness flag set and no fault or retained panic. The changed 16 MHz ADC / 80%-carrier trigger completed a 303 mA step with 6.53 ms rise time, 8% overshoot, and 14.0 mA tail RMS error. Staged 454 mA / 10 Hz and 606 mA / 15 Hz runs tracked -12 and -18 RPM without fault. A 757 mA / 20 Hz run then completed 100,000 loop updates and 1.97 mechanical revolutions in five seconds versus 2.00 commanded, with no current-loop, ADC, encoder, reset, or protocol fault. Maximum observed phase-voltage effort was 252 permille at startup, well below the 700-permille ceiling. This accepts the timing backend and operation through 757 mA / 20 electrical Hz on the tested motor; the 1 A / 50 Hz endpoints remain available but not yet bench-qualified.

## 2026-08-19 — Make bounded move/capture/report the default bench loop

- **Decision:** Add `tools/motor_test.py` as the human-facing one-command wrapper for recurring motor development. A run preflights readiness and firmware-reported current bounds, configures and starts only through the native bounded authority path, streams current and encoder telemetry, reads the bounded 20 kHz startup trace after authority ends, generates a self-contained four-plot HTML report, and restores the preceding inactive test configuration by default. The lower-level protocol console remains available for diagnosis and individual operations.
- **Why:** The repeated configure/run/trace/analyze sequence is useful enough to become a primary development loop, but making it convenient must preserve the firmware's independent current, voltage, duty, duration, deadline, and fault contracts. Saving raw data beside the report also makes tuning comparisons repeatable instead of dependent on terminal output.
- **Supersedes:** The manual multi-command console sequence as the default commissioning workflow. It does not replace the console, expand the accepted hardware envelope, or turn the present rotating-current reference into closed-loop speed, signed-direction, or position control.
- **Affects:** `tools/motor_test.py`, `tools/README.md`, `README.md`, `docs/BRINGUP.md`, `PLAN.md`, recurring current-loop and motion-development workflow
- **Validation:** A 303 mA / 5 Hz / 2 second hardware run completed without faults, measured 5.90 RPM versus 6.00 RPM expected, reported 20.1 mA combined RMS current error and 4.8% peak bus-voltage use, produced all four plots, and restored the preceding 151.5 mA / 0.5 Hz inactive configuration. Offline report regeneration also passed.

## 2026-08-20 — Converge new motor features through the product architecture

- **Decision:** From this point, new motor-control features must land in one converged product path with a single drive supervisor, motion/current authority chain, and fault model. Commissioning tools may exercise that path, but must not introduce new bring-up-only control paths or parallel application state machines.
- **Why:** The bench-proven PWM/ADC/current backend and the host-tested servo/application modules are now mature enough that their integration boundary is the principal project risk. Additional standalone bring-up implementations would increase authority ambiguity and delay a coherent high-performance servo system.
- **Supersedes:** The implicit bring-up-stage practice of adding isolated control paths after “Promote current-regulated motor operation to a development foundation.” Existing bounded current-test and capture tooling remains supported as diagnostic clients of the converged architecture.
- **Affects:** `firmware/src/main.c`, `firmware/src/app/`, `firmware/src/control/`, drive-state and fault ownership, native motion commands, Phase 6 integration, and future bench tooling

## 2026-08-20 — Graduate useful commissioning tools or retire them

- **Decision:** Do not retain permanent commissioning-only tools, protocol endpoints, build personalities, displays, or control paths. During convergence, evaluate each existing commissioning surface: anything still useful must become a production tool or a production diagnostic capability with explicit authorization, ownership, safety, compatibility, and maintenance contracts; anything without a durable product role must be trimmed.
- **Why:** Carrying a parallel commissioning stack would preserve duplicate authority paths, obsolete behavior, and ongoing test/documentation burden. The production system should provide the diagnostics, capture, analysis, and bounded test capabilities worth keeping without weakening its normal control architecture.
- **Supersedes:** The diagnostic-client retention clause in “Converge new motor features through the product architecture.” Existing tooling is not automatically retained merely because it helped bring-up.
- **Affects:** `tools/motor_test.py`, `tools/mks57d_rs485.py`, current-test and trace protocol operations, `bridge_characterizer` and commissioning-image concepts, OLED diagnostic modes, build targets, documentation, and Phase 6 convergence scope

## 2026-08-20 — Make the product drive supervisor authoritative

- **Decision:** Firmware 0.19.0 makes `app_supervisor_t` the sole application-level owner of drive readiness, state, and bridge authority. The current hardware image must progress through `RESET_SAFE` and `DIAGNOSTIC` to `READY` only after current-control initialization and a healthy encoder sample. Bounded rotating-current operation is retained as diagnostic authority requested through the supervisor; future alignment and servo operation use distinct motion authority. Readiness loss in `READY` returns to `DIAGNOSTIC`, readiness loss while energized enters `FAULT`, and every fault clears authority. The sole `mks57d` build is now the product image; the commissioning-image definition and historical `bridge-characterization` and `current-loop-commissioning` build aliases are removed.
- **Why:** This is the first executable convergence step between the bench-proven current backend and the product motion architecture. It prevents the retained diagnostic UI and protocol compatibility endpoints from acting as a second bridge owner, makes encoder health part of the product readiness contract, and eliminates a build personality with no durable product role.
- **Supersedes:** The candidate-only application state machine and the current-loop service's implicit ownership of bridge authority. It implements “Converge new motor features through the product architecture” and the build-personality portion of “Graduate useful commissioning tools or retire them.” Protocol-1.3 `COMMISSIONING`/`CURRENT_TEST` wire names remain compatibility labels, not architectural ownership.
- **Affects:** firmware version 0.19.0, `firmware/src/main.c`, `firmware/src/app/app_state.c`, `firmware/include/mks57d/app_state.h`, `firmware/CMakeLists.txt`, watchdog liveness in expected `FAULT`, encoder readiness, current diagnostic START/STOP behavior, host tools, Phase 6 integration
- **Validation:** Native supervisor tests pass, including diagnostic/motion authority, energization gating, state/authority invariant rejection, readiness loss, and explicit fault recovery. Clean Debug and Release Arm builds pass: Debug uses 27,192 bytes of Flash and 5,472 bytes of SRAM1; Release uses 24,804 bytes of Flash and the same SRAM1. Hardware regression of READY entry, local/remote diagnostic operation, encoder-loss shutdown, and reported state remains pending.

### Validation follow-up

- The manually flashed 0.19.0 product image booted healthy on COM14 and reached the supervisor-ready condition with current control and encoder acquisition valid. A 303 mA, 5 electrical Hz, two-second remote diagnostic completed at 5.82 RPM versus 6.00 expected with 18.4 mA combined RMS current error and no faults; deadline expiry released authority. A separate 151.5 mA run showed active remote diagnostic authority and then cleared authority, backend-active state, and the remote lease immediately on explicit STOP. Both paths returned the bridge to `ZERO`, restored the guarded 25-count/0.5-Hz configuration, and left retained panic and watchdog/reset health clean. Encoder-loss fault injection and the local-button authority path remain pending.

## 2026-08-20 — Replace inherited ceilings with a traceable limit model

- **Decision:** Preserve independent current, voltage, duty, velocity, acceleration, following-error, duration, timing, and thermal bounds, but do not preserve any numeric value merely because it already exists. Every production limit must be classified as an immutable hardware/silicon constraint, a measured and explicitly qualified operating envelope, a motor/application configuration value, or a bounded diagnostic policy. Each value must have units, a documented basis, a single enforcement owner, observable reporting, and a test. Host-test fixture values are not product defaults. Unjustified legacy values are removed, made configurable, or replaced from hardware data and measurements.
- **Why:** The board and attached motor have materially more current and performance capability than the early commissioning envelope, while several motion-shell constants were selected only to exercise host tests. Treating those numbers as product truths would silently turn bring-up history into permanent performance restrictions and make later tuning unsafe and confusing. A high-performance drive needs real limits, but their values and ownership must be intentional.
- **Supersedes:** The implicit practice of carrying commissioning ceilings and host-test defaults forward until individually challenged. It reinforces, rather than removes, the safety invariant that current, duty, velocity, acceleration, and following error remain independently bounded.
- **Affects:** Phase 6 servo integration, current-envelope expansion, configuration schema, protocol telemetry, fault thresholds, test fixtures versus product defaults, `firmware/src/main.c`, `firmware/src/app/`, `firmware/src/control/`, and operating-limit documentation
- **Validation:** A limit inventory and replacement audit are pending. The current 0.19.0 request ceiling remains a firmware operating contract, not a claim about the board's physical capability; no higher-current authority is granted by this decision alone.

## 2026-08-20 — Adopt measured stepper geometry and timestamped product feedback

- **Decision:** Firmware 0.20.0 schedules the existing shared angle tracker from the on-board MT6816 at a 1 kHz foreground candidate rate using a wrap-safe microsecond timebase. Product readiness now requires the estimator to have accepted a valid sample. Native protocol 1.4 / encoder schema 2 preserves the raw schema-1 prefix and appends Q16.16 unwrapped mechanical position and velocity, estimator timestamp/faults, alignment validity/zero/direction, Q0.32 electrical phase, and latest/maximum sample intervals. The two-phase motor geometry is 16,384 encoder counts and 50 electrical cycles per mechanical revolution; positive electrical phase decreases raw encoder count. `motor_alignment_t` accepts phase-zero/quarter-phase observations transactionally and calculates electrical phase only after valid calibration. Firmware boots with alignment invalid and gains no motion authority from this change.
- **Why:** The hardware image needs to adopt the already host-tested estimator before velocity control, and alignment needs a measured motor/wiring convention rather than an assumed offset. Scheduling and interval telemetry make the initial 1 kHz choice falsifiable on hardware. Keeping alignment invalid until a supervisor-owned procedure succeeds prevents a bench-specific raw angle or host-test default from becoming product calibration.
- **Supersedes:** The 100 Hz raw-angle-only product reader and the statement that the estimator is excluded from the hardware image. It does not connect the outer servo, choose product motion limits, or create a new bridge authority path.
- **Affects:** firmware version 0.20.0, native protocol 1.4, encoder schema 2, `firmware/src/main.c`, `firmware/src/platform/timebase.c`, `firmware/src/control/angle_tracker.c`, `firmware/src/control/motor_alignment.c`, encoder readiness, host service telemetry, Phase 6 alignment and estimator integration
- **Validation:** At 757.5 mA, the production diagnostic applied `A2 → B1 → A1 → B2 → A2` and observed raw counts `14249 → 14165 → 14085 → 14004 → 13923`: quarter steps of -84, -80, -81, and -81 counts, totaling -326 versus -327.68 theoretical. The earlier 303 mA sequence independently gave -87, -78, and -83 count quarter steps. Both tests ended through explicit STOP with authority clear, the guarded 25-count/0.5-Hz configuration restored, no faults, and no retained panic. Native alignment/protocol tests pass. Clean Arm builds pass post-link checks: Debug uses 28,936 bytes Flash and 5,476 bytes SRAM1; Release uses 26,360 bytes Flash and 5,476 bytes SRAM1. Hardware validation of the 0.20.0 1 kHz schedule, stationary/active velocity noise, maximum interval, and estimator fault path remains pending flash.

### Validation follow-up

- The manually flashed 0.20.0 image reported the expected product identity and native protocol 1.4, reached `READY`, decoded encoder schema 2, and kept alignment invalid as designed. One hundred stationary queries over five seconds showed raw angle fixed at 13926, zero velocity, estimator-ready throughout, no fault, and a 5.162 ms cumulative worst-case interval. A 757.4 mA / 20 Hz / five-second production diagnostic then measured -23.51 RPM versus -24.00 expected. Its 74 active estimator observations had latest intervals of 981-1001 us (999.8 us mean), a 5.450 ms cumulative worst case, -0.3953 revolution/s mean settled velocity versus -0.4000 commanded, and no estimator, encoder, current-loop, supervisor, reset, or panic fault. Deadline release restored the prior 25-count/0.5-Hz inactive configuration. This accepts the foreground schedule under the present workload; readiness-loss injection and revalidation with outer-loop compute remain pending.

## 2026-08-20 — Make alignment a bounded production operation

- **Decision:** Firmware 0.21.0 / native protocol 1.5 implements automatic motor alignment as a production motion-authority client of the existing drive supervisor and current backend. `START_ALIGNMENT` applies phase zero, positive quarter phase, and phase zero again; the portable controller accepts only settled, current-tracking encoder windows, verifies measured geometry and return closure, and transactionally commits zero/direction only after the complete sequence passes. `GET_ALIGNMENT_STATUS` reports progress, terminal evidence, and the complete current/timing/observation policy used by the firmware. `STOP_DRIVE` is the canonical authority-independent stop, while `STOP_CURRENT_TEST` retains the same behavior as a wire-compatibility alias. Failure or abort preserves an earlier valid calibration, and every backend, encoder, readiness, communications, Menu, deadline, and loop-fault path converges on the existing supervisor release/fault and all-low `ZERO` contracts.
- **Why:** Electrical phase must be established repeatably on each motor before torque, velocity, or position control can be fit for purpose. Making alignment a bounded service through the production authority chain avoids a bring-up-only bridge path, makes its observations auditable, and promotes useful commissioning behavior into a maintained product tool.
- **Supersedes:** The pending controlled-alignment step in “Adopt measured stepper geometry and timestamped product feedback” and current-test-specific STOP as the preferred host vocabulary. It does not persist calibration across power cycles or authorize an outer servo.
- **Limits:** The initial 50-count alignment floor is a diagnostic-policy candidate supported by the repeatable 303 mA cardinal test; 165 counts is the existing current-backend request contract, not the board's physical ceiling. The 750 ms settle, 100 ms/64-sample observation, 4 s deadline, 8-count sample span, 12-count closure, and 8-count current-error thresholds are initial bench-tuning candidates under the traceable-limit decision.
- **Affects:** firmware version 0.21.0, native protocol 1.5, capabilities bit 13, `alignment_controller`, `motor_alignment`, `firmware/src/main.c`, command service, native adapter, `tools/mks57d_rs485.py`, Phase 6, and the alignment bench workflow
- **Validation:** The portable controller tests pass success, failure-with-prior-calibration, abort, timestamp-wrap, current-tracking, backend-inactive, encoder-invalid, instability, geometry, and closure paths. Byte-exact native tests pass START, 58-byte status including the reported policy, and generic STOP. Clean Arm post-link checks pass: Debug uses 32,576 bytes Flash and 5,476 bytes SRAM1; Release uses 29,240 bytes Flash and 5,476 bytes SRAM1. The completeness scanner reports no critical/high findings in project-owned control or host-tool directories; its broader firmware hits are intentional callback-context casts and imported vendor wait loops, not incomplete functions. The first hardware alignment, explicit STOP, repeated closure, and invalid-encoder/readiness-loss regressions remain pending flash.

### Validation follow-up

- The manually flashed 0.21.0 image reported protocol 1.5, supervisor `READY`, zero current-loop/encoder faults, and the intended machine-readable alignment policy. Two 757.4 mA sequences independently produced the identical `phase zero → quarter → return` observations `9302 → 9222 → 9302`: -80 counts versus 82 expected, direction -1, -2-count geometry error, and zero-count closure. Both completed in 2.55 seconds, committed valid electrical zero/phase, cleared backend and motion authority, and left reset/panic health clean. A third attempt was stopped through `STOP_DRIVE` during its first settle: state changed from active at 104 ms to `aborted` at 113 ms, authority/backend cleared, and the prior zero 9302/direction -1 remained valid. This accepts normal/repeated alignment and generic STOP on the tested motor; Menu and induced encoder-readiness-loss injection remain pending physical tests.

## 2026-08-20 — Persist motor alignment through the production configuration service

- **Decision:** Firmware 0.22.0 / native protocol 1.6 reserves N32L406 Flash pages 62 and 63 as alternating 2 KiB configuration slots. Records are versioned and length-delimited, carry generation and CRC-32, validate motor geometry and calibration semantics, and program a commit word last. Successful alignment saves automatically only after the current backend stops and motion authority is released; boot may restore alignment but never authority, pending operations, leases, faults, or startup ADC zeros. `GET_CONFIGURATION_STATUS`, idempotent `SAVE_CONFIGURATION`, and transactional `CLEAR_CALIBRATION` are the production service surface, and every write is rejected outside an inactive `READY`/`DIAGNOSTIC` safe state.
- **Why:** Electrical alignment is motor-specific product configuration needed by every future torque, velocity, and position controller. Re-running it on each boot is unnecessary motion, while a single in-place Flash record would allow power loss to destroy the only accepted calibration. Alternating whole erase pages fit the N32L406's verified 2 KiB erase and 32-bit program contract and preserve the previous record until the replacement verifies.
- **Supersedes:** The explicit non-persistence limitation in “Make alignment a bounded production operation.” It does not claim programmer-preserved configuration across reflashing or authorize aligned torque before that interface and its independent limits exist.
- **Affects:** firmware version 0.22.0, native protocol 1.6, capability bit 14, `configuration_store`, `configuration_flash`, linker Flash map, `motor_alignment` restore, automatic alignment completion, `tools/mks57d_rs485.py`, Phase 6, build verification, and the power-cycle bench gate
- **Validation:** Native tests pass first save/reload, unchanged-write suppression, interrupted commit fallback, newest-slot CRC fallback, persistent clear, motor-alignment restore, and byte-exact configuration protocol operations. Debug Arm and host builds pass; the linker reports 36,188 bytes used of the 124 KiB application region and zero allocation in either reserved slot. First-save, reset/power-cycle restore, persistent clear, and no-restored-authority hardware acceptance remain pending flash.

### Validation follow-up

- The Release Arm build also passes post-link verification, using 32,148 bytes of the 124 KiB application region and 5,476 bytes of SRAM1, with zero bytes allocated in either configuration slot or SRAM2.
- The manually flashed 0.22.0 / protocol-1.6 image passed the complete runtime persistence gate on COM14. A 757.4 mA alignment saved `9302 / direction -1 / quarter step 80` in slot 0 at generation 1; an unchanged explicit save did not advance generation. A full power cycle restored the identical stored/active calibration with authority, backend, references, duties, faults, and retained panic clear. Transactional clear selected slot 1 at generation 2 and remained invalid after a second power cycle without restoring authority. Re-alignment then accepted `9301 / direction -1 / quarter step 79`, zero closure error, and saved it in slot 0 at generation 3. This accepts first save, wear avoidance, power-cycle restore, persistent clear, slot alternation, and no-restored-authority behavior on the tested board.

## 2026-08-21 — Make aligned q-current the first production motion interface

- **Decision:** Firmware 0.23.0 / native protocol 1.7 accepts bounded signed q-current as the first production `RUN`/motion-authority command. The controller maps q-current at calibrated electrical phase plus 90 degrees into A/B phase-current references and feeds only the proven project-owned 20 kHz current backend; it does not command bridge voltage or PWM directly. The backend starts at zero reference, demand is slew-limited from accepted 1 kHz feedback, deadline and generic STOP release authority, and invalid phase/timing, velocity, acceleration, backend, reference, current-loop, or readiness state converges on the existing fault and all-low `ZERO` path.
- **Why:** Alignment becomes useful only when torque-producing current follows rotor electrical phase, and velocity/position loops need one real torque actuator beneath them. Reusing the qualified A/B current PI preserves the measured switching, ADC, modulation, and shutdown contracts while avoiding another bring-up-only controller or an unqualified d/q voltage-to-PWM path.
- **Limits:** The initial absolute q-current ceiling is 125 counts (757.4 mA nominal), the highest bench-proven current point. The 1,000 counts/s slew reaches the repeatable 303 mA point in 50 ms, deliberately slower than the measured 6.5 ms current-loop rise. The initial 1 rev/s velocity, 20 rev/s² acceleration, 2,000 us feedback interval, and 100-1,000 ms duration are explicit, independently reported motion-policy candidates for the hardware gate—not claims about motor or board capability—and must be retained, expanded, or replaced from measured evidence.
- **Supersedes:** The pending Phase-6 task to connect the bounded torque/current layer and the statement that persisted alignment does not authorize an aligned interface. It does not close velocity or position control, expand the qualified current envelope beyond 757.4 mA, or make the transitional rotating-current diagnostic a motion command.
- **Affects:** firmware version 0.23.0, native protocol 1.7, capability bit 15, `aligned_torque_controller`, shared phase-current reference generation, product supervisor integration, generic STOP and configuration-write gates, `tools/mks57d_rs485.py`, Phase 6, protocol and bench documentation
- **Validation:** Portable signed-phase, slew, deadline, STOP, phase/timing, velocity, acceleration, backend, and reference-failure tests plus byte-exact native START/status tests pass. Host, Debug Arm, and Release Arm builds pass post-link verification. Debug uses 39,836 bytes of the 124 KiB application region, Release uses 35,024 bytes, and both use 5,476 bytes of SRAM1 with zero allocation in the configuration slots or SRAM2. Completeness and COM14 hardware gates remain pending.
