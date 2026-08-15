# Memory Map and SRAM2 Startup

The initial firmware follows the N32L406xB memory map in N32L40x User Manual V2.6 rather than treating the part's 24 KiB SRAM total as one contiguous block.

| Region | Address range | Size | Initial use |
| --- | --- | ---: | --- |
| SRAM1 | `0x20000000`–`0x20003FFF` | 16 KiB | `.data`, `.bss`, `.noinit`, heap boundary, and stack |
| Unmapped gap | `0x20004000`–`0x20005FFF` | 8 KiB | Never used |
| SRAM2 | `0x20006000`–`0x20007FFF` | 8 KiB | Parity-initialized at reset; no allocations yet |

The initial vector stack pointer is `0x20004000`, so the first exception entry or `SystemInit` push lands inside SRAM1. The final 2 KiB of SRAM1 is reserved as the current stack budget, and link-time assertions reject overlap.

## SRAM2 initialization contract

The user manual warns that SRAM2 must be initialized before use because it is parity-protected. `SystemInit` therefore performs this sequence before normal application startup:

1. Write zero to every 32-bit word in SRAM2 without first reading the bank.
2. Execute a data-synchronization barrier.
3. Clear the SRAM2 parity-error status using its write-one-to-clear bit.
4. Execute another barrier and record the resulting status in boot diagnostics.

The linker deliberately rejects a nonempty `.sram2` section. This keeps the compiler, C runtime, and initial stack out of SRAM2 until the startup sequence and parity behavior are observed on a physical N32L406CBL7.

The SRAM2 parity reset/interrupt policy bits are left at their reset values for this first image because their descriptions in the available documentation are not sufficiently clear to make a new safety choice before bench testing.
