# MT6816 Encoder Bring-up

Status: firmware 0.17.8 continuously reads the encoder at 100 Hz in foreground,
including during current-loop operation, and exposes it through diagnostics and
native protocol 1.2. Position is stable at rest, follows shaft motion, wraps
once per revolution, and has verified commanded motor rotation.

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
foreground waits 20 ms after initialization, then requests one sample every
10 ms. Every sample is a single coherent four-byte burst:

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

It ranges from 0 through 16383. No floating-point degree conversion,
direction inversion, unwrapping, velocity estimate, electrical alignment, or
zero offset is applied yet.

## Foreground behavior

Encoder initialization and acquisition are active in normal boot, but an SPI
or parity failure does not fail the boot ledger or stop watchdog service. The
foreground records the failure and retries at the next sampling period. A
no-magnet or over-speed indication is retained as a sensor flag alongside the
decoded raw word; consumers must not treat a flagged angle as control-valid.

SPI initialization configures only PB3-PB6 for the encoder. Firmware 0.17.8
later claims PB0/PB1 and PA6/PA7 for current-loop PWM while separate
input monitoring reads PB7 `nEN` and configures PB8/PB9/PB12/PB13 and PA15 as
pulled-up inputs. Encoder acquisition continues throughout an active run.

## Diagnostic fields

The schema-2 encoder prefix, retained unchanged in current schema 5, contains
the current MT6816 status, underlying SPI status,
last accepted raw angle, sensor flags, accepted-sample count, error count, and
last-attempt timestamp. Status values are defined in `mks57d/mt6816.h`; SPI
transport values are defined in `mks57d/spi_bus.h`.

## Bench result and remaining validation

The displayed and native-protocol raw position tracks shaft motion, remains
stationary when the shaft is still, and rolls over repeatably at one mechanical
position per revolution. During the accepted 300 mA, 5 Hz electrical,
three-second run, angle moved smoothly from 9839 to 4993 over 2.973 seconds:
-0.2958 revolution and -5.97 RPM. This agrees with the expected 6 RPM for the
observed 50 electrical cycles per mechanical revolution.

Remaining work is to record the fitted marking, quantify raw noise and
repeatability, define the positive direction and zero convention, test the
no-magnet flag, and measure SPI timing at higher acquisition rates. Alignment
will establish the encoder-to-electrical-angle offset before the position and
velocity loops use the signal.
