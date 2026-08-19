# SERVO57D Open Firmware

Clean-sheet firmware for the N32L406CBL7-based Makerbase MKS SERVO57D RS-485
closed-loop stepper controller.

## Project ambition

This project is building a high-performance motor drive, not a permanently
derated commissioning demonstration. The goal is responsive, precise current,
velocity, and position control that makes useful use of the board's available
bus voltage, phase current, encoder, and 20 kHz control bandwidth. Safety comes
from independent measured limits, bounded authority, timing guarantees, and a
common immediate fault path—not from leaving the product constrained to tiny
current, voltage, speed, or motion ceilings.

Operating limits are expanded deliberately from bench evidence, and every
accepted point remains motor-, supply-, board-, cooling-, and enclosure-specific.
The present rotating-current test is the development foundation for aligned
torque control and closed-loop motion; it is not the final user interface.

## Run a test move and view plots

With the motor connected to a current-limited supply and the RS-485 adapter on
`COM14`, one command runs a bounded move, captures current and encoder telemetry,
analyzes the result, and opens a self-contained plot report:

```powershell
py tools/motor_test.py --port COM14 --current-ma 750 --rpm 24 --seconds 5
```

The firmware still owns the current, voltage, duty, duration, deadline, and
fault limits. Press Ctrl+C to send STOP. Each run is saved under ignored
`scratch/motor-runs/`, and the tool restores the preceding inactive test
configuration unless `--keep-config` is requested. Use `--no-open` to capture
without opening a browser or `--replot RUN_DIRECTORY` to reopen a saved run.

This interface currently commands a positive-frequency rotating current vector;
`--rpm` is a speed magnitude derived from the tested motor geometry, not yet a
closed-loop shaft-speed, signed-direction, or position command. Those controls
are the next motor-control milestone.

## Current operating envelope

Firmware 0.18.2 is working current-regulated motor firmware. It runs from the
fitted 8 MHz crystal at a bench-proven 64 MHz system clock, closes independent
A/B winding-current loops at 20 kHz, samples the encoder at 100 Hz, updates the
OLED, and serves a native RS-485 protocol-1.3 control, telemetry, and bounded
20 kHz startup-trace console.

The image preloads PA6/PA7/PB0/PB1 low and maps them to TIM3. After independent
zero calibration, a released-then-held Enter button can grant bridge authority
to the fixed-point current loop. Next selects the initial electrical phase;
raw Enter release or Menu returns to `ZERO`. The local rotating reference is
151.5 mA. RS-485 can configure 1-165 ADC counts (about 6.06 mA-1.00 A)
and 0.001-50 Hz electrical frequency, start a 0.1-60 second run, stream current,
duty, fault, reset, and encoder state, or stop local or remote authority. The
independent raw-current trip is about 1.212 A, phase voltage is limited to 70%
of the bus, and the timer guardian latches missed current-loop updates.

The loop starts from all-low `ZERO` and uses low-zero sign-magnitude PWM: for
each winding, only the leg selected by the voltage-command sign switches while
the opposite leg remains low. Current is sampled at 80% of the carrier through
a 16 MHz, TIM2-released two-rank ADC/DMA sequence. The phase-specific bridge mapping
matches the asymmetric A+/B- shunt placement.

Each MCU bridge command drives tied EG3013 HIN/LIN inputs. Command low selects
the low-side FET and command high selects the high-side FET, so all-low is a
deterministic zero-voltage vector, not an all-FET-off state. All current-loop
faults converge on that vector. Reset/halt waveforms, thermal limits, and the
wider voltage/current/speed envelope remain engineering
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
- independent startup zero calibration and signed-current conversion using the
  measured 3.3 V ADC reference, 6.65 sense gain, 20 mOhm shunts, and
  6.059 mA/count scale;
- delayed 80%-carrier ADC/DMA sampling and 100,000 consecutive fault-free
  current-loop updates during a five-second 757 mA motor run;
- correct A/B feedback and all four bridge-leg polarities;
- encoder-confirmed rotation at -5.97 RPM from a commanded 5 Hz electrical
  vector (6.00 RPM expected), with zero encoder, SPI, current-loop, or reset
  faults;
- firmware 0.18.2 proportional gain `Kp=2` with the integral gain retained at
  `1/64` per 20 kHz step: a 303 mA startup step has 6.53 ms 10-90% rise time,
  8% overshoot, and 14.0 mA tail RMS error; 606 mA / 15 Hz tracks -17.78 RPM
  versus -18 RPM commanded, and 757 mA / 20 Hz tracks 1.97 revolutions over
  five seconds versus 2.00 commanded with 25.2% peak voltage effort.

The next functional milestone is encoder/electrical-angle alignment followed
by velocity and position control. Remaining characterization includes
higher-current through the 1 A endpoint and enclosed thermal behavior, current-sense temperature and
unit-to-unit tolerance, bus-voltage protection, bootstrap/duty limits,
reset/halt waveforms, and timer capture for step/direction/enable. See
[the project plan](PLAN.md).

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
