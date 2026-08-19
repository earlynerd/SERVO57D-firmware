#!/usr/bin/env python3
"""Analyze JSON-lines captures from the MKS57D commissioning console."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any, Iterable


LOOP_FREQUENCY_HZ = 20_000.0
COUNTS_TO_MILLIAMPERES = 3.3 / 4095.0 / (6.65 * 0.020) * 1000.0


def _mean(values: Iterable[float]) -> float:
    values = list(values)
    if not values:
        raise ValueError("cannot average an empty sequence")
    return sum(values) / len(values)


def _rms(values: Iterable[float]) -> float:
    values = list(values)
    if not values:
        raise ValueError("cannot calculate RMS of an empty sequence")
    return math.sqrt(sum(value * value for value in values) / len(values))


def _solve_3x3(matrix: list[list[float]], vector: list[float]) -> list[float]:
    augmented = [row[:] + [value] for row, value in zip(matrix, vector)]
    for column in range(3):
        pivot = max(range(column, 3), key=lambda row: abs(augmented[row][column]))
        if abs(augmented[pivot][column]) < 1.0e-12:
            raise ValueError("capture does not span enough phase for a sine fit")
        augmented[column], augmented[pivot] = augmented[pivot], augmented[column]
        divisor = augmented[column][column]
        augmented[column] = [value / divisor for value in augmented[column]]
        for row in range(3):
            if row == column:
                continue
            scale = augmented[row][column]
            augmented[row] = [
                value - scale * pivot_value
                for value, pivot_value in zip(augmented[row], augmented[column])
            ]
    return [augmented[row][3] for row in range(3)]


def _fit_sine(
    times: list[float], values: list[float], frequency_hz: float
) -> dict[str, float]:
    omega = 2.0 * math.pi * frequency_hz
    rows = [
        (math.cos(omega * time), math.sin(omega * time), 1.0)
        for time in times
    ]
    normal = [
        [sum(row[i] * row[j] for row in rows) for j in range(3)]
        for i in range(3)
    ]
    projected = [
        sum(row[i] * value for row, value in zip(rows, values))
        for i in range(3)
    ]
    cosine, sine, offset = _solve_3x3(normal, projected)
    amplitude = math.hypot(cosine, sine)
    phase_degrees = math.degrees(math.atan2(-sine, cosine))
    return {
        "amplitude_counts": amplitude,
        "phase_degrees": phase_degrees,
        "offset_counts": offset,
    }


def _fit_quadrature(
    primary: list[float],
    quadrature: list[float],
    measured: list[float],
    cross_sign_for_lag: float,
) -> dict[str, float]:
    rows = list(zip(primary, quadrature, [1.0] * len(primary)))
    normal = [
        [sum(row[i] * row[j] for row in rows) for j in range(3)]
        for i in range(3)
    ]
    projected = [
        sum(row[i] * value for row, value in zip(rows, measured))
        for i in range(3)
    ]
    direct, cross, offset = _solve_3x3(normal, projected)
    return {
        "gain": math.hypot(direct, cross),
        "phase_lag_degrees": math.degrees(
            math.atan2(cross_sign_for_lag * cross, direct)
        ),
        "offset_counts": offset,
    }


def _estimate_reference_frequency(
    times: list[float], references_a: list[float], references_b: list[float]
) -> float:
    phases = [math.atan2(b, a) for a, b in zip(references_a, references_b)]
    unwrapped = [phases[0]]
    for phase in phases[1:]:
        delta = _wrapped_degrees(math.degrees(phase - unwrapped[-1]))
        unwrapped.append(unwrapped[-1] + math.radians(delta))
    mean_time = _mean(times)
    mean_phase = _mean(unwrapped)
    denominator = sum((time - mean_time) ** 2 for time in times)
    slope = sum(
        (time - mean_time) * (phase - mean_phase)
        for time, phase in zip(times, unwrapped)
    ) / denominator
    return slope / (2.0 * math.pi)


def _wrapped_degrees(value: float) -> float:
    return (value + 180.0) % 360.0 - 180.0


def load_capture(path: Path) -> list[dict[str, Any]]:
    samples = []
    with path.open("r", encoding="utf-8-sig") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                sample = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(f"invalid JSON on line {line_number}: {error}") from error
            if "loop" not in sample or "test" not in sample:
                raise ValueError(f"line {line_number} is not a commissioning sample")
            samples.append(sample)
    if len(samples) < 3:
        raise ValueError("capture needs at least three commissioning samples")
    return samples


def analyze(samples: list[dict[str, Any]], settle_seconds: float) -> dict[str, Any]:
    active = [
        sample
        for sample in samples
        if "backend_active" in sample.get("flags", [])
    ]
    if len(active) < 3:
        raise ValueError("capture needs at least three active-loop samples")

    first_count = active[0]["loop"]["sample_count"]
    for sample in active:
        sample["analysis_time_seconds"] = (
            sample["loop"]["sample_count"] - first_count
        ) / LOOP_FREQUENCY_HZ
    settled = [
        sample
        for sample in active
        if sample["analysis_time_seconds"] >= settle_seconds
    ]
    if len(settled) < 3:
        raise ValueError("settling exclusion leaves fewer than three samples")

    times = [sample["analysis_time_seconds"] for sample in settled]
    frequency_hz = float(settled[0]["test"]["frequency_hz"])
    references_by_axis = {
        axis: [
            float(sample["loop"]["reference_counts"][axis])
            for sample in settled
        ]
        for axis in ("a", "b")
    }
    measured_by_axis = {
        axis: [
            float(sample["loop"]["measured_counts"][axis])
            for sample in settled
        ]
        for axis in ("a", "b")
    }
    axis_results: dict[str, Any] = {}
    all_errors = []
    for axis in ("a", "b"):
        references = references_by_axis[axis]
        measured = measured_by_axis[axis]
        errors = [actual - requested for actual, requested in zip(measured, references)]
        all_errors.extend(errors)
        other_axis = "b" if axis == "a" else "a"
        quadrature_fit = _fit_quadrature(
            references,
            references_by_axis[other_axis],
            measured,
            1.0 if axis == "a" else -1.0,
        )
        axis_results[axis] = {
            "rms_error_counts": _rms(errors),
            "rms_error_milliamperes": _rms(errors)
            * COUNTS_TO_MILLIAMPERES,
            "mean_error_counts": _mean(errors),
            "peak_absolute_error_counts": max(abs(value) for value in errors),
            "fundamental_gain": quadrature_fit["gain"],
            "fundamental_phase_lag_degrees": quadrature_fit[
                "phase_lag_degrees"
            ],
        }

    voltages = [
        abs(float(sample["loop"]["phase_voltage_permille"][axis]))
        for sample in settled
        for axis in ("a", "b")
    ]
    voltage_limit = float(settled[0]["loop"]["phase_voltage_limit_permille"])
    sample_deltas = [
        later["loop"]["sample_count"] - earlier["loop"]["sample_count"]
        for earlier, later in zip(active, active[1:])
    ]
    encoder_samples = [sample.get("encoder") for sample in settled]
    encoder_samples = [sample for sample in encoder_samples if sample is not None]
    encoder_motion: dict[str, Any] = {"available": False}
    if len(encoder_samples) >= 2:
        raw_values = [int(sample["angle_raw"]) for sample in encoder_samples]
        unwrapped = [raw_values[0]]
        for raw in raw_values[1:]:
            delta = raw - raw_values[len(unwrapped) - 1]
            if delta > 8192:
                delta -= 16384
            elif delta < -8192:
                delta += 16384
            unwrapped.append(unwrapped[-1] + delta)
        elapsed = times[-1] - times[0]
        revolutions = (unwrapped[-1] - unwrapped[0]) / 16384.0
        encoder_motion = {
            "available": True,
            "revolutions": revolutions,
            "rpm": revolutions / elapsed * 60.0 if elapsed > 0.0 else 0.0,
            "error_count_delta": int(encoder_samples[-1]["error_count"])
            - int(encoder_samples[0]["error_count"]),
        }

    return {
        "active_samples": len(active),
        "settled_samples": len(settled),
        "settle_seconds": settle_seconds,
        "active_duration_seconds": active[-1]["analysis_time_seconds"],
        "telemetry_rate_hz": LOOP_FREQUENCY_HZ / _mean(sample_deltas),
        "loop_frequency_hz": LOOP_FREQUENCY_HZ,
        "test_frequency_hz": frequency_hz,
        "observed_reference_frequency_hz": _estimate_reference_frequency(
            times,
            references_by_axis["a"],
            references_by_axis["b"],
        ),
        "axis": axis_results,
        "combined_rms_error_counts": _rms(all_errors),
        "combined_rms_error_milliamperes": _rms(all_errors)
        * COUNTS_TO_MILLIAMPERES,
        "peak_absolute_error_counts": max(abs(value) for value in all_errors),
        "maximum_absolute_voltage_permille": max(voltages),
        "voltage_saturation_fraction": sum(
            value >= voltage_limit for value in voltages
        ) / len(voltages),
        "faults": sorted(
            {
                fault
                for sample in samples
                for fault in sample["loop"].get("faults", [])
            }
        ),
        "encoder": encoder_motion,
    }


def compact_samples(samples: list[dict[str, Any]]) -> list[dict[str, Any]]:
    active = [
        sample
        for sample in samples
        if "backend_active" in sample.get("flags", [])
    ]
    if not active:
        return []
    first_count = active[0]["loop"]["sample_count"]
    return [
        {
            "t": round(
                (sample["loop"]["sample_count"] - first_count)
                / LOOP_FREQUENCY_HZ,
                6,
            ),
            "ra": sample["loop"]["reference_counts"]["a"],
            "ma": sample["loop"]["measured_counts"]["a"],
            "rb": sample["loop"]["reference_counts"]["b"],
            "mb": sample["loop"]["measured_counts"]["b"],
            "va": sample["loop"]["phase_voltage_permille"]["a"],
            "vb": sample["loop"]["phase_voltage_permille"]["b"],
        }
        for sample in active
    ]


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--settle-seconds", type=float, default=0.2)
    parser.add_argument("--compact-json", type=Path)
    return parser


def main() -> int:
    args = make_parser().parse_args()
    if args.settle_seconds < 0.0:
        raise ValueError("--settle-seconds must be nonnegative")
    samples = load_capture(args.capture)
    result = analyze(samples, args.settle_seconds)
    if args.compact_json is not None:
        args.compact_json.parent.mkdir(parents=True, exist_ok=True)
        with args.compact_json.open("w", encoding="utf-8") as stream:
            json.dump(compact_samples(samples), stream, separators=(",", ":"))
            stream.write("\n")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
