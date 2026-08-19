# Peripheral Bring-up Plan

This plan separates low-energy peripheral operation from energy-controlling
bridge timing. Safe peripherals may run during normal diagnostic boot as their
drivers mature; enabling one never authorizes bridge output. Bench-proven pin
assignments are identified below. Passive input mappings are bench-proven on
the tested board; bridge and isolated-output mappings remain provisional.

## Evidence summary

| Function | Candidate mapping | Confidence | Evidence and remaining proof |
| --- | --- | --- | --- |
| OLED bus | I2C1 AF7 on PA4/PA5 at 333.3 kHz; PB2 active-low reset; address `0x3C` | High; bench-proven on the fitted panel | The schematic shows external 4.7 kOhm pull-ups and no PB2 bias. The fitted display accepts the SSD1306-compatible 72-by-40 profile and sustained 50 Hz two-page updates. Exact controller marking remains unknown. |
| Bus voltage | PA3 ADC input, 15.4 kOhm over 1 kOhm divider | High for routing and ratio; reference/tolerance unmeasured | At 12 V input the tested board reported 895 counts, nominally 11.83 V with a 3.3 V ADC reference. Actual reference and divider tolerance remain calibration inputs. |
| Winding current | PA1 `currentB`, PA2 `currentA`; 20 mOhm shunts and 6.65 differential gain | High for topology and stable passive readings | Zero-current readings were B=2053 and A=2041 on the tested board with no more than one count of short-term fluctuation. Gain/sign tolerance, clipping, bandwidth, and timer-relative settling remain measurements. |
| Encoder | SPI1 on PB3 SCK, PB4 MISO, PB5 MOSI, PB6 software CS; MT6816-compatible protocol | High; bench-proven motion and wrap behavior | Position is stable at rest, follows shaft motion consistently, and wraps repeatably once per revolution. Exact marking, quantitative noise, zero alignment, and control-rate signal integrity remain open. |
| Bridge waveform | TIM3 channels 1-4 on PA6, PA7, PB0, PB1 | Medium-high | Schematic routing and public N32 work strongly support the mapping. Alternate functions, EG3013 behavior, ADC trigger placement, and shutdown semantics require register review and oscilloscope proof. |
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

The diagnostic image holds PB2 low from passive initialization, then performs
a bounded reset and initialization at address `0x3C`. The original 4 MHz PCLK
and current 16 MHz PCLK both select dividers producing approximately 333.3 kHz.
The fitted panel has drawn the
expected pixels successfully. Foreground refreshes only two display pages at
50 Hz; a display failure is non-fatal and permanently stops further updates
until reset.

Display refresh belongs in foreground housekeeping. It must not run from
SysTick, a control ISR, or any safety path.

### 2. Passive ADC acquisition

The diagnostic image actively runs the ADC layer for PA1 `currentB`, PA2
`currentA`, and PA3 `vBus`. It performs bounded,
software-triggered, single-channel conversions in that order and publishes a
host-tested all-or-nothing raw 12-bit sample every 50 ms. The OLED cycles
labeled A/B/V raw values once per second.

Passive initialization uses HSI divided to the ADC's required 1 MHz timing
clock and a synchronous HCLK-derived sampling clock no faster than 2 MHz.
Scan, DMA, interrupts, internal channels, and timer triggers remain disabled.
Schematic-derived engineering conversion is host-tested but is not yet used by
the active raw-value display. The detailed register contract, timing assumptions, evidence
confidence, and activation checklist are in [Passive ADC bring-up](ADC.md).

ADC work remains split into two hardware milestones:

- **Passive acquisition:** complete for basic raw readings on the tested board;
  reference accuracy, gain/sign, clipping, supply-range behavior, and settling
  remain to be measured.
- **Synchronous current acquisition:** only after PWM timing exists, trigger
  PA1/PA2 at a measured quiet point, use DMA or a bounded completion ISR, and
  reject missing, duplicate, clipped, or late sequences.

Production telemetry and limits must not claim calibrated amperes or volts
until measured ADC reference and per-channel zero offsets are captured as
versioned calibration data.

### 3. Encoder SPI

The boot path activates a slow, polling, read-only MT6816-compatible transaction while
the bridge remains disabled. SPI1 runs at 500 kHz or lower in mode 3 and reads
registers `0x03` through `0x05` in one four-byte CS window every 10 ms after a
20 ms power-up delay. The driver rejects odd parity and publishes no-magnet and
over-speed flags with raw 14-bit angle and status counters in diagnostic schema
2. Transport and parity failures remain non-fatal and are retried.

GPIOB activation for SPI is constrained to PB3-PB6, and the current image
leaves PB0/PB1 high impedance while reading PB7 `nEN` as an input. Bench testing has
proven stable rest readings, consistent shaft response, and repeatable
wraparound. DMA, interrupts, quantitative noise, zero alignment, and the
proposed 5-10 kHz acquisition rate remain later work.
See [MT6816 encoder bring-up](ENCODER.md).

### 4. Inputs and RS-485

Firmware 0.14.0 samples eight passive inputs every 10 ms. PB8 Enter, PB9 Menu,
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

The diagnostic
image actively configures USART1 for 115200 8N1, preloads PC13 low for
receive, and runs continuous circular RX DMA on channel 4. Foreground drains a
bounded number of bytes into the native v1 COBS/CRC parser. TX uses channel 5
and keeps PC13 high until USART transmission-complete, not merely DMA
completion.

The first protocol slice replies only to complete, CRC-valid address-1 ping,
identity, and capability requests. Framing, address checks, command validation,
and reply creation remain bounded foreground work and may never command a
timer compare or bridge-enable register directly. Adapter and oscilloscope
proof is specified in [USART1 / RS-485 bring-up](RS485.md). Complete native
commands and responses have been observed through `485_A2`/`485_B2`;
reset-time and turnaround waveforms remain to be scoped.

Timer capture and step/direction/enable operating semantics remain deferred.
The proven PB7 `nEN` input mapping must not be treated as a hardware bridge-off
mechanism without tracing its downstream relationship and measuring it.

### 5. PWM and synchronous ADC

The initial GPIO bridge characterizer established the direct pin/driver mapping.
The retained bridge characterizer configures TIM3 channels 1-4 on AF2 for PA6, PA7, PB0,
and PB1. Each MCU command drives tied HIN/LIN inputs;
low selects the low-side FET, high selects the high-side FET, and the documented
dead time is nominally 120 ns. Next selects A1/A2/B1/B2, and holding Enter
applies edge-aligned 20 kHz, 50% PWM to that leg while the other three commands
remain low. Compare and auto-reload preloads are enabled, and a forced update
applies each start/stop request at a defined timer boundary.
Bench testing confirms all four selections produce the expected phase and
polarity, proving the TIM3 AF2 pin mapping on the tested board.

Firmware 0.12.1 attempted to select TIM3 update as `TRGO` for a two-rank
`currentB`/`currentA` sequence, but DMA channel 1 reported `ERRF` on hardware.
Firmware 0.14.0 retains the bench-proven target two-rank `currentB/currentA`
scan and arms ADC/DMA before TIM3 starts. Each 20 kHz TIM3 update triggers one
sequence into the 64-halfword ring. After independent 32-sample startup zero
calibration, the OLED shows both signed currents in milliamperes. Bridge `RUN`
remains suppressed until a bounded current-loop backend owns it.

Production PWM and current acquisition still require:

- a single idempotent all-low zero-vector mechanism and characterization of
  any separate hardware shutdown path;
- EG3013 truth table, dead time, bootstrap, and reset behavior;
- preload/update behavior with a non-motor load or scoped gate-driver inputs;
- measurement of switching-edge contamination at the implemented update
  trigger and, if needed, an auxiliary-timer offset trigger or deliberately
  designed dual-sample schedule;
- debugger halt, watchdog, clock failure, malformed commands, and missed
  control deadlines all converge on a characterized deterministic state.

The first timing experiment uses edge-aligned TIM3 with an unambiguous
period boundary. Center-aligned PWM remains a later option after its two-update
behavior and sample symmetry are measured.
