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
7. Confirm a debugger halt does not pause IWDG or TIM3 in firmware 0.15.0; capture PA6/PA7/PB0/PB1, gate outputs, and the watchdog reset transition.
8. Load the matching ELF symbols and verify `g_diagnostics` has magic `0x4D4B5335`, schema `4`, size `184`, firmware version `0.15.0`, and an even stable sequence.
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
