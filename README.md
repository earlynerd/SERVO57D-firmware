# SERVO57D Open Firmware

Clean-sheet firmware for the N32L406CBL7-based Makerbase MKS SERVO57D RS-485
closed-loop stepper controller.

## Current operating snapshot

Firmware 0.38.3 / native protocol 1.19 is the current source and flashed
baseline. Firmware 0.30.1 corrected the fast
phase predictor to the measured 55 us DMA-to-PWM-application interval, and
matched +8 rev/s bursts then staged current-loop proportional gains of 2, 3,
and 4 while retaining `Ki=1/64` and every electrical limit. Velocity RMS error
fell from 0.797 to 0.621 to 0.460 rev/s, and A/B current RMS error fell from
about 146 to 100 to 87 counts. The Kp=4 burst peaked at 563 of the unchanged
700-permille phase-voltage limit, timing remained stable, authority released,
and no current, predictor, encoder, backend, supervisor, reset, or panic fault
appeared.

The 0.31.0 candidate moves current-loop Kp/Ki from compile-time-only constants
into the product configuration service. Protocol 1.14 reports compiled-default,
stored, and volatile-active values; applies or reverts gains only while the
drive is inactive and forced to `ZERO`; and retains explicit dual-slot saving as
the only persistence action. A guided host sweep uses the existing
supervisor-owned current diagnostic, restores the starting volatile gains on
exit, writes normalized artifacts, and generates an offline-replottable HTML
comparison. The image also retains the traceable 512 rev/s²
observed-acceleration shutdown, an explicit shared cascade-deadline contract,
wide-range current-slew arithmetic, and one shared set of safety-relevant
control-math helpers. Its bounded 0.1 rev/s smoke completed 40,001 current-loop
updates with the live 512 rev/s² policy, normal zero-output release, preserved
generation-3 calibration, and no faults/reset/panic. Six earlier alternating
signed position moves bench-confirmed the coherent microsecond
timebase: maximum successful prediction age remained 1,435-1,483 us with no
predictor, backend, encoder, supervisor, reset, or panic fault.

Firmware 0.32.2 retains that tuning workflow and stages the MT6816 transport at
8 MHz with a deterministic 4 kHz rotor/velocity/position release. It preserves
the prior velocity-filter pole with `alpha=0.03283179`, preserves the 50 ms
position-settle policy with 200 samples, optimizes the complete deferred-control
chain at `-O2`, and enables Cortex-M4F single-precision hardware through the
`softfp` ABI. Encoder observations are timestamped when CS asserts at the start
of the coherent four-byte transaction rather than after DMA and the CS hold.
The user's preliminary 4 kHz run is encouraging, but formal interval, noise,
preemption, stack, and motion evidence remains open. The 20 kHz fixed-point
current path and all existing authority, deadline, current, voltage, duty,
motion, and fault bounds are unchanged.

The normal 20 kHz path now uses the already validated immutable controller
configuration and leaves DWT/TIM2/preload timing capture dormant until an
operator explicitly arms the one-shot trace. Raw ADC range, hard-current,
reference, output-duty, PWM readback, and missed-output checks remain active.
The 4 kHz rotor runtime publishes a 56-byte progress record on every sample and
the 576-byte controller snapshot at 100 Hz or on transitions; foreground
safety, readiness, events, and watchdog work run on a wrap-safe 1 ms cadence
while RS-485 draining and raw Right-button sampling remain wake-driven.

Firmware 0.33.0 adds an optional bounded frequency ramp to the retained
rotating-current diagnostic. The ramp reaches the configured electrical
frequency before the full requested hold/test window begins; STOP, Right-button,
transport-failure, fault, and common `ZERO` behavior are unchanged, and the
single wrap-safe authority deadline covers ramp plus hold. The tuning tool uses
50 electrical Hz/s by default, arms its high-resolution trace only after ramp
plus settling, and records ramp time separately from the scored window.

Firmware 0.34.0 advances that diagnostic's electrical angle and ramp at the
20 kHz ADC/current-loop rate instead of holding each reference for one
millisecond. Protocol 1.16 status and tuning reports also expose each run's
total missed PWM-output boundaries and maximum consecutive misses; the existing
guardian still faults on the second consecutive miss.

Firmware 0.35.0 adds a selectable fixed-point rotating-frame PI only to that
bounded diagnostic. It Park-transforms measured A/B current at the ADC sample
angle and inverse-Park-transforms its d/q voltage at the measured 55 us PWM
application boundary. The existing stationary A/B PI remains the default and
the production torque/velocity/position path is unchanged. Protocol 1.17 keeps
the legacy six-byte diagnostic configuration request, adds an optional
controller-mode byte, and reports the active diagnostic mode in status schema
5. Tuning reports now plot and summarize d/q current and voltage for direct
same-condition comparison.

Firmware 0.35.1 widens only the rotating-current diagnostic permission from
250 to 1,000 electrical Hz. At that ceiling the 20 kHz current loop retains 20
updates per electrical cycle and the 4 kHz encoder retains four observations
per cycle. This is an implementation-based search bound for other motors, not
qualification of the attached motor; current, voltage, duty, timing, deadline,
STOP, fault, and `ZERO` enforcement are unchanged.

Firmware 0.36.0 promotes the proven fixed-point rotating-frame PI into the
production aligned-q actuator used by torque, velocity, and position control.
Each 20 kHz current event predicts both the current-sample phase for Park and
the measured 55 us PWM-application phase for inverse Park, then regulates
`d=0` and the signed q-current command. Static current vectors and alignment
remain on stationary A/B PI because they deliberately operate without a valid
rotor-aligned frame. Protocol, configuration persistence, bridge authority,
electrical limits, deadlines, guardian, STOP, faults, and `ZERO` are unchanged.

Firmware 0.36.1 raises the aligned actuator's independent observed-acceleration
shutdown from 512 to 8,192 rev/s². Acceleration alone has no defensible board or
motor protection threshold without qualified torque and inertia data, so the
new plausibility boundary sits above the approximately 5,350 rev/s² largest
nominal-cadence velocity change the 4 kHz filtered estimator can legitimately
publish. Current, voltage, duty, speed, following-error, feedback-age, deadline,
authority, fault, and `ZERO` enforcement remain independent and unchanged.

Firmware 0.37.0 makes direct-velocity acceleration an explicit command value.
New host commands default to the bench-proven 16 rev/s² launch, and legacy
10-byte requests receive the same default; callers may explicitly select a
positive value through the retained 256 rev/s² controller capability. Position
profiles and their inner-loop slew headroom are unchanged.

Firmware 0.37.1 retains protocol 1.18 and removes a reporting-only A/B polar
conversion from the ordinary aligned-current path before PWM staging. The
accepted 4 kHz observation still refreshes status, while an armed 20 kHz trace
reconstructs the exact per-event A/B reference after the PWM-stage timestamp.
Control output, trace fidelity, authority, electrical limits, deadlines,
guardian behavior, faults, and `ZERO` are unchanged. Native/Python tests and
Debug/Release Arm builds pass.

Firmware 0.38.0 / protocol 1.19 adds an explicitly armed, aggregate 256-release
runtime profile for the 4 kHz deferred-control chain. It records total and
maximum DWT cycles for pend latency, dispatch/copy, encoder decode, estimator,
active control, publication, complete PendSV work, and 1 kHz foreground work,
plus higher-priority current-loop completions. The profiler is dormant unless
armed, consumes no trace buffer, and can run alongside the existing 20 kHz
current trace.

Firmware 0.38.1 retains protocol 1.19 and assigns the active-high PD0 LED only
to the raw 4 kHz deferred-chain deadline. At each 250 us TIM6 release boundary,
the LED turns on if the preceding acquisition-through-PendSV job is still
incomplete and turns off when the newest overdue job completes. There is no
pulse stretching or latching; overlapping misses remain continuously on, and
the former physical heartbeat is now a software-only diagnostic counter.

Firmware 0.38.2 retains protocol 1.19, publication rates, layouts, and sequence
protection while forcing the fixed rotor-runtime structure assignments to
expand as aligned 32-bit loads/stores. Arm disassembly showed that 0.38.1 moved
472 controller bytes through four calls to the nano C library's byte-at-a-time
`memcpy` on every 100 Hz full publication. Native/Python tests and Debug/Release
Arm builds pass. After flashing, the previously motion-correlated PD0 pulses are
no longer visible while the rotor moves.

Firmware 0.38.3 retains protocol 1.19 and makes four exact hot-path reductions:
active 4 kHz controllers read the backend's atomic `active` field instead of a
full critical-section snapshot; each accepted observation caches its dynamic
55 us phase advance; aligned prediction reporting occurs only after PWM staging
and the immediate trace timestamp; and each sine/cosine pair shares lookup index
and interpolation-fraction work. The 55 us horizon remains the measured timing
contract, while its cached phase is recomputed from every observed phase rate.
Control outputs, numerical rounding, publication/protocol layouts, trace timing,
authority, limits, faults, STOP, and `ZERO` are unchanged.

Firmware 0.38.3 is flashed on the COM14 controller. On 0.38.1, PD0 was dark
while idle but rapidly emitted individually very-low-duty pulses during motion;
after the 0.38.2 word-copy change, no blue light was visible during motion.
Firmware 0.38.3 completed simultaneous current-trace/runtime-profile captures at
both +4 and -4 rev/s plus a profiler-disarmed +4 rev/s control run. All 512
profiled releases completed with no incomplete release: total PendSV averaged
105.34/104.46 us and peaked at 164.34/164.11 us for the positive/negative runs,
respectively, against the 250 us period. Both 256-sample current traces retained
at least 31.22 us PWM-preload margin, zero missed updates, and clean finite-
deadline `ZERO` release without reset or panic evidence. The disarmed run had
nearly identical tracking. A separate +8 rev/s attempt was intentionally not
accepted after proving too aggressive for the unloaded setup and faulting safely.

Firmware 0.38.0 was flashed and accepted on the COM14 controller. A +4 rev/s,
16 rev/s², 606 mA, two-second run armed both profilers at steady state and
completed with 0.0967 rev/s RMS error, zero faults, and normal `ZERO` release.
All 256 deferred releases were complete: pend latency averaged 2.89 us and
peaked at 99.77 us; PendSV work averaged 144.09 us and peaked at 336.61 us,
including 20 kHz preemption. The simultaneous current trace retained 256
consecutive samples, 29.59 us minimum PWM-preload margin, and zero missed
updates. Generation-3 alignment and stored Kp=4/Ki=1/64 remained intact.

Firmware 0.37.1 was flashed and accepted on the COM14 controller. Signed
30.3 mA, 100 ms torque pulses completed normally. Matched +4 rev/s for 2 s and
-4 rev/s for 3 s at 16 rev/s2 with a 606 mA permission measured 0.1015 and
0.1437 rev/s RMS velocity error without current limiting or faults. Five
sequential trace arms captured 1,280 consecutive 20 kHz samples: trigger-to-DMA
was 4.25-6.00 us, DMA-to-PWM staging was 20.72-21.66 us, minimum preload margin
was 31.09 us, and prediction age was 174-456 us with zero missed updates. A
separate 0.1 rev/s run accepted scheduled generic STOP. The controller ended
inactive with stored Kp=4/Ki=1/64 restored, valid generation-3 alignment, zero
references/duties, and no fault, watchdog-reset, or retained-panic evidence.

The first flashed 0.37.0 direct-velocity gate reached +4 rev/s through a
16 rev/s² launch smoothly and quietly with 0.073 rev/s RMS velocity error,
only 26 counts peak q-current request, clean 20 kHz timing, no faults or missed
updates, and normal `READY`/`ZERO` release.

The mirrored -4 rev/s gate used the same launch and 606 mA permission, measured
0.132 rev/s RMS velocity error and 61 counts peak q-current request, and ended
normally. Its armed 256-sample trace retained consecutive 20 kHz updates,
4.25-4.31 us trigger-to-DMA time, 22.47-22.97 us DMA-to-PWM time, at least
29.78 us preload margin, 174-425 us prediction age, and zero missed updates or
faults. Signed 30.3 mA torque pulses and mirrored 0.25-revolution position moves
also passed polarity, ordinary release, generic STOP, following-error shutdown,
in-place recovery, and post-recovery motion gates. Position settling remains a
tuning task: one move ended safely at -0.00482 revolution after its deadline,
while the mirrored move and positive repeat settled within 0.00085 revolution.

Paired 303 mA, Kp=9/Ki=0.5 captures now bench-confirm that diagnostic through
200 electrical Hz. At 200 Hz, rotating mode reduced phase lag from 39.09 to
-0.01 degrees and RMS current error from 149.4 to 7.9 mA, while using 445/700
permille peak phase voltage. Its worst DMA-to-stage time was 21.73 us with
30.59 us minimum preload margin, zero missed PWM updates, no faults, normal
`ZERO` release, and restoration of the prior stationary configuration.

The flashed baseline has a bench-proven 20 kHz two-phase current loop, a
deterministic 4 kHz rotor service, persisted alignment, and bounded aligned
torque, signed velocity, and relative-position control. At 24 V, a +8 rev/s
request reaches target. A +12 rev/s request reaches the 2.999 A nominal
q-demand and 70%-of-bus phase-voltage ceilings and plateaus near 10 rev/s
without predictor, encoder, backend, current-loop, supervisor, reset, or panic
faults. Automatic-injected VBUS telemetry reports physical bus and commanded
phase volts without delaying the current-loop DMA event.

The next work is firmware control-path performance rather than final
motor/load tuning. The accepted 0.37.0 through 0.38.3 current, timing, and motion captures are
regression baselines for reducing ordinary 20 kHz fixed-point current-path and
4 kHz rotor/control overhead, then improving high-electrical-frequency current
tracking and estimator/phase/outer-loop scheduling without weakening authority,
deadline, electrical, motion, or fault contracts. Torque-reaction fixture
construction, higher-current force qualification, final current-gain selection,
and low-speed position tuning are deferred until those tuning-sensitive
firmware changes settle.
Exact live, validated, evaluation, and hard limits are owned by
[the operating-limit inventory](docs/OPERATING_LIMITS.md); active work is owned
by [the project plan](PLAN.md).

## Evaluation-firmware warning

This firmware deliberately exposes unqualified operating zones so the drive's
real boundaries can be measured. Firmware acceptance is not a claim that the
attached motor, supply, load, mechanics, cooling, or tuning will perform well
there. Current saturation, poor tracking, stalls, heating, supply disturbance,
vibration, and energetic motion are possible.

Use a current-limited supply, a suitable mechanical fixture, finite current,
voltage, duration, and motion bounds, and captured telemetry. Keep the physical
Right-button stop and supply cutoff immediately available. The common all-low
`ZERO` state dynamically brakes the motor; the proven board path has no passive
software coast state.

## Run a bounded test

With the motor safely connected and the RS-485 adapter on `COM14`, this command
runs a bounded rotating-current diagnostic, captures telemetry, analyzes it,
and opens a self-contained report:

```powershell
py tools/motor_test.py --port COM14 --current-ma 750 --rpm 24 --seconds 5
```

For production motion status and a conservative relative move:

```powershell
py tools/mks57d_rs485.py --port COM14 velocity-status
py tools/mks57d_rs485.py --port COM14 velocity --rps 8 --current-limit-ma 3000 --duration-ms 3000 --trace-at-seconds 1 --profile-at-seconds 1
py tools/mks57d_rs485.py --port COM14 position-status
py tools/mks57d_rs485.py --port COM14 position --revolutions 0.25 --max-rpm 30 --acceleration-rps2 1 --current-limit-ma 606 --duration-ms 3000
```

On firmware 0.29.0 or newer, an operator can acknowledge and attempt in-place
recovery after removing a fault's initiating condition:

```powershell
py tools/mks57d_rs485.py --port COM14 clear-faults
```

`clear-faults` establishes `ZERO`, rebuilds the ADC/DMA, PWM/current backend,
and controller runtime, and returns the supervisor to uncommanded
`DIAGNOSTIC`. Fresh current and encoder production must restore `READY`
before another command can energize the bridge. A persistent condition faults
again normally. `stop` remains a separate authority-termination request and
does not clear fault latches.

Read the safety prerequisites and only the applicable procedure in
[the bench bring-up guide](docs/BRINGUP.md) before hardware work. The
[tool guide](tools/README.md) documents capture formats and additional commands.

## Architecture and scope

One product drive supervisor owns `RESET_SAFE`, `DIAGNOSTIC`, `READY`,
`ALIGN`, `RUN`, and `FAULT`. The project-owned timer, ADC, modulation,
current backend, and direct-GPIO `ZERO` mechanism are the only bridge authority
path. Motion controllers may request bounded current through that path; they do
not write bridge registers.

The project is actively engineering toward Makerbase's advertised 12–24 V,
0–5200 mA, 20 kHz loop, 3000+ RPM, and 256-subdivision endpoints—or toward a
measured explanation and implementation change where the present board or
control architecture prevents them. Those advertised values are product goals,
not verified safe limits.

This remains a clean-sheet implementation. It will not extract, disassemble, or
reproduce Makerbase firmware or its bootloader. The canonical interface is a
project-owned versioned protocol over a transport-independent command service;
Modbus RTU and documented Makerbase commands are optional adapters. The project
does not redesign the PCB or make the controller suitable for safety-critical
machinery.

## Documentation

Use [the documentation index](docs/README.md) to select the minimum relevant
material. In particular:

- [operating limits](docs/OPERATING_LIMITS.md) own numeric envelopes;
- [protocol](docs/PROTOCOL.md) owns commands and wire formats;
- [architecture](docs/ARCHITECTURE.md) and
  [real-time architecture](docs/REALTIME_ARCHITECTURE.md) own control and timing
  contracts;
- [the project plan](PLAN.md) contains active incomplete work only;
- `DECISIONS.md` and `DEBUG_LOG.md` preserve structural and debugging history
  and are searched when relevant, not read routinely from cover to cover.
- [the RP2040 load-cell instrument](instruments/rp2040_loadcell/README.md) is a
  separate passive USB measurement project that the torque CLI can optionally
  coordinate without transferring motor authority.

Build, flash, and host-test instructions are in
[the building guide](docs/BUILDING.md).

## Repository layout

| Path | Purpose |
| --- | --- |
| `firmware/` | Embedded firmware and portable control/application code |
| `tests/` | Native tests and deterministic host plants |
| `docs/` | Routed architecture, hardware, bring-up, and protocol documentation |
| `tools/` | Host control, analysis, build, programming, and reference helpers |
| `instruments/` | Standalone measurement firmware and its host utilities |
| `reference/` | External-document catalog and ignored local cache |
| `vendor/` | Imported manufacturer-support manifest and required source subset |

## License

A project license has not been selected. See [LICENSE](LICENSE). Choose an
open-source license before publishing project-owned firmware.
