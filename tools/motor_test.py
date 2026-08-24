#!/usr/bin/env python3
"""Run one bounded MKS57D motor diagnostic, capture it, and open plots."""

from __future__ import annotations

import argparse
import html
import json
import math
import struct
import sys
import time
import webbrowser
from datetime import datetime
from pathlib import Path
from typing import Any, Iterable

try:
    from .analyze_current_loop import analyze as analyze_current_loop
    from .mks57d_rs485 import (
        COMMAND_CONFIGURE_CURRENT_TEST,
        COMMAND_START_CURRENT_TEST,
        COMMAND_STOP_CURRENT_TEST,
        COUNTS_TO_MILLIAMPERES,
        DEFAULT_ADDRESS,
        LEG_VALUES,
        Client,
        ProtocolError,
        arm_current_trace,
        open_serial,
        query_encoder,
        query_status,
        read_current_trace,
    )
except ImportError:  # Direct execution from tools/.
    from analyze_current_loop import analyze as analyze_current_loop
    from mks57d_rs485 import (
        COMMAND_CONFIGURE_CURRENT_TEST,
        COMMAND_START_CURRENT_TEST,
        COMMAND_STOP_CURRENT_TEST,
        COUNTS_TO_MILLIAMPERES,
        DEFAULT_ADDRESS,
        LEG_VALUES,
        Client,
        ProtocolError,
        arm_current_trace,
        open_serial,
        query_encoder,
        query_status,
        read_current_trace,
    )


LOOP_FREQUENCY_HZ = 20_000.0
ELECTRICAL_CYCLES_PER_REVOLUTION = 50.0
REQUIRED_READY_FLAGS = {
    "adc_ready",
    "adc_snapshot_valid",
    "adc_calibration_ready",
    "current_loop_initialized",
    "bridge_ready",
}
AUTHORITY_FLAGS = {
    "authority_active",
    "backend_active",
    "remote_authority",
    "remote_start_pending",
    "remote_stop_pending",
}


def _write_jsonl(path: Path, values: Iterable[dict[str, Any]]) -> None:
    with path.open("w", encoding="utf-8") as stream:
        for value in values:
            stream.write(json.dumps(value, sort_keys=True) + "\n")


def _load_jsonl(path: Path) -> list[dict[str, Any]]:
    values = []
    with path.open("r", encoding="utf-8-sig") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                values.append(json.loads(line))
            except json.JSONDecodeError as error:
                raise ValueError(f"invalid JSON on {path}:{line_number}: {error}") from error
    return values


def _format_number(value: float) -> str:
    absolute = abs(value)
    if absolute >= 100.0:
        return f"{value:.0f}"
    if absolute >= 10.0:
        return f"{value:.1f}"
    return f"{value:.2f}"


def _plot_series_css(
    color_count: int = 5,
    dash_pattern: str = "7 5",
    legend_width_pixels: int = 20,
) -> str:
    color_classes = "".join(
        f".s{index}{{--series-color:var(--s{index})}}"
        for index in range(1, color_count + 1)
    )
    return (
        ".series{fill:none;stroke:var(--series-color);stroke-width:2.2;"
        "vector-effect:non-scaling-stroke}"
        f".dashed{{stroke-dasharray:{dash_pattern}}}"
        ".point{fill:var(--series-color);stroke:none}"
        f"{color_classes}"
        f".legend-line{{width:{legend_width_pixels}px;border-top:3px solid "
        "var(--series-color);display:inline-block}"
        ".legend-line.dashed{border-top-style:dashed}"
    )


def _polyline_plot(
    title: str,
    x_label: str,
    y_label: str,
    x_values: list[float],
    series: list[tuple[str, list[float], int, bool]],
) -> str:
    if not x_values or not series:
        return f"<section><h2>{html.escape(title)}</h2><p>No samples captured.</p></section>"
    width, height = 1100.0, 330.0
    left, right, top, bottom = 76.0, 24.0, 18.0, 52.0
    plot_width = width - left - right
    plot_height = height - top - bottom
    x_min, x_max = min(x_values), max(x_values)
    if math.isclose(x_min, x_max):
        x_min -= 0.5
        x_max += 0.5
    all_y = [value for _, values, _, _ in series for value in values]
    y_min, y_max = min(all_y), max(all_y)
    if math.isclose(y_min, y_max):
        padding = max(1.0, abs(y_min) * 0.1)
    else:
        padding = (y_max - y_min) * 0.08
    y_min -= padding
    y_max += padding

    def sx(value: float) -> float:
        return left + (value - x_min) * plot_width / (x_max - x_min)

    def sy(value: float) -> float:
        return top + (y_max - value) * plot_height / (y_max - y_min)

    grid = []
    for index in range(6):
        fraction = index / 5.0
        x_value = x_min + fraction * (x_max - x_min)
        x = sx(x_value)
        grid.append(
            f'<line class="grid" x1="{x:.2f}" y1="{top:.2f}" '
            f'x2="{x:.2f}" y2="{top + plot_height:.2f}" />'
        )
        grid.append(
            f'<text class="tick" x="{x:.2f}" y="{height - 27:.2f}" '
            f'text-anchor="middle">{html.escape(_format_number(x_value))}</text>'
        )
        y_value = y_min + fraction * (y_max - y_min)
        y = sy(y_value)
        grid.append(
            f'<line class="grid" x1="{left:.2f}" y1="{y:.2f}" '
            f'x2="{left + plot_width:.2f}" y2="{y:.2f}" />'
        )
        grid.append(
            f'<text class="tick" x="{left - 10:.2f}" y="{y + 4:.2f}" '
            f'text-anchor="end">{html.escape(_format_number(y_value))}</text>'
        )
    paths = []
    legend = []
    for label, values, color_index, dashed in series:
        if len(values) != len(x_values):
            raise ValueError(f"plot series {label!r} has the wrong length")
        points = " ".join(
            f"{sx(x):.2f},{sy(y):.2f}" for x, y in zip(x_values, values)
        )
        dash_class = " dashed" if dashed else ""
        paths.append(
            f'<polyline class="series s{color_index}{dash_class}" points="{points}" />'
        )
        legend.append(
            f'<span><i class="legend-line s{color_index}{dash_class}"></i>'
            f'{html.escape(label)}</span>'
        )
    return f"""
    <section>
      <h2>{html.escape(title)}</h2>
      <div class="legend">{''.join(legend)}</div>
      <svg viewBox="0 0 {width:.0f} {height:.0f}" role="img" aria-label="{html.escape(title)}">
        <title>{html.escape(title)}</title>
        <rect class="frame" x="{left:.2f}" y="{top:.2f}" width="{plot_width:.2f}" height="{plot_height:.2f}" />
        {''.join(grid)}
        {''.join(paths)}
        <text class="axis-label" x="{left + plot_width / 2:.2f}" y="{height - 5:.2f}" text-anchor="middle">{html.escape(x_label)}</text>
        <text class="axis-label" transform="translate(18 {top + plot_height / 2:.2f}) rotate(-90)" text-anchor="middle">{html.escape(y_label)}</text>
      </svg>
    </section>
    """


def _active_samples(telemetry: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [
        sample
        for sample in telemetry
        if "backend_active" in sample.get("flags", [])
    ]


def _sample_times(samples: list[dict[str, Any]]) -> list[float]:
    if not samples:
        return []
    first = int(samples[0]["loop"]["sample_count"])
    return [
        (int(sample["loop"]["sample_count"]) - first) / LOOP_FREQUENCY_HZ
        for sample in samples
    ]


def _unwrapped_encoder_revolutions(
    samples: list[dict[str, Any]],
) -> list[float]:
    raw_values = [int(sample["encoder"]["angle_raw"]) for sample in samples]
    if not raw_values:
        return []
    unwrapped = [raw_values[0]]
    for raw in raw_values[1:]:
        delta = raw - (unwrapped[-1] % 16384)
        if delta > 8192:
            delta -= 16384
        elif delta < -8192:
            delta += 16384
        unwrapped.append(unwrapped[-1] + delta)
    return [(value - unwrapped[0]) / 16384.0 for value in unwrapped]


def _estimator_summary(samples: list[dict[str, Any]]) -> dict[str, Any] | None:
    estimators = [
        sample["encoder"]["estimator"]
        for sample in samples
        if "estimator" in sample.get("encoder", {})
    ]
    if not estimators:
        return None
    final = estimators[-1]
    return {
        "sample_count": len(estimators),
        "ready_on_all_samples": all(
            "estimator_ready" in estimator.get("flags", [])
            for estimator in estimators
        ),
        "faults": sorted(
            {
                fault
                for estimator in estimators
                for fault in estimator.get("faults", [])
            }
        ),
        "final_position_revolutions": final["position_revolutions"],
        "final_velocity_revolutions_per_second": final[
            "velocity_revolutions_per_second"
        ],
        "latest_sample_interval_us": final["sample_interval_us"],
        "maximum_sample_interval_us": max(
            int(estimator["maximum_sample_interval_us"])
            for estimator in estimators
        ),
    }


def _check_encoder_preflight(encoder: dict[str, Any]) -> None:
    if encoder["status"] != "ok" or encoder["transport_status"] != "ok":
        raise ProtocolError("encoder preflight is not healthy")
    if encoder["no_magnet"] or encoder["over_speed"]:
        raise ProtocolError("encoder preflight has an active sensor flag")
    estimator = encoder.get("estimator")
    if estimator is None:
        return
    if "estimator_ready" not in estimator.get("flags", []):
        raise ProtocolError("mechanical estimator is not ready")
    if estimator.get("faults"):
        raise ProtocolError(
            "mechanical estimator has faults: "
            + ", ".join(estimator["faults"])
        )


def write_report(
    path: Path,
    bundle: dict[str, Any],
    telemetry: list[dict[str, Any]],
    trace: list[dict[str, Any]],
) -> None:
    request = bundle["request"]
    analysis = bundle.get("analysis") or {}
    active = _active_samples(telemetry)
    times = _sample_times(active)
    current_plot = ""
    encoder_plot = ""
    voltage_plot = ""
    if active:
        current_plot = _polyline_plot(
            "Rotating phase-current tracking",
            "Run time (s)",
            "Current (mA)",
            times,
            [
                (
                    "A reference",
                    [sample["loop"]["reference_counts"]["a"] * COUNTS_TO_MILLIAMPERES for sample in active],
                    1,
                    True,
                ),
                (
                    "A measured",
                    [sample["loop"]["measured_counts"]["a"] * COUNTS_TO_MILLIAMPERES for sample in active],
                    1,
                    False,
                ),
                (
                    "B reference",
                    [sample["loop"]["reference_counts"]["b"] * COUNTS_TO_MILLIAMPERES for sample in active],
                    2,
                    True,
                ),
                (
                    "B measured",
                    [sample["loop"]["measured_counts"]["b"] * COUNTS_TO_MILLIAMPERES for sample in active],
                    2,
                    False,
                ),
            ],
        )
        encoder_revolutions = _unwrapped_encoder_revolutions(active)
        direction = -1.0 if encoder_revolutions[-1] < 0.0 else 1.0
        expected_revolutions = [
            direction
            * float(request["electrical_hz"])
            * elapsed
            / ELECTRICAL_CYCLES_PER_REVOLUTION
            for elapsed in times
        ]
        encoder_plot = _polyline_plot(
            "Encoder motion",
            "Run time (s)",
            "Revolutions from start",
            times,
            [
                ("Measured", encoder_revolutions, 3, False),
                (
                    "Expected at observed direction",
                    expected_revolutions,
                    4,
                    True,
                ),
            ],
        )
        physical_voltage_available = all(
            sample["loop"].get("phase_voltage_command_volts", {}).get(axis)
            is not None
            and sample["loop"].get("phase_voltage_limit_volts") is not None
            for sample in active
            for axis in ("a", "b")
        )
        if physical_voltage_available:
            voltage_plot = _polyline_plot(
                "Commanded average phase voltage",
                "Run time (s)",
                "Absolute phase voltage (V)",
                times,
                [
                    (
                        "Phase A",
                        [
                            abs(sample["loop"]["phase_voltage_command_volts"]["a"])
                            for sample in active
                        ],
                        1,
                        False,
                    ),
                    (
                        "Phase B",
                        [
                            abs(sample["loop"]["phase_voltage_command_volts"]["b"])
                            for sample in active
                        ],
                        2,
                        False,
                    ),
                    (
                        "Configured ceiling",
                        [
                            sample["loop"]["phase_voltage_limit_volts"]
                            for sample in active
                        ],
                        5,
                        True,
                    ),
                ],
            )
    trace_plot = ""
    if trace:
        trace_times = [float(sample["time_seconds"]) * 1000.0 for sample in trace]
        trace_plot = _polyline_plot(
            "First 12.8 ms at 20 kHz",
            "Time after enable (ms)",
            "Current (mA)",
            trace_times,
            [
                (
                    "A reference",
                    [sample["reference_counts"]["a"] * COUNTS_TO_MILLIAMPERES for sample in trace],
                    1,
                    True,
                ),
                (
                    "A measured",
                    [sample["measured_counts"]["a"] * COUNTS_TO_MILLIAMPERES for sample in trace],
                    1,
                    False,
                ),
                (
                    "B reference",
                    [sample["reference_counts"]["b"] * COUNTS_TO_MILLIAMPERES for sample in trace],
                    2,
                    True,
                ),
                (
                    "B measured",
                    [sample["measured_counts"]["b"] * COUNTS_TO_MILLIAMPERES for sample in trace],
                    2,
                    False,
                ),
            ],
        )
    encoder = analysis.get("encoder", {})
    measured_rpm = encoder.get("rpm")
    rms_error = analysis.get("combined_rms_error_milliamperes")
    max_voltage = analysis.get("maximum_absolute_phase_voltage_volts")
    voltage_limit = analysis.get("phase_voltage_limit_volts")
    faults = bundle.get("faults", analysis.get("faults", []))
    estimator = bundle.get("estimator")

    def stat(label: str, value: str) -> str:
        return (
            '<div class="stat"><span class="stat-label">'
            + html.escape(label)
            + '</span><strong>'
            + html.escape(value)
            + "</strong></div>"
        )

    stats = [
        stat("Applied phase current", f"{request['applied_current_ma']:.1f} mA"),
        stat("Electrical frequency", f"{request['electrical_hz']:.3f} Hz"),
        stat("Expected shaft speed", f"{request['expected_rpm']:.2f} RPM"),
        stat(
            "Measured shaft speed",
            "unavailable" if measured_rpm is None else f"{abs(measured_rpm):.2f} RPM",
        ),
        stat(
            "Current tracking RMS",
            "unavailable" if rms_error is None else f"{rms_error:.1f} mA",
        ),
        stat(
            "Peak commanded phase voltage",
            "unavailable"
            if max_voltage is None
            else (
                f"{max_voltage:.2f} V"
                if voltage_limit is None
                else f"{max_voltage:.2f} V / {float(voltage_limit):.2f} V limit"
            ),
        ),
        stat(
            "Estimator max interval",
            "unavailable"
            if estimator is None
            else f"{estimator['maximum_sample_interval_us']} us",
        ),
        stat("Faults", "none" if not faults else ", ".join(faults)),
    ]
    generated = html.escape(bundle.get("generated_at", ""))
    document = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MKS57D motor test report</title>
  <style>
    :root {{ color-scheme: light dark; --bg:#fafafa; --fg:#1b1b1b; --muted:#626262; --grid:#d8d8d8; --surface:#ffffff; --s1:#1769aa; --s2:#d97706; --s3:#0f8a62; --s4:#7c3aed; --s5:#b91c1c; }}
    @media (prefers-color-scheme: dark) {{ :root {{ --bg:#151515; --fg:#eeeeee; --muted:#aaaaaa; --grid:#3b3b3b; --surface:#1c1c1c; --s1:#63b3ed; --s2:#f6ad55; --s3:#68d391; --s4:#b794f4; --s5:#fc8181; }} }}
    * {{ box-sizing:border-box; }} body {{ margin:0; background:var(--bg); color:var(--fg); font:15px/1.45 system-ui,sans-serif; }}
    main {{ max-width:1220px; margin:0 auto; padding:24px; }} h1 {{ margin:0 0 4px; font-size:1.7rem; }} h2 {{ margin:0 0 8px; font-size:1.1rem; }}
    .sub {{ color:var(--muted); margin:0 0 18px; }} .stats {{ display:grid; grid-template-columns:repeat(auto-fit,minmax(180px,1fr)); gap:10px; margin-bottom:22px; }}
    .stat {{ background:var(--surface); border:1px solid var(--grid); border-radius:8px; padding:10px 12px; }} .stat-label {{ display:block; color:var(--muted); font-size:.82rem; }} .stat strong {{ font-size:1.08rem; font-weight:600; }}
    section {{ margin:24px 0 34px; }} svg {{ display:block; width:100%; height:auto; min-height:240px; }} .frame {{ fill:var(--surface); stroke:var(--grid); }}
    .grid {{ stroke:var(--grid); stroke-width:1; }} .tick {{ fill:var(--muted); font-size:12px; }} .axis-label {{ fill:var(--fg); font-size:13px; }}
    {_plot_series_css()}
    .legend {{ display:flex; flex-wrap:wrap; gap:7px 18px; color:var(--muted); margin-bottom:3px; }} .legend span {{ display:inline-flex; align-items:center; gap:6px; }}
    @media (max-width:600px) {{ main {{ padding:16px; }} svg {{ min-height:180px; }} }}
  </style>
</head>
<body><main>
  <h1>MKS57D bounded motor test</h1>
  <p class="sub">{generated} · {html.escape(str(request['leg']))} initial phase · {request['duration_s']:.3f} s requested</p>
  <div class="stats">{''.join(stats)}</div>
  {current_plot}{encoder_plot}{voltage_plot}{trace_plot}
</main></body></html>
"""
    path.write_text(document, encoding="utf-8")


def _configure(
    client: Client, counts: int, frequency_hz: float
) -> dict[str, float | int]:
    frequency_millihz = round(frequency_hz * 1000.0)
    body = client.transact(
        COMMAND_CONFIGURE_CURRENT_TEST,
        struct.pack(">HI", counts, frequency_millihz),
    )
    if len(body) != 6:
        raise ProtocolError("configure response has an unexpected length")
    applied_counts, applied_frequency_millihz = struct.unpack(">HI", body)
    return {
        "counts": applied_counts,
        "frequency_hz": applied_frequency_millihz / 1000.0,
    }


def _preflight(status: dict[str, Any], counts: int) -> None:
    flags = set(status["flags"])
    missing = sorted(REQUIRED_READY_FLAGS - flags)
    if missing:
        raise ProtocolError("device is not ready: missing " + ", ".join(missing))
    active = sorted(AUTHORITY_FLAGS & flags)
    if active:
        raise ProtocolError("device already has active authority: " + ", ".join(active))
    if status["loop"]["faults"]:
        raise ProtocolError(
            "device has latched faults: " + ", ".join(status["loop"]["faults"])
        )
    if "fault_present" in flags:
        raise ProtocolError("device supervisor reports a latched fault")
    maximum = int(status["test"]["maximum_amplitude_counts"])
    if counts > maximum:
        raise ProtocolError(
            f"request is {counts} counts, but this firmware reports a {maximum}-count maximum"
        )


def _run_capture(
    client: Client,
    leg: str,
    duration_ms: int,
    interval: float,
    telemetry_path: Path,
    trace_at_seconds: float | None = None,
    ramp_duration_ms: int = 0,
) -> tuple[list[dict[str, Any]], bool]:
    total_duration_ms = ramp_duration_ms + duration_ms
    start_payload = (
        struct.pack(">BII", LEG_VALUES[leg], ramp_duration_ms, duration_ms)
        if ramp_duration_ms > 0
        else struct.pack(">BI", LEG_VALUES[leg], duration_ms)
    )
    client.transact(
        COMMAND_START_CURRENT_TEST,
        start_payload,
    )
    samples: list[dict[str, Any]] = []
    completed = False
    trace_armed = False
    deadline = time.monotonic() + total_duration_ms / 1000.0 + 5.0
    with telemetry_path.open("w", encoding="utf-8") as stream:
        while True:
            status = query_status(client)
            status["encoder"] = query_encoder(client)
            remaining = int(status["test"]["remote_run_remaining_millis"])
            flags = set(status["flags"])
            authority_active = "remote_authority" in flags
            elapsed = (
                max(0.0, (total_duration_ms - remaining) / 1000.0)
                if authority_active
                else 0.0
            )
            status["test"]["run_elapsed_seconds"] = round(elapsed, 6)
            samples.append(status)
            stream.write(json.dumps(status, sort_keys=True) + "\n")
            stream.flush()
            if (
                trace_at_seconds is not None
                and not trace_armed
                and authority_active
                and elapsed >= trace_at_seconds
            ):
                arm_current_trace(client)
                trace_armed = True
            measured = status["loop"]["measured_nominal_milliamperes"]
            voltage = status["loop"].get("phase_voltage_command_volts", {})
            voltage_limit = status["loop"].get("phase_voltage_limit_volts")
            voltage_values = [voltage.get(axis) for axis in ("a", "b")]
            voltage_text = "Vph=unavailable"
            if voltage_limit is not None and all(
                value is not None for value in voltage_values
            ):
                voltage_text = (
                    f"|Vph|={max(abs(float(value)) for value in voltage_values):5.2f}/"
                    f"{float(voltage_limit):5.2f} V"
                )
            print(
                f"\r{elapsed:6.2f}/{total_duration_ms / 1000.0:.2f} s  "
                f"I=({measured['a']:+7.1f},{measured['b']:+7.1f}) mA  "
                f"{voltage_text}  "
                f"angle={status['encoder']['angle_degrees']:7.2f} deg",
                end="",
                flush=True,
            )
            if not ({"remote_authority", "remote_start_pending"} & flags):
                completed = True
                print()
                break
            if time.monotonic() > deadline:
                raise ProtocolError("run did not return authority before the host deadline")
            time.sleep(interval)
    return samples, completed


def _make_run_directory(root: Path, current_ma: float, frequency_hz: float, seconds: float) -> Path:
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    label = (
        f"{timestamp}-{round(current_ma):04d}mA-"
        f"{frequency_hz:g}Hz-{seconds:g}s"
    )
    path = root / label
    suffix = 2
    while path.exists():
        path = root / f"{label}-{suffix}"
        suffix += 1
    path.mkdir(parents=True)
    return path


def _validate_run_arguments(args: argparse.Namespace) -> tuple[int, float, int]:
    if not args.port:
        raise ValueError("--port is required for a new run")
    if args.current_ma is None or args.current_ma <= 0.0:
        raise ValueError("--current-ma must be positive")
    frequency_hz = args.electrical_hz
    if args.rpm is not None:
        if args.rpm <= 0.0:
            raise ValueError("--rpm must be positive; the present test does not expose direction control")
        frequency_hz = args.rpm / 1.2
    if frequency_hz is None or not 0.001 <= frequency_hz <= 250.0:
        raise ValueError("electrical frequency must be in the range 0.001..250 Hz")
    if args.seconds is None or not 0.003 <= args.seconds <= 2147483.647:
        raise ValueError("--seconds must be in the range 0.003..2147483.647")
    if not 0.01 <= args.interval <= 2.0:
        raise ValueError("--interval must be in the range 0.01..2.0 seconds")
    counts = round(args.current_ma / COUNTS_TO_MILLIAMPERES)
    if counts < 1:
        raise ValueError("requested current rounds below one ADC count")
    return counts, frequency_hz, round(args.seconds * 1000.0)


def _open_report(path: Path) -> None:
    if not webbrowser.open(path.resolve().as_uri()):
        print(f"Could not open a browser automatically; open {path.resolve()}")


def _replot(run_directory: Path, open_browser: bool) -> int:
    summary_path = run_directory / "summary.json"
    telemetry_path = run_directory / "telemetry.jsonl"
    trace_path = run_directory / "trace.jsonl"
    bundle = json.loads(summary_path.read_text(encoding="utf-8"))
    telemetry = _load_jsonl(telemetry_path)
    trace = _load_jsonl(trace_path) if trace_path.exists() else []
    report_path = run_directory / "report.html"
    write_report(report_path, bundle, telemetry, trace)
    print(f"Report: {report_path.resolve()}")
    if open_browser:
        _open_report(report_path)
    return 0


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port, for example COM14")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--address", type=int, default=DEFAULT_ADDRESS)
    parser.add_argument("--timeout", type=float, default=0.75)
    parser.add_argument("--current-ma", type=float, help="requested peak phase current")
    speed = parser.add_mutually_exclusive_group()
    speed.add_argument("--electrical-hz", type=float, help="rotating-vector electrical frequency")
    speed.add_argument("--rpm", type=float, help="shaft-speed magnitude, converted using 1.2 RPM per electrical Hz")
    parser.add_argument("--seconds", type=float, help="bounded run duration")
    parser.add_argument("--leg", choices=LEG_VALUES, default="A1", help="initial electrical phase, not direction")
    parser.add_argument("--interval", type=float, default=0.05, help="telemetry polling interval")
    parser.add_argument("--settle-seconds", type=float, default=0.2, help="exclude this startup interval from summary metrics")
    parser.add_argument("--output-root", type=Path, default=Path("scratch/motor-runs"))
    parser.add_argument("--keep-config", action="store_true", help="leave the requested inactive test configuration after the run")
    parser.add_argument("--no-open", action="store_true", help="generate the report without opening it")
    parser.add_argument("--replot", type=Path, metavar="RUN_DIRECTORY", help="regenerate a saved run's report without accessing hardware")
    return parser


def main() -> int:
    args = make_parser().parse_args()
    if args.replot is not None:
        return _replot(args.replot, not args.no_open)
    if not 1 <= args.address <= 247:
        raise ValueError("address must be in the range 1..247")
    if args.settle_seconds < 0.0:
        raise ValueError("--settle-seconds must be nonnegative")
    counts, frequency_hz, duration_ms = _validate_run_arguments(args)
    requested_current_ma = counts * COUNTS_TO_MILLIAMPERES
    run_directory = _make_run_directory(
        args.output_root,
        requested_current_ma,
        frequency_hz,
        duration_ms / 1000.0,
    )
    telemetry_path = run_directory / "telemetry.jsonl"
    trace_path = run_directory / "trace.jsonl"
    summary_path = run_directory / "summary.json"
    report_path = run_directory / "report.html"
    samples: list[dict[str, Any]] = []
    trace: list[dict[str, Any]] = []
    initial_status: dict[str, Any] = {}
    final_status: dict[str, Any] = {}
    restored_status: dict[str, Any] = {}
    completed = False
    started = False
    stop_succeeded = True
    restore_error: str | None = None
    with open_serial(args) as port:
        client = Client(port, args.address)
        initial_status = query_status(client)
        _preflight(initial_status, counts)
        encoder = query_encoder(client)
        _check_encoder_preflight(encoder)
        original_counts = int(initial_status["test"]["amplitude_counts"])
        original_frequency_hz = float(initial_status["test"]["frequency_hz"])
        applied = _configure(client, counts, frequency_hz)
        expected_rpm = applied["frequency_hz"] * 1.2
        expected_revolutions = (
            applied["frequency_hz"]
            * duration_ms
            / 1000.0
            / ELECTRICAL_CYCLES_PER_REVOLUTION
        )
        print(
            f"Running {applied['counts'] * COUNTS_TO_MILLIAMPERES:.1f} mA, "
            f"{applied['frequency_hz']:.3f} electrical Hz for {duration_ms / 1000.0:.3f} s "
            f"(~{expected_rpm:.2f} RPM, {expected_revolutions:.3f} rev)."
        )
        print("Press Ctrl+C at any time to send STOP.")
        try:
            started = True
            samples, completed = _run_capture(
                client,
                args.leg,
                duration_ms,
                args.interval,
                telemetry_path,
            )
            trace = read_current_trace(client)
            _write_jsonl(trace_path, trace)
            final_status = query_status(client)
        except KeyboardInterrupt:
            print("\nStopping...", file=sys.stderr)
            raise
        finally:
            if started and not completed:
                try:
                    client.transact(COMMAND_STOP_CURRENT_TEST)
                except (ProtocolError, OSError) as error:
                    stop_succeeded = False
                    print(
                        "error: STOP was not acknowledged; rely on the firmware "
                        f"deadline or press the Right button: {error}",
                        file=sys.stderr,
                    )
            if not args.keep_config and stop_succeeded:
                try:
                    _configure(client, original_counts, original_frequency_hz)
                    restored_status = query_status(client)
                except (ProtocolError, OSError) as error:
                    restore_error = str(error)

    analysis: dict[str, Any] | None = None
    analysis_error: str | None = None
    try:
        analysis = analyze_current_loop(samples, args.settle_seconds)
    except ValueError as error:
        analysis_error = str(error)
    request = {
        "requested_current_ma": args.current_ma,
        "applied_current_ma": applied["counts"] * COUNTS_TO_MILLIAMPERES,
        "counts": applied["counts"],
        "electrical_hz": applied["frequency_hz"],
        "duration_s": duration_ms / 1000.0,
        "expected_rpm": applied["frequency_hz"] * 1.2,
        "expected_revolutions": applied["frequency_hz"]
        * duration_ms
        / 1000.0
        / ELECTRICAL_CYCLES_PER_REVOLUTION,
        "leg": args.leg,
    }
    observed_faults = {
        fault
        for sample in samples
        for fault in sample.get("loop", {}).get("faults", [])
    }
    if any("fault_present" in sample.get("flags", []) for sample in samples):
        observed_faults.add("drive_supervisor_fault")
    estimator = _estimator_summary(samples)
    if estimator is not None:
        if not estimator["ready_on_all_samples"]:
            observed_faults.add("estimator_not_ready")
        observed_faults.update(
            f"estimator_{fault}" for fault in estimator["faults"]
        )
    faults = sorted(observed_faults)
    bundle = {
        "schema": 1,
        "generated_at": datetime.now().astimezone().isoformat(timespec="seconds"),
        "request": request,
        "analysis": analysis,
        "analysis_error": analysis_error,
        "estimator": estimator,
        "faults": faults,
        "trace_sample_count": len(trace),
        "configuration_restored": not args.keep_config and restore_error is None,
        "configuration_restore_error": restore_error,
        "initial_status": initial_status,
        "final_status": final_status,
        "restored_status": restored_status,
    }
    summary_path.write_text(json.dumps(bundle, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_report(report_path, bundle, samples, trace)
    print(f"Capture: {run_directory.resolve()}")
    if analysis is not None:
        encoder_result = analysis.get("encoder", {})
        rpm = encoder_result.get("rpm")
        rpm_text = "unavailable" if rpm is None else f"{abs(rpm):.2f} RPM"
        peak_phase_voltage = analysis.get(
            "maximum_absolute_phase_voltage_volts"
        )
        voltage_text = (
            "peak phase voltage unavailable"
            if peak_phase_voltage is None
            else f"{float(peak_phase_voltage):.2f} V peak phase voltage"
        )
        print(
            f"Result: {rpm_text}, {analysis['combined_rms_error_milliamperes']:.1f} mA RMS error, "
            f"{voltage_text}, faults={analysis['faults'] or 'none'}"
        )
    elif analysis_error:
        print(f"Analysis note: {analysis_error}")
    if estimator is not None:
        print(
            "Estimator: "
            f"ready_all={estimator['ready_on_all_samples']}, "
            f"max_interval={estimator['maximum_sample_interval_us']} us, "
            f"faults={estimator['faults'] or 'none'}"
        )
    if restore_error:
        print(f"warning: could not restore the prior inactive configuration: {restore_error}", file=sys.stderr)
    print(f"Report: {report_path.resolve()}")
    if not args.no_open:
        _open_report(report_path)
    return 3 if faults else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
    except (ProtocolError, OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
