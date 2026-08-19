# Safe Bring-up Boot Self-Test

Status: implemented and host-tested at the ledger layer. The image proceeds
through these gates on the bench-proven board, but the masks have not yet been
correlated through a debugger with physical bridge-pin waveforms. Passing the
software checks does not prove the external electrical bridge state.

## Contract

The safe bring-up image maintains a monotonic boot self-test ledger with three masks:

- `required`: every check that this image must complete before it is healthy;
- `passed`: completed checks that have not failed;
- `failed`: latched failures that cannot be cleared during the boot.

A failed check is removed from `passed`, added to `failed`, and cannot be restored by a later pass call. The image is ready only when `failed` is zero and every bit in `required` is present in `passed`.

## Required bring-up gates

| Bit | Gate | Runtime evidence |
| ---: | --- | --- |
| 0 | Early memory | SRAM2 parity initialization completed and the early platform state is ready |
| 1 | Clock | Reset-default 4 MHz MSI and bus/flash settings passed bounded verification |
| 2 | Interrupt policy | PRIGROUP read back as four preemption bits/no subpriorities |
| 3 | Safe board I/O | Before peripheral initialization, GPIOA remains clock-gated; PB2 holds the display in reset, PD0 drives the status LED, and bridge-related GPIOB pins remain high impedance |
| 4 | Timebase | 1 kHz SysTick configured with priority 15 under the expected grouping |
| 5 | Application state | The reset-safe state accepted only the passive-initialization transition into diagnostics |
| 6 | Watchdog | LSI and IWDG setup synchronized, verified, and started successfully |

Early-memory failure cannot safely initialize the diagnostic record because SRAM invariants are not established. It still enters the common panic path and remains debugger-visible through the platform status and panic code. Every later pass or failure is published immediately.

## Reset-safe board gate

`board_passive_invariants_hold()` performs read-only checks after `board_init_passive()`:

- GPIOA is disabled at this early gate, so PA6/PA7 could not yet have been configured;
- GPIOB is enabled only to hold PB2 display reset; PB0/PB1/PB7 read back
  high impedance with no pulls;
- GPIOD is enabled and PD0 reads back as an output.

This check applies only to the reset-safe state before peripheral and future
bridge initialization. Firmware 0.14.0 subsequently preloads PA6/PA7/PB0/PB1
low before configuring TIM3 channels 1-4 on AF2. Because each signal drives
tied active-high HIN and active-low LIN inputs, this commands all four low-side
FETs and creates a zero-voltage vector; it is not an all-FET-off state. This
boot gate does not validate external gate-driver behavior.

## Diagnostic publication

The three ledger masks remain at schema-1-compatible offsets 52, 56, and 60.
Schema 2 appends encoder fields after the original 64-byte prefix. A debugger
can watch boot progress bit by bit or identify the last failed gate.

## Watchdog relationship

The foreground passes health to the watchdog supervisor only while the application remains in diagnostics and `boot_self_test_ready()` remains true. A ledger failure therefore cannot be hidden by continued foreground execution or SysTick activity.

The IWDG is still a recovery layer rather than the immediate safety response. Hardware and the common characterized bridge-fault primitive must act before any watchdog timeout.

## Bench validation

During first safe bring-up:

- verify `self_test_required == self_test_passed == 0x7F` and `self_test_failed == 0`;
- single-step or use controlled bench-only fault injection to observe individual gate publication;
- compare GPIOA/GPIOB/GPIOD clock and mode/pull registers against the self-test result;
- monitor PA6, PA7, PB0, PB1, and PB7 through power-on, reset, halt, resume, panic, and IWDG reset;
- treat any mismatch between the ledger and physical signals as a stop condition.
