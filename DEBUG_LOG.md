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
