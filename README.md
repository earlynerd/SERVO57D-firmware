# SERVO57D Open Firmware

Clean-sheet firmware for the N32L406CBL7-based Makerbase MKS SERVO57D RS-485
closed-loop stepper controller.

## Current operating snapshot

Firmware 0.32.2 / native protocol 1.14 is the current source candidate;
firmware 0.30.3 / protocol 1.13 is the last documented flashed baseline. Firmware 0.30.1 corrected the fast
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

The flashed baseline has a bench-proven 20 kHz two-phase current loop, a
deterministic 1 kHz rotor service, persisted alignment, and bounded aligned
torque, signed velocity, and relative-position control. At 24 V, a +8 rev/s
request reaches target. A +12 rev/s request reaches the 2.999 A nominal
q-demand and 70%-of-bus phase-voltage ceilings and plateaus near 10 rev/s
without predictor, encoder, backend, current-loop, supervisor, reset, or panic
faults. Automatic-injected VBUS telemetry reports physical bus and commanded
phase volts without delaying the current-loop DMA event.

The next control work is capturing formal 0.32.2 evidence for 8 MHz / 4 kHz
encoder timing and acquisition timestamps, outer-loop numerical/timing health,
volatile gain apply/revert, and explicit persistence across a power cycle, then
using its fixed-condition sweep before negative-direction speed staging and the
remaining position fault/stop gates.
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
py tools/mks57d_rs485.py --port COM14 velocity --rps 8 --current-limit-ma 3000 --duration-ms 3000 --trace-at-seconds 1
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

Build, flash, and host-test instructions are in
[the building guide](docs/BUILDING.md).

## Repository layout

| Path | Purpose |
| --- | --- |
| `firmware/` | Embedded firmware and portable control/application code |
| `tests/` | Native tests and deterministic host plants |
| `docs/` | Routed architecture, hardware, bring-up, and protocol documentation |
| `tools/` | Host control, analysis, build, programming, and reference helpers |
| `reference/` | External-document catalog and ignored local cache |
| `vendor/` | Imported manufacturer-support manifest and required source subset |

## License

A project license has not been selected. See [LICENSE](LICENSE). Choose an
open-source license before publishing project-owned firmware.
