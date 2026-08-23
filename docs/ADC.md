# ADC Bring-up

Status: firmware 0.32.2 retains the TIM2 compare at 80% of each 20 kHz carrier to
software-start the two-rank `currentB/currentA` sequence from a bounded ISR.
The current-loop channels use 16 MHz, 7.5-cycle sampling and one two-halfword DMA
transaction per sequence; transfer completion owns the fast fixed-point loop.
Its explicitly armed trace records the 32 MHz TIM2 trigger phase and
trigger-to-DMA-entry latency beside 64 MHz DWT current-ISR timing; it does not
stream from interrupt context. Normal disarmed control does not read TIM2 or
DWT for trace timing and does not query preload margin; capture starts on the
first complete ADC transaction after arm and stops when the trace fills,
authority stops, or a fault occurs.
ADC/DMA configuration and arming still finish before the PWM timers start.
Firmware averages 32 bridge-zeroed startup snapshots for independent A/B
offsets, then the OLED shows both signed currents as compact `A+#####mA` and
`B+#####mA` rows. Switched-current sign and timing are bench-proven through
approximately 160,000 fault-free current-loop samples at 151.5 mA and
303 mA, including encoder-confirmed motor rotation. The
fixed-destination and single-channel ring diagnostics both produced stable
2039-2044 PA2 readings, proving the basic request, channel, addresses, widths,
memory increment, and ring size. The host-tested conversion module supplies
the active engineering-unit display using the tested board's measured 3.3 V
reference and verified 6.65 gain.

## Signal contract

| Sample field | Board net | MCU pin | ADC channel | Confidence and evidence |
| --- | --- | --- | ---: | --- |
| `current_b_raw` | `currentB` | PA1 | 2 | High: published schematic plus Nations channel mapping |
| `current_a_raw` | `currentA` | PA2 | 3 | High: published schematic plus Nations channel mapping |
| `vbus_raw` | `vBus` | PA3 | 4 | High: published schematic plus Nations channel mapping |

The two current inputs are outputs from the board's external GS8632
amplifiers. The internal MCU op-amps remain disabled. The active synchronous
path publishes the A/B pair atomically as `adc1_current_snapshot_t` and the
injected bus sample as `adc1_vbus_snapshot_t`; both reject raw values above
4095.

Channel identity, target synchronous acquisition, current signs, and dynamic
operation agree with the tested board. Its ADC reference and current-sense gain
have been measured and verified. Bandwidth, clipping, amplifier settling,
temperature/unit tolerance, and divider tolerance remain characterization
items. Displayed milliamperes use the measured scale but are not yet
protection-grade limits across temperature and production variation.

## Engineering conversion

The schematic-derived transfer functions are:

```text
current = (raw - zero_raw) * (adc_reference_volts / 4095)
          / (6.65 * 0.020 ohm)

vbus = raw * (adc_reference_volts / 4095) * 16.4
```

The current amplifier's mid-rail bias has unity gain; 6.65 is the verified
differential shunt-voltage gain, not 7.65. The bus divider is 15.4 kOhm above
1 kOhm. With the tested board's measured 3.3 V reference and fitted 20 mOhm
shunts, these factors are 6.059 mA per current count and approximately
13.22 mV per bus-voltage count. `adc_current_pair_convert_milliamperes()`
accepts the ADC reference and independent A/B zero counts at runtime. Firmware
0.19.0 uses 3.3 V and measures each zero from 32 synchronized samples over
approximately 320 ms while bridge authority remains inhibited.

## Acquisition design

`adc1_init_passive(hclk_hz)` has an explicit activation point and performs the
following bounded common setup:

1. Select the smallest supported synchronous HCLK divider that keeps the ADC
   sampling clock at or below 16 MHz for an HCLK from 1 to 64 MHz.
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
alive; the OLED shows the numeric status as `A####`. In that historical image,
`vBus` was not part of the synchronous sequence and its earlier 20 Hz polling
was inactive. On the tested board DMA channel 1 instead set
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
than one count during observation. With the measured 3.3 V reference, 895
corresponds to about 11.83 V. These are observations from one board, not
production calibration constants.

Firmware 0.13.0 displayed stable alternating A/B samples without `A0010`, so
the target acquisition architecture and corrected ADC/DMA-before-TIM3 startup
order are accepted as a bring-up result. The B channel has a visibly different
zero point from A, confirming that offsets must remain independent. Firmware
0.14.0 displays both zero-calibrated currents simultaneously;
with no commanded current they dither near 0 mA and remain within approximately
+/-12 mA. At 6.06 mA/count this is a roughly two-count residual, consistent
with the observed ADC quantization/noise floor.

Firmware 0.19.0 retains the proven ADC/DMA-before-TIM3 initialization order but
does not reuse the carrier-boundary trigger for switched-current regulation.
TIM2 is reset by TIM3 update, compares at 80% of the carrier, and its short ISR
sets the ADC software-start bit. The current channels use 7.5-cycle apertures
at the 16 MHz ADC clock. Low-zero sign-magnitude modulation confines
the loop's switching edges to the first 70% of the period under the current
phase-voltage bound, so sampling retains at least 5 microseconds after the latest
permitted PWM edge. The first firmware 0.30.0 timing burst measured trigger
delivery at 41.094 us and DMA entry 3.938 us later. Phase prediction, A/B
mapping, PI control, and compare staging then took 20.578-21.141 us end to end,
including priority-1 guardian preemption. The staged compares therefore miss
the 50 us update and become active at 100 us, with 33.031-33.594 us of preload
margin. Firmware 0.30.1 replaces the old 7 us estimate with the measured 55 us
DMA-completion-to-application prediction lead. DMA completion publishes one new
output generation; the TIM3 update guardian allows the intentional intervening
empty update and faults on a second consecutive update without a new output.

Firmware 0.28.0 adds a one-rank PA3 automatic-injected conversion after every
regular A/B pair. The regular pair still completes DMA and releases the 20 kHz
current loop first; VBUS conversion does not enter that interrupt path. At the
16 MHz ADC clock, the two 7.5-cycle current conversions require approximately
2.5 microseconds and the 55.5-cycle VBUS conversion requires approximately
4.25 microseconds. From the 80%-carrier trigger, the complete sequence therefore
ends near 46.75 microseconds in the 50-microsecond period, leaving a nominal
3.25-microsecond margin before the next trigger. Foreground accepts the latest
completed injected value without reconfiguring or stopping the production ADC.
Commissioning status schema 3 publishes its raw value, validity, and accepted
sample count; the host converts it to measured bus volts and converts the raw
phase-command ratio into commanded carrier-average phase volts. The timing
addition is bench-confirmed: inactive status reported 23.829 V at the 24 V
supply setting, all 22 active samples held 23.776-23.815 V during a one-second 1 rev/s /
606 mA run, 20,001 regular current-loop updates completed, the VBUS counter
advanced, and all ADC/deadline/control/reset/panic checks remained clear.

Firmware 0.29.0 makes an operator-acknowledged ADC/DMA or current-backend fault
recoverable without an MCU reset. `CLEAR_FAULTS` first holds the bridge in
direct-GPIO `ZERO`, disables and clears DMA channel 1 and ADC trigger state,
re-runs the bounded ADC start/calibration sequence, reinstalls the current-event
handler, and only then rebuilds zero-duty TIM3/current-loop state. It does not
require a successful sample before attempting the reset. Fresh samples remain
an ordinary control input for returning from `DIAGNOSTIC` to `READY`; a DMA,
range, or current condition that persists re-latches through the normal path.

Remaining analog work is to characterize temperature and unit-to-unit gain
tolerance, amplifier settling/bandwidth and clipping, and repeat across bus
voltage. Switching-correlated offset or noise should be quantified beyond
the successful current operating point. The 80%-phase trigger should still be
externally checked for switching-edge contamination. When explicitly armed,
the internal burst quantifies trigger delivery, ISR latency, and
conversion/control completion relative to the application boundary. Those
optional timer/cycle reads are dormant during ordinary control.
Analog-watchdog thresholds and production calibration/tolerance for the active
`vBus` measurement remain current/voltage-envelope work.

## Trust summary

The PA1/PA2/PA3 net routing and ADC channel mapping have high-confidence
schematic and manufacturer support. Ready, calibration, clock, resolution, and
conversion sequencing are high-confidence user-manual requirements. The ADC
analog-LDO write is medium-confidence because it is present in Nations' driver
but undocumented in the user manual. The resistor-derived and measured
scaling, passive readings, synchronous two-channel path, per-channel startup
zero calibration, milliamp display, dynamic sign, closed-loop tracking, and
motor rotation have bench support. Overall confidence is high for acquisition,
tested-board absolute current, and relative current regulation. Release-grade
protection thresholds still require temperature and production-tolerance
characterization.
