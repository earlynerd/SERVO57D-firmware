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

Every firmware build also cross-compiles `mks57d_future_control`, containing
the standalone step/direction decoder and portable d/q current controller that
are intentionally not linked into `mks57d.elf`. The explicit target is
`future-control-arm`; successful compilation does not grant either primitive
product motion or bridge authority.

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

The linker follows the N32L406xB memory map in User Manual V2.6: 128 KiB
flash, 16 KiB SRAM1 at `0x20000000`, an 8 KiB address gap, and 8 KiB SRAM2 at
`0x20006000`. Application Flash is limited to 124 KiB; the final two 2 KiB
erase pages at `0x0801F000` and `0x0801F800` are reserved as alternating
configuration slots. The image confines all allocated runtime sections to
SRAM1 and reserves its final 2 KiB for the stack. SRAM2 is initialized for
parity but a link-time assertion rejects allocations there until hardware
validation.

Every firmware build also runs a post-link check that verifies the initial
vector stack pointer, SRAM bank boundaries, and all three application/config
Flash boundaries. This catches a regression to the incompatible contiguous
layouts present in the vendor SDK and CMSIS pack metadata or an image that can
overwrite persistent configuration.

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
After reset, bridge authority remains under the product supervisor. RS-485
diagnostic and motion commands are the current operating interfaces; local
Left/Center inputs cannot energize the bridge.

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
on `PATH` while Visual Studio's developer environment is not loaded. Without
the repository's configure-time guard, that state reaches compilation before
MSVC reports misleading missing-standard-header errors such as:

```text
fatal error C1083: Cannot open include file: 'stdbool.h': No such file or directory
```

This is an incomplete `INCLUDE`/`LIB`/SDK environment, not evidence that the C
source or repository include paths are broken. Run:

```powershell
pwsh -File tools/build.ps1 -Target host-tests
```

The host configuration detects this incomplete MSVC environment, removes only
the stale `build/host-debug/tests/host_unit_tests.exe`, and fails with the
wrapper command before generation completes. This prevents a subsequently
invoked CTest from reporting an older executable as the result of a failed
current build.

The wrapper locates Visual Studio, enters its developer-command environment,
then configures, builds, and runs CTest. `-Target all` builds the Debug Arm
image plus host tests; it does not build the Release preset. Complete validation
therefore also runs:

```powershell
cmake --preset firmware-release
cmake --build --preset firmware-release
```

To make the individual CMake commands work in an existing ordinary PowerShell
session, dot-source the same environment importer used by the wrapper:

```powershell
. .\tools\import-msvc-environment.ps1
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

Dot-sourcing is required because an ordinarily invoked PowerShell script cannot
modify its parent shell's environment. The imported values affect only the
current PowerShell process and its children. A bare CMake host build is also
appropriate from a Visual Studio Developer PowerShell or Developer Command
Prompt, where those variables are already present.

## Current image behavior

Firmware 0.31.0 / protocol 1.14 is the current unflashed source candidate;
firmware 0.30.3 / protocol 1.13 is the flashed evaluation build. The current
motion baseline retains the 0.27.1 identity, readiness, live-policy,
calibration restore, and bounded positive-velocity smoke checks through a
12 rev/s request. At 24 V, +8 rev/s reaches
target without q-current clipping; +12 rev/s reaches the 2.999 A nominal
demand and the 70%-of-bus phase-voltage ceiling (16.8 V at the nominal 24 V
setting) and plateaus near 10 rev/s, without a
predictor, encoder, backend, current-loop, supervisor, reset, or panic fault.
Firmware 0.26.0 remains the bench-qualified relative-position baseline.
Firmware 0.28.0 retains the 0.27.1 motion envelope and adds automatic-injected VBUS telemetry plus physical
host electrical units. Firmware 0.29.0 adds explicit in-place fault recovery
through a direct-GPIO `ZERO`, ADC/DMA plus PWM/current-backend rebuild, and
controller/supervisor reset without erasing calibration or reset history.
Firmware 0.29.1 separates the controllers' 2 ms feedback-interval contract
from a 3 ms fast-predictor horizon, no later than the independent encoder-
production deadline, and reports typed predictor rejection evidence in aligned-
torque status schema 2.
Firmware 0.29.2 retains that protocol and those limits while reconciling the
priority-15 SysTick epoch with higher-priority microsecond readers through a
bounded monotonic publication.
Firmware 0.29.3 retains protocol 1.12, makes the observed-acceleration shutdown
traceable to the cascade slew envelope, and closes the remaining non-tuning
review correctness and repository-hygiene findings. Firmware 0.30.0 adds the
re-armable 20 kHz current/timing burst; 0.30.1-0.30.3 use it to correct the
phase-prediction boundary and stage the tested Kp from 2 through 4. Firmware
0.31.0 moves Kp/Ki into volatile-active, stored, and compiled-default product
configuration and adds the guided sweep/report workflow without adding another
bridge or motion owner.
Firmware 0.27.1 includes the
independent 3 ms encoder-production guard and adds bounded 20 kHz electrical-
phase prediction, a 16 rev/s/2.999 A nominal motion evaluation envelope, and explicit
position-cascade headroom without changing the wire layout. It:

1. Verifies the reset-default 4 MHz MSI, then starts the fitted 8 MHz HSE and PLL x8 for 64 MHz HCLK with one Flash wait state, PCLK2 32 MHz, PCLK1 16 MHz, and bounded readiness/source/readback checks.
2. Initializes and verifies four NVIC preemption bits with no subpriorities.
3. Initializes the full SRAM2 bank with stores, clears its parity-error status, and does not allocate from it.
4. Verifies PA6, PA7, PB0, PB1, and PB7 begin input/no-pull in the reset-safe board state. This is an initial-state gate, not a permanent reservation of the bridge pins.
5. Configures the active-high PD0 status LED output.
6. Starts a 1 kHz SysTick timebase at the lowest programmable priority, 15;
   its microsecond view uses bounded raw-snapshot and exclusive-publication
   retries so current control may preempt epoch service without observing
   backward time.
7. Enters `APP_STATE_DIAGNOSTIC`, then reaches `READY` only after current-path and encoder readiness; the LED toggles every 250 ms.
8. Snapshots and clears sticky reset flags for debugger-visible reset-cause diagnostics.
9. Runs and publishes a seven-gate boot self-test, then preloads PA6/PA7/PB0/PB1 low, initializes edge-aligned TIM3 from its 32 MHz timer clock at 20 kHz with zero compare values, and assigns channels 1-4 to the four pins on AF2.
10. Initializes mode-3 SPI1 on PB3-PB6 at 500 kHz or lower. TIM6 releases a 1 kHz MT6816 transaction, TIM7 owns bounded CS timing, SPI1 DMA channels 2/3 move the frame, and PendSV decodes accepted samples and advances the shared rotor runtime. Foreground independently requires accepted encoder progress within 3 ms; loss removes readiness while idle or faults every energized authority through `ZERO`.
11. Configures USART1 AF4 on PA9/PA10 at 115200 8N1, holds PC13 low for receive, and moves RX/TX bytes with reserved DMA channels 4/5 without unsolicited transmission.
12. Parses native v1.14 COBS/CRC frames in foreground and replies to valid
    address-1 discovery, boot, raw/estimated encoder, current-diagnostic,
    automatic-alignment, generic-STOP, persistent-configuration, and aligned
    q-current, signed velocity, relative-position, and explicit fault-recovery requests,
    including live status while active. Inactive configuration operations may
    apply or revert current-loop gains without saving them; explicit full-
    configuration save remains the only gain-persistence operation.
13. Initializes the SSD1306-compatible 72-by-40 OLED over 333.3 kHz I2C1 and performs bounded 5 Hz partial updates in the current-loop display.
14. Configures and arms DMA channel 1 plus a two-rank `currentB/currentA` ADC sequence before starting TIM3. TIM2 resets from TIM3 update and its 80%-phase compare ISR software-starts each two-halfword DMA sequence; regular completion releases the current loop, then a one-rank automatic-injected PA3 conversion captures VBUS. Thirty-two startup current snapshots establish independent A/B zeros before the OLED displays both signed currents in milliamperes.
15. Samples and independently debounces the three keys, M_IN1/M_IN2, and the no-pull PA0/PA8/PB7 pulse-interface inputs every 10 ms.
16. Keeps the bridge in `ZERO` until the product supervisor reaches `READY`. The retired local Left/Center phase selector cannot request authority. RS-485 may request the bounded rotating-current diagnostic through the supervisor/current backend; deadline, STOP, transport failure, or the physical Right button returns it to `ZERO`.
17. Keeps RS-485, display, and configuration work in foreground while the timer/DMA/PendSV rotor service remains deterministic and observable during active operation.
18. Loads motor alignment and current-loop gains only from a schema/range/CRC/
    commit-valid record. Schema-1 records receive the 0.31.0 gain defaults in
    RAM and migrate transactionally on explicit save. A successful alignment is
    saved automatically only after backend and authority release while merging
    the previously stored/default gains, so it cannot promote a volatile trial;
    no active operation or startup ADC zero is persisted.
19. Enters `RUN` motion authority only for a valid aligned q-current request,
    waits for a newly accepted encoder sample, starts the existing 20 kHz
    backend at zero demand from that sample, and publishes bounded q-current plus
    calibrated phase/velocity/timestamp seeds at 1 kHz. Every 20 kHz current
    event predicts phase to the measured PWM application boundary and regenerates signed
    A/B references under independent current, slew, velocity, acceleration,
    prediction-age, feedback-age, duration, and fault contracts.
20. Starts a valid velocity request at zero q-current from newly accepted
    feedback, runs an acceleration-limited PI at the 1 kHz rotor release, and
    updates only the bounded aligned-q-current actuator. Target speed, observed
    speed, per-command current, reference acceleration, feedback age, deadline,
    actuator health, and common STOP/fault paths remain independently enforced.
    Command permission is 16 rev/s and 495 counts, inner slew is 256 rev/s²,
    and observed speed remains independently bounded at 20 rev/s.
21. Starts a valid relative-position request only near rest, advances a bounded
    trapezoidal profile, applies independent following-error and settling
    policy, and changes only the target of the existing velocity/current
    actuator. Travel, trajectory speed/acceleration, current, feedback age,
    duration, STOP, Right-button, and fault limits remain separate. The profile
    permits 64 rev/s² while the inner slew retains fourfold headroom; corrected
    velocity may reach 17 rev/s above the 16 rev/s profile range.
22. Publishes firmware `0.31.0`, authoritative drive state, reset cause,
    retained panic, uptime, heartbeat, watchdog health, priority policy,
    self-test masks, raw encoder state, RS-485 transport state, native-protocol
    counters, and current-loop state through the unchanged 240-byte schema-5
    `g_diagnostics` RAM record; estimator, alignment, and configuration fields
    are presently on wire rather than appended to that debugger ABI.
23. Starts a nominal one-second IWDG and services it only through the foreground liveness supervisor after every self-test gate passes. The watchdog continues during debugger halt.
24. Commands the all-low zero vector, latches a panic code in `.noinit` RAM, and halts on core exceptions, unclaimed interrupts, watchdog setup failure, or liveness failure; an active IWDG then resets the running panic loop.

Firmware 0.24.13 passed the deterministic rotor-service regression on COM14:
more than 54,000 idle samples held 1000-1001 us intervals with zero transport
errors, and a 606 mA aligned-q-current run completed 100,000 current-loop
updates over five seconds with zero encoder, DMA, estimator, backend, control,
reset, or panic faults. Earlier automatic-alignment and persistent-configuration
power-cycle gates remain accepted.

Firmware 0.24.15 was flashed and passed its ordinary hardware smoke check.
Firmware 0.25.1 passes native tests plus clean Debug and Release Arm builds.
Mirrored ±0.1 rev/s, 25-count, two-second COM14 commands moved in the requested
encoder coordinate and completed with clean deadline release and no faults.
Debug uses 47,764
bytes of the 124 KiB application region and 6,796 bytes of SRAM1; Release uses
42,848 bytes and the same SRAM1, with no allocation in the configuration slots
or SRAM2. READY boot, passive velocity status, and positive/negative low-speed
deadline completion have passed. Explicit STOP, physical Right-button stop, and
hand-loaded current saturation/recovery also pass. Initial velocity qualification
is accepted. Physical encoder/readiness-loss injection is indefinitely deferred
on this assembly; common fault/ZERO behavior remains host/native tested.

Firmware 0.26.0 passes the native C suite, the Python host-tool suite, and clean
Debug/Release Arm post-link builds. Debug uses 54,252 bytes of the 124 KiB
application region and 7,404 bytes SRAM1; Release uses 48,568 bytes and the
same SRAM1. It is flashed and passed mirrored ±0.25-revolution settling plus
generic STOP with 1000 us captured encoder intervals and no faults. The
expanded 2-4 rev/s velocity envelope remains unqualified.

Firmware 0.26.1 passes the native C suite, all 10 applicable Python host-tool
tests (two optional reference-cache tests skip), and clean Debug/Release Arm
post-link builds. Debug uses 54,456 bytes of the 124 KiB application region and 7,404
bytes SRAM1; Release uses 48,788 bytes and the same SRAM1. Neither image
allocates a configuration slot or SRAM2, and the 240-byte debugger diagnostic
ABI remains verified. The encoder-progress guard still needs ordinary flash
identity/readiness and bounded-motion smoke checks.

Firmware 0.27.0 passes the expanded native C suite, all 10 applicable Python
host-tool tests (the same two optional reference-cache tests skip), and clean
Debug/Release Arm post-link builds. Debug uses 55,808 bytes of the 124 KiB
application region and 7,444 bytes SRAM1; Release uses 50,192 bytes and the same
SRAM1. Neither image allocates a configuration slot or SRAM2, and the unchanged
240-byte debugger diagnostic ABI verifies. Disassembly confirms the 20 kHz
prediction path has no floating-point or software-division call; worst-case
cycle instrumentation and the ordinary liveness/prediction hardware smoke gate
remain required.

Firmware 0.27.1 retains protocol 1.9, passes the native suite including the
position-correction-headroom regression, and passes all 10 applicable Python
host-tool tests (the same two optional reference-cache tests skip). Clean Debug
and Release post-link builds pass: Debug uses 55,828 bytes Flash, Release uses
50,248 bytes, and both use 7,444 bytes SRAM1 with no configuration-slot or SRAM2
allocation. The 240-byte debugger diagnostic ABI remains verified. Hardware
smoke confirmation passes through a +12 rev/s request. Raising the bus from
12 V to 24 V removes clipping at +6 rev/s and reduces RMS velocity error from
1.197 to 0.638 rev/s. At +8 rev/s the motor reaches target; at +12 rev/s both
q-demand and phase voltage saturate in most samples and speed plateaus near
10 rev/s. Negative-direction high-speed response, current-loop phase tracking,
scoped predictor/current timing, and corrected-position cascade confirmation
remain open.

Firmware 0.28.0 / protocol 1.10 passes the rebuilt native C suite, 14 applicable
Python host tests (two optional reference-cache tests skip), and clean Debug
and Release Arm post-link builds. Debug uses 56,124 bytes Flash, Release uses
50,520 bytes, and both use 7,452 bytes SRAM1 with no configuration-slot or
SRAM2 allocation. The 240-byte debugger diagnostic ABI remains verified.
Commissioning status schema 3 expands the maximum native payload to 72 bytes
and appends a validity-tagged PA3 VBUS sample/count without changing any older
status offset. Hardware identity reports 0.28.0/protocol 1.10. Inactive status
reported 23.829 V at the 24 V supply setting; all 22 samples from a one-second
1 rev/s / 606 mA regression held 23.776-23.815 V, completed 20,001 current-loop updates with an
advancing VBUS sample counter, returned all duties to zero, and retained clear
ADC, deadline, encoder, backend, reset, and panic state.

Firmware 0.29.0 / protocol 1.11 passes 16 Python console tests and the rebuilt
native C suite, including byte-exact typed `CLEAR_FAULTS` coverage. Clean Debug
and Release Arm post-link builds use 58,160/52,352 bytes Flash and 7,464 bytes
SRAM1. Its flashed image bench-confirms following-error acknowledgment,
calibration preservation, no-reset recovery, propagated backend-fault recovery,
and `no_fault` idempotence. The first later bounded move exposed the separate
zero-headroom predictor-age defect.

Firmware 0.29.1 / protocol 1.12 passes 18 Python console tests and the rebuilt
native C suite, including byte-exact 72-byte aligned-torque status and rejected-
age boundary coverage. Clean Debug and Release Arm post-link builds pass: Debug
uses 58,540 bytes Flash, Release uses 52,844 bytes, and both use 7,476 bytes
SRAM1 with no configuration-slot or SRAM2 allocation. The unchanged 240-byte
debugger diagnostic ABI remains verified. Its flashed recovery gate passed and
its retained predictor evidence isolated a later failure as unsigned -424 us
from the preempted-SysTick epoch window.

Firmware 0.29.3 / protocol 1.12 passes the 20-test Python suite with two
hardware-only skips, the rebuilt native suite, and strict GCC compilation of
all 36 host sources. Regressions cover the wide signed current-reversal slew,
shared control-math saturation, and inherited coherent-timebase cases. Clean
Debug and Release Arm post-link builds pass: Debug uses 60,784 bytes Flash,
Release uses 52,952 bytes, and both use 7,480 bytes SRAM1 with no configuration-
slot or SRAM2 allocation. The 240-byte debugger ABI is unchanged. The flashed
image reports the 512 rev/s² policy and passed a bounded 0.1 rev/s smoke with
normal release, preserved calibration, and no fault/reset/panic evidence.

Firmware 0.30.0 / protocol 1.13 passes the rebuilt native suite and all 22
Python console tests, including byte-exact arm/trace-schema coverage,
schema-1 host compatibility, and normalized velocity-capture artifacts. Clean
Debug and Release Arm post-link builds pass: Debug uses 61,496 bytes Flash,
Release uses 53,608 bytes, and both use 11,564 bytes SRAM1. The 8,192-byte
trace buffer remains in SRAM1; configuration slots, SRAM2, and the 240-byte
debugger diagnostic ABI remain untouched. Its first +8 rev/s burst measured the
ADC/DMA and control path and exposed the old nominal 7 us prediction lead as
one carrier short.

Firmware 0.30.1 / protocol 1.13 changes only the prediction lead to the measured
55 us DMA-to-application interval. The rebuilt native suite and all 22
applicable Python tests pass with two optional skips. Clean Debug/Release Arm
builds use 61,484/53,612 bytes Flash and 11,564 bytes SRAM1. Matched +8 rev/s
bursts on 0.30.1-0.30.3 staged Kp=2/3/4 with Ki=1/64, reducing velocity RMS
error from 0.797 to 0.621 to 0.460 rev/s and A/B current RMS error from about
146 to 100 to 87 counts. Kp=4 peaked at 563/700 permille phase-voltage command,
released normally, and left all current, timing, predictor, encoder, backend,
supervisor, reset, and panic evidence clear.

Firmware 0.31.0 / protocol 1.14 passes the rebuilt native suite; the 38-test
Python suite completes with two optional PyMuPDF skips. Tests cover schema-1 migration,
schema-2 dual-slot persistence, idle-only apply/revert, explicit promotion,
alignment/calibration non-promotion, live-limit validation, abort restoration,
offline replotting, and embedded settled 20 kHz trial waveforms. Clean
Debug/Release Arm post-link builds use 62,864/54,908 bytes Flash and 11,564
bytes SRAM1, with no configuration-slot or SRAM2 allocation and the 240-byte
debugger diagnostic ABI unchanged. Hardware flash, apply/revert, sweep-abort,
save, and power-cycle gates remain open.
