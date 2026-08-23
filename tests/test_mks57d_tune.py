import json
import math
import struct
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from tools import mks57d_tune as tune


def configuration(active_kp: int = 4 * 65536, active_ki: int = 1024) -> dict:
    def gains(kp: int, ki: int) -> dict:
        return {
            "proportional_q16_per_count": kp,
            "integral_q16_per_count_per_step": ki,
            "proportional_per_count": kp / 65536.0,
            "integral_per_count_per_step": ki / 65536.0,
        }

    return {
        "schema": 2,
        "tuning_supported": True,
        "flags": [
            "write_supported",
            "active_calibration_valid",
            "active_matches_record",
        ],
        "generation": 7,
        "default": {"current_loop_gains": gains(4 * 65536, 1024)},
        "stored": {"current_loop_gains": gains(4 * 65536, 1024)},
        "active": {"current_loop_gains": gains(active_kp, active_ki)},
        "limits": {
            "maximum_current_loop_gains": gains(8 * 65536, 4096)
        },
    }


def ready_status() -> dict:
    return {
        "flags": [
            "adc_ready",
            "adc_snapshot_valid",
            "adc_calibration_ready",
            "current_loop_initialized",
            "bridge_ready",
        ],
        "test": {
            "amplitude_counts": 25,
            "maximum_amplitude_counts": 600,
            "frequency_hz": 0.5,
            "remote_run_remaining_millis": 0,
        },
        "loop": {"faults": [], "phase_voltage_limit_permille": 700},
        "reset": {"watchdog_reset": False, "retained_panic": 0},
    }


def healthy_encoder() -> dict:
    return {
        "status": "ok",
        "transport_status": "ok",
        "no_magnet": False,
        "over_speed": False,
        "estimator": {"flags": ["estimator_ready"], "faults": []},
    }


def analysis() -> dict:
    return {
        "fundamental_gain": 0.91,
        "phase_lag_degrees": 9.0,
        "combined_rms_error_milliamperes": 42.0,
        "maximum_absolute_voltage_permille": 430.0,
        "maximum_absolute_phase_voltage_volts": 10.3,
        "voltage_saturation_fraction": 0.0,
        "encoder": {
            "revolutions": 0.1,
            "rpm": 6.0,
            "error_count_delta": 0,
        },
    }


def trace() -> list[dict]:
    return [
        {
            "timing": {
                "pwm_preload_margin_us": 32.5,
                "trigger_to_dma_us": 4.0,
                "dma_to_pwm_stage_us": 21.0,
            }
        }
    ]


def sweep_args(root: Path) -> SimpleNamespace:
    return SimpleNamespace(
        port="COM14",
        current_ma=303.0,
        seconds=0.5,
        settle_seconds=0.1,
        interval=0.05,
        kp=[3.0],
        ki=[0.015625],
        electrical_hz=[20.0],
        leg="A1",
        output_root=root,
    )


class FakeClient:
    def __init__(self) -> None:
        self.active_kp = 4 * 65536
        self.active_ki = 1024
        self.commands: list[tuple[int, bytes]] = []

    def transact(self, command: int, payload: bytes = b"") -> bytes:
        self.commands.append((command, payload))
        if command == tune.COMMAND_SAVE_CONFIGURATION:
            raise AssertionError("sweep must never save configuration")
        if command == tune.COMMAND_SET_CURRENT_LOOP_GAINS:
            self.active_kp, self.active_ki = struct.unpack(">ii", payload)
        return b""


class TuningWorkflowTests(unittest.TestCase):
    def test_high_resolution_trace_drives_gain_phase_and_error_metrics(self) -> None:
        samples = []
        captured = []
        for index in range(32):
            phase = 2.0 * math.pi * index / 32.0
            reference = complex(200.0 * math.cos(phase), 200.0 * math.sin(phase))
            measured = 0.8 * reference * complex(
                math.cos(math.radians(-30.0)),
                math.sin(math.radians(-30.0)),
            )
            captured.append(
                {
                    "reference_counts": {
                        "a": reference.real,
                        "b": reference.imag,
                    },
                    "measured_counts": {
                        "a": measured.real,
                        "b": measured.imag,
                    },
                    "phase_voltage_permille": {"a": 100, "b": -100},
                    "phase_voltage_command_volts": {"a": 2.4, "b": -2.4},
                }
            )

        result = tune.analyze_trial(samples, captured, 0.1, 700.0)

        self.assertAlmostEqual(result["fundamental_gain"], 0.8, places=6)
        self.assertAlmostEqual(result["phase_lag_degrees"], 30.0, places=6)
        self.assertEqual(result["source"], "20 kHz current trace")
        self.assertEqual(result["voltage_saturation_fraction"], 0.0)

    def common_patches(self, client: FakeClient):
        def capture(
            _client,
            _leg,
            _duration,
            _interval,
            path,
            trace_at_seconds=None,
        ):
            self.assertEqual(trace_at_seconds, 0.1)
            path.write_text("{}\n{}\n{}\n", encoding="utf-8")
            return ([ready_status(), ready_status(), ready_status()], True)

        return (
            mock.patch.object(
                tune, "query_identity", return_value={"firmware": "0.31.0", "protocol": "1.14"}
            ),
            mock.patch.object(tune, "query_status", side_effect=lambda _client: ready_status()),
            mock.patch.object(
                tune,
                "query_configuration",
                side_effect=lambda _client: configuration(
                    client.active_kp, client.active_ki
                ),
            ),
            mock.patch.object(tune, "query_encoder", return_value=healthy_encoder()),
            mock.patch.object(tune, "read_current_trace", return_value=trace()),
            mock.patch.object(tune.motor_test, "_run_capture", side_effect=capture),
            mock.patch.object(
                tune.motor_test,
                "_configure",
                side_effect=lambda _client, counts, frequency: {
                    "counts": counts,
                    "frequency_hz": frequency,
                },
            ),
            mock.patch.object(tune, "analyze_trial", return_value=analysis()),
        )

    def test_sweep_restores_gains_and_writes_normalized_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            session_directory = root / "session"
            session_directory.mkdir()
            client = FakeClient()
            patches = self.common_patches(client)
            with patches[0], patches[1], patches[2], patches[3], patches[4], patches[5], patches[6], patches[7]:
                session = tune.execute_sweep(
                    client, sweep_args(root), session_directory
                )

            self.assertEqual(session["status"], "complete")
            self.assertTrue(session["restore"]["gains_restored"])
            self.assertEqual(client.active_kp, 4 * 65536)
            self.assertNotIn(
                tune.COMMAND_SAVE_CONFIGURATION,
                [command for command, _ in client.commands],
            )
            self.assertIn(
                tune.COMMAND_STOP_DRIVE,
                [command for command, _ in client.commands],
            )
            self.assertTrue((session_directory / "session.json").exists())
            self.assertTrue((session_directory / "summary.csv").exists())
            report = (session_directory / "report.html").read_text(
                encoding="utf-8"
            )
            self.assertIn("Current magnitude tracking", report)
            self.assertIn("Sweeps never save configuration", report)
            self.assertIn(
                ".series{fill:none;stroke:var(--series-color)", report
            )
            self.assertIn(
                ".legend-line{width:18px;border-top:3px solid "
                "var(--series-color)",
                report,
            )
            self.assertIn(".s1{--series-color:var(--s1)}", report)
            self.assertNotIn(".s1{stroke:", report)
            self.assertNotIn("fill:var(--s1)", report)
            trial_directory = session_directory / session["trials"][0][
                "artifact_directory"
            ]
            self.assertTrue((trial_directory / "telemetry.jsonl").exists())
            self.assertTrue((trial_directory / "trace.jsonl").exists())
            self.assertTrue((trial_directory / "trial.json").exists())

    def test_shared_plot_style_keeps_lines_open_and_colors_legends(self) -> None:
        style = tune.motor_test._plot_series_css()

        self.assertIn(
            ".series{fill:none;stroke:var(--series-color)", style
        )
        self.assertIn(
            ".legend-line{width:20px;border-top:3px solid "
            "var(--series-color)",
            style,
        )
        self.assertIn(".s5{--series-color:var(--s5)}", style)
        self.assertNotIn(".s1{stroke:", style)
        self.assertNotIn("fill:var(--s1)", style)

    def test_failure_still_stops_and_restores_starting_gains(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            client = FakeClient()
            patches = self.common_patches(client)
            patches = list(patches)
            patches[5] = mock.patch.object(
                tune.motor_test,
                "_run_capture",
                side_effect=tune.ProtocolError("injected capture failure"),
            )
            with patches[0], patches[1], patches[2], patches[3], patches[4], patches[5], patches[6], patches[7]:
                with self.assertRaisesRegex(
                    tune.ProtocolError, "injected capture failure"
                ):
                    tune.execute_sweep(client, sweep_args(root), root)

            saved = json.loads((root / "session.json").read_text(encoding="utf-8"))
            self.assertEqual(saved["status"], "aborted")
            self.assertTrue(saved["restore"]["stop_attempted"])
            self.assertTrue(saved["restore"]["gains_restored"])
            self.assertEqual(client.active_kp, 4 * 65536)
            self.assertTrue((root / "report.html").exists())

    def test_replot_is_offline_and_recreates_report(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            session = {
                "schema": 1,
                "status": "complete",
                "generated_at": "2026-08-22T00:00:00-07:00",
                "identity": {"firmware": "0.31.0"},
                "request": {"current_milliamperes": 303.0},
                "restore": {"gains_restored": True},
                "persistence": {"available": True},
                "commands": {},
                "trials": [],
            }
            (root / "session.json").write_text(
                json.dumps(session), encoding="utf-8"
            )

            result = tune._replot(root, False)

            self.assertEqual(result, 0)
            self.assertTrue((root / "summary.csv").exists())
            self.assertTrue((root / "report.html").exists())

    def test_report_embeds_saved_high_resolution_waveforms(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            trial = root / "trial-001"
            trial.mkdir()
            samples = []
            for index in range(4):
                samples.append(
                    {
                        "time_seconds": index / 20_000.0,
                        "reference_counts": {"a": index, "b": -index},
                        "measured_counts": {"a": index + 1, "b": 1 - index},
                        "phase_voltage_permille": {
                            "a": 10 * index,
                            "b": -10 * index,
                        },
                    }
                )
            tune._write_jsonl(trial / "trace.jsonl", samples)
            report = root / "report.html"
            tune.write_report(
                report,
                {
                    "schema": 1,
                    "status": "complete",
                    "generated_at": "2026-08-22T00:00:00-07:00",
                    "identity": {"firmware": "0.31.0"},
                    "request": {"current_milliamperes": 303.0},
                    "restore": {"gains_restored": True},
                    "persistence": {"available": True},
                    "commands": {},
                    "trials": [
                        {
                            "trial": 1,
                            "kp": 4.0,
                            "ki": 0.015625,
                            "electrical_frequency_hz": 20.0,
                            "artifact_directory": trial.name,
                        }
                    ],
                },
            )

            document = report.read_text(encoding="utf-8")
            self.assertIn("Settled 20 kHz trial waveforms", document)
            self.assertIn("Current reference and measurement", document)
            self.assertIn("Phase-voltage command", document)

    def test_validation_uses_live_current_and_gain_limits(self) -> None:
        args = sweep_args(Path("unused"))
        status = ready_status()
        status["test"]["maximum_amplitude_counts"] = 10
        with self.assertRaisesRegex(tune.ProtocolError, "live firmware"):
            tune._validate_sweep(args, configuration(), status)
        args.current_ma = 30.0
        args.kp = [9.0]
        with self.assertRaisesRegex(tune.ProtocolError, "live maximum"):
            tune._validate_sweep(args, configuration(), ready_status())

    def test_persist_requires_confirmation_and_valid_alignment(self) -> None:
        client = mock.Mock()
        args = SimpleNamespace(confirm_save_active_configuration=False)
        with self.assertRaisesRegex(tune.ProtocolError, "requires"):
            tune._persist(client, args)
        client.transact.assert_not_called()

        blocked = configuration()
        blocked["flags"].remove("active_calibration_valid")
        args.confirm_save_active_configuration = True
        with (
            mock.patch.object(
                tune, "_safe_inactive_preflight", return_value=(ready_status(), blocked)
            ),
            self.assertRaisesRegex(tune.ProtocolError, "active_calibration_valid"),
        ):
            tune._persist(client, args)
        client.transact.assert_not_called()

    def test_confirmed_persist_saves_and_verifies_full_configuration(self) -> None:
        client = mock.Mock()
        current = configuration()
        args = SimpleNamespace(confirm_save_active_configuration=True)
        with (
            mock.patch.object(
                tune,
                "_safe_inactive_preflight",
                return_value=(ready_status(), current),
            ),
            mock.patch.object(tune, "query_configuration", return_value=current),
            mock.patch.object(tune, "print_current_loop_gain_summary"),
        ):
            result = tune._persist(client, args)

        self.assertEqual(result, 0)
        client.transact.assert_called_once_with(
            tune.COMMAND_SAVE_CONFIGURATION
        )


if __name__ == "__main__":
    unittest.main()
