# Passive ADC Bring-up

Status: an inactive, bounded polling driver and a host-tested raw-sample
contract are compiled into the project. Neither is called by the boot image.
No ADC, HSI, or GPIOA register is changed at reset, and no sampled value is
yet treated as a physical voltage or current.

## Provisional signal contract

| Sample field | Board net | MCU pin | ADC channel | Confidence and evidence |
| --- | --- | --- | ---: | --- |
| `current_b_raw` | `currentB` | PA1 | 2 | High: published schematic plus Nations channel mapping |
| `current_a_raw` | `currentA` | PA2 | 3 | High: published schematic plus Nations channel mapping |
| `vbus_raw` | `vBus` | PA3 | 4 | High: published schematic plus Nations channel mapping |

The two current inputs are outputs from the board's external GS8632
amplifiers. The internal MCU op-amps remain disabled. `adc_sample_t` preserves
the schematic order above, stores only right-aligned 12-bit values, and adds a
wrapping capture index. Construction rejects any raw value above 4095 and does
not modify the destination on failure.

Channel identity, reference voltage, zero offsets, gain, sign, bandwidth,
clipping, divider ratio, and agreement with the purchased board all remain
hardware measurements. The firmware therefore does not expose amperes, volts,
or calibrated limits at this stage.

## Inactive acquisition design

`adc1_init_passive(hclk_hz)` has an explicit activation point and performs only
the following bounded setup:

1. Select the smallest supported synchronous HCLK divider that keeps the ADC
   sampling clock at or below 2 MHz for an HCLK from 1 to 64 MHz.
2. Enable the 16 MHz HSI only as the source of the required ADC 1 MHz timing
   clock, using divide-by-16. It does not select HSI as the system clock.
3. Enable GPIOA and ADC clocks, reset the ADC, and place PA1/PA2/PA3 in analog
   mode with no pull resistors.
4. Select 12-bit, right-aligned, single-ended, synchronous-clock operation with
   scan, continuous conversion, DMA, interrupts, watchdog, and internal
   temperature/reference channels disabled.
5. Wait for ADC ready and one power-on calibration to complete. Every wait has
   a finite polling budget and reports a distinct status.

The Nations V1.2.2 ADC driver also sets value `0x28` in an analog-LDO control
register at ADC offset `0x60`. That step is retained here because the official
driver requires it, but the N32L40x user manual does not name the register.
This is a medium-confidence, SDK-backed detail that must be verified on the
board before activation.

`adc1_read_passive()` performs three independent software-triggered
conversions in the fixed order `currentB`, `currentA`, `vBus`. It does not use a
three-rank scan because, without DMA, successive results could overwrite the
single regular-data register before software consumes each channel. Output is
published only after all three conversions succeed, so callers never receive
a partially updated sample.

The current channels provisionally use 28.5 sampling cycles and the bus input
uses 55.5 sampling cycles. With the conservative 2 MHz ADC clock, the nominal
three-conversion sequence is about 75 microseconds plus the documented
first-conversion overhead. These are bring-up settings, not a real-time timing
contract; source impedance, settling, and loop timing must be measured.

## Activation gate

The driver must remain uncalled until a separate passive-board change does all
of the following:

1. Confirm PA1, PA2, and PA3 against the purchased PCB revision.
2. Keep the motor disconnected, use a current-limited supply, and independently
   hold the bridge disabled.
3. Revise the passive-board self-test: enabling GPIOA invalidates the present
   proof that PA6/PA7 could not have been configured, so those bridge pins must
   be inspected directly instead.
4. Check HSI ready, ADC ready, calibration completion, and bounded timeout
   behavior on silicon before accepting samples.
5. Capture repeated raw samples with the bridge disabled and measure current
   offsets/noise, bus-divider scaling, ADC reference behavior, and amplifier
   settling.
6. Add timestamp and status publication to diagnostics only after the sampling
   behavior is known. Keep all polling in foreground housekeeping.

DMA, ADC interrupts, timer triggers, analog-watchdog thresholds, offset
calibration, engineering-unit conversion, and synchronous PWM sampling belong
to later milestones. In particular, this passive path is not the Phase 5
current-loop acquisition backend.

## Trust summary

The PA1/PA2/PA3 net routing and ADC channel mapping have high-confidence
schematic and manufacturer support. Ready, calibration, clock, resolution, and
conversion sequencing are high-confidence user-manual requirements. The ADC
analog-LDO write is medium-confidence because it is present in Nations' driver
but undocumented in the user manual. Sampling times and analog scaling are
provisional until bench measurements. Overall confidence is high for an
inactive compile-time scaffold and intentionally insufficient for boot
activation.
