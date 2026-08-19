# SERVO57D Open Firmware

Feasibility-stage clean-sheet firmware for the N32L406CBL7-based Makerbase MKS
SERVO57D RS-485 closed-loop stepper controller.

## Current safety boundary

Firmware 0.17.3 is a bounded current-loop commissioning image. It runs from
the fitted 8 MHz crystal at a bench-proven 64 MHz system clock, samples the
encoder and current channels, updates the OLED, and serves a native RS-485
commissioning console.

The image preloads PA6/PA7/PB0/PB1 low and maps them to TIM3. After independent
zero calibration, a released-then-held Enter button can grant bridge authority
to a fixed-point 20 kHz A/B current loop. Next selects the initial electrical
phase; raw Enter release or Menu returns to `ZERO`. The commissioning command
is limited to about 150 mA nominal, raw-count overcurrent trips at about 600 mA
nominal, phase voltage is limited to 10% of the bus, and a timer guardian
latches missed current-loop updates. Native RS-485 commands can query the
complete live current-loop state, configure the bounded rotating reference,
start a 0.1-60 second run, or stop either local or remote authority. RS-485
query and stop processing remains active while the bridge runs.

The loop starts from all-low `ZERO` and uses low-zero sign-magnitude PWM: for
each winding, only the leg selected by the voltage-command sign switches while
the opposite leg remains low. A hardware `NRST` event still occurs before the
first current sample with the debugger disconnected; the temporary NRST lead
is the next isolated hardware variable.

Each MCU bridge command drives tied EG3013 HIN/LIN inputs. Command low selects
the low-side FET and command high selects the high-side FET, so all-low is a
deterministic zero-voltage vector, not an all-FET-off state. Reset, debugger
halt, watchdog-reset, gate, switch-node, current-feedback polarity, and
delayed current-sample waveforms still require scoped validation. Do not connect a motor
until the motor-disconnected commissioning sequence has passed.

A separate portable application/control build exercises motion ownership,
remote lease expiry, step/direction behavior, bounded trajectories, servo
behavior, and fault recovery against host-side deterministic plants. It is not
linked into the hardware image.

## Current project status

Bench-proven on the tested board:

- 64 MHz HSE/PLL clock operation and explicit APB/timer clock derivation;
- OLED, MT6816 encoder, local and isolated inputs, and RS-485 command/response;
- all four TIM3 AF2 bridge outputs in the earlier bounded characterizer;
- 20 kHz two-rank current ADC acquisition through circular DMA in the earlier
  bridge-disabled direct-trigger image;
- independent startup zero calibration and nominal signed-current conversion.

The open safety gates are scoped reset/halt/watchdog bridge behavior, dynamic
current-limit and bus-voltage trip proof, bootstrap/duty constraints, and
bench validation of the delayed TIM2-compare current-sampling point and
current-feedback signs.
Timer capture and operating semantics for the already-verified isolated
step/direction/enable inputs remain deferred.

The current-loop software path is implemented but not commissioned on switched
hardware. See [the project plan](PLAN.md) for the go/no-go gates and remaining
work.

## Intended outcome and scope

If the feasibility gates succeed, the project should provide reproducible open
firmware with bounded two-phase current, velocity, and position control;
step/direction and RS-485 interfaces; and documented calibration,
configuration, fault handling, and update procedures.

This is a clean-sheet implementation. It will not extract, disassemble, or
reproduce Makerbase firmware or its bootloader. The canonical interface is a
project-owned versioned protocol over a transport-independent command service;
Modbus RTU and publicly documented Makerbase commands are optional adapters.
The project does not redesign the PCB or make the controller suitable for
safety-critical machinery.

## Documentation

Use the [documentation index](docs/README.md) to select the subsystem material
needed for a task. The index routes to hardware evidence, architecture,
building, bench procedures, protocol details, and reference provenance.
`DECISIONS.md` remains the append-only architectural history; it is not routine
cover-to-cover reading.

## Repository layout

| Path | Purpose |
| --- | --- |
| `firmware/` | Embedded firmware and portable control/application code |
| `tests/` | Native tests and deterministic host plants |
| `docs/` | Routed architecture, hardware, bring-up, and protocol documentation |
| `tools/` | Build, programming, and reference-cache helpers |
| `reference/` | External-document catalog and ignored local cache |
| `vendor/` | Imported manufacturer-support manifest and required source subset |
| `DECISIONS.md` | Append-only architectural and behavioral history |

External manufacturer material remains under ignored `reference/local/` and
`vendor/local/` directories until its provenance and redistribution terms are
recorded.

## License

A project license has not been selected. See [LICENSE](LICENSE). Choose an
open-source license before publishing project-owned firmware.
