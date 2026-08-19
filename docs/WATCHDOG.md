# Independent Watchdog Policy

Status: implemented in the bridge-characterization image and host-tested at the policy layer, but not yet verified on an N32L406CBL7 board. This watchdog does not authorize bridge operation and is not the fast bridge-state mechanism.

## Hardware configuration

The characterization image uses the independent watchdog (IWDG), not the window watchdog. IWDG is clocked from the nominal 40 kHz low-speed internal oscillator and remains independent of the 64 MHz PLL system clock.

| Setting | Value | Nominal result |
| --- | ---: | ---: |
| LSI | Explicitly enabled and readiness checked | 40 kHz nominal |
| IWDG prescaler | `/32` | 1.25 kHz counter clock |
| Reload | `1249` | 1,000 ms reset timeout |
| Foreground service interval | 100 ms | Ten intended service opportunities per nominal timeout |
| Foreground polling deadline | 250 ms | A longer foreground gap permanently refuses further service |

The timeout is nominal because LSI is an RC oscillator. Its actual frequency and reset latency must be measured on the purchased board before this timing becomes a release constraint.

Initialization uses bounded waits throughout:

1. Enable LSI and wait for its ready flag.
2. Unlock the IWDG prescaler and reload registers.
3. Wait until neither register is synchronizing.
4. Write `/32` and `1249`, wait for synchronization, and verify both fields.
5. Reload the counter, then enable IWDG.

Any bounded-wait or verification failure enters the common panic path. If option bytes already selected hardware-started IWDG, the same setup is attempted early in `main`; the actual retail option-byte state remains a bench item.

## Service ownership

There is deliberately no public raw-feed function. The cooperative foreground loop owns the sole supervisor API, and the hardware reload key is private to `watchdog.c`.

For the current characterization image, a service is permitted only when:

- the application remains in `APP_STATE_DIAGNOSTIC`;
- the foreground loop has returned within 250 ms of its preceding poll; and
- the 1 kHz timebase advances far enough to reach the next 100 ms service point.

An unhealthy state or foreground deadline miss latches policy failure. The main loop then enters `platform_panic()`, which disables interrupts, commands all four bridge inputs low, and stops servicing IWDG. A stalled SysTick also prevents the service deadline from becoming due, so IWDG eventually resets the MCU.

Interrupt handlers, including SysTick and the future current-control path, must never reload IWDG. This prevents one still-running interrupt from hiding a dead foreground or another failed execution domain.

Before motor-control `RUN` exists, the foreground supervisor must require explicit liveness evidence from the control-deadline guardian, accepted current-sample epochs, and any other safety-critical owner. IWDG remains a final recovery layer; a missed fast-loop deadline must command the proven deterministic bridge state immediately rather than wait for reset.

## Reset diagnostics

`SystemInit()` snapshots the sticky RCC reset flags into `g_platform_boot_diagnostics.reset_flags`, then clears the hardware flags so the next boot has an unambiguous cause. `RCC_CTRLSTS_IWDGRSTF` identifies an IWDG reset to a debugger or future diagnostic transport.

The `.noinit` panic code remains separate: it describes the last software panic when retained RAM is meaningful, while the RCC flags describe the reset source.

## Debugger halt policy

Firmware 0.14.0 clears both `DBG_CTRL.IWDG_STOP` and `DBG_CTRL.TIM3_STOP`;
IWDG and active TIM3 PWM continue while the Cortex-M4 is halted. A halt
therefore preserves the last timer command until the nominal
one-second watchdog reset returns the MCU pins to reset state. The tied HIN/LIN
inputs make that high-impedance reset interval electrically undefined, so the
actual gate waveforms must be measured. `platform_panic()` is a running path
and commands the all-low vector before waiting for IWDG reset.

## Bench validation gate

With the motor disconnected and a current-limited supply:

- confirm LSI readiness and the programmed IWDG prescaler/reload fields;
- measure the real watchdog reset interval over supply and temperature conditions available during bring-up;
- prove a running-but-unserviced image resets and reports `IWDGRSTF` on the next boot;
- prove normal heartbeat operation does not reset over an extended run;
- halt under SWD and verify watchdog reset timing plus all four gate-command and gate-output waveforms;
- repeat power-cycle, external reset, software panic, and watchdog reset while monitoring all bridge-control pins.

Until these checks pass, the watchdog is software-complete but hardware-unverified.
