import csv
import json
import struct
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from tools import mks57d_rs485 as console


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
    "loop": {
        "fault_flags_hex": "0x00000000",
        "faults": [],
        "sample_count": 40000,
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
        payload_length: int = 10,
    ) -> None:
        self.start_error = start_error
        self.start_command = start_command
        self.payload_length = payload_length
        self.start_count = 0

    def transact(self, command: int, payload: bytes = b"") -> bytes:
        self.assert_start_payload(command, payload)
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


class VelocityCaptureTests(unittest.TestCase):
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
        self.assertEqual(position.current_limit_ma, 3000.0)

    def test_live_motion_lines_report_nominal_amperes(self) -> None:
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

        self.assertIn("Iq=+0.012/0.151 A", console._velocity_live_line(velocity_row, 2000))
        self.assertIn("Iq=+0.055/0.606 A", console._position_live_line(position_row, 3000))

    def test_stop_after_is_scoped_to_motion_capture_parsers(self) -> None:
        parser = console.make_parser()
        align = parser.parse_args(["align", "--counts", "50"])
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
                result = console._run_velocity_capture(
                    FakeClient(),
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
            self.assertEqual(metadata["analysis"]["sample_count"], 3)
            self.assertNotIn("policy", metadata["initial"]["velocity"])
            self.assertNotIn("policy", metadata["final"]["velocity"])
            self.assertEqual(len(rows), 3)
            self.assertEqual(rows[-1]["state"], "complete")
            self.assertIn("measured_velocity_rps", rows[-1])
            self.assertNotIn("policy", rows[-1])
            self.assertFalse((run_directory / "telemetry.jsonl").exists())

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
