# Debugger Diagnostic Record

Status: firmware 0.27.1 publishes schema 5 continuously and the layout remains
ABI-checked by host and post-link tests. The new encoder-production guard does
not change this RAM ABI; liveness is reflected operationally in drive state and
through the native protocol's existing estimator-ready flag. Equivalent
current, encoder, fault, and reset data are bench-proven through the OLED and
native protocol; direct debugger inspection remains optional.

## Purpose

The first diagnostic channel is a structured RAM record exported as the ELF
symbol `g_diagnostics`. It reports boot, active encoder, DMA-backed RS-485,
native-protocol, and current-loop state without emitting unsolicited serial
traffic. Display and user-input monitoring are advertised as capabilities, but
their live state is not included.

The record is not assigned a fixed SRAM address. A debugger locates it through
symbols in the matching `mks57d.elf`. Native commands may later expose selected
diagnostic semantics, but no transport may expose the in-memory C layout as a
wire payload.

## Version 5 layout

All fields are naturally aligned 32-bit unsigned values. Schema version 5 is
240 bytes. The original 64-byte schema-1, 92-byte schema-2, 136-byte schema-3,
and 184-byte schema-4 prefixes are unchanged; current-loop fields are appended.

| Offset | Field | Meaning |
| ---: | --- | --- |
| 0 | `magic` | `0x4D4B5335` record identifier |
| 4 | `schema_version` | Record schema, currently `5` |
| 8 | `record_size` | Total bytes available, currently `240` |
| 12 | `sequence` | Odd while the foreground writer is updating, even when stable |
| 16 | `firmware_version` | Major in bits 31:24, minor in 23:16, patch in 15:0; currently `0.27.1` |
| 20 | `capabilities` | Product-image, status-LED, IWDG, reset-cause, NVIC-policy, encoder-SPI, RS-485-DMA, native-protocol, display-I2C, passive-ADC, user-input-monitor, rotating-current diagnostic, current-loop, automatic-alignment, persistent-configuration, aligned-torque, and velocity-control capability bits |
| 24 | `app_state` | Numeric `app_state_t` value |
| 28 | `uptime_millis` | Latest published 1 kHz timebase value |
| 32 | `heartbeat_count` | Number of active-high PD0 LED toggles completed |
| 36 | `watchdog_status` | Numeric `watchdog_status_t` value from the foreground supervisor |
| 40 | `platform_boot_status` | Numeric `platform_boot_status_t` value |
| 44 | `reset_flags` | RCC reset flags captured before they were cleared |
| 48 | `retained_panic` | Valid preceding panic retained across an IWDG reset, or `PANIC_NONE` |
| 52 | `self_test_required` | Boot gates required by this image, currently `0x7F` |
| 56 | `self_test_passed` | Gates completed without a latched failure |
| 60 | `self_test_failed` | Latched gate failures; healthy value is zero |
| 64 | `encoder_status` | Numeric `mt6816_status_t`; zero means not attempted |
| 68 | `encoder_transport_status` | Numeric `spi_status_t` from the latest transaction |
| 72 | `encoder_angle_raw` | Latest parity-valid 14-bit angle, 0-16383 |
| 76 | `encoder_flags` | Bit 0 no-magnet, bit 1 over-speed |
| 80 | `encoder_sample_count` | Number of parity-valid bursts accepted |
| 84 | `encoder_error_count` | Initialization, transport, or parity failures |
| 88 | `encoder_last_attempt_millis` | Uptime timestamp of the latest attempt |
| 92 | `rs485_status` | Numeric `rs485_status_t`; zero is ready |
| 96 | `rs485_rx_bytes` | Total bytes completed into the circular DMA buffer |
| 100 | `rs485_rx_idle_events` | USART IDLE-line events acknowledged |
| 104 | `rs485_rx_error_count` | USART receive and RX-DMA errors |
| 108 | `rs485_rx_overrun_count` | Foreground cursor-lap events |
| 112 | `rs485_rx_dropped_bytes` | Exact oldest-byte count discarded after cursor laps |
| 116 | `rs485_last_rx_byte` | Last byte drained by foreground; meaningful after `rs485_rx_bytes` becomes nonzero |
| 120 | `rs485_tx_bytes` | Bytes whose final stop bits completed |
| 124 | `rs485_tx_frame_count` | Frames completed and returned to receive mode |
| 128 | `rs485_tx_error_count` | TX DMA transfer errors |
| 132 | `rs485_tx_busy` | One while a DMA/USART line transmission owns PC13 direction control |
| 136 | `native_protocol_ready` | One after the foreground native server is initialized |
| 140 | `native_protocol_bytes_consumed` | Bytes passed from the RX drain into the streaming parser |
| 144 | `native_protocol_valid_frames` | Version-1 frames accepted after COBS, length, and CRC validation, including filtered addresses/types |
| 148 | `native_protocol_responses_sent` | Replies accepted by the RS-485 TX API |
| 152 | `native_protocol_cobs_errors` | Invalid COBS frames discarded |
| 156 | `native_protocol_length_errors` | Short, inconsistent, or oversized frames discarded |
| 160 | `native_protocol_crc_errors` | Frames discarded for CRC-16 mismatch |
| 164 | `native_protocol_version_errors` | CRC-valid frames using an unsupported protocol version |
| 168 | `native_protocol_ignored_addresses` | Valid frames addressed to another device |
| 172 | `native_protocol_broadcasts_dropped` | Valid broadcasts dropped because the first commands are not broadcast-safe |
| 176 | `native_protocol_unexpected_message_types` | Responses or events observed by the request server and ignored |
| 180 | `native_protocol_transmit_rejections` | Replies rejected because encoding or the bounded TX path was unavailable |
| 184 | `current_loop_ready` | One after zero calibration and backend initialization |
| 188 | `current_loop_active` | One while the supervisor-authorized current backend is active |
| 192 | `current_loop_fault_flags` | Latched phase-loop and backend fault bitmap |
| 196 | `current_loop_sample_count` | DMA samples accepted by the active loop |
| 200 | `current_loop_a_reference_counts` | Signed A reference encoded in two's-complement 32-bit form |
| 204 | `current_loop_b_reference_counts` | Signed B reference encoded in two's-complement 32-bit form |
| 208 | `current_loop_a_measured_counts` | Latest signed A feedback relative to its zero |
| 212 | `current_loop_b_measured_counts` | Latest signed B feedback relative to its zero |
| 216 | `current_loop_phase_a_voltage_permille` | Latest signed A phase-voltage request |
| 220 | `current_loop_phase_b_voltage_permille` | Latest signed B phase-voltage request |
| 224 | `current_loop_duty_a1_permille` | Latest staged A1 duty |
| 228 | `current_loop_duty_a2_permille` | Latest staged A2 duty |
| 232 | `current_loop_duty_b1_permille` | Latest staged B1 duty |
| 236 | `current_loop_duty_b2_permille` | Latest staged B2 duty |

`current_loop_fault_flags` uses bits 0 invalid sample, 1 A overcurrent, 2 B
overcurrent, 3 invalid reference, 4 invalid modulator output, 16 ADC/DMA, 17
PWM backend, 18 missed control deadline, 19 internal backend failure, and 20
invalid or stale electrical-phase prediction.

The format is append-only within a schema: new fields may be appended and `record_size` increased, but existing fields must not be reordered or reinterpreted. An incompatible change increments `schema_version` and receives a separate consumer path.

## Consistent-read procedure

The cooperative foreground loop is the sole writer. It publishes after every
boot gate, immediately after watchdog initialization, after each 250 ms
heartbeat, after encoder activity, after RS-485 initialization or a
transport failure, after each non-empty RS-485 foreground drain, and
after each 10 ms current-loop snapshot, and immediately before entering a
watchdog-related panic.

A live reader should:

1. Read `sequence`.
2. Retry if it is odd.
3. Read the record through the advertised size supported by the reader.
4. Read `sequence` again.
5. Accept the snapshot only when both sequence reads match and are even.
6. Validate `magic`, `schema_version`, and `record_size` before interpreting fields.

The Cortex-M data-memory barriers around publication keep the odd/even sequence
contract ordered. A debugger that halts the image will normally see an
already-stable record.

## Panic retention

`g_last_panic` remains in `.noinit`. Startup accepts it as preceding-boot history only when RCC reports an IWDG reset and the numeric code is in range. `diagnostics_init()` copies that value into `retained_panic`, then clears `g_last_panic` so a later watchdog-only stall cannot inherit an older software panic.

If firmware is currently stopped inside `platform_panic()`, inspect `g_last_panic` directly. After IWDG resets the MCU, the next boot publishes the same code into `g_diagnostics.retained_panic`.

## Hardware validation

For a new board or diagnostic-schema validation:

- load the matching ELF symbols and inspect `g_diagnostics` before and after heartbeat changes;
- confirm `firmware_version` decodes to `0.27.1`, schema is 5, and record size is 240;
- confirm `sequence` is even when the core is halted;
- confirm required and passed self-test masks are `0x7F` with a zero failed mask;
- compare `reset_flags` against power-on, NRST, and induced IWDG resets;
- confirm a watchdog-related panic appears as `retained_panic` after reboot;
- rotate the encoder magnet and verify angle, counts, status, and timestamp;
- remove the magnet and confirm flag bit 0 without a boot panic;
- send known bytes from an external RS-485 adapter and verify RX byte, IDLE,
  last-byte, and error fields without unsolicited board transmission;
- trigger one bounded TX call and confirm byte/frame completion and busy state
  agree with scoped PC13 and final-stop-bit timing;
- send valid and deliberately malformed native frames and confirm response,
  COBS, length, CRC, address, broadcast, type, and TX-rejection counters;
- during a bounded current diagnostic, confirm active, sample count,
  references, measured currents, staged duties, and any latched fault agree
  with the scoped signals;
- leave PA6, PA7, PB0, PB1, and PB7 under oscilloscope observation throughout.

RS-485 command/response behavior is bench-proven, while the diagnostic record
and exact direction/bus waveforms still require debugger and oscilloscope
correlation on the purchased board.
