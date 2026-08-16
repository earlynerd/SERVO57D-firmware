# Building

The repository has two independent builds:

- The N32L406CBL7 firmware, cross-compiled with Arm GNU GCC.
- Native unit tests for hardware-independent state and safety logic.

They use separate CMake configurations because a single build directory cannot mix the Arm and host compilers.

## Tested Windows toolchain

- Arm GNU Toolchain 15.2.Rel1 (`arm-none-eabi-gcc`)
- CMake 4.2.3; the project requires CMake 3.24 or newer
- Ninja 1.13
- PowerShell 7
- Visual Studio 2022 C/C++ Build Tools for native tests

The build does not depend on PlatformIO, an IDE project file, or a globally installed Nations SDK.

## One-command local build

From the repository root:

```powershell
pwsh -File tools/build.ps1 -Target all
```

The script locates the Visual Studio C compiler for host tests and builds both configurations. Use `-Target firmware` or `-Target host-tests` to run only one side.

## Firmware build

```powershell
cmake --preset firmware-debug
cmake --build --preset firmware-debug
```

For the size-optimized configuration:

```powershell
cmake --preset firmware-release
cmake --build --preset firmware-release
```

Artifacts are written under `build/firmware-debug/firmware/` or `build/firmware-release/firmware/`:

- `mks57d.elf` for debugging and symbol inspection
- `mks57d.hex` for tools that accept Intel HEX
- `mks57d.bin` for raw flash programming
- `mks57d.map` for memory and symbol analysis

The linker follows the N32L406xB memory map in User Manual V2.6: 128 KiB flash, 16 KiB SRAM1 at `0x20000000`, an 8 KiB address gap, and 8 KiB SRAM2 at `0x20006000`. The initial image confines all allocated sections to SRAM1 and reserves its final 2 KiB for the stack. SRAM2 is initialized for parity but a link-time assertion rejects allocations there until hardware validation.

Every firmware build also runs a post-link check that verifies the initial vector stack pointer and the SRAM bank boundary symbols. This catches a regression to the incompatible contiguous layouts present in the vendor SDK and CMSIS pack metadata.

## Host tests

On Windows, use the wrapper so the MSVC environment is loaded before CMake:

```powershell
pwsh -File tools/build.ps1 -Target host-tests
```

On a system where a native C compiler and Ninja are already on `PATH`:

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

## Current image behavior

This is a passive diagnostic image, not motor-driving firmware. It:

1. Verifies and retains the reset-default 4 MHz MSI clock with bounded readiness/source checks.
2. Initializes and verifies four NVIC preemption bits with no subpriorities.
3. Initializes the full SRAM2 bank with stores, clears its parity-error status, and does not allocate from it.
4. Leaves PA6, PA7, PB0, and PB1 in their reset configuration.
5. Configures only the provisional PB9 status LED output.
6. Starts a 1 kHz SysTick timebase at the lowest programmable priority, 15.
7. Enters `APP_STATE_DIAGNOSTIC` and toggles the LED every 250 ms.
8. Snapshots and clears sticky reset flags for debugger-visible reset-cause diagnostics.
9. Runs and publishes a seven-gate boot self-test, including read-only confirmation that GPIOA remains clock-gated and PB0/PB1 remain reset-mode inputs.
10. Publishes firmware `0.1.0`, boot state, reset cause, retained panic, uptime, heartbeat, watchdog health, priority policy, and self-test masks through the 64-byte `g_diagnostics` RAM record.
11. Starts a nominal one-second IWDG and services it only through the foreground liveness supervisor after every self-test gate passes.
12. Latches a panic code in `.noinit` RAM and halts on core exceptions, unclaimed interrupts, watchdog setup failure, or liveness failure; an active IWDG then resets the running panic loop.

Do not flash even this image until the purchased board revision and PB9 assignment have been checked. There is intentionally no flash command yet; the pyOCD target and destructive-unlock procedure must be proven on the actual board first.
