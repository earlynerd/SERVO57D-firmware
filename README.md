# SERVO57D Open Firmware

Clean-sheet firmware for the N32L406CBL7-based Makerbase MKS SERVO57D RS-485
closed-loop stepper controller.

## Current operating snapshot

Firmware 0.29.2 / native protocol 1.12 is the current source candidate. It
retains operator-acknowledged in-place fault recovery and the separated 2 ms
controller/3 ms predictor timing contracts, and makes the microsecond timebase
coherent when the priority-2 current ISR preempts the priority-15 SysTick
handler. Firmware 0.29.1 / protocol 1.12 is flashed. It bench-confirmed recovery
without reset and supplied typed predictor evidence showing that a later signed
move failed because the old timebase could report `now` 424 us before the
encoder observation, not because encoder production was stale.

The flashed baseline has a bench-proven 20 kHz two-phase current loop, a
deterministic 1 kHz rotor service, persisted alignment, and bounded aligned
torque, signed velocity, and relative-position control. At 24 V, a +8 rev/s
request reaches target. A +12 rev/s request reaches the 2.999 A nominal
q-demand and 70%-of-bus phase-voltage ceilings and plateaus near 10 rev/s
without predictor, encoder, backend, current-loop, supervisor, reset, or panic
faults. Automatic-injected VBUS telemetry reports physical bus and commanded
phase volts without delaying the current-loop DMA event.

The next control work is flashing 0.29.2 and repeating alternating signed
position moves to close the coherent-timebase gate, followed by high-electrical-frequency current tracking, measured
predictor/output timing, negative-direction speed staging, and the remaining
position fault/stop gates. Exact live, validated, evaluation, and hard limits are owned by
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
