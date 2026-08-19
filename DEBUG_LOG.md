# Debug Log

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
