# Debug Log

## 2026-08-20 — Firmware 0.22 configuration persistence passed hardware acceptance

- **Observation:** After flashing 0.22.0, automatic alignment save, unchanged-save wear avoidance, power-cycle restore, persistent clear, slot alternation, and safe boot restoration required validation on COM14.
- **Root cause:** No defect observed; `firmware/src/services/configuration_store.c:223` selected and verified alternating records, while `firmware/src/main.c:1241` restored only validated calibration and `firmware/src/main.c:1987` saved only after alignment completion and authority release.
- **Fix:** No firmware change required; accept the dual-slot runtime persistence path after generation 1 restore, generation 2 persistent clear, and generation 3 re-alignment all completed without authority, backend, fault, reset, or panic leakage.
- **Class:** configuration-persistence-bench-validation
- **Recently-touched?** yes
- **Time to fix:** no fix required; approximately one complete save/power-cycle/clear/power-cycle/restore pass

## 2026-08-20 — Firmware 0.20 estimator passed initial hardware regression

- **Observation:** After flashing 0.20.0, the new 1 kHz estimator needed identity, idle-noise, timing, direction, velocity, and active-load validation before servo integration.
- **Root cause:** No defect observed; `firmware/src/platform/timebase.c:36` and `firmware/src/main.c:1307` produced valid timestamped samples at idle and through a 757 mA / 20 Hz bounded run.
- **Fix:** No firmware change required; accept the foreground estimator schedule under the present workload and retain readiness-loss injection plus outer-loop-load revalidation as later gates.
- **Class:** encoder-estimator-bench-validation
- **Recently-touched?** yes
- **Time to fix:** no fix required; approximately one bench-validation pass

## 2026-08-17 — Valid L0 option word crashed J-Link flash preflight

- **Observation:** After J-Link read `FLASH_OB = 0x03FFFFFC`, `flash-jlink.ps1` stopped with `Cannot convert value "-2147483648" to type "System.UInt32"` before programming.
- **Root cause:** `tools/flash-jlink.ps1:200` — PowerShell parsed the `0x80000000` RDP2 mask literal as signed `Int32.MinValue`, and the explicit `UInt32` cast threw before the bit test.
- **Fix:** Construct the RDP2 mask with `Convert.ToUInt32("80000000", 16)` and reuse the resulting unsigned value.
- **Class:** powershell-signed-hex-literal
- **Recently-touched?** yes
- **Time to fix:** approximately 5 minutes

## 2026-08-17 — Passive bridge check rejected safe reset pin modes

- **Observation:** The corrected image programmed and verified, but the blue LED never blinked after reset or a power cycle.
- **Root cause:** `firmware/src/board/board.c:69` — the post-peripheral bridge invariant accepted only PMODE `00` (analog), while the N32L406 reset-default bridge pins remained safely high-impedance in PMODE `11` (digital input), causing panic code `0x0A` before the heartbeat loop.
- **Fix:** Treat analog and floating digital-input modes as non-driving while continuing to reject output, alternate-function, or pull-enabled bridge pins; the corrected image was confirmed blinking on hardware.
- **Class:** gpio-safe-state-encoding-assumption
- **Recently-touched?** yes
- **Time to fix:** approximately 20 minutes

## 2026-08-17 — RS-485 beacon absent at the expected connector

- **Observation:** The USB RS-485 adapter received no traffic in either A/B polarity even though firmware initialization and transmit-completion diagnostics succeeded.
- **Root cause:** The published schematic's connector net labels do not match the tested board's observed connectivity: the beacon is present only on the connector labeled `485_A2`/`485_B2`, not the connector apparently tied to the transceiver as `485_A`/`485_B`.
- **Fix:** Move the adapter to the `485_A2`/`485_B2` connector. Configure the terminal for the firmware's intended 115200 baud, 8 data bits, no parity, and 1 stop bit; the earlier corrupt display was caused by YAT being set to 1.5 stop bits.
- **Class:** schematic-to-hardware-connector-mismatch
- **Recently-touched?** no
- **Time to fix:** approximately one bench-debug session

## 2026-08-18 — Terminal EOL bytes corrupted every request after the first

- **Observation:** One native RS-485 request received a valid response after reset, while repeated copies received no reply even though the UART baud rate and physical link were correct.
- **Root cause:** `firmware/src/protocol/native_protocol.c:554` — YAT appended `0D 0A` after every zero-delimited COBS request, so the streaming parser accumulated those legal nonzero bytes as the prefix of the next candidate frame. The live DMA ring contained five complete requests each followed by CR/LF, while diagnostics reported 70 bytes consumed, one valid frame and response, and four CRC failures.
- **Fix:** Disable YAT's end-of-line suffix for binary sends; repeated requests and responses were then confirmed working without a reset between frames.
- **Class:** host-tool-framing-configuration
- **Recently-touched?** no
- **Time to fix:** approximately one bench-debug session

## 2026-08-18 — Display test label crowded its border

- **Observation:** The OLED initialized and drew the complete test image, but the `M` intersected the left border and the `D` touched the right border.
- **Root cause:** `firmware/src/drivers/display_test_pattern.c:11` — the useful 70-pixel-wide label was unnecessarily enclosed by a border on the 72-pixel-wide panel.
- **Fix:** Remove the now-proven window-test border and retain the full-size label across pixels 1-70.
- **Class:** display-test-pattern-layout
- **Recently-touched?** yes
- **Time to fix:** approximately 5 minutes

## 2026-08-18 — Encoder position display passed rotational bench check

- **Observation:** The displayed raw position follows shaft motion consistently, remains stationary with the shaft stopped, and rolls over at the same mechanical point on every revolution.
- **Root cause:** No defect observed; `firmware/src/drivers/mt6816.c:47` assembles the coherent 14-bit angle and `firmware/src/main.c:396` samples it at 100 Hz before the 50 Hz display update.
- **Fix:** No firmware change required; accept coherent sampling, direction response, stationary stability, and repeatable wraparound as bench-proven for this board.
- **Class:** encoder-bench-validation
- **Recently-touched?** yes
- **Time to fix:** no fix required; one flash-and-rotate validation

## 2026-08-18 — Passive current and bus ADC readings passed first bench check

- **Observation:** With 12 V input and the bridge disabled, `currentA` reads 2041, `currentB` reads 2053, and `vBus` reads 895; each channel fluctuates by no more than one ADC count.
- **Root cause:** No defect observed; `firmware/src/platform/adc1.c:258` through `:268` performs the intended independent currentB, currentA, and vBus conversions, and `firmware/src/main.c:437` publishes only complete valid samples.
- **Fix:** No firmware change required; accept mid-rail current bias, one-count stationary noise, and a stable 12 V bus response as bench-proven for this board.
- **Class:** passive-adc-bench-validation
- **Recently-touched?** yes
- **Time to fix:** no fix required; one flash-and-observe validation

## 2026-08-18 — Mandatory ADC startup blanked the OLED and stopped heartbeat

- **Observation:** After flashing firmware 0.12.0, the OLED was blank from boot and the blue heartbeat LED never blinked.
- **Root cause:** `firmware/src/main.c:307-311` in firmware 0.12.0 cleared the initialized OLED, then treated any synchronous ADC startup error as a terminal bridge-characterizer panic before the watchdog, heartbeat, or first display render began.
- **Fix:** Firmware 0.12.1 keeps PWM inhibited on acquisition failure while allowing diagnostic boot to continue, and displays the numeric ADC status as `A####`; the underlying ADC failure code remains to be read on hardware.
- **Class:** fail-closed-observability-loss
- **Recently-touched?** yes
- **Time to fix:** approximately 15 minutes; bench confirmation pending

## 2026-08-18 — ADC DMA channel 1 reports an immediate bus error

- **Observation:** Firmware 0.12.1 restored heartbeat and displayed `A0010`; enum value 10 is `ADC1_STATUS_DMA_ERROR`. The status originates from `DMA_INTSTS.ERRF1` in `adc1_read_synchronized_current()`.
- **Code audit:** The emitted image writes `PADDR=0x4002084C` (`ADC_DAT`), `MADDR=0x20000228` (aligned SRAM1), transfer count 64, ADC request select 0, circular mode, memory increment, and 16-bit peripheral/memory widths before enabling channel 1. Those addresses and encodings match the device header and linker image.
- **Manufacturer evidence:** N32L40x user manual V2.6.0 defines `ERRF` as a returned bus error/reserved-address access and says hardware clears `CHEN`. Nations SDK 2.3.0 `ADC/ADC_DMA` uses the same DMA channel, request, `ADC_DAT` address, and 16-bit widths. Errata item 5.4 concerns stale first data after DMA re-enable, not an immediate bus error.
- **Next isolation:** Firmware 0.12.2 mirrors the SDK example with one PA2 channel, software-triggered continuous conversion, a single fixed halfword DMA destination, DMA enabled before ADC start, and the result displayed as `A####`. TIM3 triggering, multi-rank scan, memory increment, and bridge switching are excluded.
- **Class:** dma-adc-bus-error
- **Recently-touched?** yes
- **Status:** Unresolved pending firmware 0.12.2 bench result.

## 2026-08-18 — Manufacturer-example fixed ADC-DMA destination works

- **Observation:** Firmware 0.12.2 continuously displayed PA2 `currentA` between 2039 and 2044, typically 2042, with the bridge held at the all-low vector.
- **Root cause:** No defect remains in the basic path. The result proves ADC DMA request 0, DMA channel 1, `ADC_DAT`, the SRAM1 destination, 16-bit peripheral/memory widths, circular mode, software-triggered continuous conversion, and the manufacturer initialization order. The firmware 0.12.1 `ERRF` must depend on one or more removed features.
- **Next isolation:** Firmware 0.12.3 changes only the DMA destination behavior: restore memory increment, transfer count 64, and the 64-halfword circular buffer while retaining one PA2 channel and software-triggered continuous conversion. Bridge switching remains suppressed.
- **Class:** adc-dma-isolation-pass
- **Recently-touched?** yes
- **Status:** Basic ADC-DMA path resolved; original ring/scan/trigger failure remains under investigation.

## 2026-08-18 — Incrementing 64-halfword ADC DMA ring works

- **Observation:** Firmware 0.12.3 produced the same stable PA2 result as the fixed-destination image, with no `A0010` DMA error.
- **Root cause:** `firmware/src/platform/adc1.c` memory increment, transfer count 64, circular reload, SRAM range, and stable latest-sample extraction are not causes of the firmware 0.12.1 failure.
- **Fix:** Firmware 0.13.0 skips the remaining isolated scan test and restores the target two-rank `currentB/currentA` TIM3-triggered acquisition. ADC/DMA are now fully configured and armed before `firmware/src/board/board.c` starts TIM3, correcting the principal initialization-order difference from the manufacturer examples.
- **Class:** adc-dma-isolation-pass
- **Recently-touched?** yes
- **Status:** Ring path resolved; target synchronous acquisition pending firmware 0.13.0 bench result.

## 2026-08-18 — Target TIM3-synchronous A/B acquisition works

- **Observation:** Firmware 0.13.0 alternated stable A/B readings on the OLED without a DMA error; both channels behaved like the earlier isolated acquisition, with a small repeatable difference between their zero-current offsets.
- **Root cause:** The firmware 0.12.1 failure was an ADC/DMA/TIM3 initialization-order defect. That image reconfigured the ADC/DMA path after TIM3 was already producing triggers. In the working path, `firmware/src/main.c:309` arms the complete acquisition path before `firmware/src/main.c:312` starts TIM3, while `firmware/src/platform/adc1.c` enables DMA before powering, calibrating, and externally arming the ADC. The bench test does not isolate the failing sub-step more narrowly, but it excludes the ring, two-rank scan, and TIM3 trigger as inherent causes.
- **Fix:** Retain the manufacturer-style DMA-before-ADC order and start TIM3 only after the complete acquisition path is armed. Accept the 20 kHz two-rank transport as the current-controller input path, and calibrate the independently observed A/B offsets separately.
- **Class:** adc-dma-timer-initialization-order
- **Recently-touched?** yes
- **Status:** Resolved and bench-proven on firmware 0.13.0.

## 2026-08-18 — Independent zero calibration and milliamp display work

- **Observation:** Firmware 0.14.0 displays both A and B simultaneously in signed milliamperes. With no commanded current, both dither near 0 mA and remain within approximately +/-12 mA.
- **Root cause:** No defect observed. The residual display motion is approximately two 6.06 mA ADC counts at nominal 3.3 V scaling, consistent with quantization and the previously observed one-to-two-count raw noise. Independent 32-sample startup averages remove the distinct A/B DC offsets.
- **Fix:** No firmware fix required; accept startup zero calibration, sign conversion, compact dual-channel rendering, and nominal milliamp scaling as bench-proven. Actual ADC-reference and analog gain tolerance remain calibration work before these readings become protection-grade.
- **Class:** current-scaling-bench-validation
- **Recently-touched?** yes
- **Status:** Resolved and bench-proven on firmware 0.14.0.

## 2026-08-18 — HSE/PLL 64 MHz clock migration works

- **Observation:** Firmware 0.15.0 boots and behaves the same as the proven 4 MHz image after moving to the fitted 8 MHz HSE through PLL at 64 MHz.
- **Root cause:** No defect observed. `firmware/src/platform/system.c` allows boot to continue only after hardware reports HSE and PLL ready, PLL selected as SYSCLK, 64 MHz HCLK, one Flash wait state, PCLK1 16 MHz, PCLK2 32 MHz, and the doubled 32 MHz APB1 timer clock. Normal heartbeat and peripheral behavior exercise the derived clocks after that gate.
- **Fix:** No firmware fix required; accept the 64 MHz clock tree and per-peripheral clock derivation as bench-proven. Retain MSI only as the reset/startup source.
- **Class:** system-clock-bench-validation
- **Recently-touched?** yes
- **Status:** Resolved and bench-proven on firmware 0.15.0.

## 2026-08-18 — Delayed TIM2 current trigger produced no ADC samples

- **Observation:** Firmware 0.16.0 remained at `A-------mA` indefinitely after flashing, although the immediately preceding TIM3-triggered image produced valid calibrated A/B current readings. Enabling TIM2 CC2 in firmware 0.16.1 did not change the symptom.
- **Root cause:** The new direct TIM2_CC2-to-ADC trigger path did not produce accepted conversions on the tested board. The exact internal-event failure is unresolved; the original claim that `CC2EN` alone was the cause was disproved by the 0.16.1 bench result.
- **Fix:** Firmware 0.16.2 restored the manufacturer-style ADC/DMA-before-TIM3 order, direct TIM3-update trigger, and 28.5-cycle apertures. Calibrated current display returned immediately, proving the ADC/DMA path remained functional.
- **Class:** timer-trigger-path-regression
- **Recently-touched?** yes
- **Status:** Missing-sample regression resolved and bench-confirmed in firmware 0.16.2; exact direct TIM2_CC2 routing failure remains unexplained.

## 2026-08-18 — Current loop drops out immediately after Enter

- **Observation:** With a motor attached, holding Enter produces only an infinitesimal shaft movement. The OLED never captures nonzero current, and the bench supply confirms that the nominal 150 mA command is not sustained.
- **Code audit:** An interrupt fault can stop the backend before the 200 ms OLED current refresh. The candidates are the 100-count raw trip in `phase_current_loop_step()`, PWM staging failure in `adc_current_event()`, and the consecutive-empty-update guardian in `pwm_update_event()`. Firmware 0.16.2 also sampled at TIM3 update, a switching boundary that the project log explicitly had not accepted for switched-current feedback.
- **Fix under test:** Firmware 0.16.3 moves the trigger to a 65%-phase TIM2 compare ISR, restores 7.5-cycle current apertures, and persistently displays the first latched loop fault as `F####`. It does not change the 25-count reference, 100-count raw trip, PI gains, voltage bound, or duty bound.
- **Class:** immediate-current-loop-dropout
- **Recently-touched?** yes
- **Status:** Corrected diagnostic/timing image built; bench result pending.

## 2026-08-18 — OLED remains zero and button test provides no diagnostic sample

- **Observation:** Firmware 0.16.3 continuously displays `00000mA`; pressing or holding Enter produces no perceptible shaft motion and no OLED fault code. The OLED's 200 ms cadence cannot show a drive interval shorter than one refresh.
- **Code audit:** `firmware/src/main.c` drained and parsed RS-485 only while `bridge_characterizer.active` was false. The on-wire service exposed identity and capabilities but none of the ADC, input-authority, current-loop, PWM-duty, sample-count, or fault state needed to separate an input/authority failure from an interrupt/backend failure. This is an observability defect, not yet proof of the motor-current root cause.
- **Fix under test:** Firmware 0.17.0 keeps RS-485 parsing live during RUN and adds a schema-versioned commissioning status query plus inactive-only reference configuration, duration-bounded START, and always-available STOP. The host console emits JSON snapshots before, during, and after a run.
- **Class:** current-loop-observability-gap
- **Recently-touched?** yes
- **Status:** Host tests and target build pass; bench query result pending.

## 2026-08-18 — RS-485 proves authority starts but no loop sample completes before reset

- **Observation:** On firmware 0.17.0, the inactive RS-485 snapshot was completely ready with no faults and raw A/B exactly at their calibrated zeros. A five-second START was accepted and the first immediate snapshot showed foreground authority active, backend active, A reference 25 counts, `sample_count=0`, zero measured currents, and zero output duties. The next 200 ms query timed out. After communication returned, authority and faults were clear, `sample_count` was zero, and B had recalibrated from 2059 to 2057, proving a reset occurred.
- **Code audit:** A fast-loop fault disables the backend in `current_loop_backend.c`. Before the 10 ms status publisher can preserve it, the 1 ms reference update in `main.c` calls `current_loop_backend_set_reference_counts()`, treats the expected rejection as an invariant failure, enters `platform_panic(PANIC_BRIDGE_CHARACTERIZER_INIT)`, and is reset by IWDG. The reset explains both the timeout and erased volatile fault. The underlying fast-loop fault remains to be identified; zero completed samples makes raw overcurrent on the first switched sample and the no-output deadline guardian the remaining discriminated outcomes.
- **Fix under test:** Firmware 0.17.1 handles a latched backend fault as a preserved `ZERO` stop instead of a panic, retains the fault-causing measured current when the phase loop received a sample, and extends commissioning status schema 2 with retained panic and watchdog-reset evidence.
- **Class:** current-loop-fault-evidence-erased-by-reset
- **Recently-touched?** yes
- **Follow-up:** Firmware 0.17.1 repeated the same sequence and reset before the second poll. After reboot, commissioning schema 2 reported retained panic 0 and no IWDG reset. The bench supply was limited to 1 A and never entered constant-current mode, excluding a sustained or supply-limited greater-than-1-A load. The reset is therefore earlier/different than the corrected foreground panic path; pin, power-on/brownout, software, and other RCC reset classes remain to be separated.
- **Status:** Root cause of the 0.17.0 panic path resolved, but it was not the only reset mechanism. Firmware 0.17.2 adds full RCC reset flags and uptime over RS-485; exact reset cause pending one run.

## 2026-08-18 — Current-loop start asserts the external reset class

- **Observation:** Firmware 0.17.2 booted cleanly and reported a 70-second baseline uptime. A five-second A1 START again showed authority active, backend active, a 25-count A reference, zero completed samples, and zero published duties, then the first 200 ms follow-up timed out. After communication returned, uptime was 8.4 seconds and the complete RCC record contained only `PINRSTF` (`0x04000000`). Retained panic was zero and neither watchdog, software, nor power-on reset was reported. The 1 A bench supply had remained in constant-voltage mode.
- **Code audit:** `firmware/src/platform/current_loop_backend.c` staged `{500,500,500,500}` before marking the loop active, so all four tied EG3013 inputs began 20 kHz 50% switching together before the first ADC completion. The bench-proven characterizer instead drove one selected TIM3 channel and held the other three at zero. `firmware/src/control/phase_current_loop.c` perpetuated the same four-leg centered switching for every command.
- **Fix under test:** Firmware 0.17.3 starts from `{0,0,0,0}` and uses low-zero sign-magnitude duties: only one leg per commanded phase switches, with the opposing leg held low. The 10% phase-voltage limit, 25-count reference, 100-count raw trip, and all fault shutdown paths are unchanged.
- **Class:** pwm-common-mode-reset-coupling
- **Recently-touched?** yes
- **Status:** Reset class resolved as external-pin reset; modulation correction built pending bench confirmation.

### Follow-up — Debugger removal and low-zero modulation do not stop PINRSTF

- Firmware 0.17.3 was confirmed over RS-485 with the debugger disconnected and remained healthy for 57 seconds before START. The five-second A1 command was accepted, but the next 200 ms poll again timed out before one completed loop sample. After reboot, uptime was 8.8 seconds and the sole reset flag was again `PINRSTF` (`0x04000000`).
- This excludes the attached probe and the prior `{500,500,500,500}` startup waveform as sufficient causes. The temporary wire still soldered to NRST is the leading isolated variable and must be removed or terminated before another firmware hypothesis is tested.
- **Status:** Unresolved external NRST assertion; no further firmware change indicated until the NRST lead is eliminated.

## 2026-08-18 — Sticky ADC start flag limited synchronous acquisition to one sample

- **Observation:** Firmware 0.17.5 completed startup calibration but a bridge run latched backend deadline fault `F0019` without a completed loop output. Firmware 0.17.6 restored changing current samples on the OLED and completed 75 current-loop samples during the next bounded run.
- **Root cause:** `firmware/src/platform/adc1.c` treated `ADC_STS.STR` as a live conversion-busy flag. On the N32L40x it is a sticky regular-conversion-started status set by hardware and cleared by software, so every trigger after the first conversion was rejected. Foreground calibration also counted repeated reads of that one DMA snapshot as distinct samples.
- **Fix:** Clear `ENDC`, `STR`, and `ENDCA` after each DMA-complete sequence, and require a fresh DMA sequence number for every foreground synchronized-current read.
- **Class:** adc-sticky-status-misinterpreted-as-busy
- **Recently-touched?** yes
- **Status:** Resolved and bench-confirmed on firmware 0.17.6.

## 2026-08-18 — Uniform phase-leg mapping made the A current loop positive feedback

- **Observation:** Firmware 0.17.6 completed 75 loop samples after an A1 start, but measured A moved to -101 counts while the reference and A1 voltage command were positive, then latched `overcurrent_a`. The supply never reached its 1 A current limit.
- **Root cause:** `firmware/src/control/phase_current_loop.c` mapped positive voltage to leg 1 for both windings. The board is asymmetric: A1 is A+ and the A shunt is under its low-side FET, so positive measured A current is A- to A+ and requires A2 drive. B1 is B+ while the B shunt is under B-, so positive B current correctly requires B1 drive.
- **Fix:** Map positive A voltage to A2 and negative A voltage to A1 while retaining positive B to B1 and negative B to B2. Update the selected-leg initial phases so their names continue to identify the first driven leg.
- **Class:** asymmetric-bridge-current-polarity
- **Recently-touched?** yes
- **Status:** Resolved and bench-confirmed on firmware 0.17.7. Two 150 mA one-second runs completed approximately 40,000 combined samples across all four legs without a fault; a subsequent 300 mA, 5 Hz, three-second run completed 59,900 samples without a fault or reset.

## 2026-08-19 — Encoder telemetry confirms synchronized motor rotation

- **Observation:** Firmware 0.17.8 ran a 50-count nominal 300 mA, 5 Hz electrical current vector for three seconds while streaming the 100 Hz encoder acquisition. During active authority the encoder moved monotonically from 9839 to 4993 counts in 2.973 seconds with no encoder or SPI errors and no sensor flags.
- **Root cause:** No remaining motor-motion defect was observed. Earlier firmware could prove rotating stator current but could not distinguish it from a stationary rotor because encoder acquisition paused during bridge authority and was absent from RS-485 telemetry.
- **Fix:** Keep encoder acquisition active during the current test and expose it through `GET_ENCODER_STATUS`; the host `run` stream now correlates mechanical angle with current references and duties.
- **Class:** motor-rotation-bench-validation
- **Recently-touched?** yes
- **Status:** Resolved and bench-confirmed on firmware 0.17.8. Measured motion was -0.2958 revolution at -5.97 RPM versus the 6.00 RPM expected for a 5 Hz vector and 50 electrical cycles per mechanical revolution. The current loop completed 59,905 samples without a control fault or reset.

## 2026-08-19 — Free-rotor motion distorted single-phase current-step tuning

- **Observation:** A 50-count B1 startup trace appeared to have 38% overshoot, 12.46-count tail RMS error, and 23 counts of cross-axis current, while the preceding A1 trace settled with 4% overshoot and 0.73-count tail RMS error.
- **Root cause:** `firmware/src/main.c:94-106` selects a static single-phase vector and `firmware/src/control/rotating_current_test.c:83-89` holds it during the 0.001 Hz test. With the rotor free, that vector produces torque; encoder motion and back-EMF changed both measured axes during the 12.8 ms record. A polarity-balanced 25-count repeat moved the fast response between A/B according to torque direction instead of following one ADC channel.
- **Fix:** Treat the four-polarity free-rotor traces as disturbance-response data rather than an independent phase-step plant test; make only one bounded proportional-gain change and repeat the same comparison before accepting it.
- **Class:** current-loop-test-mechanical-contamination
- **Recently-touched?** no — the new trace records the staged controller result after `firmware/src/platform/current_loop_backend.c:103` and did not cause a deadline fault.
- **Time to fix:** approximately 35 minutes of code audit and bounded bench traces.

## 2026-08-19 — Kp=2 rotating-vector tuning passed the bounded sweep

- **Observation:** Doubling proportional gain did not consistently improve the mechanically contaminated A1/A2/B1/B2 free-rotor startup traces, but the representative rotating-vector loop remained stable and slightly improved at 5 Hz.
- **Root cause:** No firmware defect remained. Static single-phase steps accelerated the free rotor, so their apparent rise and tail metrics included rotor motion and back-EMF. The 303 mA rotating-vector sweep better represents the intended current-loop workload. Its 20 Hz degradation coincided with the unchanged 100-permille voltage ceiling rather than instability or excess PI gain.
- **Fix:** Accept firmware 0.18.1 `Kp=2`, retain `Ki=1/64` per 20 kHz step and all existing bounds, and document 5-10 Hz as the well-controlled commissioning band, 15 Hz as a degraded edge, and 20 Hz as voltage-headroom limited. Restore the inactive volatile test configuration to the firmware default of 25 counts at 0.5 Hz after testing.
- **Class:** current-loop-tuning-validation
- **Recently-touched?** yes — only `firmware/src/main.c` changed the loop behavior; the trace, PWM, sampling, and fault paths were unchanged from the validated 0.18.0 image.
- **Status:** Resolved and bench-proven on firmware 0.18.1. All startup traces and 5/10/15/20 Hz runs completed without a control, ADC, encoder, reset, or protocol fault; only 20 Hz briefly reached the phase-voltage ceiling.
- **Time to fix:** approximately 50 minutes of code audit, bounded bench sweeps, and analysis.

## 2026-08-19 — Conservative commissioning ceilings limited visible motion

- **Observation:** The motor had only been seen moving slowly, and the active default after tuning was 151.5 mA at 0.5 electrical Hz (approximately 0.6 RPM). The user requested faster, larger motion at up to 1 A and established high-performance motor drive as the project target.
- **Root cause:** `firmware/src/main.c:567-572` retained commissioning-era 303 mA, 10%-bus voltage, and 20 Hz ceilings. The voltage ceiling was coupled to `firmware/include/mks57d/tim2_current_trigger.h:10`, where the 30%-carrier sample deliberately followed every permitted PWM edge; simply raising duty would move the asymmetric-shunt measurement outside its guaranteed zero-vector window.
- **Fix under test:** Firmware 0.18.2 raises the reference/trip to 1.00/1.21 A, ADC clock to 16 MHz, zero-vector sample to 80% of the carrier, phase voltage to 70% of the bus, and test frequency to 50 electrical Hz. This preserves 5 microseconds from the latest permitted PWM edge to sampling and approximately 7 microseconds for conversion-complete control/preload work.
- **Class:** commissioning-envelope-overconstraint
- **Recently-touched?** yes — the limiting constants and current-trace tuning image were the current working-tree focus.
- **Status:** Resolved and bench-confirmed on firmware 0.18.2. Identity, boot, and status were clean after flash; a 303 mA step completed with 6.53 ms 10-90% rise time, 8% overshoot, and 14.0 mA tail RMS error. Staged 454 mA / 10 Hz and 606 mA / 15 Hz runs tracked -12 and -18 RPM without faults. A visible 757 mA / 20 Hz, five-second run completed 100,000 loop updates and 1.97 mechanical revolutions versus 2.00 commanded, with zero current-loop, ADC, encoder, reset, or protocol faults. Peak observed phase-voltage effort was 25.2% of the bus, leaving substantial headroom below the 70% ceiling.
- **Time to fix:** approximately 55 minutes of code/timing audit, implementation, build validation, bounded bench expansion, and analysis.

## 2026-08-20 — Firmware 0.21 automatic alignment passed initial hardware acceptance

- **Observation:** After flashing 0.21.0, automatic alignment needed identity, readiness, current/encoder health, repeatability, transactional calibration, generic STOP, authority release, and reset/fault validation on COM14.
- **Root cause:** No defect observed; `firmware/src/control/alignment_controller.c:246`, `firmware/src/main.c:388`, and `firmware/src/main.c:1698` produced two identical successful 757.4 mA sequences of `9302 → 9222 → 9302`, followed by a clean STOP-aborted attempt.
- **Fix:** No firmware change required; accept successful/repeatable alignment and generic STOP on the tested motor while retaining Menu and induced encoder-readiness-loss injection as later physical tests.
- **Class:** automatic-alignment-bench-validation
- **Recently-touched?** yes
- **Time to fix:** no fix required; approximately one bounded bench-validation pass

## 2026-08-21 — Validated torque envelope was incorrectly used as command permission

- **Observation:** Firmware 0.23.0 refused an aligned-torque interval above one second. The same reported policy would also refuse half of the attached 3 A motor's rated current and any open-torque motion above 1 rev/s or 20 rev/s².
- **Root cause:** `firmware/src/main.c` installed the initial 100-1,000 ms, 125-count, 1 rev/s, and 20 rev/s² hardware-gate candidates as the only production command policy. `firmware/src/control/aligned_torque_controller.c` and the host tool then correctly enforced those advertised values; the rejection was not a protocol or hardware fault. The design had conflated the last validated point with permission to evaluate the next one.
- **Fix:** Firmware 0.23.1 derives the torque duration range from feedback/deadline timing (3 ms through `INT32_MAX`), opens the torque and shared current backend to a 248-count/1.503 A medium evaluation point, raises the independent raw trip to 300 counts/1.818 A, and records the remaining velocity and acceleration values by limit class. Host/native tests and clean Debug/Release Arm builds pass. Hardware validation starts from the existing 757.4 mA evidence before advancing through 909 mA, 1.212 A, and 1.503 A.
- **Class:** validated-envelope-used-as-command-ceiling
- **Recently-touched?** yes — the 0.23.0 aligned-torque integration introduced the rejected policy.
- **Status:** Software rejection resolved; multi-second and expanded-current COM14 confirmation pending.

### Follow-up — Evaluation permission expanded to the stated motor/speed boundary

The first correction still stopped at half of the motor's rated current and proposed fixing phase-refresh quality before permitting a higher speed. That retained the same validation-before-evaluation mistake. The final 0.23.1 candidate instead permits 2.999 A nominal, 5 rev/s, 1,000 rev/s² observed acceleration, 10,000 counts/s slew, and 250 electrical Hz so the present 1 kHz phase-refresh and current-loop boundaries can be measured directly. Independent current trip, voltage/duty timing, feedback freshness, finite deadline, STOP, supervisor, and common fault shutdown remain active.

## 2026-08-21 — Torque start charged command latency to feedback timing

- **Observation:** On freshly flashed firmware 0.23.1, a 5-count, 5,000 ms aligned-torque request was accepted but entered `failed / feedback_timing` at zero elapsed milliseconds after only four 20 kHz backend samples. No current reference was applied, the backend reported no current-loop fault, and the bridge returned to `ZERO`. Encoder health remained clean at a 1,000 us latest interval but reported a 5,117 us cumulative maximum against the controller's 2,000 us active-feedback contract.
- **Root cause:** `firmware/src/main.c` consumed the torque request in the protocol-processing portion of the foreground loop and seeded `aligned_torque_controller_start()` from the prior `angle_tracker.last_timestamp_us`. The next controller update occurred only after the encoder read later in the loop, so request receive/parse latency was incorrectly included in the first active feedback interval. `firmware/src/control/aligned_torque_controller.c` correctly rejected that stale interval; the watchdog was not the defect.
- **Fix:** Firmware 0.23.2 keeps the request pending until a new encoder sample has been accepted, seeds phase/velocity/timestamp and starts the zero-reference backend from that observation, and uses the following accepted sample as the first controller update. Menu now cancels a pending start as well as an active run. A host regression locks the no-same-sample-update contract.
- **Class:** pre-authority-command-latency-counted-as-feedback-age
- **Recently-touched?** yes — the 0.23.0 aligned-torque integration introduced the protocol-loop start ordering.
- **Status:** Resolved in source; host and clean Debug/Release Arm validation pass, COM14 confirmation pending.

## 2026-08-21 — Cold estimator configuration was mistaken for invalid configuration

- **Observation:** Firmware 0.24.0 entered the platform panic path before normal service after moving rotor-control initialization into the new runtime owner.
- **Root cause:** Startup required an angle tracker to be sample-ready even though a correctly configured tracker is intentionally not ready until its first accepted encoder observation.
- **Fix:** Firmware 0.24.1 validates the tracker configuration independently from first-sample readiness and retains readiness as a runtime state.
- **Class:** cold-state-validity-conflation
- **Recently-touched?** yes
- **Status:** Resolved; host regression and subsequent 0.24.x hardware boots pass with no retained panic.

## 2026-08-21 — Forced timer updates duplicated the encoder transaction

- **Observation:** The first timer/DMA encoder images produced one failed transfer per millisecond. DMA channels 2/3 showed simultaneous `ERRF`; the interrupt-driven SPI isolation image failed similarly. Later images reduced this to one startup-only error followed by exact 1 kHz operation.
- **Root cause:** TIM7 update interrupts were enabled while `EVTGEN.UDGN` force-loaded the one-shot timer, creating a synthetic pending interrupt in addition to the real CS delay. The duplicate interrupt re-entered the transport during `TRANSFER`. TIM6 initialization had the same forced-update exposure. The N32L40x also demonstrates a first-transfer DMA anomaly, and mode-3 SCK was not explicitly held high before SPI enable.
- **Fix:** Firmware 0.24.13 masks timer update interrupts around forced loads, drains peripheral/NVIC pending state with barriers, arms both DMA channels before exposing SPI requests, holds SCK at the CPOL-high idle level before enabling SPI, and treats one post-power-up DMA exchange as initialization priming rather than a rotor sample. Every later error remains reportable.
- **Class:** encoder-timer-dma-startup-order
- **Recently-touched?** yes
- **Status:** Resolved and bench-proven. Idle operation reported zero errors across more than 54,000 samples at 1000-1001 us intervals. A 606 mA aligned-torque run then completed 100,000 current-loop samples over five seconds with zero encoder, DMA, estimator, backend, control, reset, or panic faults and returned every duty/reference to zero at deadline.

## 2026-08-21 — Velocity capture flooded the terminal with repeated snapshots

- **Observation:** The first flashed 0.25.0 velocity test produced an unwieldy amount of JSON in the terminal during a two-second command.
- **Root cause:** Pre-fix `tools/mks57d_rs485.py:1362-1365` queried velocity, drive, and encoder status every capture interval, attached the complete nested objects—including unchanged policy—and printed the whole result as a new line every 20 ms.
- **Fix:** Store static context once in `metadata.json`, stream selected dynamic fields incrementally to `telemetry.csv`, overwrite a compact live line at about 5 Hz, and make full nested JSONL capture opt-in.
- **Class:** telemetry-output-amplification
- **Recently-touched?** yes — the initial velocity CLI was added in the same development session.
- **Time to fix:** approximately 25 minutes.
