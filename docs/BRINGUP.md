# Bench Bring-up Procedure

This procedure intentionally delays motor connection. Each stage should produce recorded evidence before proceeding.

## Equipment

- Current-limited bench supply suitable for the controller input voltage.
- Multimeter.
- Oscilloscope with appropriately rated probes.
- Logic analyzer where useful.
- Raspberry Pi Pico CMSIS-DAP v1/HID probe.
- USB-to-RS-485 adapter.
- Temporary fine wire or probe for NRST if required.

## Stage 0 — Unpowered inspection

1. Record board revision, MCU marking, encoder marking, gate-driver marking, MOSFET marking, and visible rework.
2. Photograph both sides at sufficient resolution to trace nets.
3. Confirm J4 ground, target 3.3 V, SWDIO, and SWCLK against the schematic.
4. Locate NRST and confirm it is not exposed on J4.
5. Check for shorts between input power, 5 V, 3.3 V, and ground.
6. Confirm motor outputs are disconnected.

## Stage 1 — Power only

1. Set a conservative supply current limit.
2. Power the board without a programmer or motor.
3. Verify input current is plausible and stable.
4. Measure 5 V, 3.3 V, MCU reset level, bridge enable, and all four phase-control signals.
5. Stop immediately for unexpected heating, rail collapse, oscillation, or excessive current.

## Stage 2 — Debug attachment and protection

1. Power the Pico from USB and connect only GND, SWDIO, and SWCLK initially.
2. Use a conservative SWD rate, beginning around 200 kHz.
3. Attempt a non-destructive debug-port connection and identify the Cortex-M4.
4. If connection is unreliable, add NRST and use connect-under-reset or coordinate connection with controller power-up.
5. Ask the Nations tool to determine protection state before starting an unlock.
6. Record L0, L1, or L2 and all tool messages.
7. If L1, acknowledge that release to L0 irreversibly erases Makerbase firmware, configuration, and calibration.
8. Perform the unlock only on a board designated for replacement-firmware work.
9. Power-cycle and verify repeatable attachment.

## Stage 3 — Minimal replacement image

1. Flash a minimal vector table, clock setup, safe GPIO initialization, and LED heartbeat.
2. Confirm the bridge remains disabled before, during, and after programming.
3. Exercise reset, power-cycle, watchdog, and debugger halt.
4. Confirm SWD remains recoverable after every case.
5. Verify flash contents through both the vendor utility and the development programmer path.
6. Confirm an unserviced running image resets near the measured IWDG interval and exposes `RCC_CTRLSTS_IWDGRSTF` in `g_platform_boot_diagnostics.reset_flags` after reboot.
7. Confirm a debugger halt does not pause IWDG, TIM2, or TIM3 in firmware 0.17.3; capture PA6/PA7/PB0/PB1, gate outputs, and the watchdog reset transition.
8. Load the matching ELF symbols and verify `g_diagnostics` has magic `0x4D4B5335`, schema `5`, size `240`, firmware version `0.17.3`, and an even stable sequence.
9. Scope PD0 and confirm the active-high heartbeat without button contention on PB9.
10. Scope PB3-PB6 and confirm the bounded MT6816 mode-3 burst described in [encoder bring-up](ENCODER.md), including non-fatal no-magnet behavior.
11. Compare the diagnostic reset and retained-panic fields across power-on, NRST, software panic, and IWDG reset cases.
12. Verify the required/passed self-test masks are both `0x7F`, the failed mask is zero, and the reported board-safety result agrees with scoped PA6/PA7/PB0/PB1/PB7 levels.

## Stage 4 — Passive inputs

Use the last passive image or perform these checks before the characterization
image takes ownership of PA6/PA7/PB0/PB1:

1. Read current-sense zero offsets and bus voltage repeatedly.
2. Read encoder position through complete mechanical revolutions.
3. Test buttons, display, isolated inputs, and RS-485.
4. Confirm every pin assignment against observation or continuity rather than relying solely on names.
5. Record ADC noise and encoder noise at multiple supply voltages.

## Stage 5 — Bridge logic without a motor

Do not begin until safe reset behavior and passive peripherals have passed.

1. Scope gate-driver inputs before enabling any output.
2. Apply a low-frequency, low-duty diagnostic pattern.
3. Observe high-side and low-side gate signals and bridge switch nodes.
4. Test reset, watchdog, firmware fault, breakpoint, and communications loss while the pattern is active.
5. Confirm the all-low zero-vector fault path acts immediately in every case;
   this PCB has no defined software-commanded all-FET-off state.
6. Characterize minimum pulse width, dead time, propagation delay, and bootstrap behavior.

## Stage 6 — Bounded current-loop commissioning without a motor

Do not begin until Stage 5 has passed. Keep the motor disconnected and use a
current-limited supply. Firmware 0.17.3 retains the released-then-held Enter
path, where raw release or Menu commands `ZERO`. It also accepts
duration-bounded RS-485 commissioning authority; timeout, Menu, transport
failure, or STOP commands `ZERO`, and status remains queryable during RUN.

Before applying authority, capture one machine-readable baseline. Replace
`COM29` with the port reported by the `list` command:

```powershell
py tools/mks57d_rs485.py list
py tools/mks57d_rs485.py --port COM29 identity
py tools/mks57d_rs485.py --port COM29 boot
py tools/mks57d_rs485.py --port COM29 status
py tools/mks57d_rs485.py --port COM29 configure --counts 25 --frequency-hz 0.5
py tools/mks57d_rs485.py --port COM29 run --leg A1 --duration-ms 10000
```

The `run` command streams current-loop snapshots until the run stops. An
explicit `stop` command remains available from another terminal or after an
interrupted watch.

1. Scope the TIM2 65%-compare ISR and ADC/DMA timing relative to TIM3. Confirm
   both sample apertures lie after the latest permitted PWM edge and control
   execution stages the following preload on time.
2. With the motor outputs open, hold Enter briefly. Confirm only the selected
   leg switches at no more than the 10% phase-voltage bound, the opposing leg
   remains low, raw release reaches all-low `ZERO`, and no
   unexpected supply-current increase occurs.
3. Verify both current channels remain bounded and do not show switching-edge
   clipping or a false current approaching the 100-count raw trip.
4. Test the deadline, DMA-error, invalid-reference, and raw-overcurrent fault
   paths by controlled injection where practical; every case must latch the
   direct-GPIO all-low path without foreground assistance.
5. Repeat the reset, watchdog, firmware-fault, breakpoint, and communications
   loss captures with the commissioning path active.
6. Only after the open-output captures pass, use a non-motor inductive or
   resistive test load at low bus voltage to establish current-feedback sign,
   loop stability, and trip latency independently for A and B.
7. Stop for oscillation, incorrect sign, ADC clipping, missed-update trips,
   duty outside the configured bounds, or any fault that fails to remain
   latched.

Firmware 0.17.3 displays a current-loop shutdown as persistent `F####`. The
number is the one-based fault-bit position: `F0002`/`F0003` are A/B raw
overcurrent, `F0017` ADC/DMA, `F0018` PWM, `F0019` deadline, and `F0020`
internal backend failure.

## Abort conditions

Stop and return to the previous stage for any of the following:

- Unexpected supply-current increase or heating.
- Any half-bridge commanding both MOSFETs on simultaneously.
- Bridge activation during reset, programming, or debugger halt.
- Loss of reliable SWD recovery.
- ADC current signal clipping, inversion, or unexplained large offset.
- Encoder discontinuities not explained by normal angle wrap.
- A fault path that depends on background code continuing to run.

## Suggested test record

For each session record:

- Date, board serial/revision, and firmware commit.
- Supply voltage and current limit.
- Motor/load connection state.
- Programmer, SWD speed, and reset wiring.
- Instrument models and probe points.
- Expected result, observed result, and captured waveforms.
- Decision to proceed, repeat, or stop.
