#!/usr/bin/env python3
"""Safely sweep, compare, apply, and explicitly persist MKS57D motor tuning."""

from __future__ import annotations

import argparse
import csv
import html
import json
import math
import sys
import time
import webbrowser
from datetime import datetime
from pathlib import Path
from typing import Any, Iterable

try:
    from . import motor_test
    from .mks57d_rs485 import (
        COMMAND_SAVE_CONFIGURATION,
        COMMAND_SET_CURRENT_LOOP_GAINS,
        COMMAND_STOP_DRIVE,
        COUNTS_TO_MILLIAMPERES,
        DEFAULT_ADDRESS,
        Client,
        ProtocolError,
        current_loop_gain_from_q16,
        current_loop_gain_to_q16,
        open_serial,
        print_current_loop_gain_summary,
        query_configuration,
        query_encoder,
        query_identity,
        query_status,
        read_current_trace,
        set_current_loop_gains,
        set_current_loop_gains_q16,
    )
except ImportError:  # Direct execution from tools/.
    import motor_test
    from mks57d_rs485 import (
        COMMAND_SAVE_CONFIGURATION,
        COMMAND_SET_CURRENT_LOOP_GAINS,
        COMMAND_STOP_DRIVE,
        COUNTS_TO_MILLIAMPERES,
        DEFAULT_ADDRESS,
        Client,
        ProtocolError,
        current_loop_gain_from_q16,
        current_loop_gain_to_q16,
        open_serial,
        print_current_loop_gain_summary,
        query_configuration,
        query_encoder,
        query_identity,
        query_status,
        read_current_trace,
        set_current_loop_gains,
        set_current_loop_gains_q16,
    )


DIAGNOSTIC_MINIMUM_FREQUENCY_HZ = 0.001
DIAGNOSTIC_MAXIMUM_FREQUENCY_HZ = 250.0
DIAGNOSTIC_MINIMUM_DURATION_SECONDS = 0.003
DIAGNOSTIC_MAXIMUM_DURATION_SECONDS = 2_147_483.647
DIAGNOSTIC_REFERENCE_UPDATE_HZ = 1_000
MAXIMUM_LIST_LENGTH = 16
MAXIMUM_TRIAL_COUNT = 64
SUMMARY_FIELDS = (
    "trial",
    "status",
    "error",
    "kp",
    "ki",
    "kp_q16",
    "ki_q16",
    "electrical_frequency_hz",
    "current_milliamperes",
    "duration_seconds",
    "fundamental_gain",
    "phase_lag_degrees",
    "rms_current_error_milliamperes",
    "maximum_voltage_permille",
    "maximum_phase_voltage_volts",
    "voltage_clamp_fraction",
    "encoder_revolutions",
    "encoder_rpm",
    "encoder_error_count_delta",
    "minimum_pwm_preload_margin_us",
    "maximum_trigger_to_dma_us",
    "maximum_dma_to_pwm_stage_us",
    "faults",
    "artifact_directory",
)


def _write_json(path: Path, value: Any) -> None:
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def _write_jsonl(path: Path, values: Iterable[dict[str, Any]]) -> None:
    with path.open("w", encoding="utf-8") as stream:
        for value in values:
            stream.write(json.dumps(value, sort_keys=True) + "\n")


def _read_jsonl(path: Path) -> list[dict[str, Any]]:
    values: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8-sig") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                value = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(
                    f"invalid JSON on {path}:{line_number}: {error}"
                ) from error
            if not isinstance(value, dict):
                raise ValueError(
                    f"expected an object on {path}:{line_number}"
                )
            values.append(value)
    return values


def _parse_float_list(text: str, name: str) -> list[float]:
    pieces = [piece.strip() for piece in text.split(",")]
    if not pieces or any(not piece for piece in pieces):
        raise argparse.ArgumentTypeError(f"{name} must be a comma-separated list")
    if len(pieces) > MAXIMUM_LIST_LENGTH:
        raise argparse.ArgumentTypeError(
            f"{name} accepts at most {MAXIMUM_LIST_LENGTH} values"
        )
    try:
        values = [float(piece) for piece in pieces]
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"{name} contains a non-number") from error
    if any(not math.isfinite(value) for value in values):
        raise argparse.ArgumentTypeError(f"{name} values must be finite")
    return values


def _unique(values: Iterable[float]) -> list[float]:
    result: list[float] = []
    for value in values:
        if value not in result:
            result.append(value)
    return result


def _gain_q16(configuration: dict[str, Any], which: str) -> tuple[int, int]:
    gains = configuration[which]["current_loop_gains"]
    return (
        int(gains["proportional_q16_per_count"]),
        int(gains["integral_q16_per_count_per_step"]),
    )


def _persistence_status(configuration: dict[str, Any]) -> dict[str, Any]:
    flags = set(configuration.get("flags", []))
    missing = sorted(
        {"write_supported", "active_calibration_valid"} - flags
    )
    return {
        "available": not missing,
        "missing_requirements": missing,
        "note": (
            "SAVE_CONFIGURATION persists the full active motor configuration, "
            "including alignment and current-loop gains."
        ),
    }


def _tuning_preflight(status: dict[str, Any], counts: int) -> None:
    motor_test._preflight(status, counts)
    reset = status.get("reset", {})
    if reset.get("watchdog_reset"):
        raise ProtocolError("preflight reports a watchdog reset")
    if reset.get("retained_panic"):
        raise ProtocolError("preflight reports a retained panic code")


def _validate_sweep(
    args: argparse.Namespace,
    configuration: dict[str, Any],
    status: dict[str, Any],
) -> tuple[int, int, list[tuple[float, float, float]]]:
    if not configuration.get("tuning_supported"):
        raise ProtocolError(
            "configuration schema 2 is required for volatile tuning"
        )
    if args.current_ma <= 0.0 or not math.isfinite(args.current_ma):
        raise ProtocolError("--current-ma must be a finite positive value")
    counts = round(args.current_ma / COUNTS_TO_MILLIAMPERES)
    if counts < 1:
        raise ProtocolError("requested current rounds below one ADC count")
    maximum_counts = int(status["test"]["maximum_amplitude_counts"])
    if counts > maximum_counts:
        raise ProtocolError(
            f"requested current is {counts} counts, but the live firmware "
            f"reports a {maximum_counts}-count diagnostic maximum"
        )
    if not (
        DIAGNOSTIC_MINIMUM_DURATION_SECONDS
        <= args.seconds
        <= DIAGNOSTIC_MAXIMUM_DURATION_SECONDS
    ):
        raise ProtocolError(
            "--seconds must be in the diagnostic range "
            f"{DIAGNOSTIC_MINIMUM_DURATION_SECONDS:g}.."
            f"{DIAGNOSTIC_MAXIMUM_DURATION_SECONDS:g}"
        )
    if args.settle_seconds < 0.0 or args.settle_seconds >= args.seconds:
        raise ProtocolError(
            "--settle-seconds must be nonnegative and shorter than --seconds"
        )
    if not 0.01 <= args.interval <= 2.0:
        raise ProtocolError("--interval must be in the range 0.01..2.0 seconds")
    if args.seconds - args.settle_seconds < max(0.05, args.interval):
        raise ProtocolError(
            "leave at least 50 ms (and one telemetry interval) after settling "
            "for the high-resolution trace"
        )
    frequencies = _unique(args.electrical_hz)
    if any(
        frequency < DIAGNOSTIC_MINIMUM_FREQUENCY_HZ
        or frequency > DIAGNOSTIC_MAXIMUM_FREQUENCY_HZ
        for frequency in frequencies
    ):
        raise ProtocolError(
            "electrical frequencies must be in the diagnostic range "
            f"{DIAGNOSTIC_MINIMUM_FREQUENCY_HZ:g}.."
            f"{DIAGNOSTIC_MAXIMUM_FREQUENCY_HZ:g} Hz"
        )
    kps = _unique(args.kp)
    kis = _unique(args.ki)
    maxima = configuration["limits"]["maximum_current_loop_gains"]
    maximum_kp = int(maxima["proportional_q16_per_count"])
    maximum_ki = int(maxima["integral_q16_per_count_per_step"])
    for value in kps:
        q16 = current_loop_gain_to_q16(value, "Kp")
        if q16 > maximum_kp:
            raise ProtocolError(
                f"Kp {value:g} exceeds the live maximum "
                f"{current_loop_gain_from_q16(maximum_kp):g}"
            )
    for value in kis:
        q16 = current_loop_gain_to_q16(value, "Ki")
        if q16 > maximum_ki:
            raise ProtocolError(
                f"Ki {value:g} exceeds the live maximum "
                f"{current_loop_gain_from_q16(maximum_ki):g}"
            )
    trials = [
        (kp, ki, frequency)
        for kp in kps
        for ki in kis
        for frequency in frequencies
    ]
    if not trials or len(trials) > MAXIMUM_TRIAL_COUNT:
        raise ProtocolError(
            f"sweep expands to {len(trials)} trials; maximum is "
            f"{MAXIMUM_TRIAL_COUNT}"
        )
    return counts, round(args.seconds * 1000.0), trials


def _make_session_directory(root: Path) -> Path:
    label = datetime.now().strftime("%Y%m%d-%H%M%S-current-loop")
    path = root / label
    suffix = 2
    while path.exists():
        path = root / f"{label}-{suffix}"
        suffix += 1
    path.mkdir(parents=True)
    return path


def _observed_faults(samples: list[dict[str, Any]]) -> list[str]:
    faults = {
        fault
        for sample in samples
        for fault in sample.get("loop", {}).get("faults", [])
    }
    if any("fault_present" in sample.get("flags", []) for sample in samples):
        faults.add("drive_supervisor_fault")
    for sample in samples:
        estimator = sample.get("encoder", {}).get("estimator", {})
        faults.update(
            f"estimator_{fault}" for fault in estimator.get("faults", [])
        )
    return sorted(faults)


def _analyze_encoder_motion(
    samples: list[dict[str, Any]], settle_seconds: float
) -> dict[str, Any]:
    active = [
        sample
        for sample in samples
        if "backend_active" in sample.get("flags", [])
        and "encoder" in sample
    ]
    if len(active) < 2:
        return {"available": False}
    first_loop_count = int(active[0]["loop"]["sample_count"])
    settled = [
        sample
        for sample in active
        if (
            int(sample["loop"]["sample_count"]) - first_loop_count
        ) / motor_test.LOOP_FREQUENCY_HZ >= settle_seconds
    ]
    if len(settled) < 2:
        return {"available": False}
    revolutions = motor_test._unwrapped_encoder_revolutions(settled)
    elapsed_seconds = (
        int(settled[-1]["loop"]["sample_count"])
        - int(settled[0]["loop"]["sample_count"])
    ) / motor_test.LOOP_FREQUENCY_HZ
    errors = [int(sample["encoder"]["error_count"]) for sample in settled]
    return {
        "available": True,
        "revolutions": revolutions[-1],
        "rpm": (
            revolutions[-1] / elapsed_seconds * 60.0
            if elapsed_seconds > 0.0
            else 0.0
        ),
        "error_count_delta": errors[-1] - errors[0],
    }


def analyze_trial(
    samples: list[dict[str, Any]],
    trace: list[dict[str, Any]],
    settle_seconds: float,
    voltage_limit_permille: float,
) -> dict[str, Any]:
    if len(trace) < 3:
        raise ProtocolError(
            "high-resolution current trace has fewer than three samples"
        )
    reference = [
        complex(
            float(sample["reference_counts"]["a"]),
            float(sample["reference_counts"]["b"]),
        )
        for sample in trace
    ]
    measured = [
        complex(
            float(sample["measured_counts"]["a"]),
            float(sample["measured_counts"]["b"]),
        )
        for sample in trace
    ]
    denominator = sum(abs(value) ** 2 for value in reference)
    if denominator <= 0.0:
        raise ProtocolError("high-resolution trace has no current reference")
    transfer = sum(
        actual * requested.conjugate()
        for requested, actual in zip(reference, measured)
    ) / denominator
    errors = [
        actual_axis - requested_axis
        for sample in trace
        for requested_axis, actual_axis in (
            (
                float(sample["reference_counts"]["a"]),
                float(sample["measured_counts"]["a"]),
            ),
            (
                float(sample["reference_counts"]["b"]),
                float(sample["measured_counts"]["b"]),
            ),
        )
    ]
    voltages = [
        abs(float(sample["phase_voltage_permille"][axis]))
        for sample in trace
        for axis in ("a", "b")
    ]
    physical_voltages = [
        abs(float(sample["phase_voltage_command_volts"][axis]))
        for sample in trace
        for axis in ("a", "b")
        if sample.get("phase_voltage_command_volts", {}).get(axis) is not None
    ]
    return {
        "source": "20 kHz current trace",
        "trace_sample_count": len(trace),
        "fundamental_gain": abs(transfer),
        "phase_lag_degrees": -math.degrees(math.atan2(transfer.imag, transfer.real)),
        "combined_rms_error_counts": math.sqrt(
            sum(error * error for error in errors) / len(errors)
        ),
        "combined_rms_error_milliamperes": math.sqrt(
            sum(error * error for error in errors) / len(errors)
        ) * COUNTS_TO_MILLIAMPERES,
        "maximum_absolute_voltage_permille": max(voltages),
        "maximum_absolute_phase_voltage_volts": (
            max(physical_voltages) if physical_voltages else None
        ),
        "voltage_saturation_fraction": sum(
            value >= voltage_limit_permille for value in voltages
        ) / len(voltages),
        "encoder": _analyze_encoder_motion(samples, settle_seconds),
    }


def _trace_timing(trace: list[dict[str, Any]]) -> dict[str, Any]:
    def values(name: str) -> list[float]:
        return [
            float(sample["timing"][name])
            for sample in trace
            if sample.get("timing", {}).get(name) is not None
        ]

    margins = values("pwm_preload_margin_us")
    trigger_dma = values("trigger_to_dma_us")
    dma_stage = values("dma_to_pwm_stage_us")
    return {
        "trace_sample_count": len(trace),
        "minimum_pwm_preload_margin_us": min(margins) if margins else None,
        "maximum_trigger_to_dma_us": max(trigger_dma) if trigger_dma else None,
        "maximum_dma_to_pwm_stage_us": max(dma_stage) if dma_stage else None,
    }


def _trial_summary(
    index: int,
    kp: float,
    ki: float,
    frequency: float,
    current_ma: float,
    duration_s: float,
    analysis: dict[str, Any],
    trace: list[dict[str, Any]],
    faults: list[str],
    artifact_directory: str,
) -> dict[str, Any]:
    encoder = analysis.get("encoder", {})
    timing = _trace_timing(trace)
    return {
        "trial": index,
        "status": "pass" if not faults else "fault",
        "kp": kp,
        "ki": ki,
        "kp_q16": current_loop_gain_to_q16(kp, "Kp"),
        "ki_q16": current_loop_gain_to_q16(ki, "Ki"),
        "electrical_frequency_hz": frequency,
        "current_milliamperes": current_ma,
        "duration_seconds": duration_s,
        "fundamental_gain": analysis["fundamental_gain"],
        "phase_lag_degrees": analysis["phase_lag_degrees"],
        "rms_current_error_milliamperes": analysis[
            "combined_rms_error_milliamperes"
        ],
        "maximum_voltage_permille": analysis[
            "maximum_absolute_voltage_permille"
        ],
        "maximum_phase_voltage_volts": analysis.get(
            "maximum_absolute_phase_voltage_volts"
        ),
        "voltage_clamp_fraction": analysis["voltage_saturation_fraction"],
        "encoder_revolutions": encoder.get("revolutions"),
        "encoder_rpm": encoder.get("rpm"),
        "encoder_error_count_delta": encoder.get("error_count_delta"),
        **timing,
        "faults": faults,
        "artifact_directory": artifact_directory,
    }


def _write_summary_csv(path: Path, trials: list[dict[str, Any]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=SUMMARY_FIELDS)
        writer.writeheader()
        for trial in trials:
            row = {key: trial.get(key) for key in SUMMARY_FIELDS}
            row["faults"] = ";".join(trial.get("faults", []))
            writer.writerow(row)


def _comparison_plot(
    title: str,
    y_label: str,
    trials: list[dict[str, Any]],
    value_key: str,
) -> str:
    usable = [trial for trial in trials if trial.get(value_key) is not None]
    if not usable:
        return f"<section><h2>{html.escape(title)}</h2><p>No data.</p></section>"
    groups: dict[tuple[float, float], list[dict[str, Any]]] = {}
    for trial in usable:
        groups.setdefault((trial["kp"], trial["ki"]), []).append(trial)
    width, height = 1060.0, 340.0
    left, right, top, bottom = 74.0, 24.0, 18.0, 54.0
    plot_w, plot_h = width - left - right, height - top - bottom
    xs = [float(trial["electrical_frequency_hz"]) for trial in usable]
    ys = [float(trial[value_key]) for trial in usable]
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    if math.isclose(x0, x1):
        x0, x1 = x0 - 0.5, x1 + 0.5
    if math.isclose(y0, y1):
        pad = max(abs(y0) * 0.1, 1.0e-6)
    else:
        pad = (y1 - y0) * 0.08
    y0, y1 = y0 - pad, y1 + pad

    def sx(value: float) -> float:
        return left + (value - x0) * plot_w / (x1 - x0)

    def sy(value: float) -> float:
        return top + (y1 - value) * plot_h / (y1 - y0)

    grid: list[str] = []
    for index in range(6):
        fraction = index / 5.0
        xv = x0 + fraction * (x1 - x0)
        yv = y0 + fraction * (y1 - y0)
        x = sx(xv)
        y = sy(yv)
        grid.append(f'<line class="grid" x1="{x:.2f}" y1="{top}" x2="{x:.2f}" y2="{top + plot_h}"/>')
        grid.append(f'<text class="tick" x="{x:.2f}" y="{height - 28}" text-anchor="middle">{xv:g}</text>')
        grid.append(f'<line class="grid" x1="{left}" y1="{y:.2f}" x2="{left + plot_w}" y2="{y:.2f}"/>')
        grid.append(f'<text class="tick" x="{left - 8}" y="{y + 4:.2f}" text-anchor="end">{yv:.3g}</text>')
    paths: list[str] = []
    legends: list[str] = []
    for color, (gains, group) in enumerate(sorted(groups.items()), 1):
        ordered = sorted(group, key=lambda row: row["electrical_frequency_hz"])
        points = " ".join(
            f"{sx(float(row['electrical_frequency_hz'])):.2f},{sy(float(row[value_key])):.2f}"
            for row in ordered
        )
        color_class = f"s{((color - 1) % 6) + 1}"
        paths.append(f'<polyline class="series {color_class}" points="{points}"/>')
        paths.extend(
            f'<circle class="point {color_class}" cx="{sx(float(row["electrical_frequency_hz"])):.2f}" cy="{sy(float(row[value_key])):.2f}" r="3.5"/>'
            for row in ordered
        )
        legends.append(
            f'<span><i class="legend-line {color_class}"></i>Kp={gains[0]:g}, Ki={gains[1]:g}</span>'
        )
    return f"""
    <section><h2>{html.escape(title)}</h2>
      <div class="legend">{''.join(legends)}</div>
      <svg viewBox="0 0 {width:.0f} {height:.0f}" role="img" aria-label="{html.escape(title)}">
        <rect class="frame" x="{left}" y="{top}" width="{plot_w}" height="{plot_h}"/>
        {''.join(grid)}{''.join(paths)}
        <text class="axis" x="{left + plot_w / 2:.2f}" y="{height - 5}" text-anchor="middle">Electrical frequency (Hz)</text>
        <text class="axis" transform="translate(17 {top + plot_h / 2:.2f}) rotate(-90)" text-anchor="middle">{html.escape(y_label)}</text>
      </svg>
    </section>"""


def _trial_waveform_plots(
    report_path: Path, trials: list[dict[str, Any]]
) -> str:
    sections: list[str] = []
    for trial in trials:
        artifact_directory = trial.get("artifact_directory")
        if not artifact_directory:
            continue
        trace_path = report_path.parent / str(artifact_directory) / "trace.jsonl"
        try:
            trace = _read_jsonl(trace_path)
        except (OSError, ValueError) as error:
            sections.append(
                "<section><h2>Trial "
                f"{trial.get('trial', '?')} high-resolution waveform</h2>"
                f"<p>{html.escape(str(error))}</p></section>"
            )
            continue
        usable = [
            sample
            for sample in trace
            if "time_seconds" in sample
            and "reference_counts" in sample
            and "measured_counts" in sample
            and "phase_voltage_permille" in sample
        ]
        if not usable:
            sections.append(
                "<section><h2>Trial "
                f"{trial.get('trial', '?')} high-resolution waveform</h2>"
                "<p>No plottable trace samples.</p></section>"
            )
            continue
        time_ms = [float(sample["time_seconds"]) * 1000.0 for sample in usable]
        current_plot = motor_test._polyline_plot(
            "Current reference and measurement",
            "Burst time (ms)",
            "Current (mA nominal)",
            time_ms,
            [
                (
                    "A reference",
                    [
                        float(sample["reference_counts"]["a"])
                        * COUNTS_TO_MILLIAMPERES
                        for sample in usable
                    ],
                    1,
                    True,
                ),
                (
                    "A measured",
                    [
                        float(sample["measured_counts"]["a"])
                        * COUNTS_TO_MILLIAMPERES
                        for sample in usable
                    ],
                    1,
                    False,
                ),
                (
                    "B reference",
                    [
                        float(sample["reference_counts"]["b"])
                        * COUNTS_TO_MILLIAMPERES
                        for sample in usable
                    ],
                    2,
                    True,
                ),
                (
                    "B measured",
                    [
                        float(sample["measured_counts"]["b"])
                        * COUNTS_TO_MILLIAMPERES
                        for sample in usable
                    ],
                    2,
                    False,
                ),
            ],
        )
        voltage_plot = motor_test._polyline_plot(
            "Phase-voltage command",
            "Burst time (ms)",
            "Command (permille of bus)",
            time_ms,
            [
                (
                    "Phase A",
                    [
                        float(sample["phase_voltage_permille"]["a"])
                        for sample in usable
                    ],
                    3,
                    False,
                ),
                (
                    "Phase B",
                    [
                        float(sample["phase_voltage_permille"]["b"])
                        for sample in usable
                    ],
                    4,
                    False,
                ),
            ],
        )
        sections.append(
            "<div class=\"trial-waveforms\"><h2>Trial "
            f"{trial.get('trial', '?')}: Kp={trial.get('kp', '?')}, "
            f"Ki={trial.get('ki', '?')}, "
            f"{trial.get('electrical_frequency_hz', '?')} Hz</h2>"
            f"{current_plot}{voltage_plot}</div>"
        )
    if not sections:
        return ""
    return (
        "<section><h2>Settled 20 kHz trial waveforms</h2>"
        "<p class=\"note\">Each burst is captured after the requested settling "
        "interval. Dashed lines are current references.</p></section>"
        + "".join(sections)
    )


def write_report(path: Path, session: dict[str, Any]) -> None:
    trials = session.get("trials", [])
    rows = []
    for trial in trials:
        faults = trial.get("faults") or []
        rows.append(
            "<tr>"
            f"<td>{trial['trial']}</td><td>{html.escape(trial.get('status', 'unknown'))}</td><td>{trial['kp']:g}</td><td>{trial['ki']:g}</td>"
            f"<td>{trial['electrical_frequency_hz']:g}</td>"
            f"<td>{trial.get('fundamental_gain', float('nan')):.3f}</td>"
            f"<td>{trial.get('phase_lag_degrees', float('nan')):.1f}</td>"
            f"<td>{trial.get('rms_current_error_milliamperes', float('nan')):.1f}</td>"
            f"<td>{trial.get('maximum_voltage_permille', float('nan')):.0f}</td>"
            f"<td>{100.0 * trial.get('voltage_clamp_fraction', 0.0):.2f}%</td>"
            f"<td>{trial.get('encoder_error_count_delta') if trial.get('encoder_error_count_delta') is not None else 'n/a'}</td>"
            f"<td>{trial.get('minimum_pwm_preload_margin_us') if trial.get('minimum_pwm_preload_margin_us') is not None else 'n/a'}</td>"
            f"<td>{trial.get('maximum_trigger_to_dma_us') if trial.get('maximum_trigger_to_dma_us') is not None else 'n/a'}</td>"
            f"<td>{trial.get('maximum_dma_to_pwm_stage_us') if trial.get('maximum_dma_to_pwm_stage_us') is not None else 'n/a'}</td>"
            f"<td>{html.escape(', '.join(faults) if faults else 'none')}</td>"
            f"<td>{html.escape(str(trial.get('error') or 'none'))}</td>"
            "</tr>"
        )
    persistence = session.get("persistence", {})
    persistence_text = (
        "available after explicit confirmation"
        if persistence.get("available")
        else "unavailable: "
        + ", ".join(persistence.get("missing_requirements", []))
    )
    commands = session.get("commands", {})
    plots = "".join(
        (
            _comparison_plot("Current magnitude tracking", "Measured/reference gain", trials, "fundamental_gain"),
            _comparison_plot("Current phase response", "Phase lag (degrees)", trials, "phase_lag_degrees"),
            _comparison_plot("Current tracking error", "RMS error (mA)", trials, "rms_current_error_milliamperes"),
            _comparison_plot("Voltage effort", "Peak command (permille)", trials, "maximum_voltage_permille"),
            _comparison_plot("Voltage clamp fraction", "Clamped fraction", trials, "voltage_clamp_fraction"),
            _comparison_plot("Encoder motion", "Encoder revolutions", trials, "encoder_revolutions"),
            _comparison_plot("Encoder speed", "Encoder RPM", trials, "encoder_rpm"),
            _comparison_plot("Encoder transport errors", "Error-count delta", trials, "encoder_error_count_delta"),
            _comparison_plot("PWM timing margin", "Minimum margin (us)", trials, "minimum_pwm_preload_margin_us"),
        )
    )
    waveform_plots = _trial_waveform_plots(path, trials)
    document = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MKS57D current-loop tuning report</title><style>
:root{{--bg:#f7f8fa;--fg:#172033;--muted:#667085;--card:#fff;--grid:#d0d5dd;--s1:#1769aa;--s2:#d97706;--s3:#0f8a62;--s4:#7c3aed;--s5:#b91c1c;--s6:#0891b2}}
@media(prefers-color-scheme:dark){{:root{{--bg:#111827;--fg:#f3f4f6;--muted:#9ca3af;--card:#1f2937;--grid:#4b5563;--s1:#63b3ed;--s2:#f6ad55;--s3:#68d391;--s4:#b794f4;--s5:#fc8181;--s6:#67e8f9}}}}
*{{box-sizing:border-box}}body{{margin:0;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}}main{{max-width:1200px;margin:auto;padding:24px}}h1{{margin:0}}h2{{font-size:1.1rem;margin-bottom:8px}}.sub,.note{{color:var(--muted)}}.cards{{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:10px;margin:18px 0}}.card,section{{background:var(--card);border:1px solid var(--grid);border-radius:8px;padding:14px}}section{{margin:18px 0}}.card b{{display:block;font-size:1.08rem}}svg{{display:block;width:100%;height:auto}}.frame{{fill:var(--card);stroke:var(--grid)}}.grid{{stroke:var(--grid);stroke-width:1}}.tick{{fill:var(--muted);font-size:12px}}.axis,.axis-label{{fill:var(--fg);font-size:13px}}{motor_test._plot_series_css(6, "8 5", 18)}.legend{{display:flex;flex-wrap:wrap;gap:8px 18px;color:var(--muted)}}.legend span{{display:flex;align-items:center;gap:6px}}.trial-waveforms{{border-left:4px solid var(--grid);padding-left:14px}}.table-wrap{{overflow:auto}}table{{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}}th,td{{padding:7px;border-bottom:1px solid var(--grid);text-align:right;white-space:nowrap}}th:first-child,td:first-child{{text-align:left}}code{{white-space:pre-wrap;overflow-wrap:anywhere}}@media(max-width:600px){{main{{padding:12px}}}}
</style></head><body><main>
<h1>MKS57D current-loop tuning</h1><p class="sub">{html.escape(session.get('generated_at', ''))} · {html.escape(session.get('status', 'unknown'))}</p>
<div class="cards"><div class="card"><span>Trials</span><b>{len(trials)}</b></div><div class="card"><span>Current</span><b>{session.get('request', {}).get('current_milliamperes', 0):.1f} mA</b></div><div class="card"><span>Firmware</span><b>{html.escape(session.get('identity', {}).get('firmware', 'unknown'))}</b></div><div class="card"><span>Restored</span><b>{'yes' if session.get('restore', {}).get('gains_restored') else 'no'}</b></div></div>
<p class="note">The rotating-current diagnostic updates its reference at {DIAGNOSTIC_REFERENCE_UPDATE_HZ} Hz. Treat results near that rate as quantized diagnostic evidence, not a backend-rate frequency response. Persistence is {html.escape(persistence_text)}. Sweeps never save configuration.</p>
{plots}
{waveform_plots}
<section><h2>Trial and safety summary</h2><div class="table-wrap"><table><thead><tr><th>Trial</th><th>Status</th><th>Kp</th><th>Ki</th><th>Hz</th><th>Gain</th><th>Lag deg</th><th>RMS mA</th><th>Peak permille</th><th>Clamp</th><th>Encoder errors</th><th>Min margin us</th><th>Max trigger-DMA us</th><th>Max DMA-stage us</th><th>Faults</th><th>Error</th></tr></thead><tbody>{''.join(rows)}</tbody></table></div></section>
<section><h2>Apply and persist separately</h2><p>Choose a result deliberately; this report does not nominate or save a winner.</p><p><code>{html.escape(commands.get('apply_template', ''))}</code></p><p><code>{html.escape(commands.get('persist', ''))}</code></p></section>
</main></body></html>"""
    path.write_text(document, encoding="utf-8")


def _trial_directory(session_directory: Path, index: int, kp: float, ki: float, frequency: float) -> Path:
    path = session_directory / (
        f"trial-{index:03d}-kp{kp:g}-ki{ki:g}-{frequency:g}Hz"
    )
    path.mkdir()
    return path


def execute_sweep(
    client: Client,
    args: argparse.Namespace,
    session_directory: Path,
) -> dict[str, Any]:
    identity = query_identity(client)
    initial_status = query_status(client)
    initial_configuration = query_configuration(client)
    initial_encoder = query_encoder(client)
    motor_test._check_encoder_preflight(initial_encoder)
    counts, duration_ms, requested_trials = _validate_sweep(
        args, initial_configuration, initial_status
    )
    _tuning_preflight(initial_status, counts)
    initial_kp_q16, initial_ki_q16 = _gain_q16(
        initial_configuration, "active"
    )
    initial_test_counts = int(initial_status["test"]["amplitude_counts"])
    initial_test_frequency = float(initial_status["test"]["frequency_hz"])
    session: dict[str, Any] = {
        "schema": 1,
        "generated_at": datetime.now().astimezone().isoformat(timespec="seconds"),
        "status": "running",
        "identity": identity,
        "request": {
            "current_milliamperes": counts * COUNTS_TO_MILLIAMPERES,
            "current_counts": counts,
            "duration_seconds": duration_ms / 1000.0,
            "settle_seconds": args.settle_seconds,
            "telemetry_interval_seconds": args.interval,
            "kp": _unique(args.kp),
            "ki": _unique(args.ki),
            "electrical_frequency_hz": _unique(args.electrical_hz),
            "trial_count": len(requested_trials),
        },
        "diagnostic_limits": {
            "maximum_current_counts_live": int(
                initial_status["test"]["maximum_amplitude_counts"]
            ),
            "frequency_hz": {
                "minimum": DIAGNOSTIC_MINIMUM_FREQUENCY_HZ,
                "maximum": DIAGNOSTIC_MAXIMUM_FREQUENCY_HZ,
                "source": "protocol-1.14 diagnostic contract",
            },
            "duration_seconds": {
                "minimum": DIAGNOSTIC_MINIMUM_DURATION_SECONDS,
                "maximum": DIAGNOSTIC_MAXIMUM_DURATION_SECONDS,
                "source": "protocol-1.14 diagnostic contract",
            },
            "reference_update_hz": DIAGNOSTIC_REFERENCE_UPDATE_HZ,
        },
        "initial": {
            "status": initial_status,
            "encoder": initial_encoder,
            "configuration": initial_configuration,
        },
        "persistence": _persistence_status(initial_configuration),
        "trials": [],
        "restore": {
            "stop_attempted": False,
            "stop_error": None,
            "gains_restored": False,
            "gain_restore_error": None,
            "diagnostic_configuration_restored": False,
            "diagnostic_configuration_restore_error": None,
        },
        "commands": {
            "apply_template": (
                f"py tools/mks57d_rs485.py --port {args.port} "
                "set-current-loop-gains --kp <KP> --ki <KI>"
            ),
            "persist": (
                f"py tools/mks57d_tune.py --port {args.port} persist "
                "--confirm-save-active-configuration"
            ),
        },
    }
    session_path = session_directory / "session.json"
    _write_json(session_path, session)
    primary_error: BaseException | None = None
    active_trial: dict[str, Any] | None = None
    try:
        for index, (kp, ki, frequency) in enumerate(requested_trials, 1):
            trial_path = _trial_directory(
                session_directory, index, kp, ki, frequency
            )
            active_trial = {
                "trial": index,
                "status": "running",
                "error": None,
                "kp": kp,
                "ki": ki,
                "kp_q16": current_loop_gain_to_q16(kp, "Kp"),
                "ki_q16": current_loop_gain_to_q16(ki, "Ki"),
                "electrical_frequency_hz": frequency,
                "current_milliamperes": counts * COUNTS_TO_MILLIAMPERES,
                "duration_seconds": duration_ms / 1000.0,
                "faults": [],
                "artifact_directory": trial_path.name,
            }
            print(
                f"[{index}/{len(requested_trials)}] Kp={kp:g}, Ki={ki:g}, "
                f"{frequency:g} Hz"
            )
            status = query_status(client)
            _tuning_preflight(status, counts)
            configuration = query_configuration(client)
            expected_q16 = (
                current_loop_gain_to_q16(kp, "Kp"),
                current_loop_gain_to_q16(ki, "Ki"),
            )
            maxima = configuration["limits"][
                "maximum_current_loop_gains"
            ]
            if expected_q16[0] > int(
                maxima["proportional_q16_per_count"]
            ) or expected_q16[1] > int(
                maxima["integral_q16_per_count_per_step"]
            ):
                raise ProtocolError("gain request exceeds live limits")
            set_current_loop_gains_q16(client, *expected_q16)
            applied_configuration = query_configuration(client)
            applied_q16 = _gain_q16(applied_configuration, "active")
            if applied_q16 != expected_q16:
                raise ProtocolError("volatile gain readback does not match request")
            applied = motor_test._configure(client, counts, frequency)
            if not math.isclose(
                float(applied["frequency_hz"]), frequency, abs_tol=0.0005
            ):
                raise ProtocolError("diagnostic frequency readback does not match")
            telemetry_path = trial_path / "telemetry.jsonl"
            samples, completed = motor_test._run_capture(
                client,
                args.leg,
                duration_ms,
                args.interval,
                telemetry_path,
                trace_at_seconds=args.settle_seconds,
            )
            trace = read_current_trace(client)
            _write_jsonl(trial_path / "trace.jsonl", trace)
            final_status = query_status(client)
            faults = _observed_faults(samples + [final_status])
            if not completed:
                raise ProtocolError("diagnostic did not release authority")
            if int(final_status["test"]["remote_run_remaining_millis"]) != 0:
                raise ProtocolError("diagnostic authority ended before its deadline")
            if final_status.get("reset", {}).get("watchdog_reset"):
                faults.append("watchdog_reset")
            if final_status.get("reset", {}).get("retained_panic"):
                faults.append("retained_panic")
            if faults:
                raise ProtocolError(
                    "trial reported faults: " + ", ".join(sorted(set(faults)))
                )
            analysis = analyze_trial(
                samples,
                trace,
                args.settle_seconds,
                float(final_status["loop"]["phase_voltage_limit_permille"]),
            )
            summary = _trial_summary(
                index,
                kp,
                ki,
                float(applied["frequency_hz"]),
                float(applied["counts"]) * COUNTS_TO_MILLIAMPERES,
                duration_ms / 1000.0,
                analysis,
                trace,
                [],
                trial_path.name,
            )
            _write_json(
                trial_path / "trial.json",
                {
                    "schema": 1,
                    "request": summary,
                    "analysis": analysis,
                    "applied_configuration": applied_configuration,
                    "final_status": final_status,
                    "trace_timing": _trace_timing(trace),
                },
            )
            session["trials"].append(summary)
            active_trial = None
            _write_json(session_path, session)
            print(
                f"  gain={summary['fundamental_gain']:.3f}, "
                f"lag={summary['phase_lag_degrees']:.1f} deg, "
                f"RMS={summary['rms_current_error_milliamperes']:.1f} mA, "
                f"peak={summary['maximum_voltage_permille']:.0f}/1000"
            )
        session["status"] = "complete"
    except (ProtocolError, OSError, ValueError, KeyboardInterrupt) as error:
        primary_error = error
        session["status"] = "aborted"
        session["error"] = str(error)
        if active_trial is not None:
            active_trial["status"] = "aborted"
            active_trial["error"] = str(error)
            session["trials"].append(active_trial)
            _write_json(
                session_directory
                / active_trial["artifact_directory"]
                / "trial.json",
                {"schema": 1, "request": active_trial},
            )
    finally:
        session["restore"]["stop_attempted"] = True
        try:
            client.transact(COMMAND_STOP_DRIVE)
        except (ProtocolError, OSError) as error:
            session["restore"]["stop_error"] = str(error)
        try:
            set_current_loop_gains_q16(
                client, initial_kp_q16, initial_ki_q16
            )
            restored_configuration = query_configuration(client)
            if _gain_q16(restored_configuration, "active") != (
                initial_kp_q16,
                initial_ki_q16,
            ):
                raise ProtocolError("restored gain readback does not match")
            session["restore"]["gains_restored"] = True
            session["restore"]["configuration"] = restored_configuration
        except (ProtocolError, OSError, KeyError) as error:
            session["restore"]["gain_restore_error"] = str(error)
        try:
            motor_test._configure(
                client, initial_test_counts, initial_test_frequency
            )
            session["restore"]["diagnostic_configuration_restored"] = True
        except (ProtocolError, OSError) as error:
            session["restore"][
                "diagnostic_configuration_restore_error"
            ] = str(error)
        _write_summary_csv(
            session_directory / "summary.csv", session["trials"]
        )
        _write_json(session_path, session)
        write_report(session_directory / "report.html", session)
    if primary_error is not None:
        raise primary_error
    if not session["restore"]["gains_restored"]:
        raise ProtocolError(
            "sweep completed but starting gains could not be restored: "
            + str(session["restore"]["gain_restore_error"])
        )
    return session


def _open_report(path: Path) -> None:
    if not webbrowser.open(path.resolve().as_uri()):
        print(f"Could not open a browser automatically; open {path.resolve()}")


def _replot(path: Path, open_browser: bool) -> int:
    session_path = path / "session.json"
    session = json.loads(session_path.read_text(encoding="utf-8"))
    _write_summary_csv(path / "summary.csv", session.get("trials", []))
    report = path / "report.html"
    write_report(report, session)
    print(f"Report: {report.resolve()}")
    if open_browser:
        _open_report(report)
    return 0


def _safe_inactive_preflight(client: Client) -> tuple[dict[str, Any], dict[str, Any]]:
    status = query_status(client)
    _tuning_preflight(status, 1)
    configuration = query_configuration(client)
    if not configuration.get("tuning_supported"):
        raise ProtocolError("configuration schema 2 tuning is required")
    return status, configuration


def _apply(client: Client, args: argparse.Namespace) -> int:
    _, configuration = _safe_inactive_preflight(client)
    updated = set_current_loop_gains(
        client, args.kp, args.ki, configuration
    )
    print_current_loop_gain_summary(updated)
    print("Applied volatile gains only; stored configuration was not changed.")
    return 0


def _persist(client: Client, args: argparse.Namespace) -> int:
    if not args.confirm_save_active_configuration:
        raise ProtocolError(
            "persistence requires --confirm-save-active-configuration"
        )
    _, before = _safe_inactive_preflight(client)
    persistence = _persistence_status(before)
    if not persistence["available"]:
        raise ProtocolError(
            "active configuration cannot be persisted: missing "
            + ", ".join(persistence["missing_requirements"])
        )
    active_gains = _gain_q16(before, "active")
    client.transact(COMMAND_SAVE_CONFIGURATION)
    after = query_configuration(client)
    if _gain_q16(after, "stored") != active_gains:
        raise ProtocolError("stored gain readback does not match active gains")
    if "active_matches_record" not in after.get("flags", []):
        raise ProtocolError("configuration save did not produce a matching record")
    print_current_loop_gain_summary(after)
    print(
        "Persisted the full active motor configuration, including alignment "
        f"(generation {after.get('generation')})."
    )
    return 0


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port, for example COM14")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--address", type=int, default=DEFAULT_ADDRESS)
    parser.add_argument("--timeout", type=float, default=0.75)
    parser.add_argument(
        "--replot",
        type=Path,
        metavar="SESSION_DIRECTORY",
        help="rebuild report.html and summary.csv without hardware",
    )
    parser.add_argument(
        "--no-open", action="store_true", help="do not open the HTML report"
    )
    commands = parser.add_subparsers(dest="command")
    sweep = commands.add_parser(
        "sweep", help="run a volatile fixed-current gain/frequency sweep"
    )
    sweep.add_argument(
        "--kp",
        required=True,
        type=lambda text: _parse_float_list(text, "--kp"),
        help="comma-separated proportional gains",
    )
    sweep.add_argument(
        "--ki",
        required=True,
        type=lambda text: _parse_float_list(text, "--ki"),
        help="comma-separated integral gains per 20 kHz step",
    )
    sweep.add_argument(
        "--electrical-hz",
        required=True,
        type=lambda text: _parse_float_list(text, "--electrical-hz"),
        help="comma-separated rotating-current frequencies",
    )
    sweep.add_argument("--current-ma", type=float, required=True)
    sweep.add_argument("--seconds", type=float, default=1.0)
    sweep.add_argument("--settle-seconds", type=float, default=0.2)
    sweep.add_argument("--interval", type=float, default=0.05)
    sweep.add_argument("--leg", choices=motor_test.LEG_VALUES, default="A1")
    sweep.add_argument(
        "--output-root", type=Path, default=Path("scratch/tuning-runs")
    )
    apply = commands.add_parser(
        "apply", help="apply one volatile gain pair without saving"
    )
    apply.add_argument("--kp", type=float, required=True)
    apply.add_argument("--ki", type=float, required=True)
    persist = commands.add_parser(
        "persist", help="persist the full active configuration explicitly"
    )
    persist.add_argument(
        "--confirm-save-active-configuration", action="store_true"
    )
    return parser


def main() -> int:
    args = make_parser().parse_args()
    if args.replot is not None:
        return _replot(args.replot, not args.no_open)
    if args.command is None:
        raise ProtocolError("choose sweep, apply, persist, or --replot")
    if not 1 <= args.address <= 247:
        raise ProtocolError("address must be in the range 1..247")
    if not args.port:
        raise ProtocolError("--port is required")
    if args.command == "sweep":
        session_directory = _make_session_directory(args.output_root)
        try:
            with open_serial(args) as port:
                session = execute_sweep(
                    Client(port, args.address), args, session_directory
                )
        finally:
            print(f"Capture: {session_directory.resolve()}")
        report = session_directory / "report.html"
        print(
            f"Result: {len(session['trials'])} trials, starting gains restored, "
            "no configuration saved."
        )
        print(f"Report: {report.resolve()}")
        if not args.no_open:
            _open_report(report)
        return 0
    with open_serial(args) as port:
        client = Client(port, args.address)
        if args.command == "apply":
            return _apply(client, args)
        return _persist(client, args)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
    except (ProtocolError, OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
