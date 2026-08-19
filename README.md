# SERVO57D Open Firmware

Clean-sheet firmware for the N32L406CBL7-based Makerbase MKS SERVO57D RS-485
closed-loop stepper controller.

## Current operating envelope

Firmware 0.17.8 is working current-regulated motor firmware. It runs from the
fitted 8 MHz crystal at a bench-proven 64 MHz system clock, closes independent
A/B winding-current loops at 20 kHz, samples the encoder at 100 Hz, updates the
OLED, and serves a native RS-485 control and telemetry console.

The image preloads PA6/PA7/PB0/PB1 low and maps them to TIM3. After independent
zero calibration, a released-then-held Enter button can grant bridge authority
to the fixed-point current loop. Next selects the initial electrical phase;
raw Enter release or Menu returns to `ZERO`. The local rotating reference is
nominally 150 mA. RS-485 can configure 1-50 ADC counts (about 6-303 mA nominal)
and 0.001-20 Hz electrical frequency, start a 0.1-60 second run, stream current,
duty, fault, reset, and encoder state, or stop local or remote authority. The
raw-current trip remains about 600 mA nominal, phase voltage is limited to 10%
of the bus, and the timer guardian latches missed current-loop updates.

The loop starts from all-low `ZERO` and uses low-zero sign-magnitude PWM: for
each winding, only the leg selected by the voltage-command sign switches while
the opposite leg remains low. Current is sampled at 30% of the carrier through
a TIM2-released two-rank ADC/DMA sequence. The phase-specific bridge mapping
matches the asymmetric A+/B- shunt placement.

Each MCU bridge command drives tied EG3013 HIN/LIN inputs. Command low selects
the low-side FET and command high selects the high-side FET, so all-low is a
deterministic zero-voltage vector, not an all-FET-off state. All current-loop
faults converge on that vector. Reset/halt waveforms, absolute current scaling,
thermal limits, and the wider voltage/current/speed envelope remain engineering
work; they no longer block bounded motor operation on the tested board.

A separate portable application/control build exercises motion ownership,
remote lease expiry, step/direction behavior, bounded trajectories, servo
behavior, and fault recovery against host-side deterministic plants. It is not
linked into the hardware image.

## Current project status

Bench-proven on the tested board:

- 64 MHz HSE/PLL clock operation and explicit APB/timer clock derivation;
- OLED, MT6816 encoder, local and isolated inputs, and RS-485 command/response;
- all four TIM3 AF2 bridge outputs and 20 kHz two-rank ADC/DMA current acquisition;
- independent startup zero calibration and nominal signed-current conversion;
- delayed 30%-carrier ADC/DMA sampling and approximately 160,000 consecutive
  fault-free current-loop updates across 150 mA and 300 mA tests;
- correct A/B feedback and all four bridge-leg polarities;
- encoder-confirmed rotation at -5.97 RPM from a commanded 5 Hz electrical
  vector (6.00 RPM expected), with zero encoder, SPI, current-loop, or reset
  faults.

The next functional milestone is encoder/electrical-angle alignment followed
by velocity and position control. Remaining characterization includes actual
ADC-reference/current gain, current-loop bandwidth, higher-current thermal
behavior, bus-voltage protection, bootstrap/duty limits, reset/halt waveforms,
and timer capture for step/direction/enable. See [the project plan](PLAN.md).

## Intended outcome and scope

The project is progressing toward reproducible open firmware with bounded
two-phase current, velocity, and position control;
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
