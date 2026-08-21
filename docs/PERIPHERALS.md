# Peripheral Map and Status

This document summarizes the active peripheral assignments and the remaining
characterization work. The tested board's display, ADC, encoder, inputs,
RS-485, PWM outputs, and synchronous current path are bench-proven. Mappings
remain revision-specific until another board revision is checked.

## Evidence summary

| Function | Candidate mapping | Confidence | Evidence and remaining proof |
| --- | --- | --- | --- |
| OLED bus | I2C1 AF7 on PA4/PA5 at 333.3 kHz; PB2 active-low reset; address `0x3C` | High; bench-proven on the fitted panel | The schematic shows external 4.7 kOhm pull-ups and no PB2 bias. The fitted display accepts the SSD1306-compatible 72-by-40 profile and sustained 50 Hz two-page updates. Exact controller marking remains unknown. |
| Bus voltage | PA3 ADC input, 15.4 kOhm over 1 kOhm divider | High for routing and ratio; divider tolerance remains | At 12 V input the tested board reported 895 counts, about 11.83 V with the measured 3.3 V ADC reference. Divider tolerance remains a calibration input. |
| Winding current | PA1 `currentB`, PA2 `currentA`; 20 mOhm shunts and 6.65 differential gain | High; bench-proven in the active loop | Phase A is positive for A- to A+ current and phase B for B+ to B-. The tested-board scale is 6.059 mA/count from the measured 3.3 V reference, verified 6.65 gain, and fitted shunts. Clipping, temperature/unit tolerance, bandwidth, and settling remain characterization items. |
| Encoder | SPI1 on PB3 SCK, PB4 MISO, PB5 MOSI, PB6 software CS; MT6816-compatible protocol | High; bench-proven motion and wrap behavior | Position is stable at rest, follows shaft motion consistently, and wraps repeatably once per revolution. Exact marking, quantitative noise, zero alignment, and control-rate signal integrity remain open. |
| Bridge waveform | TIM3 channels 1-4 on PA6, PA7, PB0, PB1 | High; all legs and motor rotation bench-proven | The AF2 mapping, all four output polarities, low-zero modulation, 16 MHz ADC with 80%-carrier current sampling, and encoder-observed rotation through 757 mA / 20 Hz are proven on the tested board. Driver timing and expanded-envelope characterization remain. |
| RS-485 | USART1 AF4 on PA9/PA10 with PC13 direction control; DMA channels 4/5 | High; command/response bench-proven | The working connector is `485_A2`/`485_B2`, contrary to the apparent published connector routing. Reset-time bus state and exact direction timing still require scope measurements. |
| Passive inputs | PB8 Enter, PB9 Menu, PA15 Next, PB13 M_IN1, PB12 M_IN2, PA0 `nSTP`, PA8 `nDIR`, PB7 `nEN` | High; all bench-proven active-low | Static OLED monitoring confirms each physical input independently. Step pulse capture, rate limits, and operating semantics remain deferred. |

## Implementation order

### 1. I2C and OLED transport

The project runs a bounded, polling I2C1 master transport and a host-tested
SSD1306-compatible command/frame layer. The proven panel profile
uses the module datasheet's 1/40 multiplex, clock, contrast, VCOMH, internal-IREF,
and active-window settings: 72 by 40 visible pixels at columns 28-99 and pages
0-4. These settings remain configuration rather than being embedded in drawing
code.

Firmware holds PB2 low from passive initialization, then performs
a bounded reset and initialization at address `0x3C`. The original 4 MHz PCLK
and current 16 MHz PCLK both select dividers producing approximately 333.3 kHz.
The fitted panel has drawn the expected pixels successfully. The transport has
sustained 50 Hz two-page updates; firmware 0.19.0 uses a 5 Hz current-loop
display. A display failure is non-fatal and stops further updates until reset.

Display refresh belongs in foreground housekeeping. It must not run from
SysTick, a control ISR, or any safety path.

### 2. Passive ADC acquisition

The project retains a bounded polling ADC path for PA1 `currentB`, PA2
`currentA`, and PA3 `vBus`. That path proved all three channels with
all-or-nothing raw 12-bit samples. Firmware 0.19.0 instead dedicates PA1/PA2 to
the 20 kHz synchronous current sequence; periodic PA3 bus-voltage sampling is
the next ADC integration item.

Passive initialization uses HSI divided to the ADC's required 1 MHz timing
clock and a synchronous HCLK-derived sampling clock no faster than 2 MHz.
Scan, DMA, interrupts, internal channels, and timer triggers remain disabled.
Schematic-derived engineering conversion is host-tested but is not yet used by
the active raw-value display. The detailed register contract, timing assumptions, evidence
confidence, and activation checklist are in [Passive ADC bring-up](ADC.md).

ADC work has two layers:

- **Passive acquisition:** complete for basic raw readings on the tested board;
  reference accuracy, gain/sign, clipping, supply-range behavior, and settling
  remain to be measured.
- **Synchronous current acquisition:** complete at 20 kHz using a TIM2 compare
  at 80% of the TIM3 carrier and DMA completion. Missing, duplicate, clipped,
  and late sequences are fault inputs.

Current control uses the tested board's measured 3.3 V reference,
schematic-and-board-verified 6.65 current-sense gain, and measured startup
zeros. Production units still require versioned per-channel calibration and
tolerance limits.

### 3. Encoder SPI

The boot path activates a polling, read-only MT6816-compatible transaction and
continues it during current-loop operation. SPI1 runs at 500 kHz or lower in
mode 3 and reads registers `0x03` through `0x05` in one four-byte CS window on a
1 kHz foreground schedule after a 20 ms power-up delay. The driver rejects odd
parity and publishes no-magnet and over-speed flags with raw 14-bit angle and
status counters. Accepted samples feed the timestamped mechanical estimator;
native encoder schema 2 adds position, velocity, alignment validity, and
sample-interval telemetry. The driver retries transport and parity failures;
the product supervisor
keeps an unready idle drive in `DIAGNOSTIC` and converts encoder-health loss
during bridge authority into `FAULT`.

GPIOB activation for SPI is constrained to PB3-PB6 before TIM3 claims PB0/PB1;
PB7 `nEN` remains an input. Bench testing has proven stable rest readings,
consistent shaft response, repeatable wraparound, and continuous observation
during a 5.97 RPM motor run. The new 1 kHz schedule, quantitative noise, and
controlled zero alignment remain hardware work; timer-released SPI/DMA remains
the fallback if measured foreground jitter is not fit for purpose.
See [MT6816 encoder bring-up](ENCODER.md).

### 4. Inputs and RS-485

Firmware 0.23.1 samples eight inputs every 10 ms. PB8 Enter, PB9 Menu,
PA15 Next, PB13 M_IN1, and PB12 M_IN2 use pull-ups and have been bench-proven;
three consecutive changed samples update each independently. The physical keys
are left Next, center Enter, and right Menu, and both auxiliary inputs respond
independently.

PA0 `nSTP`, PA8 `nDIR`, and PB7 `nEN` are configured as high-impedance inputs
with no MCU pulls. The current OLED view shows their debounced raw levels as
`S D E`, where `0` is asserted. Bench testing confirms all three active-low
signals in the expected physical locations. This 30 ms static monitor is for
pin and polarity observation only; it
does not count pulses, validate edge rates, grant motion authority, or control
the bridge.

Firmware actively configures USART1 for 115200 8N1, preloads PC13 low for
receive, and runs continuous circular RX DMA on channel 4. Foreground drains a
bounded number of bytes into the native v1 COBS/CRC parser. TX uses channel 5
and keeps PC13 high until USART transmission-complete, not merely DMA
completion.

Native protocol 1.7 replies to complete, CRC-valid address-1 discovery, boot,
raw/estimated encoder, current-loop, alignment, configuration, aligned-q-current,
and generic-STOP requests.
Framing, address checks, command validation, and reply creation remain bounded
foreground work; the product drive supervisor owns bridge authority. Complete
configuration, START, live status, encoder, and STOP exchanges have been observed
through `485_A2`/`485_B2`; alignment and persistent configuration pass their
hardware gates, while aligned q-current remains pending. Reset-time and
turnaround waveforms remain to be scoped.

Timer capture and step/direction/enable operating semantics remain deferred.
PB7 `nEN` is currently a command input, not an assumed hardware bridge-off
mechanism; any downstream hardware function still needs to be traced.

### 5. PWM and synchronous ADC

The initial GPIO bridge characterizer established the direct pin/driver
mapping. The active current-loop backend configures TIM3 channels 1-4 on AF2
for PA6, PA7, PB0, and PB1. Each MCU command drives tied HIN/LIN inputs;
low selects the low-side FET, high selects the high-side FET, and the documented
dead time is nominally 120 ns. The backend applies edge-aligned 20 kHz,
low-zero sign-magnitude duties with compare and auto-reload preloads. Start,
update, stop, and fault transitions are applied at defined timer boundaries.
Bench testing confirms all four selections, expected phase polarities, current
tracking in all four quadrants, and motor rotation.

Firmware 0.12.1 attempted to select TIM3 update as `TRGO` for a two-rank
`currentB`/`currentA` sequence, but DMA channel 1 reported `ERRF` on hardware.
Firmware 0.19.0 uses the bench-proven two-rank `currentB/currentA` scan and
arms ADC/DMA before TIM3 starts. TIM2 resets from TIM3 update; its 80%-phase
compare ISR software-starts one sequence into a two-halfword DMA buffer.
After independent 32-sample startup zero
calibration, the OLED and native protocol show both signed currents. The DMA
completion path runs the bounded PI loop and stages the next PWM duties.

Remaining PWM and current-path characterization includes:

- continued injected-fault verification of the existing idempotent all-low
  mechanism and characterization of any separate hardware shutdown path;
- quantitative EG3013 propagation, dead-time, bootstrap, and reset behavior;
- quantitative preload/update and gate-driver timing measurements;
- measurement of switching-edge contamination at the implemented delayed
  trigger and, if needed, an auxiliary-timer offset trigger or deliberately
  designed dual-sample schedule;
- reset, debugger halt, watchdog, and clock-failure waveform characterization;
  malformed commands and missed control deadlines already converge on all-low.

The active topology uses edge-aligned TIM3 with an unambiguous period boundary.
Center-aligned PWM remains an optional future refinement if measurements show
an advantage worth its two-update behavior.
