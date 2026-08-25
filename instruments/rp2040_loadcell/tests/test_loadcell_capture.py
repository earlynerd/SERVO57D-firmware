from __future__ import annotations

import argparse
import csv
import json
import sys
import tempfile
import unittest
from pathlib import Path


HOST_DIRECTORY = Path(__file__).resolve().parents[1] / "host"
sys.path.insert(0, str(HOST_DIRECTORY))

from loadcell_capture import (  # noqa: E402
    CSV_FIELDS,
    CaptureArtifacts,
    CaptureInterrupted,
    CaptureRunner,
    CaptureSession,
    ErrorResponse,
    FinalSummaryRecord,
    MarkerRecord,
    OkResponse,
    ProtocolError,
    SampleRecord,
    parse_record,
    parse_tare_complete,
    validate_start_response,
    validate_stop_response,
    validate_identifier,
)


class RecordParsingTests(unittest.TestCase):
    def test_parses_all_version_one_record_types(self) -> None:
        self.assertEqual(
            parse_record("S,1,42,123456,-765432,0x0A,3\r\n"),
            SampleRecord(
                sequence=42,
                timestamp_us=123456,
                raw_counts=-765432,
                flags=0x0A,
                dropped_total=3,
            ),
        )
        self.assertEqual(
            parse_record("M,1,torque_on,123500\n"),
            MarkerRecord(marker_id="torque_on", timestamp_us=123500),
        )
        self.assertEqual(
            parse_record("OK,1,INFO,rp2040-loadcell,0.1.0,board=unknown"),
            OkResponse(
                command="INFO",
                fields=("rp2040-loadcell", "0.1.0", "board=unknown"),
            ),
        )
        self.assertEqual(
            parse_record("OK,1,STATUS,state=idle,drdy_age_us=400"),
            OkResponse(
                command="STATUS",
                fields=("state=idle", "drdy_age_us=400"),
            ),
        )
        self.assertEqual(
            parse_record("ERR,1,CONFIG,INVALID_RATE,unsupported sample rate"),
            ErrorResponse(
                command="CONFIG",
                error_code="INVALID_RATE",
                description="unsupported sample rate",
            ),
        )
        self.assertEqual(
            parse_record("F,1,test-7,42,45,4,2,1,2,123456,132831,320.000"),
            FinalSummaryRecord(
                run_id="test-7",
                first_sequence=42,
                last_sequence=45,
                captured_sample_count=4,
                dropped_sample_count=2,
                i2c_error_count=1,
                buffer_overrun_count=2,
                first_timestamp_us=123456,
                last_timestamp_us=132831,
                average_sample_rate_sps=320.0,
            ),
        )

    def test_rejects_malformed_and_untagged_lines(self) -> None:
        malformed_lines = (
            "debug: booted",
            "",
            "S,2,1,100,2,0,0",
            "S,1,1,100,2,0",
            "S,1,-1,100,2,0,0",
            "S,1,1,100,8388608,0,0",
            "S,1,1,100,2,not-hex,0",
            "M,1,,100",
            "M,1,marker/slash,100",
            "ERR,1,CONFIG,BAD",
            "F,1,run/slash,1,2,2,0,0,0,100,200,320",
            "F,1,run,1,2,2,0,0,0,100,200,nan",
            "F,1,run,1,2,2,0,0,0,100,200",
        )
        for line in malformed_lines:
            with self.subTest(line=line):
                with self.assertRaises(ProtocolError):
                    parse_record(line)

    def test_host_identifier_contract_matches_firmware(self) -> None:
        self.assertEqual(validate_identifier("run_01.test-a", "run ID"), "run_01.test-a")
        with self.assertRaises(argparse.ArgumentTypeError):
            validate_identifier("run/01", "run ID")
        with self.assertRaises(argparse.ArgumentTypeError):
            validate_identifier("x" * 32, "run ID")

    def test_decodes_tare_complete_payload(self) -> None:
        response = parse_record("OK,1,TARE,COMPLETE,64,-101.25,3.5")
        self.assertIsInstance(response, OkResponse)
        self.assertEqual(
            parse_tare_complete(response),
            {
                "sample_count": 64,
                "mean_raw_counts": -101.25,
                "stddev_raw_counts": 3.5,
            },
        )


class CaptureSessionTests(unittest.TestCase):
    def test_detects_sequence_gaps_and_timestamp_regressions(self) -> None:
        session = CaptureSession()
        session.start_capture(10.0)
        session.accept(SampleRecord(10, 1000, 1, 0, 0), 10.1)
        session.accept(SampleRecord(11, 2000, 2, 0, 0), 10.2)
        session.accept(SampleRecord(14, 1900, 3, 0, 2), 10.3)

        self.assertEqual(session.sample_count, 3)
        self.assertEqual(session.sequence_missing_count, 2)
        self.assertEqual(len(session.sequence_gaps), 1)
        self.assertEqual(session.sequence_gaps[0].previous_sequence, 11)
        self.assertEqual(session.sequence_gaps[0].current_sequence, 14)
        self.assertEqual(session.sequence_gaps[0].missing_count, 2)
        self.assertEqual(session.timestamp_regression_count, 1)
        self.assertEqual(session.last_dropped_total, 2)

    def test_sequence_tracking_handles_uint32_wrap(self) -> None:
        session = CaptureSession()
        session.start_capture(0.0)
        session.accept(SampleRecord(0xFFFFFFFF, 100, 1, 0, 0), 0.1)
        session.accept(SampleRecord(0, 200, 2, 0, 0), 0.2)
        self.assertEqual(session.sequence_missing_count, 0)
        self.assertEqual(session.duplicate_or_out_of_order_count, 0)

    def test_accounts_for_final_summary_and_reports_mismatches(self) -> None:
        session = CaptureSession()
        session.start_capture(0.0)
        session.expect_final_summary("capture-a")
        session.accept(SampleRecord(20, 1_000_000, 10, 0, 0), 0.1)
        session.accept(SampleRecord(21, 1_003_125, 11, 0, 0), 0.2)
        final = FinalSummaryRecord(
            run_id="capture-a",
            first_sequence=20,
            last_sequence=21,
            captured_sample_count=3,
            dropped_sample_count=0,
            i2c_error_count=0,
            buffer_overrun_count=0,
            first_timestamp_us=1_000_000,
            last_timestamp_us=1_003_125,
            average_sample_rate_sps=320.0,
        )
        session.accept(final, 0.3)

        summary = session.summary_json()
        self.assertIs(session.final_summary, final)
        self.assertAlmostEqual(summary["measured_average_sample_rate_sps"], 320.0)
        self.assertEqual(
            summary["final_summary_mismatches"],
            {"captured_sample_count": {"host": 2, "device": 3}},
        )

    def test_records_malformed_lines_without_unbounded_growth(self) -> None:
        session = CaptureSession()
        for index in range(105):
            session.record_malformed(f"bad-{index}", ProtocolError("bad record"))
        self.assertEqual(session.malformed_record_count, 105)
        self.assertEqual(len(session.malformed_records), 100)

    def test_counts_saturated_samples(self) -> None:
        session = CaptureSession()
        session.start_capture(0.0)
        session.accept(SampleRecord(0, 100, (1 << 23) - 1, 0x0001, 0), 0.1)

        self.assertEqual(session.summary_json()["saturation_sample_count"], 1)


class ArtifactTests(unittest.TestCase):
    def test_writes_required_csv_and_metadata_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output_directory = Path(temporary_directory) / "capture"
            artifacts = CaptureArtifacts(output_directory)
            sample = SampleRecord(
                sequence=7,
                timestamp_us=987654,
                raw_counts=-12345,
                flags=0x10,
                dropped_total=2,
            )
            artifacts.write_sample(sample, 0.0123456789)
            metadata = {
                "schema_version": 1,
                "capture": {"run_id": "artifact-test", "complete": True},
                "device_final_summary": {
                    "captured_sample_count": 1,
                    "average_sample_rate_sps": 320.0,
                },
            }
            artifacts.finalize(metadata)

            with artifacts.csv_path.open(newline="", encoding="utf-8") as stream:
                reader = csv.DictReader(stream)
                rows = list(reader)
            self.assertEqual(tuple(reader.fieldnames or ()), CSV_FIELDS)
            self.assertEqual(len(rows), 1)
            self.assertEqual(rows[0]["sequence"], "7")
            self.assertEqual(rows[0]["device_timestamp_us"], "987654")
            self.assertEqual(rows[0]["host_receive_elapsed_seconds"], "0.012345679")
            self.assertEqual(rows[0]["raw_counts"], "-12345")
            self.assertEqual(rows[0]["flags_hex"], "0x10")
            self.assertEqual(rows[0]["dropped_total"], "2")

            loaded_metadata = json.loads(
                artifacts.metadata_path.read_text(encoding="utf-8")
            )
            self.assertEqual(loaded_metadata, metadata)
            self.assertFalse(
                artifacts.metadata_path.with_suffix(".json.tmp").exists()
            )

    def test_can_join_an_existing_parent_capture_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output_directory = Path(temporary_directory) / "torque-run"
            output_directory.mkdir()
            artifacts = CaptureArtifacts(
                output_directory,
                create_directory=False,
                metadata_name="loadcell_metadata.json",
            )
            artifacts.finalize({"capture": {"complete": True}})

            self.assertEqual(
                artifacts.metadata_path.name, "loadcell_metadata.json"
            )
            self.assertTrue((output_directory / "force_telemetry.csv").exists())


class _IncrementingClock:
    def __init__(self) -> None:
        self.value = 0.0

    def __call__(self) -> float:
        self.value += 0.0005
        return self.value


class _FakeInstrumentTransport:
    port_name = "FAKE0"

    def __init__(self) -> None:
        self.lines: list[str] = []
        self.commands: list[str] = []
        self.status_count = 0
        self.run_id = "unset"

    def write_line(self, line: str) -> None:
        self.commands.append(line)
        command = line.split(" ", 1)[0]
        if command == "INFO":
            self.lines.append("OK,1,INFO,rp2040-loadcell,test-build\n")
        elif command == "CONFIG":
            self.lines.append("OK,1,CONFIG,320,128\n")
        elif command == "TARE":
            self.lines.extend(
                (
                    "OK,1,TARE,STARTED,2\n",
                    "OK,1,TARE,COMPLETE,2,-10.5,1.25\n",
                )
            )
        elif command == "STATUS":
            self.status_count += 1
            state = "idle"
            self.lines.append(f"OK,1,STATUS,state={state}\n")
        elif command == "START":
            run_id = line.split(" ", 1)[1]
            self.run_id = run_id
            self.lines.extend(
                (
                    f"OK,1,START,{run_id},900\n",
                    "S,1,100,1000,-11,0x0,0\n",
                    "S,1,101,4125,-10,0x0,0\n",
                )
            )
        elif command == "STOP":
            self.lines.extend(
                (
                    "OK,1,STOP,DRAINING,7250\n",
                    f"F,1,{self.run_id},100,101,2,0,0,0,1000,4125,320.0\n",
                )
            )
        else:  # pragma: no cover - makes failures explicit in this fake.
            raise AssertionError(f"unexpected command {line}")

    def read_line(self) -> str | None:
        return self.lines.pop(0) if self.lines else None

    def close(self) -> None:
        pass


class _InterruptingInstrumentTransport(_FakeInstrumentTransport):
    def __init__(self) -> None:
        super().__init__()
        self.capture_active = False
        self.did_interrupt = False

    def write_line(self, line: str) -> None:
        super().write_line(line)
        command = line.split(" ", 1)[0]
        if command == "START":
            self.capture_active = True
        elif command == "STOP":
            self.capture_active = False

    def read_line(self) -> str | None:
        if self.capture_active and not self.lines and not self.did_interrupt:
            self.did_interrupt = True
            raise KeyboardInterrupt
        return super().read_line()


class _StaleFinalInstrumentTransport(_FakeInstrumentTransport):
    def write_line(self, line: str) -> None:
        super().write_line(line)
        if line == "INFO":
            self.lines.insert(
                0,
                "F,1,previous-run,1,2,2,0,0,0,100,200,10000.0\n",
            )


class _ReconnectRunningInstrumentTransport(_FakeInstrumentTransport):
    def __init__(self) -> None:
        super().__init__()
        self.reconnect_state = "RUNNING"

    def write_line(self, line: str) -> None:
        command = line.split(" ", 1)[0]
        if command == "STATUS" and self.reconnect_state == "RUNNING":
            self.commands.append(line)
            self.lines.append("OK,1,STATUS,RUNNING\n")
            return
        if command == "STOP" and self.reconnect_state == "RUNNING":
            self.commands.append(line)
            self.lines.extend(
                (
                    "OK,1,STOP,DRAINING,500\n",
                    "F,1,abandoned-run,1,2,2,0,0,0,100,200,10000.0\n",
                )
            )
            self.reconnect_state = "IDLE"
            return
        super().write_line(line)


class CaptureRunnerTests(unittest.TestCase):
    def test_rejects_start_and_stop_without_device_timestamps(self) -> None:
        with self.assertRaises(ProtocolError):
            validate_start_response(OkResponse("START", ("run-a",)), "run-a")
        with self.assertRaises(ProtocolError):
            validate_stop_response(OkResponse("STOP", ("DRAINING",)))

    def test_waits_for_tare_complete_before_status_and_start(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            artifacts = CaptureArtifacts(Path(temporary_directory) / "capture")
            transport = _FakeInstrumentTransport()
            runner = CaptureRunner(
                transport,
                artifacts,
                clock=_IncrementingClock(),
                command_timeout_seconds=1.0,
                drain_timeout_seconds=1.0,
                quiet=True,
            )
            metadata = runner.run(
                run_id="async-tare",
                sample_rate_sps=320,
                gain=128,
                tare_sample_count=2,
                duration_seconds=0.01,
                markers=[],
                calibration={"counts_per_newton": None},
            )

            self.assertEqual(
                transport.commands,
                (
                    [
                        "INFO",
                        "STATUS",
                        "CONFIG 320 128",
                        "TARE 2",
                        "STATUS",
                        "START async-tare",
                        "STOP",
                        "STATUS",
                    ]
                ),
            )
            self.assertEqual(
                metadata["responses"]["tare"]["started"]["fields"][0],
                "STARTED",
            )
            self.assertEqual(
                metadata["responses"]["tare"]["complete"]["fields"][0],
                "COMPLETE",
            )
            self.assertEqual(
                metadata["calibration"]["tare"],
                {
                    "sample_count": 2,
                    "mean_raw_counts": -10.5,
                    "stddev_raw_counts": 1.25,
                },
            )
            self.assertGreaterEqual(
                metadata["responses"]["tare"]["timeout_seconds"], 3.0
            )
            self.assertEqual(metadata["host_summary"]["sample_count"], 2)
            self.assertEqual(metadata["host_summary"]["unexpected_responses"], [])
            self.assertEqual(
                metadata["device_final_summary"]["captured_sample_count"], 2
            )

    def test_interrupt_stops_capture_preserves_artifacts_and_is_not_success(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            artifacts = CaptureArtifacts(Path(temporary_directory) / "capture")
            transport = _InterruptingInstrumentTransport()
            runner = CaptureRunner(
                transport,
                artifacts,
                clock=_IncrementingClock(),
                command_timeout_seconds=1.0,
                drain_timeout_seconds=1.0,
                quiet=True,
            )

            with self.assertRaises(CaptureInterrupted):
                runner.run(
                    run_id="interrupt-test",
                    sample_rate_sps=320,
                    gain=128,
                    tare_sample_count=2,
                    duration_seconds=0.1,
                    markers=[],
                    calibration={"counts_per_newton": None},
                )

            self.assertIn("STOP", transport.commands)
            metadata = json.loads(
                artifacts.metadata_path.read_text(encoding="utf-8")
            )
            self.assertTrue(metadata["capture"]["interrupted"])
            self.assertTrue(metadata["capture"]["complete"])

    def test_stale_reconnect_final_cannot_complete_new_capture(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            artifacts = CaptureArtifacts(Path(temporary_directory) / "capture")
            transport = _StaleFinalInstrumentTransport()
            runner = CaptureRunner(
                transport,
                artifacts,
                clock=_IncrementingClock(),
                command_timeout_seconds=1.0,
                drain_timeout_seconds=1.0,
                quiet=True,
            )
            metadata = runner.run(
                run_id="new-run",
                sample_rate_sps=320,
                gain=128,
                tare_sample_count=2,
                duration_seconds=0.01,
                markers=[],
                calibration={"counts_per_newton": None},
            )

            self.assertEqual(metadata["device_final_summary"]["run_id"], "new-run")
            self.assertIn(
                {"type": "unclaimed_final_summary", "run_id": "previous-run"},
                metadata["host_summary"]["unexpected_responses"],
            )

    def test_marker_received_before_start_is_quarantined(self) -> None:
        session = CaptureSession()
        session.accept(MarkerRecord("previous-marker", 123), 0.0)
        session.start_capture(1.0)
        session.accept(MarkerRecord("current-marker", 456), 1.1)

        self.assertEqual(session.markers, [MarkerRecord("current-marker", 456)])
        self.assertIn(
            {"type": "marker_before_start", "marker_id": "previous-marker"},
            session.unexpected_responses,
        )

    def test_reconnect_stops_and_drains_abandoned_running_capture(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            artifacts = CaptureArtifacts(Path(temporary_directory) / "capture")
            transport = _ReconnectRunningInstrumentTransport()
            runner = CaptureRunner(
                transport,
                artifacts,
                clock=_IncrementingClock(),
                command_timeout_seconds=1.0,
                drain_timeout_seconds=1.0,
                quiet=True,
            )
            metadata = runner.run(
                run_id="reconnected-run",
                sample_rate_sps=320,
                gain=128,
                tare_sample_count=2,
                duration_seconds=0.01,
                markers=[],
                calibration={"counts_per_newton": None},
            )

            self.assertEqual(
                transport.commands[:4], ["INFO", "STATUS", "STOP", "STATUS"]
            )
            self.assertEqual(
                metadata["responses"]["reconnect_final_summary"]["run_id"],
                "abandoned-run",
            )
            self.assertEqual(
                metadata["device_final_summary"]["run_id"], "reconnected-run"
            )


if __name__ == "__main__":
    unittest.main()
