# Debugger Diagnostic Record

Status: implemented in the passive image and ABI-checked by the host tests. The record has not yet been read from physical hardware.

## Purpose

The first diagnostic channel is a structured RAM record exported as the ELF symbol `g_diagnostics`. It reports enough state to diagnose early bring-up without selecting or configuring a USART, RS-485 direction pin, display, or any additional board signal.

The record is not assigned a fixed SRAM address. A debugger locates it through symbols in the matching `mks57d.elf`. A future transport may serialize the same information, but that transport must not expose the in-memory C layout as an unframed wire protocol.

## Version 1 layout

All fields are naturally aligned 32-bit unsigned values. Schema version 1 is 52 bytes.

| Offset | Field | Meaning |
| ---: | --- | --- |
| 0 | `magic` | `0x4D4B5335` record identifier |
| 4 | `schema_version` | Record schema, currently `1` |
| 8 | `record_size` | Total bytes available, currently `52` |
| 12 | `sequence` | Odd while the foreground writer is updating, even when stable |
| 16 | `firmware_version` | Major in bits 31:24, minor in 23:16, patch in 15:0; currently `0.1.0` |
| 20 | `capabilities` | Passive-image, status-LED, IWDG, reset-cause, and NVIC-policy capability bits |
| 24 | `app_state` | Numeric `app_state_t` value |
| 28 | `uptime_millis` | Latest published 1 kHz timebase value |
| 32 | `heartbeat_count` | Number of provisional PB9 toggles completed |
| 36 | `watchdog_status` | Numeric `watchdog_status_t` value from the foreground supervisor |
| 40 | `platform_boot_status` | Numeric `platform_boot_status_t` value |
| 44 | `reset_flags` | RCC reset flags captured before they were cleared |
| 48 | `retained_panic` | Valid preceding panic retained across an IWDG reset, or `PANIC_NONE` |

The format is append-only within a schema: new fields may be appended and `record_size` increased, but existing fields must not be reordered or reinterpreted. An incompatible change increments `schema_version` and receives a separate consumer path.

## Consistent-read procedure

The cooperative foreground loop is the sole writer. It publishes immediately after watchdog initialization, after each 250 ms heartbeat, and immediately before entering a watchdog-related panic.

A live reader should:

1. Read `sequence`.
2. Retry if it is odd.
3. Read the record through the advertised size supported by the reader.
4. Read `sequence` again.
5. Accept the snapshot only when both sequence reads match and are even.
6. Validate `magic`, `schema_version`, and `record_size` before interpreting fields.

The Cortex-M data-memory barriers around publication keep the odd/even sequence contract ordered. A debugger that halts the passive image will normally see an already-stable record.

## Panic retention

`g_last_panic` remains in `.noinit`. Startup accepts it as preceding-boot history only when RCC reports an IWDG reset and the numeric code is in range. `diagnostics_init()` copies that value into `retained_panic`, then clears `g_last_panic` so a later watchdog-only stall cannot inherit an older software panic.

If firmware is currently stopped inside `platform_panic()`, inspect `g_last_panic` directly. After IWDG resets the MCU, the next boot publishes the same code into `g_diagnostics.retained_panic`.

## Hardware validation

During first passive bring-up:

- load the matching ELF symbols and inspect `g_diagnostics` before and after heartbeat changes;
- confirm `firmware_version` decodes to `0.1.0` and the record size is 52;
- confirm `sequence` is even when the core is halted;
- compare `reset_flags` against power-on, NRST, and induced IWDG resets;
- confirm a watchdog-related panic appears as `retained_panic` after reboot;
- leave PA6, PA7, PB0, and PB1 under oscilloscope observation throughout.

Serial and RS-485 diagnostics remain Phase 3 work because their pin assignments and direction-control behavior require the purchased board.
