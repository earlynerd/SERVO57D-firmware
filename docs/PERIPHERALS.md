# Peripheral Bring-up Plan

This plan separates passive observation from energy-controlling timing. A
peripheral being implemented or compiled does not authorize it to be enabled
in the boot path. All pin assignments remain provisional until checked on the
purchased RS-485 board.

## Evidence summary

| Function | Candidate mapping | Confidence | Evidence and remaining proof |
| --- | --- | --- | --- |
| OLED bus | I2C1 AF7 on PA4/PA5 at 100 kHz; PB2 active-low reset; address `0x3C` | High for schematic routing; medium for fitted controller profile | The schematic shows PA4 `SCL`, PA5 `SDA`, external 4.7 kOhm pull-ups, and PB2 `lcdRES`. A ZJY042-7240TSWEG01 module datasheet identifies SSD1306, 72 by 40 pixels, active-low reset, and I2C write address `0x78` (7-bit `0x3C`). An independent open SERVO57D implementation uses the same I2C1 mapping. The fitted module identity still requires a bench check. |
| Bus voltage | PA3 ADC input | High for published schematic | The schematic routes the `vBus` divider to PA3. Divider values, ADC scaling, loading, and agreement with the purchased revision must be measured. |
| Winding current | PA1 `currentB`, PA2 `currentA` ADC inputs | High for published schematic | The schematic shows external GS8632 amplifiers feeding the pins. Offset, gain, sign, bandwidth, clipping, and timer-relative settling remain measurements. |
| Encoder | SPI magnetic encoder; schematic identifies MT6816CT-ACD | High for schematic identity; medium for fitted part and pin AF | The published schematic identifies the device and nets. The fitted marking, exact SPI pin alternate functions, mode, timing, status/parity behavior, direction, and magnet geometry require confirmation. |
| Bridge waveform | TIM3 channels 1-4 on PA6, PA7, PB0, PB1 | Medium-high | Schematic routing and public N32 work strongly support the mapping. Alternate functions, EG3013 behavior, ADC trigger placement, and shutdown semantics require register review and oscilloscope proof. |
| RS-485 | USART1 on PA9/PA10 with PA8 direction control | High for schematic routing | Direction polarity, reset state, turnaround timing, and transceiver behavior require loopback or adapter tests. |

## Implementation order

### 1. I2C and OLED transport

The project now compiles a bounded, polling I2C1 master transport and a
host-tested SSD1306-compatible command/frame layer. The candidate panel profile
uses the module datasheet's 1/40 multiplex, clock, contrast, VCOMH, internal-IREF,
and active-window settings: 72 by 40 visible pixels at columns 28-99 and pages
0-4. These settings remain configuration rather than being embedded in drawing
code.

This code is deliberately not called by the passive boot image yet. Therefore
GPIOA remains clock-gated and the existing boot self-test continues to prove
that PA6 and PA7 could not have been configured by the image. Activation will
be a separate hardware-gated change that:

1. verifies the purchased board and OLED flex/module;
2. scopes reset, SCL, and SDA during one bounded address/init transaction;
3. records ACK/NACK and timeout status in diagnostics;
4. revises the passive-board invariant to inspect PA6/PA7 directly before
   GPIOA is intentionally enabled.

Display refresh belongs in foreground housekeeping. It must not run from
SysTick, a control ISR, or any safety path.

### 2. Passive ADC acquisition

ADC work is split into two milestones:

- **Passive acquisition:** with the bridge disabled, software-trigger a bounded
  sequence for PA1, PA2, and PA3; publish raw samples, ADC status, and timestamps;
  then measure zero offsets, noise, divider scaling, and reference behavior.
- **Synchronous current acquisition:** only after PWM timing exists, trigger
  PA1/PA2 at a measured quiet point, use DMA or a bounded completion ISR, and
  reject missing, duplicate, clipped, or late sequences.

The passive milestone must not claim current in amperes or bus voltage in volts
until measured shunt gain, amplifier bias, divider ratio, and ADC reference are
captured as versioned calibration data.

### 3. Encoder SPI

Start with a slow, polling, read-only transaction while the bridge is disabled.
Once the fitted MT6816 and frame semantics are confirmed, add a timestamped
snapshot with status/error fields. DMA and the proposed 5-10 kHz acquisition
rate come only after basic transactions, wraparound, direction, and noise are
known.

### 4. Inputs and RS-485

Bring up buttons and isolated inputs as debounced observations. Bring up
USART1 receive first, then transmit with explicit PA8 direction timing and an
external adapter. Protocol parsing stays in foreground and may never command a
timer compare or bridge-enable register directly.

### 5. PWM and synchronous ADC

PWM is the last peripheral bring-up item because TIM3 is also the bridge
actuator. No bridge-output API is added until all of these are proven:

- PA6/PA7/PB0/PB1 alternate-function mapping;
- a single idempotent emergency-off mechanism and safe `nEN` behavior;
- EG3013 truth table, dead time, bootstrap, and reset behavior;
- preload/update behavior with a non-motor load or scoped gate-driver inputs;
- a deterministic ADC trigger that is independent of duty, or a deliberately
  designed dual-sample schedule;
- debugger halt, watchdog, clock failure, malformed commands, and missed
  control deadlines all converge on the same disabled state.

The first timing experiment should use edge-aligned TIM3 with an unambiguous
period boundary. Center-aligned PWM remains a later option after its two-update
behavior and sample symmetry are measured.
