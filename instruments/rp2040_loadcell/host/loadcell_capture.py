#!/usr/bin/env python3
"""Capture raw, timestamped samples from the RP2040 load-cell instrument."""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import sys
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Protocol, TextIO, Union


PROTOCOL_VERSION = 1
UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1
RAW_24_MIN = -(1 << 23)
RAW_24_MAX = (1 << 23) - 1
CSV_FIELDS = (
    "sequence",
    "device_timestamp_us",
    "host_receive_elapsed_seconds",
    "raw_counts",
    "flags_hex",
    "dropped_total",
)
IDENTIFIER_PATTERN = re.compile(r"[A-Za-z0-9_.-]{1,31}\Z")


class CaptureError(RuntimeError):
    """Base class for host capture failures."""


class CaptureInterrupted(CaptureError):
    """The operator interrupted a capture after graceful shutdown was attempted."""


class ProtocolError(CaptureError):
    """A received line did not conform to protocol version 1."""


class ProtocolTimeout(CaptureError):
    """The device did not complete a protocol operation in time."""


class CommandRejected(CaptureError):
    """The device returned an ERR response for a host command."""

    def __init__(self, response: "ErrorResponse") -> None:
        self.response = response
        super().__init__(
            f"{response.command} rejected: {response.error_code}: "
            f"{response.description}"
        )


@dataclass(frozen=True)
class SampleRecord:
    sequence: int
    timestamp_us: int
    raw_counts: int
    flags: int
    dropped_total: int

    @property
    def flags_hex(self) -> str:
        return f"0x{self.flags:X}"


@dataclass(frozen=True)
class MarkerRecord:
    marker_id: str
    timestamp_us: int


@dataclass(frozen=True)
class OkResponse:
    command: str
    fields: tuple[str, ...]


@dataclass(frozen=True)
class ErrorResponse:
    command: str
    error_code: str
    description: str


@dataclass(frozen=True)
class FinalSummaryRecord:
    run_id: str
    first_sequence: int
    last_sequence: int
    captured_sample_count: int
    dropped_sample_count: int
    i2c_error_count: int
    buffer_overrun_count: int
    first_timestamp_us: int
    last_timestamp_us: int
    average_sample_rate_sps: float


ProtocolRecord = Union[
    SampleRecord,
    MarkerRecord,
    OkResponse,
    ErrorResponse,
    FinalSummaryRecord,
]


@dataclass(frozen=True)
class SequenceGap:
    previous_sequence: int
    current_sequence: int
    missing_count: int


@dataclass(frozen=True)
class MalformedRecord:
    line: str
    error: str


@dataclass(frozen=True)
class ScheduledMarker:
    elapsed_seconds: float
    marker_id: str


def _parse_int(
    value: str,
    field_name: str,
    *,
    minimum: int,
    maximum: int,
    base: int = 10,
) -> int:
    try:
        parsed = int(value, base)
    except ValueError as exc:
        raise ProtocolError(f"{field_name} is not an integer: {value!r}") from exc
    if parsed < minimum or parsed > maximum:
        raise ProtocolError(
            f"{field_name} is outside [{minimum}, {maximum}]: {parsed}"
        )
    return parsed


def _parse_hex(value: str, field_name: str) -> int:
    token = value[2:] if value.lower().startswith("0x") else value
    if not token or re.fullmatch(r"[0-9A-Fa-f]+", token) is None:
        raise ProtocolError(f"{field_name} is not hexadecimal: {value!r}")
    return _parse_int(token, field_name, minimum=0, maximum=UINT32_MAX, base=16)


def _require_version(fields: list[str]) -> None:
    if len(fields) < 2:
        raise ProtocolError("record has no protocol version")
    version = _parse_int(
        fields[1], "protocol version", minimum=0, maximum=UINT32_MAX
    )
    if version != PROTOCOL_VERSION:
        raise ProtocolError(
            f"unsupported protocol version {version}; expected {PROTOCOL_VERSION}"
        )


def _require_token(value: str, field_name: str) -> str:
    if not value or any(char.isspace() for char in value):
        raise ProtocolError(f"{field_name} must be a non-empty token")
    return value


def _require_identifier(value: str, field_name: str) -> str:
    if IDENTIFIER_PATTERN.fullmatch(value) is None:
        raise ProtocolError(
            f"{field_name} must be 1-31 ASCII letters, digits, '_', '.', or '-'"
        )
    return value


def parse_record(line: str) -> ProtocolRecord:
    """Parse one complete ASCII protocol line and validate its field ranges."""

    stripped = line.rstrip("\r\n")
    if not stripped:
        raise ProtocolError("empty protocol line")
    if "\r" in stripped or "\n" in stripped:
        raise ProtocolError("record contains an embedded line break")

    fields = stripped.split(",")
    tag = fields[0]
    if tag not in {"S", "M", "OK", "ERR", "F"}:
        raise ProtocolError(f"unknown or untagged record type {tag!r}")
    _require_version(fields)

    if tag == "S":
        if len(fields) != 7:
            raise ProtocolError(f"S record has {len(fields)} fields; expected 7")
        return SampleRecord(
            sequence=_parse_int(
                fields[2], "sequence", minimum=0, maximum=UINT32_MAX
            ),
            timestamp_us=_parse_int(
                fields[3], "timestamp_us", minimum=0, maximum=UINT64_MAX
            ),
            raw_counts=_parse_int(
                fields[4], "raw_counts", minimum=RAW_24_MIN, maximum=RAW_24_MAX
            ),
            flags=_parse_hex(fields[5], "flags_hex"),
            dropped_total=_parse_int(
                fields[6], "dropped_total", minimum=0, maximum=UINT32_MAX
            ),
        )

    if tag == "M":
        if len(fields) != 4:
            raise ProtocolError(f"M record has {len(fields)} fields; expected 4")
        return MarkerRecord(
            marker_id=_require_identifier(fields[2], "marker_id"),
            timestamp_us=_parse_int(
                fields[3], "timestamp_us", minimum=0, maximum=UINT64_MAX
            ),
        )

    if tag == "OK":
        if len(fields) < 3:
            raise ProtocolError("OK record has no command")
        return OkResponse(
            command=_require_token(fields[2], "command").upper(),
            fields=tuple(fields[3:]),
        )

    if tag == "ERR":
        if len(fields) != 5:
            raise ProtocolError(f"ERR record has {len(fields)} fields; expected 5")
        return ErrorResponse(
            command=_require_token(fields[2], "command").upper(),
            error_code=_require_token(fields[3], "error_code"),
            description=fields[4],
        )

    if len(fields) != 12:
        raise ProtocolError(f"F record has {len(fields)} fields; expected 12")
    run_id = _require_identifier(fields[2], "run_id")
    try:
        average_rate = float(fields[11])
    except ValueError as exc:
        raise ProtocolError(
            f"average_sample_rate_sps is not numeric: {fields[11]!r}"
        ) from exc
    if not math.isfinite(average_rate) or average_rate < 0.0:
        raise ProtocolError("average_sample_rate_sps must be finite and non-negative")
    return FinalSummaryRecord(
        run_id=run_id,
        first_sequence=_parse_int(
            fields[3], "first_sequence", minimum=0, maximum=UINT32_MAX
        ),
        last_sequence=_parse_int(
            fields[4], "last_sequence", minimum=0, maximum=UINT32_MAX
        ),
        captured_sample_count=_parse_int(
            fields[5], "captured_sample_count", minimum=0, maximum=UINT32_MAX
        ),
        dropped_sample_count=_parse_int(
            fields[6], "dropped_sample_count", minimum=0, maximum=UINT32_MAX
        ),
        i2c_error_count=_parse_int(
            fields[7], "i2c_error_count", minimum=0, maximum=UINT32_MAX
        ),
        buffer_overrun_count=_parse_int(
            fields[8], "buffer_overrun_count", minimum=0, maximum=UINT32_MAX
        ),
        first_timestamp_us=_parse_int(
            fields[9], "first_timestamp_us", minimum=0, maximum=UINT64_MAX
        ),
        last_timestamp_us=_parse_int(
            fields[10], "last_timestamp_us", minimum=0, maximum=UINT64_MAX
        ),
        average_sample_rate_sps=average_rate,
    )


class CaptureArtifacts:
    """Incrementally write sample CSV and atomically finalize capture metadata."""

    def __init__(
        self,
        output_directory: Path,
        *,
        create_directory: bool = True,
        metadata_name: str = "metadata.json",
        telemetry_name: str = "force_telemetry.csv",
    ) -> None:
        self.output_directory = output_directory
        if create_directory:
            output_directory.mkdir(parents=True, exist_ok=False)
        elif not output_directory.is_dir():
            raise CaptureError(
                "existing artifact directory does not exist: "
                f"{output_directory}"
            )
        self.csv_path = output_directory / telemetry_name
        self.metadata_path = output_directory / metadata_name
        if self.csv_path.exists() or self.metadata_path.exists():
            raise CaptureError("load-cell artifact path already exists")
        self._csv_stream: TextIO = self.csv_path.open(
            "w", newline="", encoding="utf-8"
        )
        self._writer = csv.DictWriter(self._csv_stream, fieldnames=CSV_FIELDS)
        self._writer.writeheader()
        self._closed = False

    def write_sample(self, sample: SampleRecord, elapsed_seconds: float) -> None:
        if self._closed:
            raise CaptureError("cannot write a sample after artifacts are finalized")
        self._writer.writerow(
            {
                "sequence": sample.sequence,
                "device_timestamp_us": sample.timestamp_us,
                "host_receive_elapsed_seconds": f"{elapsed_seconds:.9f}",
                "raw_counts": sample.raw_counts,
                "flags_hex": sample.flags_hex,
                "dropped_total": sample.dropped_total,
            }
        )

    def finalize(self, metadata: dict[str, Any]) -> None:
        if self._closed:
            return
        self._csv_stream.flush()
        self._csv_stream.close()
        temporary_path = self.metadata_path.with_suffix(".json.tmp")
        temporary_path.write_text(
            json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        temporary_path.replace(self.metadata_path)
        self._closed = True

    def close_without_metadata(self) -> None:
        if not self._closed:
            self._csv_stream.close()
            self._closed = True


class CaptureSession:
    """Hardware-independent record accounting for one capture."""

    def __init__(self, artifacts: CaptureArtifacts | None = None) -> None:
        self.artifacts = artifacts
        self.capture_start_monotonic: float | None = None
        self.first_sequence: int | None = None
        self.last_sequence: int | None = None
        self.first_timestamp_us: int | None = None
        self.last_timestamp_us: int | None = None
        self.sample_count = 0
        self.sequence_missing_count = 0
        self.sequence_gaps: list[SequenceGap] = []
        self.duplicate_or_out_of_order_count = 0
        self.timestamp_regression_count = 0
        self.saturation_sample_count = 0
        self.last_dropped_total = 0
        self.markers: list[MarkerRecord] = []
        self.malformed_records: list[MalformedRecord] = []
        self.malformed_record_count = 0
        self.unexpected_responses: list[dict[str, Any]] = []
        self.final_summary: FinalSummaryRecord | None = None
        self.unclaimed_final_summaries: list[FinalSummaryRecord] = []
        self.expected_final_run_id: str | None = None
        self.accepting_final_summary = False

    def start_capture(self, monotonic_time: float) -> None:
        self.capture_start_monotonic = monotonic_time

    def expect_final_summary(self, run_id: str) -> None:
        self.expected_final_run_id = run_id
        self.accepting_final_summary = True

    def record_malformed(self, line: str, error: Exception) -> None:
        self.malformed_record_count += 1
        if len(self.malformed_records) < 100:
            self.malformed_records.append(
                MalformedRecord(line=line[:512], error=str(error))
            )

    def accept(self, record: ProtocolRecord, received_monotonic: float) -> None:
        if isinstance(record, SampleRecord):
            if self.capture_start_monotonic is None:
                self.unexpected_responses.append(
                    {"type": "sample_before_start", "sequence": record.sequence}
                )
                return
            if self.last_sequence is not None:
                delta = (record.sequence - self.last_sequence) & UINT32_MAX
                if delta == 0 or delta >= (1 << 31):
                    self.duplicate_or_out_of_order_count += 1
                elif delta > 1:
                    missing = delta - 1
                    self.sequence_missing_count += missing
                    self.sequence_gaps.append(
                        SequenceGap(
                            previous_sequence=self.last_sequence,
                            current_sequence=record.sequence,
                            missing_count=missing,
                        )
                    )
            if (
                self.last_timestamp_us is not None
                and record.timestamp_us < self.last_timestamp_us
            ):
                self.timestamp_regression_count += 1
            if self.first_sequence is None:
                self.first_sequence = record.sequence
                self.first_timestamp_us = record.timestamp_us
            self.last_sequence = record.sequence
            self.last_timestamp_us = record.timestamp_us
            self.last_dropped_total = record.dropped_total
            if record.flags & 0x0001:
                self.saturation_sample_count += 1
            self.sample_count += 1
            if self.artifacts is not None:
                elapsed = max(
                    0.0, received_monotonic - self.capture_start_monotonic
                )
                self.artifacts.write_sample(record, elapsed)
            return

        if isinstance(record, MarkerRecord):
            if self.capture_start_monotonic is None:
                self.unexpected_responses.append(
                    {"type": "marker_before_start", "marker_id": record.marker_id}
                )
            else:
                self.markers.append(record)
            return

        if isinstance(record, FinalSummaryRecord):
            if (
                not self.accepting_final_summary
                or record.run_id != self.expected_final_run_id
            ):
                if len(self.unclaimed_final_summaries) < 8:
                    self.unclaimed_final_summaries.append(record)
                self.unexpected_responses.append(
                    {
                        "type": "unclaimed_final_summary",
                        "run_id": record.run_id,
                    }
                )
            elif self.final_summary is not None:
                self.unexpected_responses.append(
                    {"type": "duplicate_final_summary", "run_id": record.run_id}
                )
            else:
                self.final_summary = record
            return

        self.unexpected_responses.append(response_to_json(record))

    def _measured_rate(self) -> float | None:
        if (
            self.sample_count < 2
            or self.first_timestamp_us is None
            or self.last_timestamp_us is None
            or self.last_timestamp_us <= self.first_timestamp_us
        ):
            return None
        return (self.sample_count - 1) * 1_000_000.0 / (
            self.last_timestamp_us - self.first_timestamp_us
        )

    def final_summary_mismatches(self) -> dict[str, dict[str, Any]]:
        final = self.final_summary
        if final is None:
            return {}
        comparisons = {
            "first_sequence": (self.first_sequence, final.first_sequence),
            "last_sequence": (self.last_sequence, final.last_sequence),
            "captured_sample_count": (
                self.sample_count,
                final.captured_sample_count,
            ),
            "first_timestamp_us": (
                self.first_timestamp_us,
                final.first_timestamp_us,
            ),
            "last_timestamp_us": (self.last_timestamp_us, final.last_timestamp_us),
        }
        return {
            field: {"host": host, "device": device}
            for field, (host, device) in comparisons.items()
            if host != device
        }

    def summary_json(self) -> dict[str, Any]:
        return {
            "sample_count": self.sample_count,
            "first_sequence": self.first_sequence,
            "last_sequence": self.last_sequence,
            "first_timestamp_us": self.first_timestamp_us,
            "last_timestamp_us": self.last_timestamp_us,
            "measured_average_sample_rate_sps": self._measured_rate(),
            "sequence_missing_count": self.sequence_missing_count,
            "sequence_gaps": [asdict(gap) for gap in self.sequence_gaps],
            "duplicate_or_out_of_order_count": (
                self.duplicate_or_out_of_order_count
            ),
            "timestamp_regression_count": self.timestamp_regression_count,
            "saturation_sample_count": self.saturation_sample_count,
            "last_reported_dropped_total": self.last_dropped_total,
            "malformed_record_count": self.malformed_record_count,
            "malformed_records": [asdict(item) for item in self.malformed_records],
            "markers": [asdict(marker) for marker in self.markers],
            "final_summary_mismatches": self.final_summary_mismatches(),
            "unexpected_responses": self.unexpected_responses,
        }


def response_to_json(record: ProtocolRecord) -> dict[str, Any]:
    if isinstance(record, OkResponse):
        return {
            "type": "ok",
            "command": record.command,
            "fields": list(record.fields),
        }
    if isinstance(record, ErrorResponse):
        return {
            "type": "error",
            "command": record.command,
            "error_code": record.error_code,
            "description": record.description,
        }
    if isinstance(record, SampleRecord):
        return {"type": "sample", **asdict(record), "flags_hex": record.flags_hex}
    if isinstance(record, MarkerRecord):
        return {"type": "marker", **asdict(record)}
    return {"type": "final_summary", **asdict(record)}


def parse_tare_complete(response: OkResponse) -> dict[str, Any]:
    """Decode the fixed TARE COMPLETE payload into named metadata fields."""

    if response.command != "TARE" or len(response.fields) != 4:
        raise ProtocolError("TARE COMPLETE response has an unexpected field count")
    if response.fields[0].upper() != "COMPLETE":
        raise ProtocolError("TARE response is not the COMPLETE phase")
    sample_count = _parse_int(
        response.fields[1], "tare sample count", minimum=1, maximum=UINT32_MAX
    )
    try:
        mean_raw_counts = float(response.fields[2])
        stddev_raw_counts = float(response.fields[3])
    except ValueError as exc:
        raise ProtocolError("TARE mean and standard deviation must be numeric") from exc
    if not math.isfinite(mean_raw_counts):
        raise ProtocolError("TARE mean must be finite")
    if not math.isfinite(stddev_raw_counts) or stddev_raw_counts < 0.0:
        raise ProtocolError("TARE standard deviation must be finite and non-negative")
    return {
        "sample_count": sample_count,
        "mean_raw_counts": mean_raw_counts,
        "stddev_raw_counts": stddev_raw_counts,
    }


def validate_info_response(response: OkResponse) -> None:
    if response.command != "INFO" or len(response.fields) < 2:
        raise ProtocolError("INFO response must identify firmware and version")
    _require_token(response.fields[0], "firmware name")
    _require_token(response.fields[1], "firmware version")


def validate_config_response(
    response: OkResponse, expected_sample_rate_sps: int, expected_gain: int
) -> None:
    if response.command != "CONFIG" or len(response.fields) != 2:
        raise ProtocolError("CONFIG response has an unexpected payload")
    sample_rate_sps = _parse_int(
        response.fields[0], "configured sample rate", minimum=1, maximum=UINT32_MAX
    )
    gain = _parse_int(
        response.fields[1], "configured gain", minimum=1, maximum=UINT32_MAX
    )
    if sample_rate_sps != expected_sample_rate_sps or gain != expected_gain:
        raise ProtocolError(
            "CONFIG response does not match the requested sample rate and gain"
        )


def validate_tare_started_response(
    response: OkResponse, expected_sample_count: int
) -> None:
    if (
        response.command != "TARE"
        or len(response.fields) != 2
        or response.fields[0].upper() != "STARTED"
    ):
        raise ProtocolError("TARE did not acknowledge its asynchronous STARTED phase")
    sample_count = _parse_int(
        response.fields[1], "tare sample count", minimum=1, maximum=UINT32_MAX
    )
    if sample_count != expected_sample_count:
        raise ProtocolError("TARE STARTED sample count does not match the request")


def validate_status_response(response: OkResponse) -> None:
    if response.command != "STATUS" or not response.fields:
        raise ProtocolError("STATUS response has no state")
    _require_token(response.fields[0], "instrument state")


def status_state(response: OkResponse) -> str:
    validate_status_response(response)
    field = response.fields[0]
    state = field.split("=", 1)[1] if field.lower().startswith("state=") else field
    normalized = state.upper()
    if normalized not in {"UNAVAILABLE", "IDLE", "TARING", "RUNNING", "STOPPING"}:
        raise ProtocolError(f"STATUS response has unknown state {state!r}")
    return normalized


def validate_start_response(response: OkResponse, expected_run_id: str) -> None:
    if response.command != "START" or len(response.fields) != 2:
        raise ProtocolError("START response must contain run ID and device timestamp")
    run_id = _require_identifier(response.fields[0], "START run ID")
    _parse_int(
        response.fields[1], "START timestamp_us", minimum=0, maximum=UINT64_MAX
    )
    if run_id != expected_run_id:
        raise ProtocolError("START response run ID does not match the request")


def validate_mark_response(response: OkResponse, expected_marker_id: str) -> None:
    if response.command != "MARK" or len(response.fields) != 2:
        raise ProtocolError("MARK response must contain marker ID and device timestamp")
    marker_id = _require_identifier(response.fields[0], "MARK marker ID")
    _parse_int(
        response.fields[1], "MARK timestamp_us", minimum=0, maximum=UINT64_MAX
    )
    if marker_id != expected_marker_id:
        raise ProtocolError("MARK response marker ID does not match the request")


def validate_stop_response(response: OkResponse) -> None:
    if (
        response.command != "STOP"
        or len(response.fields) != 2
        or response.fields[0].upper() != "DRAINING"
    ):
        raise ProtocolError("STOP response must contain DRAINING and device timestamp")
    _parse_int(
        response.fields[1], "STOP timestamp_us", minimum=0, maximum=UINT64_MAX
    )


class LineTransport(Protocol):
    port_name: str

    def write_line(self, line: str) -> None: ...

    def read_line(self) -> str | None: ...

    def close(self) -> None: ...


class SerialLineTransport:
    """Newline transport that retains partial USB reads across timeouts."""

    def __init__(
        self,
        port_name: str,
        *,
        baudrate: int = 115200,
        read_timeout_seconds: float = 0.05,
    ) -> None:
        try:
            import serial
        except ImportError as exc:
            raise CaptureError(
                "pyserial is required; install host/requirements.txt"
            ) from exc
        try:
            self._serial = serial.Serial(
                port=port_name,
                baudrate=baudrate,
                timeout=read_timeout_seconds,
                write_timeout=1.0,
            )
        except serial.SerialException as exc:
            raise CaptureError(f"cannot open serial port {port_name}: {exc}") from exc
        self.port_name = port_name
        self._buffer = bytearray()

    def write_line(self, line: str) -> None:
        if "\r" in line or "\n" in line:
            raise CaptureError("command contains an embedded line break")
        try:
            self._serial.write((line + "\n").encode("ascii"))
            self._serial.flush()
        except (UnicodeEncodeError, OSError) as exc:
            raise CaptureError(f"serial write failed: {exc}") from exc

    def read_line(self) -> str | None:
        newline = self._buffer.find(b"\n")
        if newline >= 0:
            raw = bytes(self._buffer[: newline + 1])
            del self._buffer[: newline + 1]
            return raw.decode("ascii", errors="replace")

        try:
            waiting = int(self._serial.in_waiting)
            chunk = self._serial.read(min(max(waiting, 1), 4096))
        except OSError as exc:
            raise CaptureError(f"serial read failed: {exc}") from exc
        if chunk:
            self._buffer.extend(chunk)
            if len(self._buffer) > 4096:
                raw = bytes(self._buffer)
                self._buffer.clear()
                return raw.decode("ascii", errors="replace")
        newline = self._buffer.find(b"\n")
        if newline < 0:
            return None
        raw = bytes(self._buffer[: newline + 1])
        del self._buffer[: newline + 1]
        return raw.decode("ascii", errors="replace")

    def close(self) -> None:
        self._serial.close()


class CompactStatus:
    def __init__(self, *, quiet: bool, stream: TextIO = sys.stdout) -> None:
        self.quiet = quiet
        self.stream = stream
        self._interactive = bool(getattr(stream, "isatty", lambda: False)())
        self._last_update = 0.0

    def update(self, session: CaptureSession, now: float) -> None:
        if self.quiet or not self._interactive or now - self._last_update < 0.2:
            return
        last_sequence = "-" if session.last_sequence is None else session.last_sequence
        text = (
            f"samples={session.sample_count} last={last_sequence} "
            f"missing={session.sequence_missing_count} "
            f"malformed={session.malformed_record_count} "
            f"device_dropped={session.last_dropped_total}"
        )
        self.stream.write("\r" + text[:120].ljust(120))
        self.stream.flush()
        self._last_update = now

    def finish(self, session: CaptureSession) -> None:
        if self.quiet:
            return
        if self._interactive:
            self.stream.write("\r" + (" " * 120) + "\r")
        self.stream.write(
            f"Captured {session.sample_count} samples; "
            f"missing={session.sequence_missing_count}, "
            f"malformed={session.malformed_record_count}, "
            f"device_dropped={session.last_dropped_total}.\n"
        )
        self.stream.flush()


class CaptureRunner:
    """Coordinate commands and streaming while keeping parsing testable."""

    def __init__(
        self,
        transport: LineTransport,
        artifacts: CaptureArtifacts,
        *,
        clock: Callable[[], float] = time.monotonic,
        command_timeout_seconds: float = 3.0,
        drain_timeout_seconds: float = 5.0,
        quiet: bool = False,
    ) -> None:
        self.transport = transport
        self.artifacts = artifacts
        self.clock = clock
        self.command_timeout_seconds = command_timeout_seconds
        self.drain_timeout_seconds = drain_timeout_seconds
        self.session = CaptureSession(artifacts)
        self.responses: dict[str, Any] = {}
        self.status = CompactStatus(quiet=quiet)

    def _read_record(self) -> ProtocolRecord | None:
        line = self.transport.read_line()
        if line is None:
            return None
        try:
            return parse_record(line)
        except ProtocolError as exc:
            self.session.record_malformed(line.rstrip("\r\n"), exc)
            return None

    def _accept_async(self, record: ProtocolRecord | None) -> None:
        if record is not None:
            self.session.accept(record, self.clock())

    def transact(self, command_line: str) -> OkResponse:
        command = command_line.split(" ", 1)[0].upper()
        self.transport.write_line(command_line)
        deadline = self.clock() + self.command_timeout_seconds
        while self.clock() < deadline:
            record = self._read_record()
            if isinstance(record, OkResponse) and record.command == command:
                return record
            if isinstance(record, ErrorResponse) and record.command == command:
                raise CommandRejected(record)
            self._accept_async(record)
            self.status.update(self.session, self.clock())
        raise ProtocolTimeout(f"timed out waiting for {command} response")

    def wait_for_command_phase(
        self,
        command: str,
        phase: str,
        *,
        timeout_seconds: float,
    ) -> OkResponse:
        """Wait for a later phase of an asynchronous command response."""

        expected_command = command.upper()
        expected_phase = phase.upper()
        deadline = self.clock() + timeout_seconds
        while self.clock() < deadline:
            record = self._read_record()
            if isinstance(record, OkResponse) and record.command == expected_command:
                if record.fields and record.fields[0].upper() == expected_phase:
                    return record
                self._accept_async(record)
            elif (
                isinstance(record, ErrorResponse)
                and record.command == expected_command
            ):
                raise CommandRejected(record)
            else:
                self._accept_async(record)
            self.status.update(self.session, self.clock())
        raise ProtocolTimeout(
            f"timed out waiting for {expected_command} {expected_phase} response"
        )

    def wait_for_final_summary(self) -> FinalSummaryRecord:
        deadline = self.clock() + self.drain_timeout_seconds
        while self.clock() < deadline:
            if self.session.final_summary is not None:
                return self.session.final_summary
            self._accept_async(self._read_record())
            self.status.update(self.session, self.clock())
        raise ProtocolTimeout("timed out waiting for final summary after STOP")

    def poll_available(self, maximum_records: int = 512) -> int:
        """Drain currently available asynchronous records without a deadline."""

        if maximum_records <= 0:
            raise ValueError("maximum_records must be positive")
        accepted = 0
        for _ in range(maximum_records):
            record = self._read_record()
            if record is None:
                break
            self._accept_async(record)
            accepted += 1
        self.status.update(self.session, self.clock())
        return accepted

    def _wait_for_recovery_final_summary(self) -> FinalSummaryRecord:
        deadline = self.clock() + self.drain_timeout_seconds
        while self.clock() < deadline:
            if self.session.unclaimed_final_summaries:
                return self.session.unclaimed_final_summaries.pop()
            record = self._read_record()
            if isinstance(record, FinalSummaryRecord):
                return record
            self._accept_async(record)
            self.status.update(self.session, self.clock())
        raise ProtocolTimeout("timed out draining capture left by an earlier host")

    def recover_reconnected_capture(self, status: OkResponse) -> OkResponse:
        state = status_state(status)
        if state == "RUNNING":
            self.session.unclaimed_final_summaries.clear()
            stop = self.transact("STOP")
            validate_stop_response(stop)
            self.responses["reconnect_stop"] = response_to_json(stop)
            recovered_final = self._wait_for_recovery_final_summary()
            self.responses["reconnect_final_summary"] = response_to_json(
                recovered_final
            )
        elif state == "STOPPING":
            recovered_final = self._wait_for_recovery_final_summary()
            self.responses["reconnect_final_summary"] = response_to_json(
                recovered_final
            )
        elif state != "IDLE":
            raise ProtocolError(
                f"device must be IDLE before capture setup; reported {state}"
            )
        else:
            return status

        recovered_status = self.transact("STATUS")
        if status_state(recovered_status) != "IDLE":
            raise ProtocolError("device did not return to IDLE after reconnect drain")
        return recovered_status

    def _capture_until(
        self,
        deadline: float,
        markers: list[ScheduledMarker],
    ) -> None:
        pending = list(markers)
        while self.clock() < deadline:
            elapsed = self.clock() - (self.session.capture_start_monotonic or 0.0)
            while pending and elapsed >= pending[0].elapsed_seconds:
                marker = pending.pop(0)
                response = self.transact(f"MARK {marker.marker_id}")
                validate_mark_response(response, marker.marker_id)
                self.responses.setdefault("markers", []).append(
                    {
                        "requested_elapsed_seconds": marker.elapsed_seconds,
                        "marker_id": marker.marker_id,
                        "response": response_to_json(response),
                    }
                )
            self._accept_async(self._read_record())
            self.status.update(self.session, self.clock())

    def run(
        self,
        *,
        run_id: str,
        sample_rate_sps: int,
        gain: int,
        tare_sample_count: int,
        duration_seconds: float,
        markers: list[ScheduledMarker],
        calibration: dict[str, Any],
    ) -> dict[str, Any]:
        started_utc = datetime.now(timezone.utc)
        capture_started = False
        stop_sent = False
        interrupted = False
        failure: Exception | None = None
        tare_result: dict[str, Any] | None = None

        try:
            info = self.transact("INFO")
            validate_info_response(info)
            self.responses["info"] = response_to_json(info)
            status_on_connect = self.transact("STATUS")
            status_on_connect = self.recover_reconnected_capture(status_on_connect)
            self.responses["status_on_connect"] = response_to_json(status_on_connect)
            configuration = self.transact(f"CONFIG {sample_rate_sps} {gain}")
            validate_config_response(configuration, sample_rate_sps, gain)
            self.responses["configuration"] = response_to_json(configuration)
            tare_started = self.transact(f"TARE {tare_sample_count}")
            validate_tare_started_response(tare_started, tare_sample_count)
            tare_timeout_seconds = max(
                self.command_timeout_seconds,
                ((tare_sample_count / sample_rate_sps) * 2.5) + 3.0,
            )
            tare_complete = self.wait_for_command_phase(
                "TARE", "COMPLETE", timeout_seconds=tare_timeout_seconds
            )
            tare_result = parse_tare_complete(tare_complete)
            if tare_result["sample_count"] != tare_sample_count:
                raise ProtocolError(
                    f"TARE completed {tare_result['sample_count']} samples; "
                    f"requested {tare_sample_count}"
                )
            self.responses["tare"] = {
                "timeout_seconds": tare_timeout_seconds,
                "started": response_to_json(tare_started),
                "complete": response_to_json(tare_complete),
                "result": tare_result,
            }
            status_before_start = self.transact("STATUS")
            validate_status_response(status_before_start)
            self.responses["status_before_start"] = response_to_json(
                status_before_start
            )

            capture_start = self.clock()
            capture_started = True
            start = self.transact(f"START {run_id}")
            validate_start_response(start, run_id)
            self.session.start_capture(capture_start)
            self.responses["start"] = response_to_json(start)
            self._capture_until(capture_start + duration_seconds, markers)

            stop_sent = True
            self.session.expect_final_summary(run_id)
            stop = self.transact("STOP")
            validate_stop_response(stop)
            self.responses["stop"] = response_to_json(stop)
            final_summary = self.wait_for_final_summary()
            if final_summary.run_id != run_id:
                raise ProtocolError(
                    f"final summary run ID {final_summary.run_id!r} does not "
                    f"match requested run ID {run_id!r}"
                )
            status_after_stop = self.transact("STATUS")
            validate_status_response(status_after_stop)
            self.responses["status_after_stop"] = response_to_json(
                status_after_stop
            )
        except KeyboardInterrupt:
            interrupted = True
        except Exception as exc:  # Metadata is still preserved for partial captures.
            failure = exc
        finally:
            if capture_started and not stop_sent:
                try:
                    stop_sent = True
                    self.session.expect_final_summary(run_id)
                    stop = self.transact("STOP")
                    validate_stop_response(stop)
                    self.responses["stop"] = response_to_json(stop)
                except Exception as stop_error:
                    self.responses["stop_error"] = str(stop_error)
            if stop_sent and self.session.final_summary is None:
                try:
                    self.wait_for_final_summary()
                except Exception as drain_error:
                    self.responses["drain_error"] = str(drain_error)

            completed_utc = datetime.now(timezone.utc)
            final_summary = (
                None
                if self.session.final_summary is None
                else asdict(self.session.final_summary)
            )
            metadata = {
                "schema_version": 1,
                "capture": {
                    "run_id": run_id,
                    "port": self.transport.port_name,
                    "started_utc": started_utc.isoformat(),
                    "completed_utc": completed_utc.isoformat(),
                    "requested_duration_seconds": duration_seconds,
                    "interrupted": interrupted,
                    "complete": self.session.final_summary is not None,
                    "failure": None if failure is None else str(failure),
                },
                "requested_device_configuration": {
                    "sample_rate_sps": sample_rate_sps,
                    "gain": gain,
                    "tare_sample_count": tare_sample_count,
                },
                "calibration": {**calibration, "tare": tare_result},
                "scheduled_markers": [asdict(marker) for marker in markers],
                "responses": self.responses,
                "host_summary": self.session.summary_json(),
                "device_final_summary": final_summary,
                "artifacts": {
                    "metadata": self.artifacts.metadata_path.name,
                    "telemetry": self.artifacts.csv_path.name,
                },
            }
            self.artifacts.finalize(metadata)
            self.status.finish(self.session)

        if failure is not None:
            raise CaptureError(
                f"capture failed ({failure}); partial artifacts: "
                f"{self.artifacts.output_directory}"
            ) from failure
        if interrupted:
            raise CaptureInterrupted(
                "capture interrupted after graceful STOP; artifacts: "
                f"{self.artifacts.output_directory}"
            )
        return metadata


def locate_serial_port(explicit_port: str | None) -> str:
    if explicit_port:
        return explicit_port
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise CaptureError(
            "pyserial is required for automatic port discovery"
        ) from exc
    candidates = [
        port
        for port in list_ports.comports()
        if port.vid is not None
        or "USB" in (port.description or "").upper()
        or "RP2040" in (port.description or "").upper()
    ]
    if len(candidates) == 1:
        return str(candidates[0].device)
    if not candidates:
        raise CaptureError("no USB serial port found; pass --port explicitly")
    descriptions = ", ".join(
        f"{port.device} ({port.description})" for port in candidates
    )
    raise CaptureError(
        f"multiple USB serial ports found: {descriptions}; pass --port explicitly"
    )


def validate_identifier(value: str, name: str) -> str:
    if IDENTIFIER_PATTERN.fullmatch(value) is None:
        raise argparse.ArgumentTypeError(
            f"{name} must be 1-31 ASCII letters, digits, '_', '.', or '-'"
        )
    return value


def parse_marker_argument(value: str) -> ScheduledMarker:
    try:
        elapsed_text, marker_id = value.split(":", 1)
        elapsed = float(elapsed_text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            "marker must have the form ELAPSED_SECONDS:MARKER_ID"
        ) from exc
    if not math.isfinite(elapsed) or elapsed < 0.0:
        raise argparse.ArgumentTypeError("marker elapsed time must be non-negative")
    return ScheduledMarker(
        elapsed_seconds=elapsed,
        marker_id=validate_identifier(marker_id, "marker ID"),
    )


def _positive_float(value: str) -> float:
    parsed = float(value)
    if not math.isfinite(parsed) or parsed <= 0.0:
        raise argparse.ArgumentTypeError("value must be finite and positive")
    return parsed


def _nonnegative_float(value: str) -> float:
    parsed = float(value)
    if not math.isfinite(parsed) or parsed < 0.0:
        raise argparse.ArgumentTypeError("value must be finite and non-negative")
    return parsed


def _positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return parsed


def _default_run_id() -> str:
    return datetime.now(timezone.utc).strftime("run-%Y%m%dT%H%M%SZ")


def _allocate_output_directory(root: Path, run_id: str) -> Path:
    safe_run_id = re.sub(r"[^A-Za-z0-9_.-]+", "_", run_id)
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    candidate = root / f"{timestamp}_{safe_run_id}"
    suffix = 1
    while candidate.exists():
        candidate = root / f"{timestamp}_{safe_run_id}_{suffix:02d}"
        suffix += 1
    return candidate


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Capture raw NAU7802 telemetry to metadata.json and "
            "force_telemetry.csv"
        )
    )
    parser.add_argument("--port", help="USB CDC port; auto-detect when omitted")
    parser.add_argument("--baudrate", type=_positive_int, default=115200)
    parser.add_argument(
        "--sample-rate-sps", type=int, choices=(10, 20, 40, 80, 320), default=320
    )
    parser.add_argument(
        "--gain", type=int, choices=(1, 2, 4, 8, 16, 32, 64, 128), default=128
    )
    parser.add_argument("--tare-samples", type=_positive_int, default=320)
    parser.add_argument(
        "--run-id",
        type=lambda value: validate_identifier(value, "run ID"),
        default=_default_run_id(),
    )
    parser.add_argument(
        "--duration-seconds", type=_positive_float, default=1.0
    )
    parser.add_argument(
        "--marker-at",
        action="append",
        type=parse_marker_argument,
        default=[],
        metavar="SECONDS:ID",
        help="insert a marker at capture elapsed time; may be repeated",
    )
    parser.add_argument("--output-root", type=Path, default=Path("captures"))
    parser.add_argument(
        "--command-timeout-seconds", type=_positive_float, default=3.0
    )
    parser.add_argument(
        "--drain-timeout-seconds", type=_positive_float, default=5.0
    )
    parser.add_argument(
        "--connect-delay-seconds", type=_nonnegative_float, default=1.5
    )
    parser.add_argument("--counts-per-newton", type=_positive_float)
    parser.add_argument("--force-sign", type=int, choices=(-1, 1), default=1)
    parser.add_argument("--lever-radius-m", type=_positive_float)
    parser.add_argument("--quiet", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_argument_parser()
    args = parser.parse_args(argv)
    markers = sorted(args.marker_at, key=lambda marker: marker.elapsed_seconds)
    if any(marker.elapsed_seconds >= args.duration_seconds for marker in markers):
        parser.error("each --marker-at time must be less than --duration-seconds")

    artifacts: CaptureArtifacts | None = None
    transport: SerialLineTransport | None = None
    try:
        port = locate_serial_port(args.port)
        output_directory = _allocate_output_directory(args.output_root, args.run_id)
        artifacts = CaptureArtifacts(output_directory)
        transport = SerialLineTransport(port, baudrate=args.baudrate)
        if args.connect_delay_seconds:
            time.sleep(args.connect_delay_seconds)
        runner = CaptureRunner(
            transport,
            artifacts,
            command_timeout_seconds=args.command_timeout_seconds,
            drain_timeout_seconds=args.drain_timeout_seconds,
            quiet=args.quiet,
        )
        runner.run(
            run_id=args.run_id,
            sample_rate_sps=args.sample_rate_sps,
            gain=args.gain,
            tare_sample_count=args.tare_samples,
            duration_seconds=args.duration_seconds,
            markers=markers,
            calibration={
                "counts_per_newton": args.counts_per_newton,
                "force_sign": args.force_sign,
                "lever_radius_m": args.lever_radius_m,
                "application": (
                    "metadata_only; force/torque derivation is not embedded in raw CSV"
                ),
            },
        )
        if not args.quiet:
            print(f"Artifacts: {output_directory}")
        return 0
    except CaptureInterrupted as exc:
        print(f"interrupted: {exc}", file=sys.stderr)
        return 130
    except CaptureError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    finally:
        if transport is not None:
            transport.close()
        if artifacts is not None and not artifacts._closed:
            artifacts.close_without_metadata()


if __name__ == "__main__":
    raise SystemExit(main())
