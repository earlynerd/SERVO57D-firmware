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
