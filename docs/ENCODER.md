# MT6816 Encoder Bring-up

Status: firmware 0.23.0 schedules encoder reads at 1 kHz in foreground,
including during current-loop operation, assigns accepted samples microsecond
timestamps, and feeds the shared mechanical angle/velocity estimator and
automatic-alignment and aligned-torque controllers. Native protocol 1.7 exposes raw health,
unwrapped position, filtered velocity, estimator faults, alignment validity,
sample timing, alignment progress/result, and the aligned q-current phase and
motion-policy evidence. The 1 kHz schedule passed its
initial idle and active hardware regression; two automatic alignments reproduced
the accepted geometry and zero exactly, and STOP preserved the valid calibration.

## Evidence and confidence

The published RS-485 V1.1 schematic identifies U11 as `MT6816CT-ACD`, powered
from 3.3 V, and routes its four-wire interface to SPI1:

| Signal | MCU pin | Configuration |
| --- | --- | --- |
| `SPI_CLK` | PB3 | SPI1 SCK, AF1 |
| `SPI_MISO` | PB4 | SPI1 MISO, input AF1 |
| `SPI_MOSI` | PB5 | SPI1 MOSI, AF0 |
| `SPI_CS` | PB6 | Software-controlled active-low GPIO |

The mapping and four-wire behavior are bench-proven and are corroborated by an
independent open SERVO57D implementation. The fitted sensor's exact marking,
OTP configuration, and magnet geometry still need to be recorded.

The MT6816 Rev. 1.9 datasheet specifies 14-bit angle data, SPI mode 3
(`CPOL=1`, `CPHA=1`), MSB-first transfers, even parity across angle registers
`0x03` and `0x04`, a no-magnet warning in register `0x04`, and an over-speed
warning in register `0x05`. Typical sensor power-up time is 16 ms.

## Implemented transaction

`spi1_init()` configures a conservative 500 kHz-or-lower SPI1 clock. The
foreground waits 20 ms after initialization, then schedules one sample every
1 ms. Every sample is a single coherent four-byte burst:

```text
MOSI:  0x83  0x00  0x00  0x00
MISO:  ----  reg03 reg04 reg05
```

PB6 remains low for the entire burst. Fixed guard delays cover the datasheet's
chip-select setup and hold requirements at every supported core clock through
64 MHz. Each flag wait and the overall transfer length are bounded; SPI uses
neither DMA nor interrupts in this milestone.

The decoder accepts a sample only when the combined 16 bits from registers
`0x03` and `0x04` have even parity. The published raw angle is:

```text
angle_raw = (reg03 << 6) | (reg04 >> 2)
```

It ranges from 0 through 16383. Accepted unflagged samples are unwrapped into
mechanical revolutions and differentiated through the shared filtered velocity
estimator. The measured motor geometry is 50 electrical cycles per mechanical
revolution, and positive commanded electrical phase decreases encoder raw
count. Electrical phase remains invalid until the controlled alignment service
accepts a phase-zero and quarter-phase observation; no bench-specific raw zero
is compiled into the product.

## Foreground behavior

Encoder initialization and acquisition are active in normal boot. An SPI or
parity failure does not fail the boot ledger or stop watchdog service, but it
removes drive readiness and is a fault if authority is active. The foreground
records the failure and retries at the next sampling period. A
no-magnet or over-speed indication is retained as a sensor flag alongside the
decoded raw word; consumers must not treat a flagged angle as control-valid.

The 1 kHz reader is still cooperative foreground work. It reports its latest
and maximum accepted-sample intervals so the bench regression can decide from
evidence whether it is adequate or whether the planned timer-released SPI/DMA
capture is required. The estimator's 20 ms accepted-sample interval threshold
is a scheduling/feedback validity check, not a motor speed command limit. Its
20 revolutions/second plausibility threshold is 20 times the current diagnostic
mechanical-speed endpoint and is likewise not exposed as a motion set-point
ceiling; both values are part of the traceable-limit audit before release.

SPI initialization configures only PB3-PB6 for the encoder. Firmware 0.21.0
later claims PB0/PB1 and PA6/PA7 for current-loop PWM while separate
input monitoring reads PB7 `nEN` and configures PB8/PB9/PB12/PB13 and PA15 as
pulled-up inputs. Encoder acquisition continues throughout an active run.

## Diagnostic fields

The debugger schema-2 encoder prefix, retained unchanged in current schema 5, contains
the current MT6816 status, underlying SPI status,
last accepted raw angle, sensor flags, accepted-sample count, error count, and
last-attempt timestamp. Native protocol encoder schema 2 appends estimator
validity/faults, Q16.16 position and velocity, microsecond timestamp, alignment
zero/direction, Q0.32 electrical phase, and current/maximum sample intervals.
Status values are defined in `mks57d/mt6816.h`; SPI transport values are defined
in `mks57d/spi_bus.h`.

## Bench result and remaining validation

The displayed and native-protocol raw position tracks shaft motion, remains
stationary when the shaft is still, and rolls over repeatably at one mechanical
position per revolution. During the accepted 300 mA, 5 Hz electrical,
three-second run, angle moved smoothly from 9839 to 4993 over 2.973 seconds:
-0.2958 revolution and -5.97 RPM. This agrees with the expected 6 RPM for the
observed 50 electrical cycles per mechanical revolution.

At 757.5 mA, a cardinal sequence `A2 → B1 → A1 → B2 → A2` produced raw counts
`14249 → 14165 → 14085 → 14004 → 13923`: quarter steps of -84, -80, -81, and
-81 counts, totaling -326 versus -327.68 theoretical. This confirms 50
electrical cycles per revolution and negative raw direction for positive
electrical phase. The earlier 303 mA sequence independently produced -87, -78,
and -83 count steps.

Firmware 0.20.0 passed the initial 1 kHz estimator regression. One hundred
stationary protocol samples over five seconds showed raw angle fixed at 13926,
zero reported velocity, estimator-ready throughout, no fault, and a 5.162 ms
cumulative worst-case sample interval under polling load. During a bounded
757.4 mA / 20 electrical Hz run, 74 active telemetry observations reported
latest intervals of 981-1001 us (999.8 us mean) and a 5.450 ms cumulative worst
case. Estimator position changed -1.94934 revolutions over 4.974 seconds and
settled velocity averaged -0.3953 revolution/s, consistent with the independently
derived -23.51 RPM and the -24 RPM command. No encoder or estimator fault
occurred.

Remaining work is to test the no-magnet/readiness-loss and Menu-abort paths.
The bench-validated supervisor-owned automatic alignment procedure
establishes the per-motor electrical zero transactionally before motion can use
electrical phase; persistence across power cycles remains separate work.
Foreground acquisition remains subject to revalidation
after outer-loop compute is added; timer-released SPI/DMA remains available if
that later load makes its jitter unsuitable.
