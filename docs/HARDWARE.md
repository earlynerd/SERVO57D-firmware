# Hardware Notes

Primary schematic reference: Makerbase `MKS SERVO57D_485 V1.1_001`. Purchased boards must be checked for revision differences before relying on this document.

## High-level architecture

- N32L406CBL7 Cortex-M4F microcontroller.
- Two full H-bridges built from four EG3013 half-bridge gate drivers and discrete AP050N03Q MOSFETs.
- Two 20 mΩ low-side current shunts with external GS8632 amplifiers.
- SPI magnetic encoder identified as MT6816CT-ACD on the published schematic;
  the fitted device marking remains to be confirmed on hardware.
- RS-485 transceiver, isolated step/direction/enable inputs, isolated auxiliary I/O, display, buttons, and SWD header.

## Working pin map

This table is transcribed from the published schematic, not yet confirmed by continuity measurements.

| MCU pin | Schematic signal | Intended use |
| --- | --- | --- |
| PA1 | `currentB` | Amplified B-winding current ADC input |
| PA2 | `currentA` | Amplified A-winding current ADC input |
| PA3 | `vBus` | Divided bus-voltage ADC input |
| PA4 | `SCL` | Display/control bus clock |
| PA5 | `SDA` | Display/control bus data |
| PA6 | `phaseA1` | A bridge control |
| PA7 | `phaseA2` | A bridge control |
| PB0 | `phaseB1` | B bridge control |
| PB1 | `phaseB2` | B bridge control |
| PB2 | `lcdRES` | Display reset |
| PB3 | `SPI_CLK` | Encoder SPI1 clock, AF1 |
| PB4 | `SPI_MISO` | Encoder SPI1 data from sensor, input AF1 |
| PB5 | `SPI_MOSI` | Encoder SPI1 data to sensor, AF0 |
| PB6 | `SPI_CS` | Encoder software-controlled chip select |
| PB7 | `nEN` | Bridge/external enable; polarity and fan-out require measurement |
| PB8 | `KEY_ENTER` | Enter button input |
| PB9 | `KEY_MENU` | Menu button input |
| PB12 | `M_IN2` | Isolated auxiliary input |
| PB13 | `M_IN1` | Isolated auxiliary input |
| PB14 | `M_OUT1` | Isolated auxiliary output |
| PB15 | `M_OUT2` | Isolated auxiliary output |
| PA8 | `nDIR` | RS-485 direction control |
| PA9 | `TX` | RS-485 UART transmit |
| PA10 | `RX` | RS-485 UART receive |
| PA13 | `SWDIO` | Debug data |
| PA14 | `SWCLK` | Debug clock |
| PA15 | `KEY_NEXT` | Next button input |
| PD0/BOOT0 | `LED` | Active-high onboard blue status LED |

## Current sensing and internal op-amps

The PCB already amplifies both 20 mΩ shunts with the dual GS8632 and biases the outputs around `vREF`. The resulting `currentA` and `currentB` signals reach PA2 and PA1 directly.

Although the N32L406 contains two configurable op-amps, the PCB routing does not support using both for these current channels:

- OPA1 can accept PA1, but its external/ADC output is PA2, colliding with `currentA`.
- OPA2 input selections do not include PA1 or PA2.
- Enabling OPA1 on this routing could actively drive the other current-sense net.

The initial firmware therefore samples PA1 and PA2 directly. The useful portion of the Nations `OpaAdByTim` example is its timer-triggered injected-ADC and PWM timing, not its internal op-amp configuration.

## OLED candidate

The schematic routes PA4 `SCL` and PA5 `SDA` with external 4.7 kOhm pull-ups,
plus PB2 `lcdRES`, to the small OLED assembly. A ZJY042-7240TSWEG01 module
datasheet identifies an SSD1306 controller, 72 by 40 pixels, active-low reset,
and I2C write address `0x78` (7-bit `0x3C`). A separate open SERVO57D project
configures the board pins as I2C1 AF7 at 100 kHz. The candidate active window
uses controller columns 28-99 and pages 0-4.

This is strong secondary evidence, not proof of the fitted controller. The
project keeps the panel geometry and less-universal init settings configurable
until the purchased module is inspected and tested.

## Programming interface

J4 exposes target 3.3 V, GND, SWCLK, and SWDIO. It does not expose NRST. A temporary lead to MCU NRST should be considered for reliable connect-under-reset recovery.

With a raw Pico CMSIS-DAP probe, power the Pico from USB and the controller from its normal supply, and share ground. Do not connect the two regulated 3.3 V outputs together.

## Critical items to verify on physical hardware

- Exact PCB revision and whether the published schematic matches it.
- Fitted encoder marking, four-wire SPI/OTP mode, and magnet orientation.
- PB3/PB4/PB5 alternate-function selections and PD0 LED behavior on the
  purchased revision.
- Alternate-function/timer mapping of PA6, PA7, PB0, and PB1.
- EG3013 input truth table, propagation delays, dead time, and bootstrap constraints.
- `nEN` polarity and whether it disables every gate driver independently of PWM pins.
- Current-amplifier gain, bandwidth, output bias, clipping limits, and ADC scaling.
- Bus-voltage divider scaling, tolerance, loading, and safe ADC range on PA3.
- Reset behavior of the RS-485 direction signal and all isolated outputs.
- Availability of a convenient NRST test point.
