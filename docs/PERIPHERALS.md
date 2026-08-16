# Peripheral Bring-up Plan

This plan separates low-energy peripheral operation from energy-controlling
bridge timing. Safe peripherals may run during normal diagnostic boot as their
drivers mature; enabling one never authorizes bridge output. All pin
assignments remain provisional until checked on the purchased RS-485 board.

## Evidence summary

| Function | Candidate mapping | Confidence | Evidence and remaining proof |
| --- | --- | --- | --- |
| OLED bus | I2C1 AF7 on PA4/PA5 at 100 kHz; PB2 active-low reset; address `0x3C` | High for schematic routing; medium for fitted controller profile | The schematic shows PA4 `SCL`, PA5 `SDA`, external 4.7 kOhm pull-ups, and PB2 `lcdRES`. A ZJY042-7240TSWEG01 module datasheet identifies SSD1306, 72 by 40 pixels, active-low reset, and I2C write address `0x78` (7-bit `0x3C`). An independent open SERVO57D implementation uses the same I2C1 mapping. The fitted module identity still requires a bench check. |
| Bus voltage | PA3 ADC input | High for published schematic | The schematic routes the `vBus` divider to PA3. Divider values, ADC scaling, loading, and agreement with the purchased revision must be measured. |
| Winding current | PA1 `currentB`, PA2 `currentA` ADC inputs | High for published schematic | The schematic shows external GS8632 amplifiers feeding the pins. Offset, gain, sign, bandwidth, clipping, and timer-relative settling remain measurements. |
| Encoder | SPI1 on PB3 SCK, PB4 MISO, PB5 MOSI, PB6 software CS; schematic identifies MT6816CT-ACD | High for schematic routing and protocol; medium for fitted part | The schematic, MT6816 datasheet, Nations pin data, and independent board code agree on a four-wire mode-3 burst. The fitted marking, OTP mode, direction, signal integrity, and magnet geometry require confirmation. |
| Bridge waveform | TIM3 channels 1-4 on PA6, PA7, PB0, PB1 | Medium-high | Schematic routing and public N32 work strongly support the mapping. Alternate functions, EG3013 behavior, ADC trigger placement, and shutdown semantics require register review and oscilloscope proof. |
| RS-485 | USART1 AF4 on PA9/PA10 with PA8 direction control; DMA channels 4/5 | High for schematic and MCU routing; medium for reset behavior | SP485E `/RE` and `DE` are tied, proving low receive/high transmit. Active firmware and turnaround remain unverified; the direction pull-up appears to select transmit during reset. |

## Implementation order

### 1. I2C and OLED transport

The project now compiles a bounded, polling I2C1 master transport and a
host-tested SSD1306-compatible command/frame layer. The candidate panel profile
uses the module datasheet's 1/40 multiplex, clock, contrast, VCOMH, internal-IREF,
and active-window settings: 72 by 40 visible pixels at columns 28-99 and pages
0-4. These settings remain configuration rather than being embedded in drawing
code.

This code is deliberately not called by the diagnostic boot image yet. GPIOA
is now enabled later for RS-485, so the post-peripheral board invariant checks
PA6 and PA7 directly for input/no-pull state. OLED activation remains a
separate hardware-gated change that:

1. verifies the purchased board and OLED flex/module;
2. scopes reset, SCL, and SDA during one bounded address/init transaction;
3. records ACK/NACK and timeout status in diagnostics;
4. rechecks the PA6/PA7 invariant after display initialization.

Display refresh belongs in foreground housekeeping. It must not run from
SysTick, a control ISR, or any safety path.

### 2. Passive ADC acquisition

The project now compiles an inactive ADC layer for the schematic's PA1
`currentB`, PA2 `currentA`, and PA3 `vBus` inputs. It performs bounded,
software-triggered, single-channel conversions in that order and publishes a
host-tested all-or-nothing raw 12-bit sample. The layer is not called at boot,
so HSI and ADC remain clock-gated and PA1/PA2/PA3 remain input/no-pull.

Passive initialization uses HSI divided to the ADC's required 1 MHz timing
clock and a synchronous HCLK-derived sampling clock no faster than 2 MHz.
Scan, DMA, interrupts, internal channels, scaling, and timer triggers remain
disabled. The detailed register contract, timing assumptions, evidence
confidence, and activation checklist are in [Passive ADC bring-up](ADC.md).

ADC work remains split into two hardware milestones:

- **Passive acquisition:** with the bridge disabled, software-trigger a bounded
  sequence for PA1, PA2, and PA3; publish raw samples, ADC status, and
  timestamps; then measure zero offsets, noise, divider scaling, and reference
  behavior.
- **Synchronous current acquisition:** only after PWM timing exists, trigger
  PA1/PA2 at a measured quiet point, use DMA or a bounded completion ISR, and
  reject missing, duplicate, clipped, or late sequences.

The passive milestone must not claim current in amperes or bus voltage in volts
until measured shunt gain, amplifier bias, divider ratio, and ADC reference are
captured as versioned calibration data.

### 3. Encoder SPI

The boot path now activates a slow, polling, read-only MT6816 transaction while
the bridge remains disabled. SPI1 runs at 500 kHz or lower in mode 3 and reads
registers `0x03` through `0x05` in one four-byte CS window every 10 ms after a
20 ms power-up delay. The driver rejects odd parity and publishes no-magnet and
over-speed flags with raw 14-bit angle and status counters in diagnostic schema
2. Transport and parity failures remain non-fatal and are retried.

GPIOB activation is constrained to PB3-PB6; a post-init invariant checks that
PB0/PB1, PB7 `nEN`, and PB9 `KEY_MENU` remain input/no-pull. DMA, interrupts,
angle unwrapping, direction, calibration, and the proposed 5-10 kHz acquisition
rate come only after basic transactions, wraparound, and noise are measured.
See [MT6816 encoder bring-up](ENCODER.md).

### 4. Inputs and RS-485

Bring up buttons and isolated inputs as debounced observations. The diagnostic
image now actively configures USART1 for 115200 8N1, preloads PA8 low for
receive, and runs continuous circular RX DMA on channel 4. Foreground drains a
bounded number of bytes into the native v1 COBS/CRC parser. TX uses channel 5
and keeps PA8 high until USART transmission-complete, not merely DMA
completion.

The first protocol slice replies only to complete, CRC-valid address-1 ping,
identity, and capability requests. Framing, address checks, command validation,
and reply creation remain bounded foreground work and may never command a
timer compare or bridge-enable register directly. Adapter and oscilloscope
proof, including the reset-time direction pull-up, is specified in [USART1 /
RS-485 bring-up](RS485.md).

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
