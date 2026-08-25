# RP2040 USB Load-Cell Instrument Requirements

Build a standalone firmware project for an RP2040 board containing an NAU7802
and native USB. Its purpose is to capture timestamped load-cell force data
during 100-250 ms motor torque tests, with later integration into the MKS57D
torque-capture CLI.

## Known and pending hardware details

- The build environment is PlatformIO using the Earle Philhower Arduino-Pico
  core.
- NAU7802 SDA, SCL, and DRDY pins and the RP2040 I2C controller instance are
  compile-time settings. The reported bus assignment is I2C0 with SDA on
  GPIO20 and SCL on GPIO21. DRDY is not connected; `LOADCELL_DRDY_PIN=-1`
  selects I2C polling of `PU_CTRL.CR` instead.
- The reported onboard indicators are active high: blue is GPIO25, red is
  GPIO26, and green is GPIO27. Their pins and shared polarity are compile-time
  settings. Green means healthy and idle; blue means tare, capture, or STOP
  drain activity; red means the sensor is unavailable or a data-integrity
  counter has latched since reset.
- Board product identifier or schematic, load-cell wiring and capacity, and
  conditioned 24 V I/O assignments and polarity are still pending.
- Conditioned 24 V outputs are outside the first-version firmware and must
  remain inactive through boot, reset, and faults.

## Firmware MVP

- Use native USB CDC.
- Initialize the NAU7802 and verify device communication when pins are
  configured; otherwise expose a tagged `PINS_UNCONFIGURED` state.
- Perform required NAU7802 internal calibration and report failures.
- Support 10, 20, 40, 80, and 320 samples per second and gains 1 through 128;
  default to 320 SPS and gain 128.
- Poll the NAU7802 cycle-ready status without a DRDY connection and timestamp
  each ready observation using the RP2040 monotonic 64-bit microsecond timer.
  Preserve optional interrupt-edge mode for a later board with routed DRDY.
- Preserve the signed raw 24-bit ADC result.
- Keep acquisition independent of USB transmission:
  - The cycle-ready source captures timing without doing an ADC read in ISR
    context.
  - ADC reads occur outside interrupt context.
  - USB writes never wait for a host.
  - A bounded ring buffer absorbs temporary host delays.
- Track I2C errors, missing samples, buffer overruns, ADC saturation, and the
  latest ready-observation age (`drdy_age_us` in protocol version 1).
- USB disconnects or slow readers must not hang the firmware.
- Do not configure or drive any conditioned or external auxiliary output. The
  three reported onboard status LEDs are the only version-1 outputs.

## USB protocol version 1

Host commands are newline-terminated ASCII:

```text
INFO
CONFIG <sample_rate_sps> <gain>
TARE <sample_count>
START <run_id>
MARK <marker_id>
STOP
STATUS
```

`CONFIG` and `TARE` are idle-only. Unknown or malformed commands return an
error without disrupting acquisition. Run and marker identifiers are 1-31
characters from `[A-Za-z0-9_.-]`.

Sample records:

```text
S,1,<sequence>,<timestamp_us>,<raw_counts>,<flags_hex>,<dropped_total>
```

Sample flags are:

- `0x0001`: signed 24-bit ADC saturation.
- `0x0002`: timestamp is an I2C-polled ready-observation time, not a DRDY edge.

Marker records:

```text
M,1,<marker_id>,<timestamp_us>
```

Command responses:

```text
OK,1,<command>,...
ERR,1,<command>,<error_code>,<description>
```

`START` and `STOP` responses carry their device timestamp after the status
field. A successful stop response is:

```text
OK,1,STOP,DRAINING,<timestamp_us>
```

The stop timestamp is taken in the same critical section that disables further
run-edge capture. Samples and marker records queued before that boundary drain
before the final record.

An asynchronous tare emits:

```text
OK,1,TARE,STARTED,<sample_count>
OK,1,TARE,COMPLETE,<sample_count>,<mean_raw_counts>,<stddev_raw_counts>
```

`STOP` prevents new run samples, drains samples already accepted into the
buffer, and emits this final record:

```text
F,1,<run_id>,<first_sequence>,<last_sequence>,<captured_count>,<dropped_count>,<i2c_error_count>,<buffer_overrun_count>,<first_timestamp_us>,<last_timestamp_us>,<average_sample_rate_sps>
```

No untagged debug text may be mixed into the protocol stream.

## Tare and physical calibration

- `TARE` calculates and reports the mean raw offset and population standard
  deviation.
- `TARE` rejects a sample at either signed 24-bit limit as `ADC_SATURATED`
  instead of accepting a clipped offset.
- Every captured sample retains raw ADC counts.
- Firmware does not require or persist a force or torque calibration.
- The host applies tare offset, counts-per-newton, force sign, and lever radius:
  `torque_Nm = force_N * radius_m`.
- Physical calibration values belong in host capture metadata.

## Reference host utility

The Python utility must:

- Locate or open the USB CDC port.
- Read `INFO`, configure, and tare the device.
- Start and stop a named capture and insert markers.
- Detect malformed records and sequence gaps.
- Write `metadata.json` and `force_telemetry.csv`.
- Show compact status instead of printing every sample.

CSV fields:

```text
sequence
device_timestamp_us
host_receive_elapsed_seconds
raw_counts
flags_hex
dropped_total
```

## Acceptance criteria

- Reproducible clean build and documented flash procedure.
- Reliable native USB CDC enumeration.
- Streaming at the configured rate for at least five minutes without
  unexplained gaps or malformed records.
- Timestamps demonstrate the expected conversion cadence and the polled mode's
  detection latency is characterized on hardware.
- Forced USB-reader delays produce explicit overrun or drop evidence instead
  of silent loss.
- A failed output-register transaction cannot permanently stall capture while
  the ready condition remains asserted through either GPIO or `PU_CTRL.CR`;
  each foreground pass makes at most three attempts, failed transactions remain
  visible in I2C-error counters, and discarded conversions remain visible in
  drop counters.
- Tare returns plausible mean and noise statistics.
- Applying and removing a known load produces repeatable signed raw-count
  changes.
- `START`, `MARK`, and `STOP` records share the sample timestamp domain and have
  defined ordering.
- Reset and USB reconnection recover without energizing auxiliary outputs.
- A reconnecting host drains a capture found in `RUNNING` or `STOPPING`, and a
  stale final record cannot complete the next run.

## Deferred features

- Persistent force calibration.
- Calibrated force or torque output from firmware.
- GUI.
- Conditioned 24 V output use.
- Motor control or STOP authority.
- Hardware synchronization input.

A later revision may use one verified conditioned input as a read-only
synchronization channel and emit:

```text
E,1,<timestamp_us>,<channel>,<level>
```

Version 1 remains a passive measurement instrument.
