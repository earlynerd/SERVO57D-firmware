# Hardware Notes

Primary schematic reference: Makerbase `MKS SERVO57D_485 V1.1_001`. Purchased boards must be checked for revision differences before relying on this document.

## High-level architecture

- N32L406CBL7 Cortex-M4F microcontroller.
- Two full H-bridges built from four EG3013 half-bridge gate drivers and discrete AP050N03Q MOSFETs.
- Two 20 mΩ low-side current shunts with external GS8632 amplifiers.
- SPI magnetic encoder identified as MT6816CT-ACD on the published schematic;
  the fitted device is bench-compatible with that protocol, though its marking
  remains to be recorded.
- RS-485 transceiver, isolated step/direction/enable inputs, isolated auxiliary I/O, display, buttons, and SWD header.

## Working pin map

This table combines the published schematic with bench findings. Inputs,
current sensing, and all four bridge controls are bench-proven on the tested
board; isolated outputs remain provisional.

| MCU pin | Schematic signal | Intended use |
| --- | --- | --- |
| PA0 | `nSTP` | Bench-proven active-low isolated step input |
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
| PB7 | `nEN` | Isolated enable input; relation to bridge inhibit, if any, requires tracing |
| PB8 | `KEY_ENTER` | Enter button input |
| PB9 | `KEY_MENU` | Menu button input |
| PB12 | `M_IN2` | Isolated auxiliary input |
| PB13 | `M_IN1` | Isolated auxiliary input |
| PB14 | `M_OUT1` | Isolated auxiliary output |
| PB15 | `M_OUT2` | Isolated auxiliary output |
| PA8 | `nDIR` | Bench-proven active-low isolated direction input |
| PA9 | `TX` | USART1 transmit, AF4 |
| PA10 | `RX` | USART1 receive, AF4 |
| PA13 | `SWDIO` | Debug data |
| PA14 | `SWCLK` | Debug clock |
| PA15 | `KEY_NEXT` | Next button input |
| PC13 | `RE1` | Proven RS-485 direction: low receive, high transmit |
| PD0/BOOT0 | `LED` | Active-high onboard blue status LED |

The three keys short their MCU nets to ground and have no schematic external
bias, so firmware enables internal pull-ups. The M_IN1/M_IN2 optocoupler
transistor outputs have external 10 kOhm pull-ups to 3.3 V; firmware also uses
input pull-ups there for one consistent passive-input configuration. All five
signals are therefore active-low. PA15 is the default JTDI pin, but becomes
available as GPIO when the debug port is operating in SWD mode; PA13 SWDIO and
PA14 SWCLK remain untouched.

On the tested board, the physical button order is:

| Position | OLED label | Function | MCU pin |
| --- | --- | --- | --- |
| Left | `N` | Next | PA15 |
| Center | `E` | Enter | PB8 |
| Right | `M` | Menu | PB9 |

All three buttons and both isolated M_IN1/M_IN2 inputs have been observed to
change their corresponding active-low OLED state independently.

Firmware 0.19.0 also samples PA0 `nSTP`, PA8 `nDIR`, and PB7 `nEN` as
high-impedance inputs without MCU pull resistors. The OLED exposes their
debounced raw electrical levels as `S D E`. This is only a pin and polarity
check: it does not count step pulses or assign motion or bridge-enable
semantics. Bench testing confirms all three mappings and their active-low
behavior in the expected connector locations.

## Current sensing and internal op-amps

The PCB amplifies both 20 mOhm Kelvin-connected shunts with the dual GS8632
and biases the outputs around `vREF`. For each channel, the low-side Kelvin
connection reaches the inverting input through 1 kOhm with 6.65 kOhm feedback;
the high-side Kelvin connection reaches the non-inverting input through 1 kOhm
and `vREF` reaches it through 6.65 kOhm. The resulting transfer is:

```text
Vout = Vref + 6.65 * (Vhigh - Vlow)
     = Vref + 6.65 * I * 0.020 ohm
```

The mid-rail bias therefore has unity gain, while the shunt voltage has 6.65
gain. The resulting `currentA` and `currentB` signals reach PA2 and PA1
directly. On the tested board at zero commanded current and 12 V bus input,
the raw readings were approximately A=2041 and B=2053 with no more than one
count of short-term fluctuation.

The phase signs are asymmetric by construction. `phaseA1` drives connector
terminal A+ and its low-side FET contains the A shunt, so positive measured A
current flows from A- to A+ and positive A voltage must drive `phaseA2`.
`phaseB2` drives B- and its low-side FET contains the B shunt, so positive
measured B current flows from B+ to B- and positive B voltage drives
`phaseB1`.

The `vBus` divider uses 15.4 kOhm above 1 kOhm, for a 16.4 ratio. With the
tested board's measured 3.3 V ADC reference, verified 6.65 current-sense gain,
and fitted 20 mOhm shunts, one current count is 6.059 mA. One bus-voltage count
is about 13.22 mV, and the observed raw value 895 corresponds to about 11.83 V.
Runtime conversion uses this measured reference plus channel-specific startup
zero counts.

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
configures the board pins as I2C1 AF7. The active window uses controller
columns 28-99 and pages 0-4.

The fitted panel has been initialized successfully at address `0x3C`, and
drawn pixels match the 72-by-40 SSD1306-compatible profile. Firmware now runs
I2C1 at 333.3 kHz and updates two pages at 50 Hz. This proves protocol and
geometry compatibility, not the exact controller marking.

## RS-485 transceiver

U17 is labeled `SP485E` and is powered from 5 V. Its active-low `/RE` and
active-high `DE` pins are tied, so one GPIO selects receive-low or
transmit-high. Bench tracing and operation identify PC13, not PA8, as the
populated transceiver's direction control. USART1 remains on PA9/PA10.

Commands and responses are proven on the tested board through the physical
connector labeled `485_A2`/`485_B2`. This conflicts with the apparent published
connector routing, so connector identity and reset-time bus drive must be
rechecked on other revisions. See [USART1 / RS-485 bring-up](RS485.md).

## Programming interface

J4 exposes target 3.3 V, GND, SWCLK, and SWDIO. It does not expose NRST. Normal
J-Link programming works through SWD without NRST; a temporary reset lead is
only useful if connect-under-reset recovery is specifically needed.

With a raw Pico CMSIS-DAP probe, power the Pico from USB and the controller from its normal supply, and share ground. Do not connect the two regulated 3.3 V outputs together.

## Critical items to verify on physical hardware

- Exact PCB revision and whether the published schematic matches it.
- Fitted encoder marking, four-wire SPI/OTP mode, and magnet orientation.
- Scoped EG3013 propagation delays, nominal 120 ns dead time, and bootstrap constraints. Documentation establishes HIN active-high and LIN active-low.
- `nEN` polarity and whether it disables every gate driver independently of PWM pins.
- Current-amplifier bandwidth, gain/sign tolerance, clipping limits, and settling.
- ADC reference accuracy and bus-divider tolerance, loading, and safe PA3 range.
- Reset duration and RS-485 bus state before PC13 is driven low.
- Availability of a convenient NRST test point.

Each PA6/PA7/PB0/PB1 command is wired directly to both HIN and LIN of one
EG3013. Therefore low selects its low-side FET and high selects its high-side
FET. The internal diagram shows opposing HIN pull-down and LIN pull-up bias;
when those inputs are tied, a floating MCU output is not a defined all-FET-off
state. Firmware uses all four commands low as its deterministic zero-voltage
vector, while recognizing that all four low-side FETs remain on.

With a 12 V bus and the motor disconnected, firmware 0.10.0 measured 0 V DC
across both windings in `ZERO`. Holding Enter for the 500 Hz, nominally 50%
single-leg pattern produced approximately 6 V average across the selected
phase on a Fluke meter. This supports the expected two-level bridge polarity
and differential zero vector; it does not establish edge shape, actual
frequency, or propagation delay. Supply current showed no detectable increase
while the pattern was running, providing practical evidence that the EG3013's
fixed dead time is adequate and that ordinary switching has no gross
cross-conduction. Reset, watchdog, and debugger-halt transitions remain
separate validation cases.

The retained bridge characterizer replaced the foreground pattern with edge-aligned 20 kHz,
50% TIM3 PWM. Cycling A1, A2, B1, and B2 on the physical board produced the
expected selected-phase magnitude and polarity while the unselected phase
remained at zero. This bench-proves TIM3 channels 1-4 on AF2 for PA6, PA7,
PB0, and PB1 on the tested board.

Firmware 0.19.0 retains the closed hardware milestone: the two-rank A/B ADC path,
20 kHz DMA-completion PI loop, phase-specific current signs, low-zero
modulation, all four current quadrants, and continuous encoder observation are
bench-proven with an attached motor. The accepted 300 mA, 5 Hz electrical run
completed 59,905 current samples and produced smooth encoder-observed motion at
5.97 RPM with no current-loop, encoder, SPI, or reset fault.
