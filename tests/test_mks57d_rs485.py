import csv
import json
import struct
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from instruments.rp2040_loadcell.host import loadcell_capture as loadcell_host
from tools import mks57d_rs485 as console


def encoded_response(sequence: int, command: int, body: bytes = b"") -> bytes:
    payload = b"\x00" + body
    decoded = struct.pack(
        ">BBHBHB",
        console.PROTOCOL_VERSION,
        console.DEFAULT_ADDRESS,
        sequence,
        console.MESSAGE_RESPONSE,
        command,
        len(payload),
    ) + payload
    decoded += struct.pack(">H", console.crc16_ccitt_false(decoded))
    return console.cobs_encode(decoded) + b"\x00"


def velocity_status(state: str) -> dict:
    active = state not in {"complete", "stopped", "failed"}
    result = (
        "deadline"
        if state == "complete"
        else "stopped"
        if state == "stopped"
        else "none"
    )
    return {
        "schema": 1,
        "state": state,
        "result": result,
        "flags_hex": "0x01" if active else "0x00",
        "flags": ["active"] if active else [],
        "fault_flags_hex": "0x00000000",
        "faults": [],
        "target_velocity_revolutions_per_second": 0.1,
        "reference_velocity_revolutions_per_second": 0.1,
        "measured_velocity_revolutions_per_second": 0.09,
        "requested_q_current_counts": 2 if active else 0,
        "requested_q_current_nominal_milliamperes": 12.1 if active else 0.0,
        "applied_q_current_counts": 2 if active else 0,
        "applied_q_current_nominal_milliamperes": 12.1 if active else 0.0,
        "current_limit_counts": 25,
        "current_limit_nominal_milliamperes": 151.5,
        "elapsed_millis": 100 if active else 2000,
        "remaining_millis": 1900 if active else 0,
        "policy": {
            "maximum_target_velocity_revolutions_per_second": 1.0,
            "maximum_target_acceleration_revolutions_per_second2": 1.0,
            "maximum_feedback_velocity_revolutions_per_second": 5.0,
            "maximum_current_counts": 100,
            "minimum_duration_millis": 3,
            "maximum_duration_millis": 2147483647,
        },
    }


def torque_status(state: str) -> dict:
    active = state in {"ramping", "holding"}
    result = (
        "deadline"
        if state == "complete"
        else "stopped"
        if state == "stopped"
        else "none"
    )
    return {
        "schema": 2,
        "state": state,
        "result": result,
        "flags_hex": "0x01" if active else "0x00",
        "flags": ["active"] if active else [],
        "fault_flags_hex": "0x00000000",
        "faults": [],
        "requested_q_current_counts": 25 if active else 0,
        "requested_q_current_nominal_milliamperes": (
            151.5 if active else 0.0
        ),
        "applied_q_current_counts": 24 if active else 0,
        "applied_q_current_nominal_milliamperes": (
            145.4 if active else 0.0
        ),
        "phase_current_reference_counts": {
            "a": 12 if active else 0,
            "b": -21 if active else 0,
        },
        "electrical_phase_q32_hex": "0x40000000",
        "velocity_revolutions_per_second": 0.25 if active else 0.0,
        "acceleration_revolutions_per_second2": 2.0 if active else 0.0,
        "elapsed_millis": 100 if active else 250,
        "remaining_millis": 150 if active else 0,
        "backend_fault_flags_hex": "0x00000000",
        "phase_prediction": {
            "reject_reason": "none",
            "rejected_age_us": None,
            "maximum_observed_age_us": 1001,
            "maximum_age_us": 3000,
        },
        "policy": {
            "maximum_current_counts": 495,
            "maximum_current_nominal_milliamperes": 2999.0,
            "maximum_current_slew_counts_per_second": 10000,
            "maximum_current_slew_amperes_per_second": 60.59,
            "maximum_velocity_revolutions_per_second": 20.0,
            "maximum_acceleration_revolutions_per_second2": 1000.0,
            "maximum_feedback_interval_us": 3000,
            "minimum_duration_millis": 3,
            "maximum_duration_millis": 2147483647,
        },
    }


def position_status(state: str) -> dict:
    active = state in {"moving", "settling"}
    result = (
        "settled"
        if state == "complete"
        else "stopped"
        if state == "stopped"
        else "following_error"
        if state == "failed"
        else "none"
    )
    progress = {
        "idle": (0.0, 0.0, 0.0),
        "moving": (0.10, 0.09, 0.30),
        "settling": (0.25, 0.249, 0.01),
        "complete": (0.25, 0.25, 0.0),
        "stopped": (0.10, 0.09, 0.0),
        "failed": (0.20, 0.0, 0.0),
    }
    reference, measured, velocity = progress[state]
    flags = ["active"] if active else []
    faults = ["following_error"] if state == "failed" else []
    return {
        "schema": 1,
        "state": state,
        "result": result,
        "flags_hex": "0x01" if active else "0x00",
        "flags": flags,
        "fault_flags_hex": "0x00000004" if faults else "0x00000000",
        "faults": faults,
        "target_position_revolutions": 0.25,
        "reference_position_revolutions": reference,
        "measured_position_revolutions": measured,
        "reference_velocity_revolutions_per_second": velocity,
        "target_velocity_revolutions_per_second": velocity,
        "measured_velocity_revolutions_per_second": velocity,
        "requested_q_current_counts": 10 if active else 0,
        "requested_q_current_nominal_milliamperes": 60.6 if active else 0.0,
        "applied_q_current_counts": 9 if active else 0,
        "applied_q_current_nominal_milliamperes": 54.5 if active else 0.0,
        "current_limit_counts": 100,
        "current_limit_nominal_milliamperes": 605.9,
        "elapsed_millis": 100 if active else 2000,
        "remaining_millis": 1900 if active else 0,
        "policy": {
            "maximum_relative_travel_revolutions": 100.0,
            "maximum_velocity_revolutions_per_second": 4.0,
            "maximum_acceleration_revolutions_per_second2": 4.0,
            "maximum_following_error_revolutions": 0.25,
            "minimum_duration_millis": 100,
        },
    }


DRIVE_STATUS = {
    "flags_hex": "0x00000000",
    "flags": [],
    "adc": {"bus_voltage_volts": 24.0},
    "loop": {
        "fault_flags_hex": "0x00000000",
        "faults": [],
        "sample_count": 40000,
        "measured_counts": {"a": 11, "b": -20},
        "phase_voltage_command_volts": {"a": 2.4, "b": -2.4},
        "phase_voltage_limit_volts": 16.8,
        "missed_pwm_update_count": 0,
        "maximum_consecutive_missed_pwm_updates": 0,
    },
    "reset": {"retained_panic": 0, "watchdog_reset": False},
}

ENCODER_STATUS = {
    "status": "ok",
    "transport_status": "ok",
    "flags_hex": "0x00",
    "error_count": 0,
    "angle_raw": 1234,
    "estimator": {
        "position_revolutions": 0.01,
        "velocity_revolutions_per_second": 0.09,
        "sample_interval_us": 1000,
        "maximum_sample_interval_us": 1001,
        "flags_hex": "0x01",
        "flags": ["ready"],
        "fault_flags_hex": "0x00000000",
        "faults": [],
    },
}


class FakeClient:
    def __init__(
        self,
        start_error: Exception | None = None,
        start_command: int = console.COMMAND_START_VELOCITY,
        payload_length: int = 14,
    ) -> None:
        self.start_error = start_error
        self.start_command = start_command
        self.payload_length = payload_length
        self.start_count = 0
        self.last_payload = b""

    def transact(self, command: int, payload: bytes = b"") -> bytes:
        self.assert_start_payload(command, payload)
        self.last_payload = payload
        self.start_count += 1
        if self.start_error is not None:
            raise self.start_error
        return b""

    def assert_start_payload(self, command: int, payload: bytes) -> None:
        if command != self.start_command:
            raise AssertionError(f"unexpected command 0x{command:04X}")
        if len(payload) != self.payload_length:
            raise AssertionError(
                f"request must be {self.payload_length} bytes"
            )


def capture_args(root: Path) -> SimpleNamespace:
    return SimpleNamespace(
        output_root=root,
        rps=0.1,
        acceleration_rps2=16.0,
        duration_ms=2000,
        interval=0.01,
        port="COM14",
        baud=115200,
        address=1,
        timeout=0.75,
        jsonl=False,
        quiet=True,
        stop_after_seconds=None,
        trace_at_seconds=None,
    )


def torque_capture_args(root: Path) -> SimpleNamespace:
    return SimpleNamespace(
        output_root=root,
        duration_ms=250,
        interval=0.01,
        port="COM14",
        baud=115200,
        address=1,
        timeout=0.75,
        jsonl=False,
        quiet=True,
        stop_after_seconds=None,
        trace_at_seconds=None,
        loadcell_port=None,
        loadcell_baudrate=115200,
        loadcell_sample_rate_sps=320,
        loadcell_gain=128,
        loadcell_tare_samples=320,
        loadcell_counts_per_newton=None,
        loadcell_force_sign=1,
        loadcell_lever_radius_m=None,
        loadcell_connect_delay_seconds=0.0,
        loadcell_command_timeout_seconds=3.0,
        loadcell_drain_timeout_seconds=5.0,
    )


def current_trace_sample() -> dict:
    return {
        "schema": 2,
        "captured_sample_count": 1,
        "sample_index": 0,
        "loop_sample_count": 123,
        "time_seconds": 0.0,
        "reference_counts": {"a": 10, "b": -10},
        "measured_counts": {"a": 9, "b": -9},
        "phase_voltage_permille": {"a": 100, "b": -100},
        "phase_voltage_command_volts": {"a": 2.4, "b": -2.4},
        "phase_prediction": {
            "electrical_phase_q32": 0x40000000,
            "electrical_phase_turns": 0.25,
            "electrical_phase_degrees": 90.0,
            "age_us": 1000,
        },
        "timing": {
            "cycle_counter_hz": 64_000_000,
            "pwm_timer_hz": 32_000_000,
            "trigger_timer_count": 1284,
            "trigger_timer_us": 40.125,
            "trigger_to_dma_timer_ticks": 160,
            "trigger_to_dma_us": 5.0,
            "dma_to_pwm_stage_cycles": 160,
            "dma_to_pwm_stage_us": 2.5,
            "dma_to_trace_record_cycles": 192,
            "dma_to_trace_record_us": 3.0,
            "pwm_preload_margin_ticks": 240,
            "pwm_preload_margin_us": 7.5,
        },
        "bus_voltage_volts": 24.0,
        "phase_voltage_limit_volts": 16.8,
    }


class FakeLoadCellCapture:
    def __init__(
        self,
        *,
        prepare_error: Exception | None = None,
        poll_error: Exception | None = None,
    ) -> None:
        self.prepare_error = prepare_error
        self.poll_error = poll_error
        self.prepared = False
        self.started = False
        self.stopped = False
        self.closed = False
        self.motor_time_origin = None
        self.markers = []
        self.poll_count = 0
        self.application_failure = None

    def prepare(self) -> None:
        if self.prepare_error is not None:
            raise self.prepare_error
        self.prepared = True

    def start(self) -> None:
        self.started = True

    def set_motor_time_origin(self, value: float) -> None:
        self.motor_time_origin = value

    def mark(self, marker_id: str) -> None:
        self.markers.append(marker_id)

    def poll(self) -> None:
        self.poll_count += 1
        if self.poll_error is not None:
            raise self.poll_error

    def stop(self) -> None:
        if self.started:
            self.stopped = True

    def finalize(self, application_failure=None) -> dict:
        self.application_failure = application_failure
        return {
            "capture": {
                "complete": self.stopped and self.prepare_error is None,
            },
            "host_summary": {"sample_count": 79},
        }

    def close(self) -> None:
        self.closed = True


class FakeLoadCellTransport:
    port_name = "COM30"

    def __init__(self, sample_flags: str = "0x2") -> None:
        self.lines = []
        self.state = "IDLE"
        self.run_id = None
        self.sequence = 0
        self.closed = False
        self.sample_flags = sample_flags

    def write_line(self, line: str) -> None:
        fields = line.split()
        command = fields[0]
        if command == "INFO":
            self.lines.append("OK,1,INFO,rp2040-loadcell,test-build\n")
        elif command == "STATUS":
            self.lines.append(f"OK,1,STATUS,{self.state}\n")
        elif command == "CONFIG":
            self.lines.append(f"OK,1,CONFIG,{fields[1]},{fields[2]}\n")
        elif command == "TARE":
            self.lines.extend(
                [
                    f"OK,1,TARE,STARTED,{fields[1]}\n",
                    f"OK,1,TARE,COMPLETE,{fields[1]},-10.5,1.25\n",
                ]
            )
        elif command == "START":
            self.run_id = fields[1]
            self.state = "RUNNING"
            self.lines.append(f"OK,1,START,{self.run_id},500\n")
        elif command == "MARK":
            self.lines.extend(
                [
                    (
                        f"S,1,{self.sequence},1000,25,"
                        f"{self.sample_flags},0\n"
                    ),
                    f"M,1,{fields[1]},2000\n",
                    f"OK,1,MARK,{fields[1]},2000\n",
                ]
            )
            self.sequence += 1
        elif command == "STOP":
            self.state = "IDLE"
            self.lines.extend(
                [
                    f"S,1,{self.sequence},4125,24,0x2,0\n",
                    "OK,1,STOP,DRAINING,4200\n",
                    (
                        f"F,1,{self.run_id},0,{self.sequence},"
                        f"{self.sequence + 1},0,0,0,1000,4125,320.0\n"
                    ),
                ]
            )
            self.sequence += 1
        else:
            raise AssertionError(f"unexpected load-cell command {line!r}")

    def read_line(self):
        return self.lines.pop(0) if self.lines else None

    def close(self) -> None:
        self.closed = True


def position_capture_args(root: Path) -> SimpleNamespace:
    return SimpleNamespace(
        output_root=root,
        revolutions=0.25,
        max_rps=0.5,
        max_rpm=None,
        acceleration_rps2=1.0,
        duration_ms=2000,
        interval=0.01,
        port="COM14",
        baud=115200,
        address=1,
        timeout=0.75,
        jsonl=False,
        quiet=True,
        stop_after_seconds=None,
    )


class ConfigurationTuningTests(unittest.TestCase):
    @staticmethod
    def configuration_prefix(schema: int = 1) -> tuple[int, ...]:
        return (
            schema,
            0x9F,
            0,
            1,
            2,
            7,
            16384,
            50,
            9301,
            8192,
            0,
            -1,
            16384,
            50,
            9301,
            8192,
            0,
            -1,
        )

    def test_configuration_schema_one_remains_decodable(self) -> None:
        client = mock.Mock()
        client.transact.return_value = (
            console.CONFIGURATION_STATUS_V1_BODY.pack(
                *self.configuration_prefix()
            )
        )

        configuration = console.query_configuration(client)

        self.assertEqual(configuration["schema"], 1)
        self.assertFalse(configuration["tuning_supported"])
        self.assertNotIn("current_loop_gains", configuration["active"])

    def test_configuration_schema_two_decodes_all_gain_sets(self) -> None:
        client = mock.Mock()
        client.transact.return_value = (
            console.CONFIGURATION_STATUS_V2_BODY.pack(
                *self.configuration_prefix(2),
                4 * 65536,
                1024,
                3 * 65536,
                768,
                5 * 65536,
                512,
                8 * 65536,
                4096,
            )
        )

        configuration = console.query_configuration(client)

        self.assertTrue(configuration["tuning_supported"])
        self.assertEqual(
            configuration["default"]["current_loop_gains"][
                "proportional_q16_per_count"
            ],
            4 * 65536,
        )
        self.assertEqual(
            configuration["active"]["current_loop_gains"][
                "proportional_per_count"
            ],
            5.0,
        )
        self.assertEqual(
            configuration["stored"]["current_loop_gains"][
                "integral_per_count_per_step"
            ],
            768 / 65536.0,
        )
        self.assertEqual(
            configuration["limits"]["maximum_current_loop_gains"][
                "integral_q16_per_count_per_step"
            ],
            4096,
        )

    def test_set_gains_emits_big_endian_signed_q16_payload(self) -> None:
        client = mock.Mock()

        console.set_current_loop_gains_q16(client, 0x00048000, 0x00000400)

        client.transact.assert_called_once_with(
            console.COMMAND_SET_CURRENT_LOOP_GAINS,
            b"\x00\x04\x80\x00\x00\x00\x04\x00",
        )

    def test_gain_conversion_rejects_invalid_values(self) -> None:
        for value in (-1.0, float("inf"), float("nan"), 32768.0):
            with self.subTest(value=value):
                with self.assertRaises(console.ProtocolError):
                    console.current_loop_gain_to_q16(value)

    def test_set_gains_validates_firmware_reported_limits(self) -> None:
        configuration = {
            "tuning_supported": True,
            "limits": {
                "maximum_current_loop_gains": {
                    "proportional_q16_per_count": 4 * 65536,
                    "integral_q16_per_count_per_step": 4096,
                }
            },
        }
        client = mock.Mock()

        with self.assertRaisesRegex(
            console.ProtocolError, "firmware-reported maximum"
        ):
            console.set_current_loop_gains(
                client, 4.5, 0.01, configuration
            )

        client.transact.assert_not_called()

    def test_parser_exposes_volatile_gain_commands(self) -> None:
        parser = console.make_parser()
        args = parser.parse_args(
            ["set-current-loop-gains", "--kp", "4", "--ki", "0.015625"]
        )
        revert = parser.parse_args(["revert-current-loop-gains"])

        self.assertEqual(args.command, "set-current-loop-gains")
        self.assertEqual(args.kp, 4.0)
        self.assertEqual(revert.command, "revert-current-loop-gains")


class VelocityCaptureTests(unittest.TestCase):
    def test_client_discards_malformed_frame_before_matching_response(self) -> None:
        serial_port = mock.Mock()
        serial_port.read_until.side_effect = [
            b"\x05\x01\x00",
            encoded_response(1, console.COMMAND_PING, b"ok"),
        ]
        client = console.Client(serial_port, console.DEFAULT_ADDRESS)

        self.assertEqual(client.transact(console.COMMAND_PING), b"ok")
        self.assertEqual(serial_port.read_until.call_count, 2)

    def test_client_discards_stale_response_before_matching_response(self) -> None:
        serial_port = mock.Mock()
        serial_port.read_until.side_effect = [
            encoded_response(99, console.COMMAND_PING, b"stale"),
            encoded_response(1, console.COMMAND_PING, b"ok"),
        ]
        client = console.Client(serial_port, console.DEFAULT_ADDRESS)

        self.assertEqual(client.transact(console.COMMAND_PING), b"ok")
        self.assertEqual(serial_port.read_until.call_count, 2)

    def test_current_trace_schema_two_decodes_timing(self) -> None:
        body = console.CURRENT_TRACE_V2_BODY.pack(
            2,
            256,
            42,
            1234,
            -10,
            10,
            -9,
            9,
            -100,
            100,
            0x40000000,
            1001,
            1284,
            321,
            222,
            250,
            240,
        )
        client = mock.Mock()
        client.transact.return_value = body

        sample = console.query_current_trace_sample(client, 42)

        client.transact.assert_called_once_with(
            console.COMMAND_GET_CURRENT_TRACE, struct.pack(">H", 42)
        )
        self.assertEqual(sample["schema"], 2)
        self.assertEqual(
            sample["phase_prediction"]["electrical_phase_degrees"], 90.0
        )
        self.assertEqual(
            sample["timing"]["trigger_to_dma_timer_ticks"], 321
        )
        self.assertEqual(sample["timing"]["pwm_preload_margin_ticks"], 240)

    def test_current_trace_schema_one_remains_decodable(self) -> None:
        client = mock.Mock()
        client.transact.return_value = console.CURRENT_TRACE_V1_BODY.pack(
            1, 1, 0, 5, 1, -1, 2, -2, 3, -3
        )

        sample = console.query_current_trace_sample(client, 0)

        self.assertEqual(sample["schema"], 1)
        self.assertIsNone(sample["timing"]["trigger_timer_count"])
        self.assertIsNone(
            sample["phase_prediction"]["electrical_phase_q32"]
        )

    def test_current_trace_read_retries_an_idempotent_sample(self) -> None:
        sample = current_trace_sample()
        with (
            mock.patch.object(console, "query_status", return_value={}),
            mock.patch.object(
                console,
                "query_current_trace_sample",
                side_effect=[
                    console.TransportError("truncated COBS frame"),
                    sample,
                ],
            ) as query_sample,
        ):
            trace = console.read_current_trace(mock.Mock())

        self.assertEqual(trace[0]["sample_index"], 0)
        self.assertEqual(query_sample.call_count, 2)

    def test_current_trace_read_does_not_retry_device_errors(self) -> None:
        with (
            mock.patch.object(console, "query_status", return_value={}),
            mock.patch.object(
                console,
                "query_current_trace_sample",
                side_effect=console.ProtocolError(
                    "device returned invalid_payload"
                ),
            ) as query_sample,
        ):
            with self.assertRaisesRegex(
                console.ProtocolError, "invalid_payload"
            ):
                console.read_current_trace(mock.Mock())

        query_sample.assert_called_once_with(mock.ANY, 0)

    def test_arm_trace_is_a_first_class_command(self) -> None:
        parser = console.make_parser()
        client = mock.Mock()

        args = parser.parse_args(["arm-trace"])
        console.arm_current_trace(client)

        self.assertEqual(args.command, "arm-trace")
        client.transact.assert_called_once_with(
            console.COMMAND_ARM_CURRENT_TRACE
        )

    def test_commissioning_status_reports_physical_voltage(self) -> None:
        body = console.STATUS_V3_BODY.pack(
            3,
            1 << 11,
            0xFF,
            0xFF,
            0,
            0,
            0,
            123,
            2048,
            2048,
            2048,
            2048,
            10,
            -10,
            8,
            -8,
            500,
            -250,
            0,
            0,
            0,
            0,
            25,
            495,
            600,
            700,
            500,
            0,
            0,
            0,
            1815,
            1234,
        )

        status = console.parse_status(body)

        self.assertIn("vbus_snapshot_valid", status["flags"])
        self.assertAlmostEqual(
            status["adc"]["bus_voltage_volts"], 23.987, places=3
        )
        self.assertAlmostEqual(
            status["loop"]["phase_voltage_command_volts"]["a"],
            11.994,
            places=3,
        )
        self.assertAlmostEqual(
            status["loop"]["phase_voltage_command_volts"]["b"],
            -5.997,
            places=3,
        )
        self.assertAlmostEqual(
            status["loop"]["phase_voltage_limit_volts"], 16.791, places=3
        )

    def test_schema_two_status_remains_decodable_without_vbus(self) -> None:
        body = bytes(console.STATUS_V2_BODY.size)

        status = console.parse_status(body)

        self.assertIsNone(status["adc"]["bus_voltage_volts"])
        self.assertIsNone(
            status["loop"]["phase_voltage_command_volts"]["a"]
        )

    def test_schema_four_status_reports_pwm_guardian_misses(self) -> None:
        values = list(console.STATUS_V3_BODY.unpack(
            bytes(console.STATUS_V3_BODY.size)
        ))
        values[0] = 4
        body = console.STATUS_V4_BODY.pack(*values, 7, 1)

        status = console.parse_status(body)

        self.assertEqual(status["schema"], 4)
        self.assertEqual(status["loop"]["missed_pwm_update_count"], 7)
        self.assertEqual(
            status["loop"]["maximum_consecutive_missed_pwm_updates"], 1
        )

    def test_schema_five_status_reports_rotating_controller(self) -> None:
        values = list(console.STATUS_V3_BODY.unpack(
            bytes(console.STATUS_V3_BODY.size)
        ))
        values[0] = 5
        body = console.STATUS_V5_BODY.pack(*values, 7, 1, 1)

        status = console.parse_status(body)

        self.assertEqual(status["schema"], 5)
        self.assertEqual(status["test"]["controller_mode"], "rotating")
        self.assertEqual(status["loop"]["missed_pwm_update_count"], 7)

    def test_configure_current_test_selects_rotating_mode(self) -> None:
        client = mock.Mock()
        client.transact.return_value = struct.pack(">HIB", 50, 200000, 1)

        applied = console.configure_current_test(
            client, 50, 200.0, "rotating"
        )

        client.transact.assert_called_once_with(
            console.COMMAND_CONFIGURE_CURRENT_TEST,
            struct.pack(">HIB", 50, 200000, 1),
        )
        self.assertEqual(applied["controller_mode"], "rotating")

    def test_configure_current_test_keeps_legacy_stationary_payload(self) -> None:
        client = mock.Mock()
        client.transact.return_value = struct.pack(">HI", 50, 200000)

        applied = console.configure_current_test(client, 50, 200.0)

        client.transact.assert_called_once_with(
            console.COMMAND_CONFIGURE_CURRENT_TEST,
            struct.pack(">HI", 50, 200000),
        )
        self.assertEqual(applied["controller_mode"], "stationary")

    def test_schema_five_status_fits_host_wire_read_bound(self) -> None:
        payload = b"\x00" + bytes(console.STATUS_V5_BODY.size)
        decoded = struct.pack(
            ">BBHBHB",
            console.PROTOCOL_VERSION,
            console.DEFAULT_ADDRESS,
            1,
            console.MESSAGE_RESPONSE,
            console.COMMAND_GET_COMMISSIONING_STATUS,
            len(payload),
        ) + payload
        decoded += struct.pack(">H", console.crc16_ccitt_false(decoded))
        wire = console.cobs_encode(decoded) + b"\x00"

        self.assertGreater(len(wire), 84)
        self.assertLessEqual(len(wire), console.MAX_WIRE_FRAME_SIZE)
        self.assertEqual(console.decode_response(wire).payload, payload)

    def test_torque_status_reports_phase_prediction_rejection(self) -> None:
        body = bytearray(console.ALIGNED_TORQUE_STATUS_V1_BODY.size)
        body[0] = 2
        body.extend(struct.pack(">BIHH", 2, 3001, 2875, 3000))
        client = mock.Mock()
        client.transact.return_value = bytes(body)

        status = console.query_aligned_torque(client)

        self.assertEqual(status["schema"], 2)
        self.assertEqual(
            status["phase_prediction"]["reject_reason"], "stale"
        )
        self.assertEqual(
            status["phase_prediction"]["rejected_age_us"], 3001
        )
        self.assertEqual(
            status["phase_prediction"]["maximum_observed_age_us"],
            2875,
        )
        self.assertEqual(
            status["phase_prediction"]["maximum_age_us"], 3000
        )

    def test_torque_status_keeps_schema_one_compatibility(self) -> None:
        body = bytearray(console.ALIGNED_TORQUE_STATUS_V1_BODY.size)
        body[0] = 1
        client = mock.Mock()
        client.transact.return_value = bytes(body)

        status = console.query_aligned_torque(client)

        self.assertEqual(status["schema"], 1)
        self.assertEqual(
            status["phase_prediction"]["reject_reason"], "none"
        )
        self.assertIsNone(
            status["phase_prediction"]["rejected_age_us"]
        )
        self.assertIsNone(
            status["phase_prediction"]["maximum_observed_age_us"]
        )
        self.assertIsNone(
            status["phase_prediction"]["maximum_age_us"]
        )

    def test_fault_recovery_status_decodes_sources_and_blockers(self) -> None:
        body = console.FAULT_RECOVERY_STATUS_BODY.pack(
            1,
            2,
            1 << 1,
            (1 << 0) | (1 << 5),
            1 << 6,
        )

        status = console.parse_fault_recovery_status(body)

        self.assertEqual(status["result"], "blocked")
        self.assertEqual(status["blockers"], ["backend_reset_failed"])
        self.assertEqual(
            status["cleared_faults"], ["supervisor", "position"]
        )
        self.assertEqual(status["remaining_faults"], ["current_backend"])

    def test_clear_faults_is_a_first_class_command(self) -> None:
        parser = console.make_parser()
        client = mock.Mock()
        client.transact.return_value = (
            console.FAULT_RECOVERY_STATUS_BODY.pack(1, 0, 0, 0x21, 0)
        )

        args = parser.parse_args(["clear-faults"])
        status = console.clear_faults(client)

        self.assertEqual(args.command, "clear-faults")
        client.transact.assert_called_once_with(console.COMMAND_CLEAR_FAULTS)
        self.assertEqual(status["result"], "cleared")
        self.assertEqual(status["cleared_faults"], ["supervisor", "position"])

    def test_current_commands_accept_milliamperes(self) -> None:
        parser = console.make_parser()

        configure = parser.parse_args(
            ["configure", "--current-ma", "750", "--frequency-hz", "20"]
        )
        align = parser.parse_args(["align", "--current-ma", "750"])
        torque = parser.parse_args(["torque", "--current-ma", "750"])
        velocity = parser.parse_args(
            ["velocity", "--rpm", "480", "--current-limit-ma", "3000"]
        )
        position = parser.parse_args(
            [
                "position",
                "--revolutions",
                "0.25",
                "--max-rpm",
                "120",
                "--acceleration-rps2",
                "4",
                "--current-limit-ma",
                "3000",
            ]
        )

        self.assertEqual(configure.current_ma, 750.0)
        self.assertEqual(align.current_ma, 750.0)
        self.assertEqual(torque.current_ma, 750.0)
        self.assertEqual(velocity.current_limit_ma, 3000.0)
        self.assertEqual(velocity.acceleration_rps2, 16.0)
        self.assertEqual(position.current_limit_ma, 3000.0)

    def test_live_motion_lines_report_nominal_amperes(self) -> None:
        torque_row = console._torque_csv_row(
            {
                "host_elapsed_seconds": 0.1,
                "torque": torque_status("holding"),
                "drive": DRIVE_STATUS,
                "encoder": ENCODER_STATUS,
            }
        )
        velocity_row = console._velocity_csv_row(
            {
                "host_elapsed_seconds": 0.1,
                "velocity": velocity_status("tracking"),
                "drive": DRIVE_STATUS,
                "encoder": ENCODER_STATUS,
            }
        )
        position_row = console._position_csv_row(
            {
                "host_elapsed_seconds": 0.1,
                "position": position_status("moving"),
                "drive": DRIVE_STATUS,
                "encoder": ENCODER_STATUS,
            }
        )

        self.assertIn(
            "Iq=+0.145/+0.151 A",
            console._torque_live_line(torque_row, 250),
        )
        self.assertIn(
            "Iq=+0.012/0.151 A",
            console._velocity_live_line(velocity_row, 2000),
        )
        self.assertIn(
            "Iq=+0.055/0.606 A",
            console._position_live_line(position_row, 3000),
        )

    def test_stop_after_is_scoped_to_motion_capture_parsers(self) -> None:
        parser = console.make_parser()
        align = parser.parse_args(["align", "--counts", "50"])
        torque = parser.parse_args(
            [
                "torque",
                "--counts",
                "25",
                "--stop-after-seconds",
                "0.1",
                "--trace-at-seconds",
                "0.05",
                "--loadcell-port",
                "COM30",
                "--loadcell-tare-samples",
                "64",
            ]
        )
        velocity = parser.parse_args(
            [
                "velocity",
                "--rps",
                "0.1",
                "--current-limit-counts",
                "50",
                "--stop-after-seconds",
                "2",
            ]
        )
        position = parser.parse_args(
            [
                "position",
                "--revolutions",
                "0.25",
                "--max-rps",
                "0.5",
                "--acceleration-rps2",
                "1",
                "--current-limit-counts",
                "50",
                "--stop-after-seconds",
                "2",
            ]
        )

        self.assertFalse(hasattr(align, "stop_after_seconds"))
        self.assertEqual(torque.stop_after_seconds, 0.1)
        self.assertEqual(torque.trace_at_seconds, 0.05)
        self.assertEqual(torque.loadcell_port, "COM30")
        self.assertEqual(torque.loadcell_tare_samples, 64)
        self.assertEqual(velocity.stop_after_seconds, 2.0)
        self.assertEqual(position.stop_after_seconds, 2.0)

    def test_velocity_and_position_accept_rpm_units(self) -> None:
        parser = console.make_parser()
        velocity = parser.parse_args(
            [
                "velocity",
                "--rpm",
                "240",
                "--current-limit-counts",
                "100",
            ]
        )
        position = parser.parse_args(
            [
                "position",
                "--revolutions",
                "0.25",
                "--max-rpm",
                "120",
                "--acceleration-rps2",
                "4",
                "--current-limit-counts",
                "100",
            ]
        )

        self.assertEqual(velocity.rpm, 240.0)
        self.assertIsNone(velocity.rps)
        self.assertEqual(position.max_rpm, 120.0)
        self.assertEqual(position.revolutions, 0.25)

    def test_position_status_decodes_policy_and_faults(self) -> None:
        body = console.POSITION_STATUS_BODY.pack(
            1,
            5,
            6,
            0x85,
            1 << 2,
            round(1.5 * 65536),
            round(1.25 * 65536),
            round(1.0 * 65536),
            round(0.5 * 65536),
            round(0.25 * 65536),
            round(0.125 * 65536),
            -25,
            -24,
            100,
            1234,
            8766,
            100 * 65536,
            4 * 65536,
            4 * 65536,
            round(0.25 * 65536),
        )
        client = mock.Mock()
        client.transact.return_value = body

        status = console.query_position(client)

        client.transact.assert_called_once_with(
            console.COMMAND_GET_POSITION_STATUS
        )
        self.assertEqual(status["state"], "failed")
        self.assertEqual(status["result"], "following_error")
        self.assertEqual(status["faults"], ["following_error"])
        self.assertEqual(status["target_position_revolutions"], 1.5)
        self.assertEqual(
            status["policy"]["maximum_velocity_revolutions_per_second"],
            4.0,
        )
        self.assertEqual(
            status["policy"]["maximum_following_error_revolutions"],
            0.25,
        )

    def test_input_state_uses_physical_button_names(self) -> None:
        levels = 0xFF & ~(1 << 2)
        state = console.input_state(levels)

        self.assertEqual(list(state)[:3], ["left", "center", "right"])
        self.assertTrue(state["left"])
        self.assertFalse(state["center"])
        self.assertFalse(state["right"])

    def common_patches(self, velocity_side_effect):
        return (
            mock.patch.object(
                console, "query_velocity", side_effect=velocity_side_effect
            ),
            mock.patch.object(console, "query_status", return_value=DRIVE_STATUS),
            mock.patch.object(
                console, "query_encoder", return_value=ENCODER_STATUS
            ),
            mock.patch.object(
                console,
                "query_identity",
                return_value={"firmware": "0.25.0", "protocol": "1.8"},
            ),
            mock.patch.object(console, "query_configuration", return_value={}),
            mock.patch.object(console.time, "sleep", return_value=None),
        )

    def test_capture_writes_metadata_and_compact_csv(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            patches = self.common_patches(
                [
                    velocity_status("ramping"),
                    velocity_status("tracking"),
                    velocity_status("complete"),
                ]
            )
            with patches[0], patches[1], patches[2], patches[3], patches[4], patches[5]:
                client = FakeClient()
                result = console._run_velocity_capture(
                    client,
                    capture_args(root),
                    round(0.1 * 65536.0),
                    25,
                    velocity_status("idle"),
                )

            self.assertEqual(result, 0)
            run_directories = list(root.iterdir())
            self.assertEqual(len(run_directories), 1)
            run_directory = run_directories[0]
            metadata = json.loads(
                (run_directory / "metadata.json").read_text(encoding="utf-8")
            )
            with (run_directory / "telemetry.csv").open(
                encoding="utf-8", newline=""
            ) as stream:
                rows = list(csv.DictReader(stream))

            self.assertEqual(metadata["capture"]["status"], "complete")
            self.assertEqual(
                struct.unpack(">iHIi", client.last_payload),
                (round(0.1 * 65536.0), 25, 2000, 16 << 16),
            )
            self.assertEqual(
                metadata["request"][
                    "acceleration_revolutions_per_second_squared"
                ],
                16.0,
            )
            self.assertEqual(metadata["analysis"]["sample_count"], 3)
            self.assertNotIn("policy", metadata["initial"]["velocity"])
            self.assertNotIn("policy", metadata["final"]["velocity"])
            self.assertEqual(len(rows), 3)
            self.assertEqual(rows[-1]["state"], "complete")
            self.assertIn("measured_velocity_rps", rows[-1])
            self.assertNotIn("policy", rows[-1])
            self.assertFalse((run_directory / "telemetry.jsonl").exists())

    def test_capture_arms_and_saves_current_trace_csv(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            args = capture_args(root)
            args.trace_at_seconds = 0.0
            arm = mock.Mock()
            read = mock.Mock(return_value=[current_trace_sample()])
            patches = self.common_patches(
                [
                    velocity_status("tracking"),
                    velocity_status("complete"),
                ]
            )
            with (
                patches[0],
                patches[1],
                patches[2],
                patches[3],
                patches[4],
                patches[5],
                mock.patch.object(console, "arm_current_trace", arm),
                mock.patch.object(console, "read_current_trace", read),
            ):
                result = console._run_velocity_capture(
                    FakeClient(),
                    args,
                    round(0.1 * 65536.0),
                    25,
                    velocity_status("idle"),
                )

            self.assertEqual(result, 0)
            arm.assert_called_once()
            read.assert_called_once()
            run_directory = next(root.iterdir())
            metadata = json.loads(
                (run_directory / "metadata.json").read_text(encoding="utf-8")
            )
            with (run_directory / "current_trace.csv").open(
                encoding="utf-8", newline=""
            ) as stream:
                rows = list(csv.DictReader(stream))
            self.assertTrue(metadata["capture"]["trace_arm_sent"])
            self.assertEqual(metadata["capture"]["trace_sample_count"], 1)
            self.assertEqual(rows[0]["pwm_preload_margin_us"], "7.5")

    def test_ambiguous_start_error_still_sends_stop(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            client = FakeClient(console.ProtocolError("response timeout"))
            stop = mock.Mock()
            patches = self.common_patches([velocity_status("stopped")])
            with (
                patches[0],
                patches[1],
                patches[2],
                patches[3],
                patches[4],
                patches[5],
                mock.patch.object(console, "stop_drive", stop),
            ):
                with self.assertRaisesRegex(
                    console.ProtocolError, "response timeout"
                ):
                    console._run_velocity_capture(
                        client,
                        capture_args(root),
                        round(0.1 * 65536.0),
                        25,
                        velocity_status("idle"),
                    )

            stop.assert_called_once_with(client)
            run_directory = next(root.iterdir())
            metadata = json.loads(
                (run_directory / "metadata.json").read_text(encoding="utf-8")
            )
            self.assertEqual(metadata["capture"]["status"], "error")
            self.assertEqual(metadata["capture"]["error"], "response timeout")

    def test_scheduled_stop_uses_active_connection_and_passes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            args = capture_args(root)
            args.stop_after_seconds = 0.05
            stop = mock.Mock()
            patches = self.common_patches(
                [velocity_status("tracking"), velocity_status("stopped")]
            )
            with (
                patches[0],
                patches[1],
                patches[2],
                patches[3],
                patches[4],
                patches[5],
                mock.patch.object(console, "stop_drive", stop),
                mock.patch.object(
                    console.time,
                    "monotonic",
                    side_effect=[
                        100.0,
                        100.1,
                        100.1,
                        100.1,
                        100.2,
                        100.2,
                    ],
                ),
            ):
                result = console._run_velocity_capture(
                    FakeClient(),
                    args,
                    round(0.1 * 65536.0),
                    50,
                    velocity_status("idle"),
                )

            self.assertEqual(result, 0)
            stop.assert_called_once()
            metadata = json.loads(
                (next(root.iterdir()) / "metadata.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(
                metadata["final"]["velocity"]["state"], "stopped"
            )
            self.assertEqual(
                metadata["final"]["velocity"]["result"], "stopped"
            )
            self.assertTrue(metadata["capture"]["scheduled_stop_sent"])
            self.assertEqual(metadata["capture"]["status"], "complete")


class TorqueCaptureTests(unittest.TestCase):
    @staticmethod
    def torque_client(start_error: Exception | None = None) -> FakeClient:
        return FakeClient(
            start_error=start_error,
            start_command=console.COMMAND_START_ALIGNED_TORQUE,
            payload_length=6,
        )

    @staticmethod
    def common_patches(torque_side_effect):
        return (
            mock.patch.object(
                console,
                "query_aligned_torque",
                side_effect=torque_side_effect,
            ),
            mock.patch.object(console, "query_status", return_value=DRIVE_STATUS),
            mock.patch.object(
                console, "query_encoder", return_value=ENCODER_STATUS
            ),
            mock.patch.object(
                console,
                "query_identity",
                return_value={"firmware": "0.37.0", "protocol": "1.18"},
            ),
            mock.patch.object(console, "query_configuration", return_value={}),
            mock.patch.object(console.time, "sleep", return_value=None),
        )

    def test_loadcell_adapter_uses_existing_instrument_protocol(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output_directory = Path(temporary) / "torque-run"
            output_directory.mkdir()
            args = torque_capture_args(output_directory.parent)
            args.loadcell_port = "COM30"
            transport = FakeLoadCellTransport()
            with mock.patch.object(
                loadcell_host,
                "SerialLineTransport",
                return_value=transport,
            ):
                capture = console.TorqueLoadCellCapture(
                    args, output_directory
                )
                capture.prepare()
                capture.start()
                capture.set_motor_time_origin(10.0)
                capture.mark("motor_start_request")
                capture.poll()
                capture.stop()
                metadata = capture.finalize()
                capture.close()

            self.assertTrue(metadata["capture"]["complete"])
            self.assertEqual(metadata["host_summary"]["sample_count"], 2)
            self.assertEqual(
                metadata["host_summary"]["saturation_sample_count"], 0
            )
            self.assertEqual(
                metadata["motor_timeline"][0]["marker_id"],
                "motor_start_request",
            )
            self.assertTrue(
                (output_directory / "force_telemetry.csv").exists()
            )
            self.assertTrue(
                (output_directory / "loadcell_metadata.json").exists()
            )
            self.assertTrue(transport.closed)

    def test_loadcell_integrity_failure_still_drains_instrument(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output_directory = Path(temporary) / "torque-run"
            output_directory.mkdir()
            args = torque_capture_args(output_directory.parent)
            args.loadcell_port = "COM30"
            transport = FakeLoadCellTransport(sample_flags="0x3")
            with mock.patch.object(
                loadcell_host,
                "SerialLineTransport",
                return_value=transport,
            ):
                capture = console.TorqueLoadCellCapture(
                    args, output_directory
                )
                capture.prepare()
                capture.start()
                with self.assertRaisesRegex(
                    console.ProtocolError, "saturated sample"
                ):
                    capture.mark("motor_start_request")
                with self.assertRaisesRegex(
                    console.ProtocolError, "saturated sample"
                ):
                    capture.stop()
                metadata = capture.finalize()
                capture.close()

            self.assertEqual(transport.state, "IDLE")
            self.assertFalse(metadata["capture"]["complete"])
            self.assertEqual(
                metadata["host_summary"]["saturation_sample_count"], 1
            )

    def test_capture_writes_metadata_and_compact_csv(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            patches = self.common_patches(
                [
                    torque_status("ramping"),
                    torque_status("holding"),
                    torque_status("complete"),
                ]
            )
            with (
                patches[0],
                patches[1],
                patches[2],
                patches[3],
                patches[4],
                patches[5],
            ):
                client = self.torque_client()
                result = console._run_torque_capture(
                    client,
                    torque_capture_args(root),
                    25,
                    torque_status("idle"),
                )

            self.assertEqual(result, 0)
            run_directories = list(root.iterdir())
            self.assertEqual(len(run_directories), 1)
            run_directory = run_directories[0]
            metadata = json.loads(
                (run_directory / "metadata.json").read_text(encoding="utf-8")
            )
            with (run_directory / "telemetry.csv").open(
                encoding="utf-8", newline=""
            ) as stream:
                rows = list(csv.DictReader(stream))

            self.assertEqual(metadata["capture"]["status"], "complete")
            self.assertEqual(
                struct.unpack(">hI", client.last_payload),
                (25, 250),
            )
            self.assertEqual(metadata["request"]["q_current_counts"], 25)
            self.assertEqual(metadata["analysis"]["sample_count"], 3)
            self.assertEqual(
                metadata["analysis"]["maximum_phase_prediction_age_us"],
                1001,
            )
            self.assertEqual(
                metadata["analysis"]["maximum_missed_pwm_update_count"],
                0,
            )
            self.assertNotIn("policy", metadata["initial"]["torque"])
            self.assertNotIn("policy", metadata["final"]["torque"])
            self.assertEqual(len(rows), 3)
            self.assertEqual(rows[-1]["state"], "complete")
            self.assertIn("controller_acceleration_rps2", rows[-1])
            self.assertNotIn("policy", rows[-1])
            self.assertFalse((run_directory / "telemetry.jsonl").exists())

    def test_capture_coordinates_optional_loadcell_stream(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            args = torque_capture_args(root)
            args.loadcell_port = "COM30"
            loadcell = FakeLoadCellCapture()
            patches = self.common_patches(
                [torque_status("holding"), torque_status("complete")]
            )
            with (
                patches[0],
                patches[1],
                patches[2],
                patches[3],
                patches[4],
                patches[5],
                mock.patch.object(
                    console,
                    "_create_torque_loadcell_capture",
                    return_value=loadcell,
                ),
            ):
                result = console._run_torque_capture(
                    self.torque_client(),
                    args,
                    25,
                    torque_status("idle"),
                )

            self.assertEqual(result, 0)
            self.assertTrue(loadcell.prepared)
            self.assertTrue(loadcell.started)
            self.assertTrue(loadcell.stopped)
            self.assertTrue(loadcell.closed)
            self.assertIsNotNone(loadcell.motor_time_origin)
            self.assertEqual(
                loadcell.markers,
                ["motor_start_request", "motor_start_ack", "motor_terminal"],
            )
            metadata = json.loads(
                (next(root.iterdir()) / "metadata.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(metadata["capture"]["loadcell_status"], "complete")
            self.assertEqual(metadata["capture"]["loadcell_sample_count"], 79)
            self.assertEqual(metadata["request"]["loadcell"]["port"], "COM30")

    def test_loadcell_prepare_failure_does_not_start_motor(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            args = torque_capture_args(root)
            args.loadcell_port = "COM30"
            loadcell = FakeLoadCellCapture(
                prepare_error=console.ProtocolError("tare failed")
            )
            client = self.torque_client()
            stop = mock.Mock()
            patches = self.common_patches([])
            with (
                patches[0],
                patches[1],
                patches[2],
                patches[3],
                patches[4],
                patches[5],
                mock.patch.object(
                    console,
                    "_create_torque_loadcell_capture",
                    return_value=loadcell,
                ),
                mock.patch.object(console, "stop_drive", stop),
            ):
                with self.assertRaisesRegex(
                    console.ProtocolError, "tare failed"
                ):
                    console._run_torque_capture(
                        client,
                        args,
                        25,
                        torque_status("idle"),
                    )

            self.assertEqual(client.start_count, 0)
            stop.assert_not_called()
            self.assertTrue(loadcell.closed)
            metadata = json.loads(
                (next(root.iterdir()) / "metadata.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(metadata["capture"]["loadcell_status"], "error")
            self.assertIn("tare failed", metadata["capture"]["error"])

    def test_active_loadcell_failure_stops_motor(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            args = torque_capture_args(root)
            args.loadcell_port = "COM30"
            loadcell = FakeLoadCellCapture(
                poll_error=console.ProtocolError("sequence gap")
            )
            stop = mock.Mock()
            patches = self.common_patches(
                [torque_status("holding"), torque_status("stopped")]
            )
            with (
                patches[0],
                patches[1],
                patches[2],
                patches[3],
                patches[4],
                patches[5],
                mock.patch.object(
                    console,
                    "_create_torque_loadcell_capture",
                    return_value=loadcell,
                ),
                mock.patch.object(console, "stop_drive", stop),
            ):
                with self.assertRaisesRegex(
                    console.ProtocolError, "sequence gap"
                ):
                    console._run_torque_capture(
                        self.torque_client(),
                        args,
                        25,
                        torque_status("idle"),
                    )

            stop.assert_called_once()
            self.assertIn("motor_cleanup_stop_request", loadcell.markers)
            self.assertIn("motor_cleanup_stop_ack", loadcell.markers)
            self.assertTrue(loadcell.closed)
            metadata = json.loads(
                (next(root.iterdir()) / "metadata.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(metadata["capture"]["status"], "error")
            self.assertIn("sequence gap", metadata["capture"]["error"])

    def test_capture_arms_and_saves_current_trace_csv(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            args = torque_capture_args(root)
            args.trace_at_seconds = 0.0
            arm = mock.Mock()
            read = mock.Mock(return_value=[current_trace_sample()])
            patches = self.common_patches(
                [torque_status("holding"), torque_status("complete")]
            )
            with (
                patches[0],
                patches[1],
                patches[2],
                patches[3],
                patches[4],
                patches[5],
                mock.patch.object(console, "arm_current_trace", arm),
                mock.patch.object(console, "read_current_trace", read),
            ):
                result = console._run_torque_capture(
                    self.torque_client(),
                    args,
                    25,
                    torque_status("idle"),
                )

            self.assertEqual(result, 0)
            arm.assert_called_once()
            read.assert_called_once()
            run_directory = next(root.iterdir())
            metadata = json.loads(
                (run_directory / "metadata.json").read_text(encoding="utf-8")
            )
            with (run_directory / "current_trace.csv").open(
                encoding="utf-8", newline=""
            ) as stream:
                rows = list(csv.DictReader(stream))
            self.assertTrue(metadata["capture"]["trace_arm_sent"])
            self.assertEqual(metadata["capture"]["trace_sample_count"], 1)
            self.assertEqual(rows[0]["pwm_preload_margin_us"], "7.5")

    def test_ambiguous_start_error_still_sends_stop(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            client = self.torque_client(
                console.ProtocolError("response timeout")
            )
            stop = mock.Mock()
            patches = self.common_patches([torque_status("stopped")])
            with (
                patches[0],
                patches[1],
                patches[2],
                patches[3],
                patches[4],
                patches[5],
                mock.patch.object(console, "stop_drive", stop),
            ):
                with self.assertRaisesRegex(
                    console.ProtocolError, "response timeout"
                ):
                    console._run_torque_capture(
                        client,
                        torque_capture_args(root),
                        25,
                        torque_status("idle"),
                    )

            stop.assert_called_once_with(client)
            run_directory = next(root.iterdir())
            metadata = json.loads(
                (run_directory / "metadata.json").read_text(encoding="utf-8")
            )
            self.assertEqual(metadata["capture"]["status"], "error")
            self.assertEqual(metadata["capture"]["error"], "response timeout")

    def test_scheduled_stop_uses_active_connection_and_passes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            args = torque_capture_args(root)
            args.stop_after_seconds = 0.05
            stop = mock.Mock()
            patches = self.common_patches(
                [torque_status("holding"), torque_status("stopped")]
            )
            with (
                patches[0],
                patches[1],
                patches[2],
                patches[3],
                patches[4],
                patches[5],
                mock.patch.object(console, "stop_drive", stop),
                mock.patch.object(
                    console.time,
                    "monotonic",
                    side_effect=[
                        100.0,
                        100.1,
                        100.1,
                        100.1,
                        100.2,
                        100.2,
                    ],
                ),
            ):
                result = console._run_torque_capture(
                    self.torque_client(),
                    args,
                    25,
                    torque_status("idle"),
                )

            self.assertEqual(result, 0)
            stop.assert_called_once()
            metadata = json.loads(
                (next(root.iterdir()) / "metadata.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(metadata["final"]["torque"]["state"], "stopped")
            self.assertEqual(
                metadata["final"]["torque"]["result"], "stopped"
            )
            self.assertTrue(metadata["capture"]["scheduled_stop_sent"])
            self.assertEqual(metadata["capture"]["status"], "complete")


class PositionCaptureTests(unittest.TestCase):
    @staticmethod
    def position_client(start_error: Exception | None = None) -> FakeClient:
        return FakeClient(
            start_error=start_error,
            start_command=console.COMMAND_START_POSITION_RELATIVE,
            payload_length=18,
        )

    @staticmethod
    def common_patches(position_side_effect):
        return (
            mock.patch.object(
                console, "query_position", side_effect=position_side_effect
            ),
            mock.patch.object(console, "query_status", return_value=DRIVE_STATUS),
            mock.patch.object(
                console, "query_encoder", return_value=ENCODER_STATUS
            ),
            mock.patch.object(
                console,
                "query_identity",
                return_value={"firmware": "0.26.0", "protocol": "1.9"},
            ),
            mock.patch.object(console, "query_configuration", return_value={}),
            mock.patch.object(console.time, "sleep", return_value=None),
        )

    def run_capture(
        self,
        client: FakeClient,
        args: SimpleNamespace,
        initial_position: dict,
    ) -> int:
        return console._run_position_capture(
            client,
            args,
            round(args.revolutions * 65536.0),
            round(args.max_rps * 65536.0),
            round(args.acceleration_rps2 * 65536.0),
            100,
            initial_position,
            velocity_status("idle"),
        )

    def test_capture_writes_metadata_and_compact_csv(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            patches = self.common_patches(
                [
                    position_status("moving"),
                    position_status("settling"),
                    position_status("complete"),
                ]
            )
            with (
                patches[0],
                patches[1],
                patches[2],
                patches[3],
                patches[4],
                patches[5],
            ):
                result = self.run_capture(
                    self.position_client(),
                    position_capture_args(root),
                    position_status("idle"),
                )

            self.assertEqual(result, 0)
            run_directories = list(root.iterdir())
            self.assertEqual(len(run_directories), 1)
            run_directory = run_directories[0]
            metadata = json.loads(
                (run_directory / "metadata.json").read_text(encoding="utf-8")
            )
            with (run_directory / "telemetry.csv").open(
                encoding="utf-8", newline=""
            ) as stream:
                rows = list(csv.DictReader(stream))

            self.assertEqual(metadata["capture"]["status"], "complete")
            self.assertEqual(metadata["analysis"]["sample_count"], 3)
            self.assertNotIn("policy", metadata["initial"]["position"])
            self.assertNotIn("policy", metadata["final"]["position"])
            self.assertEqual(len(rows), 3)
            self.assertEqual(rows[-1]["state"], "complete")
            self.assertEqual(rows[-1]["result"], "settled")
            self.assertIn("target_position_error_revolutions", rows[-1])
            self.assertIn("profile_following_error_revolutions", rows[-1])
            self.assertNotIn("policy", rows[-1])
            self.assertFalse((run_directory / "telemetry.jsonl").exists())

    def test_ambiguous_start_error_still_sends_stop(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            client = self.position_client(
                console.ProtocolError("response timeout")
            )
            stop = mock.Mock()
            patches = self.common_patches([position_status("stopped")])
            with (
                patches[0],
                patches[1],
                patches[2],
                patches[3],
                patches[4],
                patches[5],
                mock.patch.object(console, "stop_drive", stop),
            ):
                with self.assertRaisesRegex(
                    console.ProtocolError, "response timeout"
                ):
                    self.run_capture(
                        client,
                        position_capture_args(root),
                        position_status("idle"),
                    )

            stop.assert_called_once_with(client)
            metadata = json.loads(
                (next(root.iterdir()) / "metadata.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(metadata["capture"]["status"], "error")
            self.assertEqual(metadata["capture"]["error"], "response timeout")

    def test_scheduled_stop_uses_active_connection_and_passes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            args = position_capture_args(root)
            args.stop_after_seconds = 0.05
            stop = mock.Mock()
            patches = self.common_patches(
                [position_status("moving"), position_status("stopped")]
            )
            with (
                patches[0],
                patches[1],
                patches[2],
                patches[3],
                patches[4],
                patches[5],
                mock.patch.object(console, "stop_drive", stop),
                mock.patch.object(
                    console.time,
                    "monotonic",
                    side_effect=[
                        100.0,
                        100.1,
                        100.1,
                        100.1,
                        100.2,
                        100.2,
                    ],
                ),
            ):
                result = self.run_capture(
                    self.position_client(),
                    args,
                    position_status("idle"),
                )

            self.assertEqual(result, 0)
            stop.assert_called_once()
            metadata = json.loads(
                (next(root.iterdir()) / "metadata.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(
                metadata["final"]["position"]["state"], "stopped"
            )
            self.assertEqual(
                metadata["final"]["position"]["result"], "stopped"
            )
            self.assertTrue(metadata["capture"]["scheduled_stop_sent"])
            self.assertEqual(metadata["capture"]["status"], "complete")


if __name__ == "__main__":
    unittest.main()
