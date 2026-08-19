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
