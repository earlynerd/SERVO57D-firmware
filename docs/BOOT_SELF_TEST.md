# Passive Boot Self-Test

Status: implemented and host-tested at the ledger layer, but not yet observed on physical hardware. Passing these software checks does not prove the schematic pin map or electrical bridge state.

## Contract

The passive image maintains a monotonic boot self-test ledger with three masks:

- `required`: every check that this image must complete before it is healthy;
- `passed`: completed checks that have not failed;
- `failed`: latched failures that cannot be cleared during the boot.

A failed check is removed from `passed`, added to `failed`, and cannot be restored by a later pass call. The image is ready only when `failed` is zero and every bit in `required` is present in `passed`.

## Required passive-image gates

| Bit | Gate | Runtime evidence |
| ---: | --- | --- |
| 0 | Early memory | SRAM2 parity initialization completed and the early platform state is ready |
| 1 | Clock | Reset-default 4 MHz MSI and bus/flash settings passed bounded verification |
| 2 | Interrupt policy | PRIGROUP read back as four preemption bits/no subpriorities |
| 3 | Passive board | GPIOA remains clock-gated; PB0/PB1 remain input/no-pull; provisional PB9 is the configured output |
| 4 | Timebase | 1 kHz SysTick configured with priority 15 under the expected grouping |
| 5 | Application state | The reset-safe state accepted only the passive-initialization transition into diagnostics |
| 6 | Watchdog | LSI and IWDG setup synchronized, verified, and started successfully |

Early-memory failure cannot safely initialize the diagnostic record because SRAM invariants are not established. It still enters the common panic path and remains debugger-visible through the platform status and panic code. Every later pass or failure is published immediately.

## Passive board invariant

`board_passive_invariants_hold()` performs read-only checks after `board_init_passive()`:

- the GPIOA peripheral clock is still disabled, so this image could not configure provisional bridge pins PA6/PA7;
- the GPIOB clock is enabled only because PB9 is the provisional status LED;
- PB0/PB1 mode fields remain reset-mode inputs;
- PB0/PB1 pull fields remain at no-pull;
- PB9 reads back as an output.

This is a construction check against accidental firmware edits, not proof that the purchased board uses those pins or that the external gate-driver state is safe. Oscilloscope observation and continuity checks remain mandatory.

## Diagnostic publication

Schema 1 of `g_diagnostics` appends the three ledger masks at offsets 52, 56, and 60, increasing `record_size` from 52 to 64 bytes without reordering existing fields. A debugger can watch boot progress bit by bit or identify the last failed gate.

## Watchdog relationship

The foreground passes health to the watchdog supervisor only while the application remains in diagnostics and `boot_self_test_ready()` remains true. A ledger failure therefore cannot be hidden by continued foreground execution or SysTick activity.

The IWDG is still a recovery layer rather than the immediate safety response. Once bridge operation exists, hardware and the common bridge-off primitive must act before any watchdog timeout.

## Bench validation

During first passive bring-up:

- verify `self_test_required == self_test_passed == 0x7F` and `self_test_failed == 0`;
- single-step or use controlled bench-only fault injection to observe individual gate publication;
- compare the GPIOA clock and GPIOB mode/pull register values against the self-test result;
- monitor PA6, PA7, PB0, and PB1 through power-on, reset, halt, resume, panic, and IWDG reset;
- treat any mismatch between the ledger and physical signals as a stop condition.
