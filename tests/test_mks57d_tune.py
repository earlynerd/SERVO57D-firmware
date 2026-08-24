import csv
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
            "maximum_current_loop_gains": gains(8 * 65536, 4 * 65536)
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
    def stats(mean: float, ripple: float = 1.0) -> dict:
        return {
            "mean": mean,
            "rms": math.hypot(mean, ripple),
            "ripple_rms": ripple,
            "ripple_peak_to_peak": 2.0 * ripple,
            "minimum": mean - ripple,
            "maximum": mean + ripple,
        }

    return {
        "fundamental_gain": 0.91,
        "phase_lag_degrees": 9.0,
        "combined_rms_error_milliamperes": 42.0,
        "maximum_absolute_voltage_permille": 430.0,
        "maximum_absolute_phase_voltage_volts": 10.3,
        "voltage_saturation_fraction": 0.0,
        "rotating_frame": {
            "sample_count": 256,
            "current_milliamperes": {
                "current_measured_d": stats(250.0),
                "current_measured_q": stats(-20.0),
                "current_error_d": stats(-53.0),
                "current_error_q": stats(-20.0),
            },
            "voltage_permille": {
                "d": stats(300.0, 4.0),
                "q": stats(40.0, 3.0),
            },
        },
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
        ramp_electrical_hz_per_second=50.0,
        interval=0.05,
        kp=[3.0],
        ki=[0.015625],
        electrical_hz=[20.0],
        controller="stationary",
        leg="A1",
        output_root=root,
    )


class FakeClient:
    def __init__(self) -> None:
        self.active_kp = 4 * 65536
        self.active_ki = 1024
        self.commands: list[tuple[int, bytes]] = []
        self.configured_modes: list[str] = []

    def transact(self, command: int, payload: bytes = b"") -> bytes:
        self.commands.append((command, payload))
        if command == tune.COMMAND_SAVE_CONFIGURATION:
            raise AssertionError("sweep must never save configuration")
        if command == tune.COMMAND_SET_CURRENT_LOOP_GAINS:
            self.active_kp, self.active_ki = struct.unpack(">ii", payload)
        return b""


class TuningWorkflowTests(unittest.TestCase):
    def test_capture_encodes_ramp_and_hold_as_separate_durations(self) -> None:
        status = ready_status()
        status["loop"].update(
            {
                "measured_nominal_milliamperes": {"a": 0.0, "b": 0.0},
                "phase_voltage_command_volts": {"a": 0.0, "b": 0.0},
                "phase_voltage_limit_volts": 16.8,
            }
        )
        encoder = healthy_encoder()
        encoder["angle_degrees"] = 0.0
        client = FakeClient()
        with tempfile.TemporaryDirectory() as temporary, (
            mock.patch.object(tune.motor_test, "query_status", return_value=status)
        ), mock.patch.object(
            tune.motor_test, "query_encoder", return_value=encoder
        ):
            samples, completed = tune.motor_test._run_capture(
                client,
                "A1",
                500,
                0.05,
                Path(temporary) / "telemetry.jsonl",
                ramp_duration_ms=400,
            )

        self.assertTrue(completed)
        self.assertEqual(len(samples), 1)
        self.assertEqual(
            client.commands[0],
            (
                tune.motor_test.COMMAND_START_CURRENT_TEST,
                struct.pack(">BII", tune.motor_test.LEG_VALUES["A1"], 400, 500),
            ),
        )

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
            voltage = (reference / abs(reference)) * complex(300.0, 50.0)
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
                    "phase_voltage_permille": {
                        "a": voltage.real,
                        "b": voltage.imag,
                    },
                    "phase_voltage_command_volts": {
                        "a": voltage.real * 0.024,
                        "b": voltage.imag * 0.024,
                    },
                }
            )

        result = tune.analyze_trial(samples, captured, 0.1, 700.0)

        self.assertAlmostEqual(result["fundamental_gain"], 0.8, places=6)
        self.assertAlmostEqual(result["phase_lag_degrees"], 30.0, places=6)
        self.assertEqual(result["source"], "20 kHz current trace")
        self.assertEqual(result["voltage_saturation_fraction"], 0.0)
        rotating = result["rotating_frame"]
        self.assertAlmostEqual(
            rotating["current_counts"]["current_measured_d"]["mean"],
            160.0 * math.cos(math.radians(30.0)),
            places=6,
        )
        self.assertAlmostEqual(
            rotating["current_counts"]["current_measured_q"]["mean"],
            -80.0,
            places=6,
        )
        self.assertAlmostEqual(
            rotating["voltage_permille"]["d"]["mean"], 300.0, places=6
        )
        self.assertAlmostEqual(
            rotating["voltage_permille"]["q"]["mean"], 50.0, places=6
        )

    def common_patches(self, client: FakeClient):
        def configure(_client, counts, frequency, controller_mode):
            client.configured_modes.append(controller_mode)
            return {
                "counts": counts,
                "frequency_hz": frequency,
                "controller_mode": controller_mode,
            }

        def capture(
            _client,
            _leg,
            _duration,
            _interval,
            path,
            trace_at_seconds=None,
            ramp_duration_ms=0,
        ):
            self.assertEqual(ramp_duration_ms, 400)
            self.assertEqual(trace_at_seconds, 0.5)
            path.write_text("{}\n{}\n{}\n", encoding="utf-8")
            return ([ready_status(), ready_status(), ready_status()], True)

        return (
            mock.patch.object(
                tune, "query_identity", return_value={"firmware": "0.33.0", "protocol": "1.15"}
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
                side_effect=configure,
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
            self.assertEqual(
                session["trials"][0]["ramp_duration_seconds"], 0.4
            )
            self.assertEqual(session["controller_mode"], "stationary")
            self.assertEqual(
                session["trials"][0]["controller_mode"], "stationary"
            )
            self.assertEqual(
                client.configured_modes, ["stationary", "stationary"]
            )
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
            self.assertIn("stationary: Kp=3, Ki=0.015625", report)
            summary_csv = (session_directory / "summary.csv").read_text(
                encoding="utf-8"
            )
            self.assertIn("controller_mode", summary_csv.splitlines()[0])
            self.assertIn("stationary", summary_csv)
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

    def test_rotating_sweep_restores_the_initial_stationary_mode(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            client = FakeClient()
            args = sweep_args(root)
            args.controller = "rotating"
            patches = self.common_patches(client)
            with patches[0], patches[1], patches[2], patches[3], patches[4], patches[5], patches[6], patches[7]:
                session = tune.execute_sweep(client, args, root)

            self.assertEqual(session["request"]["controller_mode"], "rotating")
            self.assertEqual(
                client.configured_modes, ["rotating", "stationary"]
            )

    def test_controller_mode_status_compatibility(self) -> None:
        legacy = ready_status()
        legacy["schema"] = 4
        legacy["test"]["controller_mode"] = "rotating"
        current = ready_status()
        current["schema"] = 5
        current["test"]["controller_mode"] = "rotating"

        self.assertEqual(
            tune._diagnostic_controller_mode(legacy), "stationary"
        )
        self.assertEqual(
            tune._diagnostic_controller_mode(current), "rotating"
        )

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
            self.assertIn(
                "legacy step",
                (root / "report.html").read_text(encoding="utf-8"),
            )

    def test_replot_upgrades_legacy_trace_with_rotating_metrics(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            trial_path = root / "trial-001"
            trial_path.mkdir()
            captured = []
            for index in range(8):
                phase = 2.0 * math.pi * index / 8.0
                reference = complex(
                    100.0 * math.cos(phase), 100.0 * math.sin(phase)
                )
                captured.append(
                    {
                        "time_seconds": index / 20_000.0,
                        "reference_counts": {
                            "a": reference.real,
                            "b": reference.imag,
                        },
                        "measured_counts": {
                            "a": 0.9 * reference.real,
                            "b": 0.9 * reference.imag,
                        },
                        "phase_voltage_permille": {
                            "a": 2.0 * reference.real,
                            "b": 2.0 * reference.imag,
                        },
                    }
                )
            tune._write_jsonl(trial_path / "trace.jsonl", captured)
            session = {
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
                        "status": "pass",
                        "kp": 4.0,
                        "ki": 0.015625,
                        "electrical_frequency_hz": 20.0,
                        "artifact_directory": trial_path.name,
                        "faults": [],
                    }
                ],
            }
            tune._write_json(root / "session.json", session)

            tune._replot(root, False)

            with (root / "summary.csv").open(
                encoding="utf-8", newline=""
            ) as stream:
                row = next(csv.DictReader(stream))
            self.assertEqual(row["controller_mode"], "stationary")
            self.assertAlmostEqual(
                float(row["rotating_current_d_mean_milliamperes"]),
                90.0 * tune.COUNTS_TO_MILLIAMPERES,
            )
            report = (root / "report.html").read_text(encoding="utf-8")
            self.assertIn("d/q current in the commanded frame", report)

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
            self.assertIn("d/q current in the commanded frame", document)
            self.assertIn("d/q commanded voltage", document)
            self.assertIn("Blue is d-axis, orange is q-axis", document)
            self.assertIn("d current ripple RMS", document)

    def test_parser_defaults_stationary_and_accepts_rotating_controller(self) -> None:
        parser = tune.make_parser()
        base = [
            "sweep",
            "--kp",
            "4",
            "--ki",
            "0.015625",
            "--electrical-hz",
            "20",
            "--current-ma",
            "303",
        ]

        self.assertEqual(parser.parse_args(base).controller, "stationary")
        self.assertEqual(
            parser.parse_args(base + ["--controller", "rotating"]).controller,
            "rotating",
        )

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
        args.kp = [5.0]
        args.ki = [4.01]
        with self.assertRaisesRegex(tune.ProtocolError, "live maximum"):
            tune._validate_sweep(args, configuration(), ready_status())

    def test_ramp_duration_and_protocol_gate_are_explicit(self) -> None:
        self.assertEqual(tune._ramp_duration_ms(200.0, 50.0), 4000)
        self.assertEqual(tune._ramp_duration_ms(200.0, 0.0), 0)
        args = sweep_args(Path("unused"))
        args.ramp_electrical_hz_per_second = -1.0
        with self.assertRaisesRegex(tune.ProtocolError, "nonnegative"):
            tune._validate_sweep(args, configuration(), ready_status())

    def test_validation_accepts_implementation_frequency_ceiling(self) -> None:
        args = sweep_args(Path("unused"))
        args.electrical_hz = [1000.0]
        _, _, trials = tune._validate_sweep(
            args, configuration(), ready_status()
        )
        self.assertEqual(trials, [(3.0, 0.015625, 1000.0)])

        args.electrical_hz = [1000.001]
        with self.assertRaisesRegex(tune.ProtocolError, "0.001..1000 Hz"):
            tune._validate_sweep(args, configuration(), ready_status())

    def test_frequency_ceiling_tracks_firmware_identity(self) -> None:
        self.assertEqual(
            tune._diagnostic_maximum_frequency_hz(
                {"firmware": "0.35.0"}
            ),
            250.0,
        )
        self.assertEqual(
            tune._diagnostic_maximum_frequency_hz(
                {"firmware": "0.35.1"}
            ),
            1000.0,
        )

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
