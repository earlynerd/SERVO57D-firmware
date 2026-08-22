import csv
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from tools import mks57d_rs485 as console


def velocity_status(state: str) -> dict:
    active = state not in {"complete", "stopped", "failed"}
    return {
        "schema": 1,
        "state": state,
        "result": "deadline" if state == "complete" else "none",
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
    def __init__(self, start_error: Exception | None = None) -> None:
        self.start_error = start_error
        self.start_count = 0

    def transact(self, command: int, payload: bytes = b"") -> bytes:
        self.assert_start_payload(command, payload)
        self.start_count += 1
        if self.start_error is not None:
            raise self.start_error
        return b""

    @staticmethod
    def assert_start_payload(command: int, payload: bytes) -> None:
        if command != console.COMMAND_START_VELOCITY:
            raise AssertionError(f"unexpected command 0x{command:04X}")
        if len(payload) != 10:
            raise AssertionError("velocity request must be 10 bytes")


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
    )


class VelocityCaptureTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
