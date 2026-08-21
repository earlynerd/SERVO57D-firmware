# Memory Map and SRAM2 Startup

The firmware follows the N32L406xB memory map in N32L40x User Manual V2.6 rather than treating the part's 24 KiB SRAM total as one contiguous block.

## Flash layout

| Region | Address range | Size | Use |
| --- | --- | ---: | --- |
| Application Flash | `0x08000000`–`0x0801EFFF` | 124 KiB | Vector table, code, constants, and initialized-data image |
| Configuration slot 0 | `0x0801F000`–`0x0801F7FF` | 2 KiB | Alternating versioned motor-configuration record |
| Configuration slot 1 | `0x0801F800`–`0x0801FFFF` | 2 KiB | Alternating versioned motor-configuration record |

The manual defines a 2 KiB minimum erase page and 32-bit programming. The
linker excludes both configuration pages from the application region and the
post-link verifier checks all three boundaries. A slot is valid only after its
magic, schema, length, generation, semantic bounds, CRC-32, and final commit
word validate. Updating erases only the inactive slot and programs its commit
word last, leaving the previous record recoverable across an interrupted write.

This reservation provides runtime reset/power-cycle persistence. Preservation
across a firmware programming operation depends on the programmer's erase
policy and is not part of the current update contract.

## SRAM layout

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
