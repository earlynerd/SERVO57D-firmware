# MT6816 Encoder Bring-up

Status: implemented as an active, bounded foreground reader and covered by
host-side protocol tests. It has not yet been exercised on the purchased
board.

## Evidence and confidence

The published RS-485 V1.1 schematic identifies U11 as `MT6816CT-ACD`, powered
from 3.3 V, and routes its four-wire interface to SPI1:

| Signal | MCU pin | Configuration |
| --- | --- | --- |
| `SPI_CLK` | PB3 | SPI1 SCK, AF1 |
| `SPI_MISO` | PB4 | SPI1 MISO, input AF1 |
| `SPI_MOSI` | PB5 | SPI1 MOSI, AF0 |
| `SPI_CS` | PB6 | Software-controlled active-low GPIO |

The mapping is high-confidence for the published schematic and is corroborated
by an independent open SERVO57D implementation. The fitted sensor identity,
OTP-selected three-wire/four-wire mode, alternate-function behavior, and
magnet geometry still need physical confirmation.

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

## Foreground and safety behavior

Encoder initialization and acquisition are active in normal boot, but an SPI
or parity failure does not fail the boot ledger or stop watchdog service. The
foreground records the failure and retries at the next sampling period. A
no-magnet or over-speed indication is retained as a sensor flag alongside the
decoded raw word; consumers must not treat a flagged angle as control-valid.

SPI initialization enables GPIOB only for PB3-PB6. A post-initialization board
invariant verifies that PB0/PB1 bridge controls, provisional PB7 `nEN`, and
PB9 `KEY_MENU` remain input/no-pull. GPIOA remains clock-gated, so PA6/PA7
bridge controls remain untouched. There is still no bridge-control API.

## Diagnostic fields

Diagnostic schema 2 appends the current MT6816 status, underlying SPI status,
last accepted raw angle, sensor flags, accepted-sample count, error count, and
last-attempt timestamp. Status values are defined in `mks57d/mt6816.h`; SPI
transport values are defined in `mks57d/spi_bus.h`.

## Bench validation

With the motor disconnected and bridge controls monitored:

1. Confirm the fitted device marking and 3.3 V supply.
2. Verify PB6 idles high, SCK idles high, the first command is `0x83`, and the
   clock is at or below 500 kHz.
3. Confirm one low-CS window contains all 32 clock edges and meets setup/hold
   timing.
4. Rotate the magnet slowly through a full revolution and check monotonic raw
   counts, wrap between 16383 and 0, direction, repeatability, and parity.
5. Remove or displace the magnet and confirm `MT6816_FLAG_NO_MAGNET` without a
   boot panic.
6. Halt and resume the debugger while observing PA6/PA7/PB0/PB1/PB7; none may
   leave its safe state.

Only after this proof should acquisition move into a timestamped rotor-feedback
ISR or DMA path and increase toward the proposed control-rate range.
