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

## First J-Link flash

After the target has been deliberately released from RDP L1 to L0, use the
guarded J-Link wrapper from the repository root:

```powershell
pwsh -File tools/flash-jlink.ps1
pwsh -File tools/flash-jlink.ps1 -Yes
```

The first invocation is a dry run. It builds the debug firmware, validates the
binary's initial stack pointer and reset vector, and prints the exact artifact
and SHA-256 without accessing the probe. `-Yes` additionally selects SEGGER's
exact `N32L406CB` target at 200 kHz SWD, programs `mks57d.elf`, independently
verifies `mks57d.bin` at `0x08000000`, resets, and starts the image. An optional
probe serial number can be selected with `-ProbeSerial`.

Use a current-limited supply appropriate for the intended run. On the tested
board the motor may remain connected while flashing; a new or reworked bridge
backend should first be checked unloaded. The script requires a valid RDP L0
option-byte state and does not release read protection or erase option bytes.
After reset, bridge authority remains under the firmware's local or RS-485
current-loop command path.

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

### Windows MSVC environment trap

Do not treat a bare `cmake --build --preset host-debug` from an ordinary
PowerShell session as the canonical Windows host build. `cl.exe` may be present
on `PATH` while Visual Studio's developer environment is not loaded. In that
state CMake configures successfully and compilation starts, but MSVC cannot find
standard headers and reports misleading errors such as:

```text
fatal error C1083: Cannot open include file: 'stdbool.h': No such file or directory
```

This is an incomplete `INCLUDE`/`LIB`/SDK environment, not evidence that the C
source or repository include paths are broken. Run:

```powershell
pwsh -File tools/build.ps1 -Target host-tests
```

The wrapper locates Visual Studio, enters its developer-command environment,
then configures, builds, and runs CTest. Use `-Target all` for the normal full
host-plus-Arm validation. A bare CMake host build is appropriate only in a
shell that was already launched from a Visual Studio Developer PowerShell or
Developer Command Prompt.

## Current image behavior

Firmware 0.21.0 is the current bench-validated current-regulated product build. It:

1. Verifies the reset-default 4 MHz MSI, then starts the fitted 8 MHz HSE and PLL x8 for 64 MHz HCLK with one Flash wait state, PCLK2 32 MHz, PCLK1 16 MHz, and bounded readiness/source/readback checks.
2. Initializes and verifies four NVIC preemption bits with no subpriorities.
3. Initializes the full SRAM2 bank with stores, clears its parity-error status, and does not allocate from it.
4. Verifies PA6, PA7, PB0, PB1, and PB7 begin input/no-pull in the reset-safe board state. This is an initial-state gate, not a permanent reservation of the bridge pins.
5. Configures the active-high PD0 status LED output.
6. Starts a 1 kHz SysTick timebase at the lowest programmable priority, 15.
7. Enters `APP_STATE_DIAGNOSTIC`, then reaches `READY` only after current-path and encoder readiness; the LED toggles every 250 ms.
8. Snapshots and clears sticky reset flags for debugger-visible reset-cause diagnostics.
9. Runs and publishes a seven-gate boot self-test, then preloads PA6/PA7/PB0/PB1 low, initializes edge-aligned TIM3 from its 32 MHz timer clock at 20 kHz with zero compare values, and assigns channels 1-4 to the four pins on AF2.
10. Initializes mode-3 SPI1 on PB3-PB6 at 500 kHz or lower and schedules bounded foreground MT6816 burst reads every 1 ms after a 20 ms power-up delay. Accepted samples receive microsecond timestamps and feed the shared mechanical estimator; native telemetry reports latest/maximum observed intervals.
11. Configures USART1 AF4 on PA9/PA10 at 115200 8N1, holds PC13 low for receive, and moves RX/TX bytes with reserved DMA channels 4/5 without unsolicited transmission.
12. Parses native v1.5 COBS/CRC frames in foreground and replies to valid address-1 discovery, boot, raw/estimated encoder, current-diagnostic, automatic-alignment, and generic-STOP requests, including live status while active.
13. Initializes the SSD1306-compatible 72-by-40 OLED over 333.3 kHz I2C1 and performs bounded 5 Hz partial updates in the current-loop display.
14. Configures and arms DMA channel 1 plus a two-rank `currentB/currentA` ADC sequence before starting TIM3. TIM2 resets from TIM3 update and its 80%-phase compare ISR software-starts each two-halfword DMA sequence; 32 startup snapshots establish independent A/B zeros before the OLED displays both signed currents in milliamperes.
15. Samples and independently debounces the three keys, M_IN1/M_IN2, and the no-pull PA0/PA8/PB7 pulse-interface inputs every 10 ms.
16. Keeps the bridge in `ZERO` until the product supervisor reaches `READY`. Holding Enter after the required release requests diagnostic authority for a bounded fixed-point A/B loop with a nominal 150 mA rotating reference; raw release or Menu stops the backend, releases authority, and returns to `ZERO`.
17. Continues RS-485 foreground processing and the 1 kHz candidate encoder schedule while active so current state, rotor motion, estimator timing, and STOP remain observable throughout a run.
18. Publishes firmware `0.21.0`, authoritative drive state, reset cause, retained panic, uptime, heartbeat, watchdog health, priority policy, self-test masks, raw encoder state, RS-485 transport state, native-protocol counters, and current-loop state through the unchanged 240-byte schema-5 `g_diagnostics` RAM record; estimator and alignment-progress fields are presently on wire rather than appended to that debugger ABI.
19. Starts a nominal one-second IWDG and services it only through the foreground liveness supervisor after every self-test gate passes. The watchdog continues during debugger halt.
20. Commands the all-low zero vector, latches a panic code in `.noinit` RAM, and halts on core exceptions, unclaimed interrupts, watchdog setup failure, or liveness failure; an active IWDG then resets the running panic loop.

Firmware 0.21.0 retains the 0.19.0 supervisor-authorized local hold-to-run and
duration-bounded RS-485 current diagnostics. The timer AF mapping, all four
bridge legs, 20 kHz DMA-completion loop, low-zero modulation, feedback signs,
active encoder polling, remote STOP, and motor rotation are bench-proven. The
0.19.0 supervisor path completed a 303 mA, 5 electrical Hz, two-second
deadline-bounded run and a separate 151.5 mA explicit-STOP run. Both released
authority, returned the bridge to `ZERO`, restored the guarded configuration,
and left reset/panic health clean. Encoder-loss fault injection and the local
button path remain pending. The 0.20.0 1 kHz estimator schedule, sample jitter,
stationary noise, and velocity output pass their initial idle and 757 mA / 20 Hz
hardware regression. Two successful 757.4 mA automatic alignments and a
generic-STOP abort pass with clean release and no fault/reset; Menu and
estimator readiness-loss injection remain pending on hardware. Use
the guarded wrapper for the same board/probe setup.
