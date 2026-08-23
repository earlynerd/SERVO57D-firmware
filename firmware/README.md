# Firmware

This directory contains the buildable N32L406CBL7 current-regulated product
image. The current source closes both winding-current loops at 20 kHz through the
authoritative drive supervisor, acquires the encoder through a deterministic
1 kHz timer/SPI-DMA/PendSV service, persists measured motor alignment, and
provides bounded signed encoder-aligned q-current as the first production `RUN`
motion operation. The deterministic rotor path is bench-proven during a 606 mA,
five-second aligned-torque run with zero encoder, DMA, estimator, backend,
control, reset, or panic faults. Earlier automatic-alignment, generic-STOP, and
configuration power-cycle gates remain accepted. Firmware 0.24.14 removed the completed local
Left/Center phase-selector and direct fixed-duty PWM bring-up path while retaining
the RS-485 rotating-current diagnostic through the supervisor/current backend.
Firmware 0.24.15 establishes a single product-owned rotor estimator and an
immutable position/velocity observation boundary. Firmware 0.25.1 closes the
first bounded signed velocity loop on that observation and commands only the
existing aligned-q-current actuator, mapping mechanical effort through the
direction measured and persisted by alignment. Firmware 0.26.0 adds a focused
relative-position trajectory through that exact controller/actuator stack. The broader lease and
step/direction motion candidate remains separately compiled and unlinked.
Firmware 0.26.0 passed mirrored ±0.25-revolution settling and generic-STOP
checks with clean authority release. The earlier velocity
deadline/polarity, physical Right-button stop, and hand-loaded
saturation/recovery evidence remains accepted. Firmware 0.26.1 adds a 3 ms
foreground encoder-progress guard; it is host-tested and Arm-build validated,
and firmware 0.27.1 adds bounded 20 kHz electrical-phase prediction with a
nominal 7 us output lead, plus a 16 rev/s evaluation
command range with correction headroom. Firmware 0.27.1 passes identity,
readiness, live-policy, calibration restore, and bounded positive-
velocity confirmation through a 12 rev/s request. At 24 V, +8 rev/s reaches
target without q-current clipping; +12 rev/s reaches both the 2.999 A nominal
demand and the 70%-of-bus phase-voltage ceiling (16.8 V at the nominal 24 V
setting) and plateaus near 10 rev/s, without a
predictor, encoder, backend, current-loop, supervisor, reset, or panic fault.
Firmware 0.28.0 / protocol 1.10 additionally samples PA3 VBUS after each
regular A/B current pair, publishes status schema 3, and makes amperes and
volts the primary host-facing electrical units without changing the controller's
raw count/ratio math. It is flashed: inactive status reported 23.829 V at the
24 V supply setting, and a one-second 1 rev/s / 606 mA regression completed
20,001 current-loop updates with advancing VBUS samples, zero terminal duties,
and no ADC, deadline, encoder, backend, reset, or panic fault. Physical
readiness-loss injection remains deferred on the
current assembly while the common fault/ZERO behavior remains automated.
Firmware 0.29.0 / protocol 1.11 adds an explicit operator `CLEAR_FAULTS`
transaction. It first establishes direct-GPIO `ZERO`, rebuilds the
timer-synchronous ADC/DMA and TIM3/current backend, resets faulted estimator and
controller operation state, preserves motor alignment/configuration and reset
history, and returns the supervisor to uncommanded `DIAGNOSTIC`. The operator
command itself is sufficient acknowledgment; a condition that persists faults
again through its ordinary monitor.
That image is flashed and bench-confirms following-error acknowledgment,
calibration preservation, no-reset recovery, propagated backend-fault recovery,
and `no_fault` idempotence. Its first later bounded move exposed a predictor-
age fault because the predictor and controller shared the same 2 ms boundary
without dispatch headroom. Firmware 0.29.1 / protocol 1.12 retains the
controllers' 2 ms timestamp-interval check, gives the 20 kHz predictor a 3 ms
horizon no later than encoder-production liveness, and appends typed rejection
reason/age plus maximum successful/configured age to aligned-torque status.
The flashed 0.29.1 evidence then identified a separate unsigned -424 us
timestamp caused by priority-2 current control preempting priority-15 SysTick
between hardware reload and software epoch publication. Firmware 0.29.2 keeps
that priority ordering and reconciles the microsecond clock through bounded
exclusive-access publication.

The 0.22.0 storage and protocol implementation passes host failure-injection
tests and Debug/Release Arm builds. First-save, unchanged-save, power-cycle
restore, persistent clear, and no-restored-authority behavior pass on COM14.
The 0.23.2-and-later aligned-torque controller and protocol pass
host validation. Its duration contract accepts explicit finite deadlines through
the wrap-safe 32-bit half-range instead of imposing the initial one-second
candidate ceiling. The shared backend and torque request path also admit the
attached motor's 2.999 A nominal rated-current evaluation point while retaining
an independent 3.635 A raw trip. A pending torque request begins only on a newly
accepted encoder sample and is first updated by the following sample, preserving
the 2 ms active-feedback watchdog without charging serial-service latency to it;
the signed deadline/STOP/fault and
expanded-current hardware gates remain pending.

## Current operating contract

- Startup verifies the reset-default 4 MHz MSI, then enables the fitted 8 MHz HSE and PLL x8 for 64 MHz HCLK. PCLK2 is 32 MHz, PCLK1 is 16 MHz, and TIM3 receives the doubled 32 MHz APB1 timer clock.
- The initial stack and runtime data use SRAM1 only. SRAM2 is initialized for parity but unavailable to the linker until bench validation.
- The active-high status LED is PD0; PB8/PB9/PA15 and PB12/PB13 are bench-proven active-low monitored inputs.
- PA6, PA7, PB0, and PB1 begin high impedance/no-pull, then firmware preloads all four low and assigns TIM3 channels 1-4 on AF2. Each signal directly drives tied EG3013 HIN/LIN inputs, so low selects the low-side FET and high selects the high-side FET.
- SPI1 on PB3-PB6 performs bounded mode-3 MT6816 reads on a deterministic 1 kHz TIM6/TIM7/SPI-DMA schedule, including while the motor runs; PendSV decodes each completed frame and advances the sole rotor runtime. An independent foreground monitor requires accepted encoder production to advance within 3 ms: loss removes idle readiness or faults energized authority through `ZERO`. Native protocol 1.12 reports raw sensor health, unwrapped mechanical position, filtered velocity, current and maximum observed sample intervals, estimator faults, alignment validity, automatic-alignment progress/results, persistent configuration, aligned-torque state/policy and predictor evidence, velocity state/policy, position state/policy, and VBUS telemetry; its existing estimator-ready flag is asserted only while this progress evidence is live.
- USART1 AF4 on PA9/PA10 receives continuously through DMA channel 4. DMA
  channel 5 provides bounded TX, and PC13 returns low only after final line
  completion. A foreground COBS/CRC parser replies only to valid address-1
  discovery, telemetry, and current-loop requests; no bytes are transmitted
  automatically. Status, STOP, and explicit fault recovery remain available.
- A bounded 333.3 kHz I2C1 PA4/PA5 transport updates the fitted SSD1306-compatible 72-by-40 panel. The current-loop display refreshes its two-page view at 5 Hz.
- TIM2 resets from each TIM3 update and raises a compare interrupt at 80% of
  the carrier; that bounded ISR software-starts a 16 MHz, 7.5-cycle PA1/PA2
  `currentB`/`currentA` ADC sequence captured as one complete DMA pair. After
  independent startup zero calibration, the OLED shows both signed currents
  in milliamperes. Acquisition failure appears as numeric status `A####`; a
  current-loop shutdown latches `F####`, where the number is the one-based
  position of the first set fault bit. A 55.5-cycle PA3 `vBus` injected
  conversion follows every regular pair; it completes after the current-loop
  DMA event and is consumed by foreground telemetry.
- All eight passive inputs are sampled every 10 ms with independent three-sample debounce. The OLED shows the PA0/PA8/PB7 raw levels as `S D E`; this validates static pin/polarity mapping and does not count step pulses.
- Earlier characterization builds used Left to select A1/A2/B1/B2 and Center to apply edge-aligned 20 kHz, 50% hardware PWM. That local phase-selector path and its direct fixed-duty PWM helper are retired. RS-485 retains the bounded production motor diagnostic through the drive supervisor and current backend: it can configure 1-495 counts and 0.001-250 electrical Hz, then request a 0.003-2,147,483.647 second run; timeout, physical Right-button stop, transport failure, or STOP returns it to `ZERO`.
- DMA completion advances the latest timestamped electrical phase from filtered mechanical velocity, maps bounded q-current into fresh A/B references, runs the fixed-point A/B PI controllers, and stages low-zero sign-magnitude TIM3 preloads. Controllers reject feedback timestamp intervals over 2 ms; the fast predictor permits age through 3 ms to cover bounded PendSV dispatch, never beyond the independent encoder-production deadline, and includes a nominal 7 us lead to the following PWM preload boundary. Stale or invalid prediction joins raw overcurrent, invalid reference/output, DMA/PWM failure, and missed-output faults on the common all-low path. Positive A voltage drives A2 and positive B voltage drives B1, matching the board's asymmetric shunt placement; the opposite signs drive A1/B2.
- Firmware 0.18.2 uses `Kp=2`, retains `Ki=1/64` per 20 kHz step, and records the first 256 successful loop outputs for post-run tuning analysis. At 12 V, a 303 mA startup step has 6.53 ms 10-90% rise time, 8% overshoot, and 14.0 mA tail RMS error. A 606 mA / 15 Hz run tracked -17.78 RPM versus -18 RPM commanded. A 757 mA / 20 Hz, five-second run completed 100,000 loop updates and 1.97 revolutions versus 2.00 commanded with no fault or reset and about 3.0 V peak commanded phase effort against the nominal 8.4 V ceiling.
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
  after the complete observation passes. STOP, Right-button stop, transport loss, encoder
  loss, current-loop failure, or readiness loss stop the backend and converge
  on the supervisor's normal release/fault path.
- The final two 2 KiB Flash pages are alternating motor-configuration slots.
  A successful alignment is automatically saved only after backend and motion
  authority release. Boot restores alignment only after schema, length,
  generation, CRC-32, commit marker, semantic bounds, and motor geometry all
  validate; it never restores authority, pending work, or current-sensor zeros.
  Explicit status/save/clear operations are available only through the same
  safe-state production command service.
- Aligned q-current enters `RUN` motion authority only from a healthy `READY`
  state with valid calibration. It starts the 20 kHz backend at zero, then each
  accepted 1 kHz encoder sample slews signed q-current and publishes measured
  phase/velocity to the backend. Every 20 kHz current event extrapolates phase,
  adds the q-axis 90 degrees, and regenerates A/B references. Current, slew,
  velocity, acceleration, prediction age, feedback
  age, duration, STOP, backend, and reference limits are independently enforced
  and reported; violations converge on the existing fault/`ZERO` path.
- Velocity enters that same `RUN` authority from `READY`, initializes at the
  measured speed and zero q-current, slews a signed reference, and applies PI
  anti-windup at the caller's explicit current limit. The ±16 rev/s,
  256 rev/s², and 2.999 A nominal envelope deliberately exposes operation above the
  accepted 1 rev/s point; acceptance is not a tracking, thermal, or mechanical
  performance guarantee. It adds no alternate
  estimator, actuator, current loop, PWM, or bridge path.
- Relative position begins only near rest, advances a caller-bounded
  trapezoidal reference, limits following error independently to 0.25
  revolution, and drives only dynamic targets into that velocity controller.
  Its profile permits 16 rev/s and 64 rev/s², the inner slew is 256 rev/s²,
  and the correction target has 1 rev/s of speed headroom above the profile.
  Travel, speed, acceleration, current, feedback age, settling, duration, STOP,
  and fault behavior remain separately enforced and reported.

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
gate is the position-specific physical Right-button and loaded following-error
check, followed by staged 2, 4, 8, 12, and 16 rev/s velocity evaluation. Physical
readiness-loss injection remains indefinitely deferred on this assembly.
