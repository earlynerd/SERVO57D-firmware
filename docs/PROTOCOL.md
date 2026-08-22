# Command Protocol Architecture

Status: native protocol 1.9, discovery, boot and encoder telemetry, the
current diagnostic service, generic drive STOP, automatic alignment, and
power-loss-safe motor-configuration storage, bounded aligned q-current, and the
first bounded velocity service, and relative-position control are implemented
and host-tested.
Firmware 0.18.2
configured, started, observed, traced, and stopped encoder-verified motor runs
on the bench. Firmware 0.19.0 routes the retained diagnostic requests through
the product drive supervisor and has passed both deadline-release and explicit-
STOP motor regressions. Address
provisioning, native-wire duplicate handling, absolute-position/homing commands, Modbus
RTU, and Makerbase compatibility remain future work.

## Decision

The firmware will expose one transport-independent command service. Native
RS-485, Modbus RTU, and Makerbase-compatible wire decoders are adapters around
that service; none owns motor behavior or bypasses common validation.

```mermaid
flowchart LR
    DMA["USART1 RX/TX DMA"] --> NATIVE["Native RS-485 v1 adapter"]
    DMA --> MODBUS["Modbus RTU adapter"]
    DMA --> LEGACY["Makerbase compatibility adapter"]
    NATIVE --> VALIDATE["Common validation and authorization"]
    MODBUS --> VALIDATE
    LEGACY --> VALIDATE
    VALIDATE --> DISPATCH["Foreground command dispatcher"]
    DISPATCH --> STATUS["Status and diagnostics"]
    DISPATCH --> CONFIG["Configuration service"]
    DISPATCH --> MOTION["Motion-command arbiter"]
    DISPATCH --> SAFETY["Fault and safe-state service"]
```

The internal command model uses named operations and typed values. Makerbase
function numbers, Modbus register addresses, and native message identifiers
exist only in adapter mapping tables. This keeps command behavior consistent
and host-testable across every interface.

## Protocol roles

### Native RS-485 v1

The native protocol is the canonical interface for new host software and
future features. Version 1 uses a bounded, delimiter-based binary frame:

```text
COBS {
    version:u8 | device_address:u8 | sequence:u16 |
    message_type:u8 | command:u16 | payload_length:u8 |
    payload:0..64 bytes | crc16:u16
} 00
```

All multi-byte values are big-endian. CRC-16/CCITT-FALSE covers the decoded
frame from `version` through the final payload byte: polynomial `0x1021`,
initial value `0xFFFF`, no reflection, and no final XOR. The two-byte CRC is
stored high byte first. The COBS-encoded frame ends with one `0x00` delimiter.
The frame `version` is the protocol major version and is currently `1`;
compatible minor revisions are reported by `GET_IDENTITY`.

The `COMMISSIONING` and `CURRENT_TEST` names in protocol 1.3 are retained wire
compatibility labels. They denote bounded product diagnostics, not a separate
firmware personality or a bridge-authority owner. New protocol work should use
product motion, service, and diagnostic names while preserving these encodings
for compatible clients.

The decoded header is 8 bytes, the maximum decoded frame is 74 bytes, and the
maximum on-wire frame including the delimiter is 76 bytes. Frames with an
invalid COBS encoding, inconsistent length, bad CRC, unsupported version, or
excessive encoded length receive no response. After an oversized frame the
parser discards through the next delimiter and starts cleanly.

The `device_address` range is 1-247; firmware currently uses address 1.
Address 0 is broadcast. The initial read-only commands are not broadcast-safe,
so a valid broadcast is counted and dropped without dispatch or response.
Frames for another address and non-request message types are likewise ignored.

Message types are `1` request, `2` response, and `3` event. Requests carry a
16-bit sequence chosen by the host; a response echoes it. Version 1 does not
yet cache or reject duplicate sequences, so the host must wait for a response
or timeout before sending another request to a device.

Every response payload begins with one status byte:

| Value | Status |
| ---: | --- |
| 0 | Success |
| 1 | Unknown command |
| 2 | Invalid payload |
| 3 | Service unavailable |
| 4 | Internal error |

Only complete, version-1, CRC-valid requests for this address receive command
errors. This prevents noise, foreign traffic, broadcasts, responses, or events
from causing reply storms.

### Implemented native commands

| Command | Name | Request payload | Successful response after status |
| ---: | --- | --- | --- |
| `0x0001` | `PING` | 0-16 opaque bytes | The same bytes |
| `0x0002` | `GET_IDENTITY` | Empty | Product ID `u32`, firmware major `u8`, minor `u8`, patch `u16`, protocol major `u8`, minor `u8` |
| `0x0003` | `GET_CAPABILITIES` | Empty | Capability bitmap `u32` |
| `0x0100` | `GET_COMMISSIONING_STATUS` | Empty | Schema-2 commissioning status block |
| `0x0101` | `CONFIGURE_CURRENT_TEST` | Amplitude counts `u16`, frequency millihertz `u32` | Applied amplitude counts `u16`, frequency millihertz `u32` |
| `0x0102` | `START_CURRENT_TEST` | Initial leg `u8`, duration milliseconds `u32` | Empty |
| `0x0103` | `STOP_CURRENT_TEST` | Empty | Empty |
| `0x0104` | `GET_BOOT_STATUS` | Empty | Schema `u8`, RCC reset flags `u32`, retained panic `u8`, uptime milliseconds `u32` |
| `0x0105` | `GET_ENCODER_STATUS` | Empty | Schema-2 raw encoder, mechanical estimator, alignment, electrical-phase, and scheduling block described below |
| `0x0106` | `GET_CURRENT_TRACE` | Sample index `u16` | Schema `u8`, captured count `u16`, echoed index `u16`, loop sample count `u32`, A/B references `i16`, A/B measurements `i16`, A/B voltage commands `i16` |
| `0x0200` | `START_ALIGNMENT` | Requested current counts `u16` | Empty |
| `0x0201` | `GET_ALIGNMENT_STATUS` | Empty | Schema-1 automatic-alignment status block described below |
| `0x0202` | `STOP_DRIVE` | Empty | Empty |
| `0x0300` | `GET_CONFIGURATION_STATUS` | Empty | Schema-1 persistent/active configuration status block described below |
| `0x0301` | `SAVE_CONFIGURATION` | Empty | Empty |
| `0x0302` | `CLEAR_CALIBRATION` | Empty | Empty |
| `0x0400` | `START_ALIGNED_TORQUE` | Signed q-current counts `i16`, duration milliseconds `u32` | Empty |
| `0x0401` | `GET_ALIGNED_TORQUE_STATUS` | Empty | Schema-1 aligned-torque state, evidence, and complete policy block described below |
| `0x0500` | `START_VELOCITY` | Signed target mechanical velocity Q16.16 rev/s `i32`, positive q-current limit counts `u16`, duration milliseconds `u32` | Empty |
| `0x0501` | `GET_VELOCITY_STATUS` | Empty | Schema-1 velocity state, evidence, and policy block described below |
| `0x0600` | `START_POSITION_RELATIVE` | Signed relative displacement Q16.16 revolutions `i32`, positive maximum velocity Q16.16 rev/s `i32`, positive maximum acceleration Q16.16 rev/s² `i32`, positive q-current limit counts `u16`, duration milliseconds `u32` | Empty |
| `0x0601` | `GET_POSITION_STATUS` | Empty | Schema-1 position state, evidence, and policy block described below |

The product ID is `0x4D4B5335` (`MKS5`). Firmware 0.19.0 / protocol 1.3 is the
bench-proven converged supervisor image. Firmware 0.20.0 / protocol 1.4 appends
mechanical-estimator, alignment, electrical-phase, and sample-interval telemetry
to `GET_ENCODER_STATUS`; the current host tool decodes both schema 1 and schema
2, while fixed-length third-party clients must check identity/schema before
decoding. Firmware 0.21.0 / protocol 1.5 adds the bounded automatic-alignment
service and a generic STOP operation; successful, repeatable alignment and STOP
are bench-proven on the tested motor. Firmware 0.22.0 / protocol 1.6 adds the
versioned dual-slot configuration record and its production service commands;
its host, target, reset, power-cycle, persistent-clear, and wear-avoidance gates
pass. Firmware 0.23.2 / protocol 1.7 provides the first production motion
interface: signed encoder-aligned q-current through the proven A/B current
backend with independent current, slew, velocity, acceleration, feedback-age,
and duration contracts. Firmware 0.25.1 / protocol 1.8 provides signed bounded
velocity with an acceleration-limited reference, PI-generated q-current,
per-command current limit, finite deadline, status, and generic STOP through
that same actuator. Positive velocity uses the same mechanical coordinate as
encoder telemetry; the persisted alignment direction maps controller effort to
q-current without changing the protocol. Firmware 0.26.0 / protocol 1.9 adds
bounded relative-position trajectories above that same velocity/current
actuator and raises the velocity evaluation ceiling to 4 rev/s. It is host-
and Arm-build validated and awaits its staged hardware gate. Protocol 1.3 added the bounded current trace validated
through complete 256-sample, fault-free 20 kHz captures and the Kp=2 tuning sweep. The
capability bitmap uses the same stable bit definitions as the debugger
diagnostic record, including the native-protocol capability.

The current-loop commands are the present low-level motor-diagnostic service;
they are not a velocity or position protocol. `CONFIGURE_CURRENT_TEST` is accepted only while
inactive. Amplitude is currently bounded to 1-495 ADC counts and frequency to
1-250000 millihertz. `START_CURRENT_TEST` accepts leg values `0=A1`, `1=A2`,
`2=B1`, and `3=B2`, with a duration from 3 to 2147483647 ms. It is unavailable
until the product supervisor reaches `READY` from calibrated current feedback,
initialized current control, and a healthy encoder sample, or while authority
is already active, a fault is latched, or the physical Right button is asserted. START requests
diagnostic authority from the supervisor before the backend can switch.
`STOP_CURRENT_TEST` remains a wire-compatible alias for generic stop behavior.
`STOP_DRIVE` is the preferred name and is always accepted; either operation
stops a current diagnostic, alignment, aligned-torque, velocity, or position operation before
releasing its authority. A remote run also stops at its deadline, on the physical Right button, or on an
RS-485 transport failure. Foreground parsing continues during a run so status
and STOP remain usable.

`START_ALIGNMENT` is accepted only from supervisor `READY`, with current and
encoder readiness intact, Right button released, no fault, no active/pending current
diagnostic, and no existing alignment operation. The requested current is
currently bounded to 50-495 ADC counts. The controller applies `(A=+I,B=0)`,
then `(A=0,B=+I)`, then `(A=+I,B=0)` through the production current backend
under `ALIGN` motion authority. It samples only settled encoder/current data,
checks the observed quarter-step geometry and final closure, and transactionally
commits zero and direction only after the whole sequence passes. Failed or
aborted attempts preserve an earlier valid calibration.

After a successful alignment, firmware first stops the current backend and
releases motion authority, then automatically persists the accepted motor
geometry, electrical zero, observed quarter step, error, and direction. An
unchanged calibration causes no Flash erase or program operation. A storage
failure does not invalidate the accepted RAM calibration for the current boot;
configuration status reports the failure and that the active calibration does
not match the stored record.

The initial 50-count minimum is an alignment policy candidate based on the
repeatable 303 mA cardinal test; 165 counts is the existing current-backend
request ceiling, not a hardware capability claim. The initial observation
policy is 750 ms settle per state, 100 ms sample windows with at least 64
samples, a 4 s total deadline, at most 8 raw counts of within-window span, 12
counts of return-closure error, and 8 counts of current-tracking error. These
values are explicitly subject to bench tuning and the project-wide limit
inventory; they are not motor speed, current, or physical travel limits.

`START_ALIGNED_TORQUE` is accepted only from supervisor `READY` with a valid
persisted or newly accepted alignment, healthy timestamped encoder feedback,
initialized current control, Right button released, no fault, and no other active or
pending drive operation. It enters `RUN` with motion authority and starts the
20 kHz backend at zero reference. Every accepted 1 kHz encoder sample maps the
signed q-current to electrical phase plus 90 degrees and slews the resulting A/B
phase references through the same bounded current PI and bridge shutdown path
used by alignment and the production diagnostic. Positive q-current maps to
`A=-Iq*sin(theta), B=Iq*cos(theta)` under the accepted motor convention.

The 0.23.2 evaluation policy is ±495 counts (±2.999 A nominal on the tested
current front end), 10,000 counts/s current slew (about 60.59 A/s), 5 mechanical
rev/s (300 RPM), 1,000 rev/s² observed acceleration, at most 2,000 us between
accepted feedback samples, and an explicit
3 through 2,147,483,647 ms finite duration. The three-millisecond minimum allows
one pre-deadline reference update even if the first accepted feedback arrives
at the full two-millisecond timing limit. The duration maximum is the largest
interval for which the signed modulo-32-bit deadline comparison remains
unambiguous; it is not a thermal, current, or communications-lease policy. The
current point matches the attached motor's reported 3 A rating and opens a
medium-capability evaluation envelope above the 757.4 mA bench-proven point;
it is not yet a qualified continuous-current rating. The current, speed,
acceleration, and slew values are evaluation permissions deliberately ahead of
the validated envelope. They are independently enforced and reported, but are
not performance guarantees or hardware/motor capability claims. Deadline and
generic STOP release motion
authority; invalid phase/timing, overspeed, overacceleration, backend loss,
reference rejection, current-loop fault, or readiness loss enters the common
fault/ZERO path.

`GET_ALIGNED_TORQUE_STATUS` returns this 62-byte schema-1 body after the common
status byte. All signed values use two's complement and all multi-byte fields
are big-endian.

| Body offset | Type | Aligned-torque schema-1 field |
| ---: | --- | --- |
| 0 | `u8` | Schema version, currently 1 |
| 1 | `u8` | State: idle, ramping, holding, complete, stopped, or failed |
| 2 | `u8` | Result: none, deadline, stopped, phase invalid, feedback timing, overspeed, overacceleration, backend inactive, or reference rejected |
| 3 | `u8` | Active/authority/backend/alignment/phase/demand-at-target flags |
| 4 | `u32` | Aligned-torque fault flags |
| 8 | `i16` | Requested q-current counts |
| 10 | `i16` | Applied, slew-limited q-current counts |
| 12 | `i16,i16` | Applied A/B phase-current references |
| 16 | `u32` | Latest calibrated electrical phase, Q0.32 turns |
| 20 | `i32` | Mechanical velocity, Q16.16 rev/s |
| 24 | `i32` | Absolute mechanical acceleration, Q16.16 rev/s² |
| 28 | `u32` | Elapsed milliseconds |
| 32 | `u32` | Remaining milliseconds while active |
| 36 | `u16` | Maximum absolute q-current counts |
| 38 | `u16` | Maximum q-current slew, counts/s |
| 40 | `i32` | Maximum absolute mechanical velocity, Q16.16 rev/s |
| 44 | `i32` | Maximum absolute mechanical acceleration, Q16.16 rev/s² |
| 48 | `u16` | Maximum accepted feedback interval, microseconds |
| 50 | `u32` | Minimum duration milliseconds |
| 54 | `u32` | Maximum duration milliseconds |
| 58 | `u32` | Current-backend fault flags |

`START_VELOCITY` is accepted only from supervisor `READY` under the same
alignment, encoder, current-backend, Right-button, and exclusivity gates as aligned
q-current. It enters `RUN`, starts the aligned actuator at zero q-current, and
then executes once for every newly accepted 1 kHz rotor observation. The
controller slews its velocity reference independently, applies a PI controller
with anti-windup at the caller's current limit, and updates the existing
slew-limited aligned-q-current target. It has no direct bridge-register or PWM
path. Deadline completes normally; invalid/timed-out feedback, observed
overspeed, numeric failure, actuator failure, current-backend failure, or
readiness loss converges on fault/ZERO. Generic STOP and the physical Right button perform the
ordinary stopped release path.

The 0.26.0 evaluation policy accepts a nonzero target through ±4 rev/s,
a positive per-command limit through 100 current counts (about 606 mA nominal),
and a 3 through 2,147,483,647 ms finite duration. The reference is limited to
4 rev/s². Observed velocity is independently bounded to 5 rev/s, feedback age
to 2,000 us, and the downstream actuator still independently enforces its
current slew, speed, acceleration, phase, backend, and deadline contracts. The
initial PI gains are Kp 100 current counts/(rev/s) and Ki 200 current
counts/rev. These are bench candidates and not final motor-independent defaults.

`GET_VELOCITY_STATUS` returns this 62-byte schema-1 body after the common status
byte. All signed values use two's complement and all multi-byte fields are
big-endian.

| Body offset | Type | Velocity schema-1 field |
| ---: | --- | --- |
| 0 | `u8` | Schema version, currently 1 |
| 1 | `u8` | State: idle, ramping, tracking, complete, stopped, or failed |
| 2 | `u8` | Result: none, deadline, stopped, invalid feedback, feedback timing, overspeed, internal numeric, or actuator fault |
| 3 | `u8` | Active/authority/backend/alignment/actuator/reference-at-target/current-at-limit flags |
| 4 | `u32` | Velocity-controller fault flags |
| 8 | `i32` | Requested target velocity, Q16.16 rev/s |
| 12 | `i32` | Acceleration-limited reference velocity, Q16.16 rev/s |
| 16 | `i32` | Measured velocity, Q16.16 rev/s |
| 20 | `i16` | Velocity PI q-current request, counts |
| 22 | `i16` | Applied slew-limited q-current, counts |
| 24 | `u16` | Per-command q-current limit, counts |
| 26 | `u32` | Elapsed milliseconds |
| 30 | `u32` | Remaining milliseconds while active |
| 34 | `i32` | Maximum target velocity, Q16.16 rev/s |
| 38 | `i32` | Maximum target-reference acceleration, Q16.16 rev/s² |
| 42 | `i32` | Maximum observed feedback velocity, Q16.16 rev/s |
| 46 | `u16` | Maximum per-command q-current limit, counts |
| 48 | `u16` | Maximum accepted feedback interval, microseconds |
| 50 | `i32` | PI proportional gain, Q16.16 current counts/(rev/s) |
| 54 | `i32` | PI integral gain, Q16.16 current counts/rev |
| 58 | `u32` | Maximum duration milliseconds; minimum is 3 ms in protocol 1.8 |

`START_POSITION_RELATIVE` is accepted only from supervisor `READY`, with valid
persisted alignment, a healthy encoder/current backend, the Right button
released, no pending or active drive operation, and measured speed no greater
than 0.1 rev/s. The profile begins at the newly accepted unwrapped mechanical
position and velocity, generates a bounded trapezoidal reference, and adds a
bounded position correction to the profile velocity. That dynamic target feeds
the existing acceleration-limited velocity PI and aligned-q-current actuator;
position control has no alternate estimator, current loop, PWM, or bridge path.

The 0.26.0 policy permits nonzero relative displacement through ±100
revolutions, maximum trajectory velocity through 4 rev/s, acceleration through
4 rev/s², q-current through 100 counts, and a finite 100 through
2,147,483,647 ms deadline. Feedback is independently limited to 5 rev/s and
2,000 us age. Following error greater than 0.25 revolution, invalid or stale
feedback, numeric failure, actuator/backend failure, or readiness loss faults
and converges on `ZERO`. Completion requires the reference profile at target,
measured position within 0.002 revolution, and measured speed within 0.02
rev/s for 50 consecutive samples. Deadline expiration releases authority
normally but reports result `deadline`, not successful `settled`. Generic STOP
and the physical Right button report `stopped` and release normally.

`GET_POSITION_STATUS` returns this 62-byte schema-1 body after the common
status byte. All signed values use two's complement and all multi-byte fields
are big-endian.

| Body offset | Type | Position schema-1 field |
| ---: | --- | --- |
| 0 | `u8` | Schema version, currently 1 |
| 1 | `u8` | State: idle, moving, settling, complete, stopped, or failed |
| 2 | `u8` | Result: none, settled, deadline, stopped, invalid feedback, feedback timing, following error, internal numeric, or actuator fault |
| 3 | `u8` | Active/authority/backend/alignment/velocity/profile-at-target/target-settled/current-at-limit flags |
| 4 | `u32` | Position-controller fault flags |
| 8 | `i32` | Absolute target position, Q16.16 revolutions |
| 12 | `i32` | Profile reference position, Q16.16 revolutions |
| 16 | `i32` | Measured position, Q16.16 revolutions |
| 20 | `i32` | Profile reference velocity, Q16.16 rev/s |
| 24 | `i32` | Corrected velocity-controller target, Q16.16 rev/s |
| 28 | `i32` | Measured velocity, Q16.16 rev/s |
| 32 | `i16` | Velocity PI q-current request, counts |
| 34 | `i16` | Applied slew-limited q-current, counts |
| 36 | `u16` | Per-command q-current limit, counts |
| 38 | `u32` | Elapsed milliseconds |
| 42 | `u32` | Remaining milliseconds while active |
| 46 | `i32` | Maximum relative travel, Q16.16 revolutions |
| 50 | `i32` | Maximum trajectory velocity, Q16.16 rev/s |
| 54 | `i32` | Maximum trajectory acceleration, Q16.16 rev/s² |
| 58 | `i32` | Maximum following error, Q16.16 revolutions |

`GET_BOOT_STATUS` exposes the complete captured RCC reset-flag mask rather
than only the IWDG summary in commissioning status. This distinguishes RAM,
MMU, pin, power-on, software, independent/window-watchdog, and low-power reset
causes and reports the current boot uptime.

`GET_ENCODER_STATUS` preserves the schema-1 raw encoder prefix and, in schema 2,
adds the product mechanical estimator, alignment gate, electrical phase, and
sampling evidence. Position and velocity use signed Q16.16 revolutions and
revolutions/second. Electrical phase uses unsigned Q0.32 turns. Until the
controlled alignment procedure accepts calibration, alignment/electrical-phase
flags remain clear and their numeric fields must not be used for control.

| Body offset | Type | Encoder schema-2 field |
| ---: | --- | --- |
| 0 | `u8` | Schema version, currently 2 |
| 1 | `u8` | `mt6816_status_t` |
| 2 | `u8` | `spi_status_t` |
| 3 | `u16` | Latest accepted raw angle |
| 5 | `u8` | MT6816 sensor flags |
| 6 | `u32` | Accepted raw-sample count |
| 10 | `u32` | Raw acquisition error count |
| 14 | `u32` | Last-attempt milliseconds |
| 18 | `u8` | Estimator-ready, alignment-valid, and electrical-phase-valid flags |
| 19 | `i32` | Unwrapped mechanical position, Q16.16 revolutions |
| 23 | `i32` | Filtered mechanical velocity, Q16.16 revolutions/second |
| 27 | `u32` | Estimator sample timestamp in microseconds, wrapping naturally |
| 31 | `u32` | Estimator fault flags |
| 35 | `u16` | Accepted electrical-zero raw count |
| 37 | `i8` | Encoder direction for increasing electrical phase: `-1`, `0` unknown, `+1` |
| 38 | `u32` | Electrical phase, Q0.32 turns; valid only when flagged |
| 42 | `u32` | Latest estimator sample interval in microseconds |
| 46 | `u32` | Maximum estimator sample interval observed since boot |

`GET_ALIGNMENT_STATUS` returns this schema-1 body after the common status byte.
All multi-byte values are big-endian and signed values use two's complement.

| Body offset | Type | Alignment schema-1 field |
| ---: | --- | --- |
| 0 | `u8` | Schema version, currently 1 |
| 1 | `u8` | Controller state |
| 2 | `u8` | Terminal/result code |
| 3 | `u8` | Active, calibration-valid, authority-active, and backend-active flags |
| 4 | `u16` | Requested alignment current in ADC counts |
| 6 | `u16` | Averaged phase-zero raw encoder count |
| 8 | `u16` | Averaged positive-quarter raw encoder count |
| 10 | `u16` | Averaged return-to-zero raw encoder count |
| 12 | `u16` | Observed absolute quarter-step magnitude |
| 14 | `i16` | Observed minus expected quarter-step error |
| 16 | `i16` | Signed return-closure error |
| 18 | `i8` | Encoder direction for increasing electrical phase |
| 19 | `u16` | Samples accepted in the active observation window |
| 21 | `u32` | Attempt elapsed time in milliseconds |
| 25 | `u32` | Time remaining before the attempt deadline |
| 29 | `u16` | Minimum alignment current in ADC counts |
| 31 | `u16` | Maximum alignment current in ADC counts |
| 33 | `u16` | Expected rounded quarter-step movement in encoder counts |
| 35 | `u16` | Maximum quarter-step error in encoder counts |
| 37 | `u32` | Settle duration per commanded phase in milliseconds |
| 41 | `u32` | Observation-window duration in milliseconds |
| 45 | `u32` | Whole-attempt deadline in milliseconds |
| 49 | `u16` | Minimum samples per observation window |
| 51 | `u16` | Maximum raw span within a window in encoder counts |
| 53 | `u16` | Maximum return-closure error in encoder counts |
| 55 | `u16` | Maximum per-axis current-tracking error in ADC counts |

States are 0 idle, 1 phase-zero settle, 2 phase-zero sample, 3 quarter settle,
4 quarter sample, 5 return settle, 6 return sample, 7 complete, 8 failed, and
9 aborted. Result codes are 0 none, 1 success, 2 aborted, 3 deadline, 4 encoder
invalid, 5 backend inactive, 6 current tracking, 7 encoder unstable, 8 geometry,
and 9 closure. Flag bits are 0 active, 1 calibration valid, 2 authority active,
and 3 backend active. The status body is 57 bytes and the complete successful
response payload is 58 bytes. Hosts should preflight from these reported policy
fields rather than duplicating firmware constants; firmware still validates
every request independently.

`GET_CONFIGURATION_STATUS` returns this schema-1 body after the common status
byte. The stored and active halves are both reported so a host can distinguish
a boot-restored calibration, an unsaved active calibration, a persistent clear,
and a record whose motor geometry is incompatible with the running firmware.

| Body offset | Type | Configuration schema-1 field |
| ---: | --- | --- |
| 0 | `u8` | Schema version, currently 1 |
| 1 | `u8` | Store/record/calibration/match/slot/write flags |
| 2 | `u8` | Last store result: 0 OK, 1 empty, 2 invalid argument, 3 I/O error, 4 verify error |
| 3 | `u8` | Active slot, 0 or 1; `0xFF` when no valid record exists |
| 4 | `u16` | Configuration-record schema supported by this firmware |
| 6 | `u32` | Active record generation |
| 10 | `u16` | Stored encoder counts per mechanical revolution |
| 12 | `u16` | Stored electrical cycles per mechanical revolution |
| 14 | `u16` | Stored electrical-zero raw encoder count |
| 16 | `u16` | Stored observed quarter-step magnitude |
| 18 | `i16` | Stored observed-minus-expected quarter-step error |
| 20 | `i8` | Stored encoder direction |
| 21 | `u16` | Active encoder counts per mechanical revolution |
| 23 | `u16` | Active electrical cycles per mechanical revolution |
| 25 | `u16` | Active electrical-zero raw encoder count |
| 27 | `u16` | Active observed quarter-step magnitude |
| 29 | `i16` | Active observed-minus-expected quarter-step error |
| 31 | `i8` | Active encoder direction |

Flag bits are 0 store initialized, 1 valid record selected, 2 stored
calibration valid, 3 active calibration valid, 4 active configuration exactly
matches the record, 5 slot 0 valid, 6 slot 1 valid, and 7 writes supported.
The status body is 32 bytes and the complete successful response payload is 33
bytes.

`SAVE_CONFIGURATION` is accepted only with a valid active alignment and while
the supervisor has no authority, the current backend, alignment controller,
and aligned-torque controller
are inactive, no start/stop request is pending, and the supervisor is in
`READY` or `DIAGNOSTIC`. `CLEAR_CALIBRATION` has the same safe-state gate; it
first commits a newer record with calibration invalid, then clears the RAM
alignment. A failed clear therefore leaves the previous active calibration
untouched. Both actions are idempotent at the configuration layer, so retrying
an already-applied request does not consume another erase cycle despite native
v1 not yet caching sequence numbers.

The N32L406 storage backend reserves Flash pages 62 and 63 as alternating 2 KiB
slots. Each record carries magic, schema, length, generation, semantic range
checks, and CRC-32; the commit word is programmed last. The previously selected
slot is never erased until a complete newer slot verifies, so reset or power
loss during an update falls back to the older record. Flash programming is a
foreground maintenance operation on this single-bank device and is performed
only after the bridge is forced to `ZERO`; no authority, lease, pending command,
fault state, or current-sensor startup zero is persisted.

`GET_CURRENT_TRACE` is available only after current-loop authority has ended.
Each start clears a fixed 256-entry buffer, then the DMA-completion ISR records
the first 12.8 ms of 20 kHz loop sample number, references, measurements, and
voltage commands. The request reads one indexed entry; every response reports
the fixed captured count so the host can reject a trace that changes while it
is being transferred. Trace capture does not alter current reference, voltage,
duty, duration, deadline, fault, or bridge-authority limits.

`GET_COMMISSIONING_STATUS` returns the following schema-2 body after the common
status byte. All multi-byte fields are big-endian; signed fields use two's
complement.

| Body offset | Type | Field |
| ---: | --- | --- |
| 0 | `u8` | Schema version, currently 2 |
| 1 | `u32` | Readiness, supervisor authority, pending-action, and fault flags; `FAULT_PRESENT` covers either a current-backend fault or product-supervisor `FAULT` |
| 5 | `u8` | Raw electrical input levels; clear means asserted |
| 6 | `u8` | Debounced electrical input levels; clear means asserted |
| 7 | `u8` | `adc1_status_t` |
| 8 | `u8` | Selected initial leg |
| 9 | `u32` | Current-loop fault flags |
| 13 | `u32` | Completed current-loop sample count |
| 17 | `u16,u16` | Latest current A/B raw ADC values |
| 21 | `u16,u16` | Calibrated current A/B zero values |
| 25 | `i16,i16` | Current A/B references in ADC counts |
| 29 | `i16,i16` | Current A/B measurements in ADC counts |
| 33 | `i16,i16` | Phase A/B controller commands in permille |
| 37 | `u16[4]` | A1, A2, B1, B2 duties in permille |
| 45 | `u16` | Configured test amplitude in ADC counts |
| 47 | `u16` | Maximum configurable test amplitude |
| 49 | `u16` | Raw hard-current limit in ADC counts |
| 51 | `u16` | Phase-voltage limit in permille |
| 53 | `u32` | Configured test frequency in millihertz |
| 57 | `u32` | Remaining remote-run time in milliseconds |
| 61 | `u8` | Panic code retained across the last watchdog reset |
| 62 | `u8` | One if the current boot followed an IWDG reset |

Input-level bits retain their established wire positions: bit 2 is the Left
button (PA15), bit 0 is Center (PB8), and bit 1 is Right (PB9). The host tools
render them in physical left/center/right order. Bits 3-7 remain M_IN1, M_IN2,
step, direction, and enable. Renaming the three unlabeled buttons does not change
the schema or protocol version.

Commissioning flag bits are: bit 0 ADC ready, 1 ADC snapshot valid, 2 zero
calibration ready, 3 current loop initialized, 4 bridge ready, 5 authority
active, 6 ISR backend active, 7 remote authority, 8 remote start pending,
9 remote stop pending, and 10 fault present. The status body is 63 bytes and
the complete successful response payload is 64 bytes.

| Bit | Capability |
| ---: | --- |
| 0 | Product firmware image |
| 1 | Status LED |
| 2 | Foreground-supervised IWDG |
| 3 | Reset-cause capture |
| 4 | Configured NVIC priority policy |
| 5 | Encoder SPI acquisition |
| 6 | RS-485 RX/TX DMA transport |
| 7 | Native v1 protocol |
| 8 | SSD1306-compatible I2C display |
| 9 | Raw ADC acquisition, including timer-synchronous current capture |
| 10 | Debounced passive-input monitor |
| 11 | Supervisor-authorized rotating-current diagnostic capability |
| 12 | TIM3 bridge PWM and bounded current-loop operation |
| 13 | Bounded production automatic alignment |
| 14 | Versioned dual-slot persistent motor configuration |
| 15 | Bounded encoder-aligned q-current operation |
| 16 | Bounded mechanical-velocity control |
| 17 | Bounded relative-position control |

Golden request vectors below use device address 1, sequence 1, and empty
payloads. Each row is a complete on-wire frame including the final delimiter:

| Command | Hex bytes |
| --- | --- |
| `PING` | `03 01 01 03 01 01 02 01 03 21 58 00` |
| `GET_IDENTITY` | `03 01 01 03 01 01 02 02 03 74 0B 00` |
| `GET_CAPABILITIES` | `03 01 01 03 01 01 02 03 03 47 3A 00` |

The base frame preserves these required properties:

- reliable resynchronization after noise, truncation, or an RX ring overrun;
- explicit protocol version, payload length, and request sequence;
- distinct request, response, and asynchronous-event message types;
- bounded fixed-size parsing with no dynamic allocation;
- device address `0` reserved for broadcasts that never receive replies;
- capability discovery so hosts do not infer features from firmware versions;
- command acceptance reported separately from later motion completion; and
- no dependence on C structure layout or the debugger diagnostic ABI.

### Modbus RTU

Modbus RTU is an optional standards-oriented integration profile for PLCs,
automation software, and generic service tools. It maps registers and command
mailboxes to the same internal service used by the native protocol.

The project-owned register map should use standard function and exception
behavior, clearly separate read-only status from writable configuration, and
provide coherent blocks where practical. The sparse Makerbase D-series map may
be offered as compatibility aliases, but it is not the canonical internal data
model.

Modbus framing requires an explicit receive-silence policy. USART IDLE is a
boundary hint; the foreground framer owns the required timing and CRC checks.

### Makerbase compatibility

The publicly documented Makerbase `FA`/`FB` RS-485 protocol is an optional
legacy profile for existing controllers and host tools. Compatibility applies
only to commands deliberately listed and tested by this project. It does not
authorize reproducing undocumented firmware or bootloader behavior.

The first compatibility subset should be read-only and low risk: identity,
version, encoder/status queries, application state, and faults. Configuration,
homing, enable, speed, position, synchronization, and calibration aliases are
added only when the corresponding internal service and safety contract exist.
Firmware-upgrade and bulk-write commands remain separate deferred decisions.

Native extensions must not be added to the Makerbase frame namespace. That
format has command-dependent implicit lengths, an 8-bit additive checksum, and
response behavior that is awkward to version and resynchronize.

Protocol mode is selected explicitly through safe configuration or a local
interface. The firmware will not guess among native, Modbus, and legacy modes
from arbitrary received bytes.

## Common behavioral and safety contract

All adapters are untrusted-input boundaries and follow the same rules:

- DMA moves bytes only. Framing, CRC, address checks, validation, dispatch, and
  reply creation execute as bounded foreground work.
- Malformed, unsupported, out-of-range, or unauthorized commands have no motor
  effect and never refresh a remote-control lease.
- A remote motion lease is refreshed only by a newly accepted command from its
  current owner or an explicit newly accepted `KEEPALIVE`. An exact retry is
  idempotent and does not refresh the lease. Lease expiry requests a bounded
  trajectory stop and disables control after the stopped trajectory and
  measured motion satisfy their completion tolerances.
- Configuration that affects control or safety is immutable while running and
  changes only through a validated safe-state transaction. Same-bank Flash
  maintenance is permitted only with bridge authority absent and the backend
  inactive; boot restore never restores authority or an active operation.
- Broadcast commands never produce replies. Broadcast motion is accepted only
  for operations explicitly declared broadcast-safe.
- Multidrop RS-485 telemetry is polled or granted a transmission slot. Devices
  do not emit arbitrary periodic traffic that can collide with another slave.
- A staged-motion plus broadcast-commit operation may provide multi-axis
  synchronization, but stale, duplicated, incomplete, or mismatched stages are
  rejected deterministically.
- Protocol commands request behavior; they never manipulate bridge GPIO,
  timer, ADC, or DMA registers directly.

### Implemented transport-independent motion contract

The portable application layer now implements the semantics below the wire
adapters:

- native, Modbus, Makerbase, step/direction, and local inputs share one motion
  authority; only its owner may change position targets;
- valid stop and disable requests remain available regardless of the current
  owner or the source's permission to initiate motion;
- the eight most recently accepted `(source, command_id)` identities are
  retained: an exact replay is a duplicate with no repeated effect, while the
  same identity with different content is a conflict;
- command acceptance is distinct from active and terminal completion, and
  status retains the two most recent terminal outcomes with source identity;
- a new explicit heartbeat supports moves longer than one lease interval; and
- step/direction is represented as timestamped cumulative hardware counts,
  with re-anchoring at enable and bounded count-rate validation.

These are application contracts, not new native-v1 wire commands. Command IDs,
payload encoding, status/event messages, permission configuration, and each
protocol adapter still need explicit mappings. The modules compile for the Arm
target; firmware 0.25.1 links the mechanical estimator, transactional alignment
controller, persistent configuration, aligned-q-current actuator, and the first
supervisor-authorized velocity operation. The general position/step-direction
motion shell remains excluded.

## Implementation sequence

1. **Complete:** define host-testable command request, response, error,
   capability, and dispatcher types with no wire-format dependency.
2. **In progress:** the COBS/CRC native v1 framer, discovery commands, boot and
   encoder telemetry, current-loop console, and safe motor-configuration
   transaction are implemented. The motor diagnostics are bench-proven;
   persistence passes its reset/power-cycle gate, and aligned q-current is
   bench-proven. Bounded velocity commands/status are implemented; their
   positive/negative deadline, STOP, Right-button stop, and hand-loaded
   saturation/recovery gates pass, completing initial velocity qualification.
   Physical feedback-loss injection is indefinitely deferred on the current
   assembly; automated common fault/ZERO coverage remains mandatory. Broader
   fuzz coverage remains.
3. Add the Modbus RTU adapter and project-owned register map to the same
   read-only services, followed by safe configuration transactions.
4. Add the documented Makerbase read-only compatibility subset and byte-level
   conformance tests from public manual examples.
5. **Application prerequisite complete:** the portable application shell,
   limits, command arbiter, retry history, lease, safe-stop, and completion
   behavior exist and have end-to-end simulated-plant tests. The hardware
   current-control state and automatic alignment are bench-proven, and
   alignment persistence passes its power-cycle gate, and the aligned torque
   and first native velocity paths are integrated. Protocol 1.9 now maps a
   focused relative-position service through that product path; its staged
   hardware gate is next. The broader lease/step-direction shell remains
   separately compiled.
6. Add telemetry scheduling, staged synchronization, compatibility matrices,
   final lease timing, and hardware-in-the-loop multidrop tests.

Every parser needs tests for valid frames, every truncated length, excessive
lengths, bad CRCs, noise before and between frames, unknown commands, invalid
addresses, broadcast reply suppression, duplicate sequences, RX overruns, and
TX-busy handling.

## Evidence from published manuals

The protocol split is based on these cached, hash-verified public documents:

- `servo57d-rs485-manual-v1.0.9`: custom framing and checksum on page 26;
  command and response behavior on pages 27-100.
- `servo57d-modbus-manual-v1.0.9`: Modbus communication constraints and
  supported functions on page 9; documented register operations on pages
  10-65.
- `servo57d-can-manual-v1.0.9`: CAN framing on page 25 and the corresponding
  command vocabulary on pages 26-98.

The manuals show that the custom RS-485 and CAN products reuse command meanings
through different transport wrappers, while the RS-485 product can select a
separate Modbus RTU mode. That makes a common semantic service with wire
adapters a natural clean-sheet boundary.

## Open specification decisions

Before native v1 grows beyond the read-only discovery slice, decide and record:

- address range, provisioning, and recovery behavior after a lost address;
- native motion command identifiers, data units, scaling, and mappings from
  wire sequences to application command identities;
- motion status/event payloads for acceptance, active state, and retained
  terminal results;
- the production lease duration and how it is provisioned;
- the project-owned Modbus register map and supported function codes; and
- the exact Makerbase compatibility matrix, baud options, and response modes.
