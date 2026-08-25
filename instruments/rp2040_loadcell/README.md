# RP2040 USB Load-Cell Instrument

Standalone passive measurement firmware for an RP2040 and NAU7802, plus a
Python capture utility. The device records signed raw 24-bit ADC samples in the
NAU7802 conversion-ready domain and streams them over native USB CDC. Force
and torque calibration remain host-side.

Firmware 0.2.0 is flashed on the target board and completed an initial no-load
COM30 smoke test on 2026-08-24. `INFO` reported the expected I2C0/polling/LED
configuration and a ready sensor. A 0.5-second host capture completed tare,
START, MARK, STOP, and final draining with 158 consecutive samples, zero
drops, I2C errors, overruns, saturations, malformed records, or timestamp
regressions, and a measured average of 314.523 SPS. Because no load cell was
connected, the raw offset/noise is only floating-input evidence and does not
qualify the analog path or calibration. The authoritative behavioral contract
is [REQUIREMENTS.md](REQUIREMENTS.md).

## Safe default configuration

`platformio.ini` records the reported NAU7802 I2C0 pair as SDA GPIO20 and SCL
GPIO21. DRDY is not connected, so `LOADCELL_DRDY_PIN=-1` selects conversion-
ready polling through the NAU7802 `PU_CTRL.CR` status bit. The firmware now
initializes I2C and the sensor using those two known pins. It drives only the
reported onboard active-high indicators: blue GPIO25, red GPIO26, and green
GPIO27. It does not configure or drive conditioned or external outputs.

The checked-in hardware flags are:

```ini
-DLOADCELL_SDA_PIN=20
-DLOADCELL_SCL_PIN=21
-DLOADCELL_DRDY_PIN=-1
-DLOADCELL_LED_BLUE_PIN=25
-DLOADCELL_LED_RED_PIN=26
-DLOADCELL_LED_GREEN_PIN=27
-DLOADCELL_LED_ACTIVE_HIGH=1
-DLOADCELL_I2C_INSTANCE=0
```

Green indicates a healthy idle instrument. Blue covers tare, active capture,
and STOP draining. Red indicates a sensor initialization failure or a sticky
data-integrity condition observed since reset: I2C error, dropped sample,
buffer overrun, or ADC saturation. LED pins may be set to `-1` individually to
disable them.

`INFO` reports `ready_mode=POLL`, `drdy_configured=0`, and
`ready_poll_interval_us=250`. A later board with a routed DRDY signal can set a
non-negative GPIO to restore interrupt-edge timestamps without changing the
host protocol.

SDA and SCL must be a valid pin pair for the selected I2C instance and
Arduino-Pico board. Invalid RP2040 controller/pin mappings fail at compile time;
runtime bus or sensor initialization failures report the sensor unavailable.
Change `board = pico` as well if the target is not electrically equivalent to a
Raspberry Pi Pico. Do not assign conditioned 24 V outputs; version 1 neither
configures nor drives them.

## Build and flash

From this directory:

```powershell
pio run
pio run --target upload
```

The first build downloads the Arduino-Pico PlatformIO integration and the
pinned SparkFun NAU7802 library. Native USB ignores the configured monitor baud
rate, but `115200` is retained for conventional serial tools.

## Host setup and tests

The capture utility requires Python 3.10 or newer and pyserial:

```powershell
py -m pip install -r host/requirements.txt
py -m unittest discover -s tests -p "test_*.py"
```

An example 250 ms raw capture is:

```powershell
py host/loadcell_capture.py --port COM30 --run-id torque-smoke --duration-seconds 0.25 --sample-rate-sps 320 --gain 128 --tare-samples 320 --marker-at 0.05:motor_start --marker-at 0.20:motor_stop
```

Omit `--port` only when exactly one plausible USB serial device is connected.
The utility creates a timestamped directory under `captures/` containing:

- `metadata.json`: request, device responses, calibration metadata, sequence
  gaps, malformed records, markers, and the device final summary.
- `force_telemetry.csv`: one row per valid sample with device and host receive
  timing.

The terminal shows compact progress and a final summary; it does not print each
sample. `--counts-per-newton`, `--force-sign`, and `--lever-radius-m` record
physical calibration metadata without modifying the raw CSV.

## Firmware structure

- `include/loadcell_config.h`: compile-time hardware settings and bounded
  capacities.
- `include/loadcell_types.h`: acquisition, health, tare, and protocol records.
- `include/static_ring.h`: fixed-capacity foreground queue.
- `src/main.cpp`: NAU7802 initialization, conversion-ready polling or optional
  DRDY timestamping, acquisition, command handling, and nonblocking USB
  transmission.
- `host/loadcell_capture.py`: protocol parser and capture CLI.
- `tests/test_loadcell_capture.py`: hardware-independent parser, accounting,
  asynchronous-tare, and artifact tests.

In the checked-in polling mode, foreground code queries `PU_CTRL.CR` shortly
before the next nominal conversion and then at nominal 250-microsecond
intervals until ready. The sample timestamp is the RP2040 time at which
ready is observed, and flag `0x0002` distinguishes it from a hardware-edge
timestamp. With a routed DRDY pin, the interrupt records only the timestamp and
sequence. I2C access, command parsing, formatting, and USB writes remain in
foreground context in either mode.

USB output advances only when CDC reports write capacity, and samples wait in
a bounded queue; overload is counted instead of silently blocking acquisition.
Failed output-register reads receive three bounded attempts. Foreground code
retains the original timestamped conversion for a later retry, so a transient
I2C fault cannot silently stall capture or invent another conversion. Each
failed bus transaction remains counted.

On connection, the host checks `STATUS`. If an earlier host left the device in
`RUNNING` or `STOPPING`, it issues or completes `STOP`, drains that run's final
record, and only then configures a new capture. Final records seen outside the
new run's STOP phase cannot mark the new capture complete.

## Initial hardware gate

Before treating a capture as evidence:

1. Confirm the exact board identity, 3.3 V domain, I2C pull-ups, the GPIO20/21
   I2C0 assignment, and load-cell wiring from the schematic.
2. Confirm `INFO` reports `ready_mode=POLL`, the expected configuration, and
   `sensor_ready=1`.
3. Confirm `STATUS` reports a fresh ready age and that sample timestamp spacing
   matches the selected rate within the polling uncertainty.
4. Run tare unloaded and inspect its raw mean and standard deviation.
5. Apply and remove a bounded known load and confirm repeatable signed raw-count
   movement without saturation.
6. Complete the five-minute and forced-reader-delay acceptance tests in
   `REQUIREMENTS.md` before relying on the instrument for torque results.

The MKS57D `torque` command now accepts `--loadcell-port` and writes the force
CSV plus instrument metadata directly into the motor run directory. It uses
this module's protocol parser/accounting rather than duplicating the wire
contract. Instrument setup or tare failure prevents motor START; a transport or
data-integrity failure during active torque makes the MKS host send its normal
generic STOP. The instrument remains passive. Synchronization input,
persistent calibration, calibrated firmware output, and conditioned/external
output control remain deferred.
