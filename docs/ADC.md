# ADC Bring-up

Status: firmware 0.17.3 uses TIM2 compare at 65% of each 20 kHz carrier to
software-start the two-rank `currentB/currentA` sequence from a bounded ISR.
The current-loop channels use 7.5-cycle sampling and one two-halfword DMA
transaction per sequence; transfer completion owns the fast fixed-point loop.
ADC/DMA configuration and arming still finish before the PWM timers start.
Firmware averages 32 bridge-zeroed startup snapshots for independent A/B
offsets, then the OLED shows both signed currents as compact `A+#####mA` and
`B+#####mA` rows. Switched-current sign and control timing are implemented but
not yet bench-proven. The
fixed-destination and single-channel ring diagnostics both produced stable
2039-2044 PA2 readings, proving the basic request, channel, addresses, widths,
memory increment, and ring size. The host-tested conversion module supplies
the active engineering-unit display using nominal reference scaling.

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

Channel identity, target synchronous acquisition, and basic operation agree with the tested board. Actual ADC
reference, gain/sign tolerance, bandwidth, clipping, amplifier settling, and
divider tolerance remain hardware measurements. Displayed milliamperes are
zero-calibrated but remain nominal-scale measurements, not yet protection-grade limits.

## Engineering conversion

The schematic-derived transfer functions are:

```text
current = (raw - zero_raw) * (adc_reference_volts / 4095)
          / (6.65 * 0.020 ohm)

vbus = raw * (adc_reference_volts / 4095) * 16.4
```

The current amplifier's mid-rail bias has unity gain; 6.65 is the differential
shunt-voltage gain, not 7.65. The bus divider is 15.4 kOhm above 1 kOhm. At a
nominal 3.3 V reference these factors are approximately 6.06 mA per current
count and 13.22 mV per bus-voltage count. `adc_sample_convert()` accepts the
ADC reference and independent A/B zero counts at runtime. Firmware 0.14.0 uses
the nominal 3.3 V reference and measures each zero from 32 synchronized samples
over approximately 320 ms while bridge authority remains inhibited.

## Acquisition design

`adc1_init_passive(hclk_hz)` has an explicit activation point and performs the
following bounded common setup:

1. Select the smallest supported synchronous HCLK divider that keeps the ADC
   sampling clock at or below 2 MHz for an HCLK from 1 to 64 MHz.
2. Enable the 16 MHz HSI only as the source of the required ADC 1 MHz timing
   clock, using divide-by-16. It does not select HSI as the system clock.
3. Enable GPIOA and ADC clocks, reset the ADC, and place PA1/PA2/PA3 in analog
   mode with no pull resistors.
4. Select 12-bit, right-aligned, single-ended, synchronous-clock operation with
   continuous conversion, interrupts, watchdog, and internal
   temperature/reference channels disabled.
5. Wait for ADC ready and one power-on calibration to complete. Every wait has
   a finite polling budget and reports a distinct status.

The Nations V1.2.2 ADC driver also sets value `0x28` in an analog-LDO control
register at ADC offset `0x60`. That step is retained here because the official
driver requires it, but the N32L40x user manual does not name the register.
This is a medium-confidence, SDK-backed detail. It works on the tested board,
but remains undocumented by the user manual.

`adc1_read_passive()` performs three independent software-triggered
conversions in the fixed order `currentB`, `currentA`, `vBus`. It does not use a
three-rank scan because, without DMA, successive results could overwrite the
single regular-data register before software consumes each channel. Output is
published only after all three conversions succeed, so callers never receive
a partially updated sample.

Passive conversions provisionally use 28.5 sampling cycles for the current
channels and 55.5 cycles for the bus input. With the conservative 2 MHz ADC clock, the nominal
three-conversion sequence is about 75 microseconds plus the documented
first-conversion overhead. These are bring-up settings, not a real-time timing
contract; source impedance, settling, and loop timing must be measured.

The firmware 0.12.1 synchronous attempt called
`adc1_start_pwm_synchronized_current()` after TIM3 was already running. That
transition:

1. reserves DMA channel 1 for ADC data, using 16-bit peripheral and memory
   transfers, memory increment, high priority, and a 64-halfword circular
   buffer without DMA interrupts;
2. enables ADC scan mode and changes the regular sequence to two ranks in the
   fixed schematic order `currentB`, then `currentA`;
3. selects `TIM3_TRGO` as the rising external regular trigger and enables one
   DMA request per completed conversion; and
4. accepts a foreground snapshot only when the DMA remaining count is stable
   around both reads, so a half-written pair is never published.

TIM3 emits update as `TRGO` once per edge-aligned 20 kHz carrier period. At the
retained 2 MHz ADC clock, the two 28.5-cycle current conversions require about
44 microseconds including conservative single-sequence overhead, leaving about
6 microseconds before the next 50 microsecond trigger. PWM is inhibited until
the first complete pair is available. A DMA error or invalid result immediately
removes PWM authority while heartbeat, protocol, and OLED diagnostics remain
alive; the OLED shows the numeric status as `A####`. `vBus` is not part of this
synchronous sequence and its earlier 20 Hz polling is temporarily inactive in
the characterization image. On the tested board DMA channel 1 instead set
`ERRF` immediately and hardware cleared `CHEN`, as specified by the DMA error
behavior in the user manual.

Firmware 0.12.2 replaced that path temporarily with
`adc1_start_sdk_dma_diagnostic()`, following the manufacturer example's order
and settings:

1. fully disable and clear DMA channel 1;
2. set `PADDR=&ADC->DAT`, a single fixed SRAM halfword destination, transfer
   count 1, peripheral/memory width 16 bits, circular mode, high priority, and
   ADC request select 0;
3. enable DMA before configuring and starting the ADC;
4. configure a one-rank PA2 sequence, continuous conversion, software trigger,
   and 55.5-cycle sampling;
5. enable and calibrate the ADC, enable ADC DMA requests, then issue the
   software start; and
6. report the transferred PA2 value continuously on the OLED.

That test deliberately removed TIM3 triggering, the two-rank scan, memory
increment, and the 64-halfword ring. Its successful `A20xx`-class result proved
the basic ADC request, DMA channel, peripheral address, SRAM address, and
halfword widths. Firmware 0.12.3 then restored only memory increment and the
64-halfword circular ring while retaining the same PA2 software-triggered
conversion and initialization order; that path also passed on hardware.

## Bench result and remaining validation

At 12 V input with no commanded bridge current, the tested board reported
`currentA=2041`, `currentB=2053`, and `vBus=895`; each channel varied by no more
than one count during observation. With nominal 3.3 V scaling, 895 corresponds
to about 11.83 V. These are observations from one board, not production
calibration constants.

Firmware 0.13.0 displayed stable alternating A/B samples without `A0010`, so
the target acquisition architecture and corrected ADC/DMA-before-TIM3 startup
order are accepted as a bring-up result. The B channel has a visibly different
zero point from A, confirming that offsets must remain independent. Firmware
0.14.0 displays both zero-calibrated currents simultaneously;
with no commanded current they dither near 0 mA and remain within approximately
+/-12 mA. At 6.06 mA/count this is a roughly two-count residual, consistent
with the observed ADC quantization/noise floor.

Firmware 0.17.3 retains the proven ADC/DMA-before-TIM3 initialization order but
does not reuse the carrier-boundary trigger for switched-current regulation.
TIM2 is reset by TIM3 update, compares at 65% of the carrier, and its short ISR
sets the ADC software-start bit. The current channels use 7.5-cycle apertures
at the retained 2 MHz ADC clock. Low-zero sign-magnitude modulation confines
the loop's switching edges to the first 10% of the period under the current
phase-voltage bound, so the first aperture begins at least 55% of a period
after the latest permitted edge; the second aperture ends before the next
carrier boundary. DMA completion executes the fixed-point A/B PI controllers,
stages the selected-leg duties, and publishes a new output generation.
The TIM3 update guardian allows one empty update for this pipelined result and
faults on a second consecutive update without a new output.

Remaining analog work is to measure the actual ADC reference, verify gain and
sign with applied current, characterize amplifier settling/bandwidth and
clipping, and repeat across bus voltage. Switching-correlated offset or noise
must be measured when the current loop drives PWM. The 65%-phase trigger must
be scoped to quantify switching-edge contamination, ISR latency, and
conversion/control completion relative to the following preload boundary.
Analog-watchdog thresholds and restoring periodic `vBus` acquisition also
remain later work.

## Trust summary

The PA1/PA2/PA3 net routing and ADC channel mapping have high-confidence
schematic and manufacturer support. Ready, calibration, clock, resolution, and
conversion sequencing are high-confidence user-manual requirements. The ADC
analog-LDO write is medium-confidence because it is present in Nations' driver
but undocumented in the user manual. The resistor-derived scaling, passive
readings, synchronous two-channel path, per-channel startup zero calibration,
and nominal milliamp display have bench support; reference accuracy and dynamic
behavior remain unmeasured. Overall confidence is high for acquisition and
relative current measurement, but intentionally insufficient for protection
thresholds until reference/gain tolerance and switched-current behavior are
measured.
