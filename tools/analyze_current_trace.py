#!/usr/bin/env python3
"""Measure startup-step quality from a 20 kHz current-loop trace."""

from __future__ import annotations

import argparse
import json
import math
import statistics
from pathlib import Path
from typing import Any


COUNTS_TO_MILLIAMPERES = 3.3 / 4095.0 / (6.65 * 0.020) * 1000.0


def load_trace(path: Path) -> list[dict[str, Any]]:
    samples = []
    with path.open("r", encoding="utf-8-sig") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                sample = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(f"invalid JSON on line {line_number}: {error}") from error
            required = {
                "time_seconds",
                "reference_counts",
                "measured_counts",
                "phase_voltage_permille",
            }
            if not required.issubset(sample):
                raise ValueError(f"line {line_number} is not a current trace sample")
            samples.append(sample)
    if len(samples) < 20:
        raise ValueError("trace needs at least 20 samples")
    return samples


def _crossing_time(
    times: list[float], normalized: list[float], threshold: float
) -> float | None:
    for index in range(1, len(times)):
        before = normalized[index - 1]
        after = normalized[index]
        if before < threshold <= after:
            fraction = (threshold - before) / (after - before)
            return times[index - 1] + fraction * (times[index] - times[index - 1])
    return None


def _settling_time(
    times: list[float], errors: list[float], tolerance_counts: float
) -> float | None:
    last_outside = -1
    for index, error in enumerate(errors):
        if abs(error) > tolerance_counts:
            last_outside = index
    if last_outside < 0:
        return times[0]
    if last_outside + 1 >= len(times):
        return None
    return times[last_outside + 1]


def analyze(
    samples: list[dict[str, Any]], tolerance_percent: float
) -> dict[str, Any]:
    first_reference = samples[0]["reference_counts"]
    axis = max(("a", "b"), key=lambda name: abs(first_reference[name]))
    other_axis = "b" if axis == "a" else "a"
    target = float(first_reference[axis])
    if target == 0.0:
        raise ValueError("trace does not begin with a nonzero current step")

    times = [float(sample["time_seconds"]) for sample in samples]
    references = [float(sample["reference_counts"][axis]) for sample in samples]
    if max(abs(reference - target) for reference in references) > 1.0:
        raise ValueError(
            "reference changes during the trace; use the 0.001 Hz test setting"
        )
    measured = [float(sample["measured_counts"][axis]) for sample in samples]
    normalized = [value / target for value in measured]
    errors = [value - target for value in measured]
    tail_length = max(10, len(samples) // 5)
    tail = measured[-tail_length:]
    tail_errors = errors[-tail_length:]
    tolerance_counts = max(2.0, abs(target) * tolerance_percent / 100.0)
    rise_10 = _crossing_time(times, normalized, 0.1)
    rise_90 = _crossing_time(times, normalized, 0.9)
    rise_time = None
    if rise_10 is not None and rise_90 is not None:
        rise_time = rise_90 - rise_10
    overshoot = max(normalized) - 1.0
    settling = _settling_time(times, errors, tolerance_counts)
    voltages = [
        abs(float(sample["phase_voltage_permille"][axis])) for sample in samples
    ]
    physical_voltages = [
        abs(float(sample["phase_voltage_command_volts"][axis]))
        for sample in samples
        if sample.get("phase_voltage_command_volts", {}).get(axis) is not None
    ]
    other_measured = [
        abs(float(sample["measured_counts"][other_axis])) for sample in samples
    ]

    return {
        "axis": axis,
        "sample_count": len(samples),
        "duration_milliseconds": times[-1] * 1000.0,
        "target_counts": target,
        "target_milliamperes": target * COUNTS_TO_MILLIAMPERES,
        "rise_time_10_90_microseconds": (
            rise_time * 1.0e6 if rise_time is not None else None
        ),
        "overshoot_percent": max(0.0, overshoot * 100.0),
        "undershoot_percent": max(0.0, -min(normalized) * 100.0),
        "settling_tolerance_counts": tolerance_counts,
        "settling_time_microseconds": (
            settling * 1.0e6
            if settling is not None
            else None
        ),
        "tail_mean_counts": statistics.fmean(tail),
        "tail_mean_error_counts": statistics.fmean(tail_errors),
        "tail_rms_error_counts": math.sqrt(
            statistics.fmean(error * error for error in tail_errors)
        ),
        "tail_rms_error_milliamperes": math.sqrt(
            statistics.fmean(error * error for error in tail_errors)
        )
        * COUNTS_TO_MILLIAMPERES,
        "peak_absolute_voltage_volts": (
            max(physical_voltages) if physical_voltages else None
        ),
        "phase_voltage_limit_volts": samples[0].get(
            "phase_voltage_limit_volts"
        ),
        "peak_absolute_voltage_permille": max(voltages),
        "voltage_saturation_fraction": sum(value >= 100.0 for value in voltages)
        / len(voltages),
        "peak_cross_axis_current_counts": max(other_measured),
    }


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--tolerance-percent", type=float, default=5.0)
    return parser


def main() -> int:
    args = make_parser().parse_args()
    if not (0.0 < args.tolerance_percent <= 100.0):
        raise ValueError("--tolerance-percent must be in (0, 100]")
    print(
        json.dumps(
            analyze(load_trace(args.trace), args.tolerance_percent),
            indent=2,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
