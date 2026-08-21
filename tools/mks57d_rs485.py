#!/usr/bin/env python3
"""MKS57D native-v1 RS-485 product service and diagnostic console."""

from __future__ import annotations

import argparse
import json
import struct
import sys
import time
from dataclasses import dataclass
from typing import Any


PROTOCOL_VERSION = 1
DEFAULT_ADDRESS = 1
MESSAGE_REQUEST = 1
MESSAGE_RESPONSE = 2

COMMAND_PING = 0x0001
COMMAND_GET_IDENTITY = 0x0002
COMMAND_GET_COMMISSIONING_STATUS = 0x0100
COMMAND_CONFIGURE_CURRENT_TEST = 0x0101
COMMAND_START_CURRENT_TEST = 0x0102
COMMAND_STOP_CURRENT_TEST = 0x0103
COMMAND_GET_BOOT_STATUS = 0x0104
COMMAND_GET_ENCODER_STATUS = 0x0105
COMMAND_GET_CURRENT_TRACE = 0x0106
COMMAND_START_ALIGNMENT = 0x0200
COMMAND_GET_ALIGNMENT_STATUS = 0x0201
COMMAND_STOP_DRIVE = 0x0202
COMMAND_GET_CONFIGURATION_STATUS = 0x0300
COMMAND_SAVE_CONFIGURATION = 0x0301
COMMAND_CLEAR_CALIBRATION = 0x0302
COMMAND_START_ALIGNED_TORQUE = 0x0400
COMMAND_GET_ALIGNED_TORQUE_STATUS = 0x0401

STATUS_NAMES = {
    0: "ok",
    1: "unknown_command",
    2: "invalid_payload",
    3: "unavailable",
    4: "internal_error",
}

FLAG_NAMES = {
    0: "adc_ready",
    1: "adc_snapshot_valid",
    2: "adc_calibration_ready",
    3: "current_loop_initialized",
    4: "bridge_ready",
    5: "authority_active",
    6: "backend_active",
    7: "remote_authority",
    8: "remote_start_pending",
    9: "remote_stop_pending",
    10: "fault_present",
}

INPUT_BITS = {
    0: "enter",
    1: "menu",
    2: "next",
    3: "m_in1",
    4: "m_in2",
    5: "step",
    6: "direction",
    7: "enable",
}

FAULT_NAMES = {
    0: "invalid_sample",
    1: "overcurrent_a",
    2: "overcurrent_b",
    3: "invalid_reference",
    4: "invalid_output",
    16: "adc",
    17: "pwm",
    18: "deadline",
    19: "internal",
}

LEG_VALUES = {"A1": 0, "A2": 1, "B1": 2, "B2": 3}
LEG_NAMES = {value: name for name, value in LEG_VALUES.items()}
ADC_STATUS_NAMES = {
    0: "ok",
    1: "invalid_argument",
    2: "not_ready",
    3: "unsupported_clock",
    4: "clock_timeout",
    5: "power_timeout",
    6: "calibration_timeout",
    7: "busy",
    8: "conversion_timeout",
    9: "data_out_of_range",
    10: "dma_error",
    11: "no_sample",
}

RESET_FLAG_NAMES = {
    23: "ram",
    25: "mmu",
    26: "pin",
    27: "power_on",
    28: "software",
    29: "independent_watchdog",
    30: "window_watchdog",
    31: "low_power",
}

ENCODER_STATUS_NAMES = {
    0: "not_attempted",
    1: "ok",
    2: "invalid_argument",
    3: "transport_error",
    4: "parity_error",
}

SPI_STATUS_NAMES = {
    0: "ok",
    1: "invalid_argument",
    2: "not_ready",
    3: "bus_busy",
    4: "transmit_timeout",
    5: "receive_timeout",
    6: "complete_timeout",
    7: "peripheral_error",
}

ENCODER_ESTIMATOR_FLAG_NAMES = {
    0: "estimator_ready",
    1: "alignment_valid",
    2: "electrical_phase_valid",
}

ENCODER_ESTIMATOR_FAULT_NAMES = {
    0: "invalid_sample",
}

ALIGNMENT_STATE_NAMES = {
    0: "idle",
    1: "phase_zero_settle",
    2: "phase_zero_sample",
    3: "phase_quarter_settle",
    4: "phase_quarter_sample",
    5: "return_zero_settle",
    6: "return_zero_sample",
    7: "complete",
    8: "failed",
    9: "aborted",
}

ALIGNMENT_RESULT_NAMES = {
    0: "none",
    1: "success",
    2: "aborted",
    3: "deadline",
    4: "encoder_invalid",
    5: "backend_inactive",
    6: "current_tracking",
    7: "encoder_unstable",
    8: "geometry",
    9: "closure",
}

ALIGNMENT_FLAG_NAMES = {
    0: "active",
    1: "calibration_valid",
    2: "authority_active",
    3: "backend_active",
}

CONFIGURATION_FLAG_NAMES = {
    0: "store_initialized",
    1: "record_valid",
    2: "stored_calibration_valid",
    3: "active_calibration_valid",
    4: "active_matches_record",
    5: "slot0_valid",
    6: "slot1_valid",
    7: "write_supported",
}

CONFIGURATION_RESULT_NAMES = {
    0: "ok",
    1: "empty",
    2: "invalid_argument",
    3: "io_error",
    4: "verify_error",
}

TORQUE_STATE_NAMES = {
    0: "idle",
    1: "ramping",
    2: "holding",
    3: "complete",
    4: "stopped",
    5: "failed",
}

TORQUE_RESULT_NAMES = {
    0: "none",
    1: "deadline",
    2: "stopped",
    3: "phase_invalid",
    4: "feedback_timing",
    5: "overspeed",
    6: "overacceleration",
    7: "backend_inactive",
    8: "reference_rejected",
}

TORQUE_FLAG_NAMES = {
    0: "active",
    1: "authority_active",
    2: "backend_active",
    3: "alignment_valid",
    4: "phase_valid",
    5: "demand_at_target",
}

TORQUE_FAULT_NAMES = {
    0: "phase_invalid",
    1: "feedback_timing",
    2: "overspeed",
    3: "overacceleration",
    4: "backend_inactive",
    5: "reference_rejected",
}

STATUS_BODY = struct.Struct(">BIBBBBIIHHHHhhhhhhHHHHHHHHIIBB")
CURRENT_TRACE_BODY = struct.Struct(">BHHIhhhhhh")
ENCODER_STATUS_V1_BODY = struct.Struct(">BBBHBIII")
ENCODER_STATUS_V2_BODY = struct.Struct(">BBBHBIIIBiiIIHbIII")
ALIGNMENT_STATUS_BODY = struct.Struct(">BBBBHHHHHhhbHIIHHHHIIIHHHH")
CONFIGURATION_STATUS_BODY = struct.Struct(">BBBBHIHHHHhbHHHHhb")
ALIGNED_TORQUE_STATUS_BODY = struct.Struct(">BBBBIhhhhIiiIIHHiiHIII")
COUNTS_TO_MILLIAMPERES = (
    3.3 / 4095.0 / (6.65 * 0.020) * 1000.0
)


class ProtocolError(RuntimeError):
    pass


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (
                crc << 1
            ) & 0xFFFF
    return crc


def cobs_encode(data: bytes) -> bytes:
    output = bytearray([0])
    code_index = 0
    code = 1
    for byte in data:
        if byte == 0:
            output[code_index] = code
            code_index = len(output)
            output.append(0)
            code = 1
        else:
            output.append(byte)
            code += 1
            if code == 0xFF:
                output[code_index] = code
                code_index = len(output)
                output.append(0)
                code = 1
    output[code_index] = code
    return bytes(output)


def cobs_decode(data: bytes) -> bytes:
    output = bytearray()
    index = 0
    while index < len(data):
        code = data[index]
        if code == 0:
            raise ProtocolError("zero byte inside COBS frame")
        index += 1
        end = index + code - 1
        if end > len(data):
            raise ProtocolError("truncated COBS frame")
        output.extend(data[index:end])
        index = end
        if code != 0xFF and index < len(data):
            output.append(0)
    return bytes(output)


def encode_request(address: int, sequence: int, command: int, payload: bytes) -> bytes:
    if len(payload) > 64:
        raise ProtocolError("payload exceeds 64 bytes")
    decoded = struct.pack(
        ">BBHBHB",
        PROTOCOL_VERSION,
        address,
        sequence,
        MESSAGE_REQUEST,
        command,
        len(payload),
    ) + payload
    decoded += struct.pack(">H", crc16_ccitt_false(decoded))
    return cobs_encode(decoded) + b"\x00"


@dataclass(frozen=True)
class Response:
    address: int
    sequence: int
    command: int
    payload: bytes


def decode_response(wire: bytes) -> Response:
    if not wire.endswith(b"\x00"):
        raise ProtocolError("response has no delimiter")
    decoded = cobs_decode(wire[:-1])
    if len(decoded) < 10:
        raise ProtocolError("response is too short")
    version, address, sequence, message_type, command, length = struct.unpack(
        ">BBHBHB", decoded[:8]
    )
    if version != PROTOCOL_VERSION or message_type != MESSAGE_RESPONSE:
        raise ProtocolError("unexpected response version or message type")
    if len(decoded) != 8 + length + 2:
        raise ProtocolError("response length field does not match frame")
    expected_crc = struct.unpack(">H", decoded[-2:])[0]
    if crc16_ccitt_false(decoded[:-2]) != expected_crc:
        raise ProtocolError("response CRC mismatch")
    return Response(address, sequence, command, decoded[8:-2])


class Client:
    def __init__(self, serial_port: Any, address: int):
        self.serial = serial_port
        self.address = address
        self.sequence = 1

    def transact(self, command: int, payload: bytes = b"") -> bytes:
        sequence = self.sequence
        self.sequence = (self.sequence + 1) & 0xFFFF
        if self.sequence == 0:
            self.sequence = 1
        self.serial.write(encode_request(self.address, sequence, command, payload))
        self.serial.flush()
        wire = self.serial.read_until(b"\x00", 76)
        if not wire:
            raise ProtocolError("response timeout")
        response = decode_response(wire)
        if (
            response.address != self.address
            or response.sequence != sequence
            or response.command != command
        ):
            raise ProtocolError("response identity does not match request")
        if not response.payload:
            raise ProtocolError("response has no status byte")
        status = response.payload[0]
        if status != 0:
            raise ProtocolError(
                f"device returned {STATUS_NAMES.get(status, f'status_{status}')}"
            )
        return response.payload[1:]


def active_names(value: int, names: dict[int, str]) -> list[str]:
    return [name for bit, name in names.items() if value & (1 << bit)]


def input_state(levels: int) -> dict[str, bool]:
    return {name: not bool(levels & (1 << bit)) for bit, name in INPUT_BITS.items()}


def parse_status(body: bytes) -> dict[str, Any]:
    if len(body) != STATUS_BODY.size:
        raise ProtocolError(
            f"commissioning status is {len(body)} bytes, expected {STATUS_BODY.size}"
        )
    values = iter(STATUS_BODY.unpack(body))
    schema = next(values)
    flags = next(values)
    raw_inputs = next(values)
    debounced_inputs = next(values)
    adc_status = next(values)
    selected_leg = next(values)
    fault_flags = next(values)
    sample_count = next(values)
    current_a_raw = next(values)
    current_b_raw = next(values)
    current_a_zero = next(values)
    current_b_zero = next(values)
    reference_a = next(values)
    reference_b = next(values)
    measured_a = next(values)
    measured_b = next(values)
    voltage_a = next(values)
    voltage_b = next(values)
    duties = [next(values) for _ in range(4)]
    amplitude = next(values)
    maximum_amplitude = next(values)
    hard_limit = next(values)
    voltage_limit = next(values)
    frequency_millihz = next(values)
    remaining_millis = next(values)
    retained_panic = next(values)
    watchdog_reset = next(values)

    return {
        "schema": schema,
        "reset": {
            "retained_panic": retained_panic,
            "watchdog_reset": bool(watchdog_reset),
        },
        "flags_hex": f"0x{flags:08X}",
        "flags": active_names(flags, FLAG_NAMES),
        "inputs": {
            "raw_levels_hex": f"0x{raw_inputs:02X}",
            "raw_pressed": input_state(raw_inputs),
            "debounced_levels_hex": f"0x{debounced_inputs:02X}",
            "debounced_pressed": input_state(debounced_inputs),
        },
        "adc": {
            "status": ADC_STATUS_NAMES.get(adc_status, f"status_{adc_status}"),
            "current_a_raw": current_a_raw,
            "current_b_raw": current_b_raw,
            "current_a_zero_raw": current_a_zero,
            "current_b_zero_raw": current_b_zero,
        },
        "test": {
            "selected_leg": LEG_NAMES.get(selected_leg, f"leg_{selected_leg}"),
            "amplitude_counts": amplitude,
            "amplitude_nominal_milliamperes": round(
                amplitude * COUNTS_TO_MILLIAMPERES, 1
            ),
            "maximum_amplitude_counts": maximum_amplitude,
            "frequency_hz": frequency_millihz / 1000.0,
            "remote_run_remaining_millis": remaining_millis,
        },
        "loop": {
            "fault_flags_hex": f"0x{fault_flags:08X}",
            "faults": active_names(fault_flags, FAULT_NAMES),
            "sample_count": sample_count,
            "reference_counts": {"a": reference_a, "b": reference_b},
            "measured_counts": {"a": measured_a, "b": measured_b},
            "measured_nominal_milliamperes": {
                "a": round(measured_a * COUNTS_TO_MILLIAMPERES, 1),
                "b": round(measured_b * COUNTS_TO_MILLIAMPERES, 1),
            },
            "phase_voltage_permille": {"a": voltage_a, "b": voltage_b},
            "duties_permille": {
                "a1": duties[0],
                "a2": duties[1],
                "b1": duties[2],
                "b2": duties[3],
            },
            "hard_current_limit_counts": hard_limit,
            "phase_voltage_limit_permille": voltage_limit,
        },
    }


def query_status(client: Client) -> dict[str, Any]:
    return parse_status(client.transact(COMMAND_GET_COMMISSIONING_STATUS))


def query_encoder(client: Client) -> dict[str, Any]:
    body = client.transact(COMMAND_GET_ENCODER_STATUS)
    if len(body) not in {
        ENCODER_STATUS_V1_BODY.size,
        ENCODER_STATUS_V2_BODY.size,
    }:
        raise ProtocolError("encoder-status response has an unexpected length")
    (
        schema,
        status,
        transport_status,
        angle_raw,
        flags,
        sample_count,
        error_count,
        last_attempt_millis,
    ) = ENCODER_STATUS_V1_BODY.unpack(
        body[: ENCODER_STATUS_V1_BODY.size]
    )
    result = {
        "schema": schema,
        "status": ENCODER_STATUS_NAMES.get(status, f"status_{status}"),
        "transport_status": SPI_STATUS_NAMES.get(
            transport_status, f"status_{transport_status}"
        ),
        "angle_raw": angle_raw,
        "angle_degrees": round(angle_raw * 360.0 / 16384.0, 3),
        "flags_hex": f"0x{flags:02X}",
        "no_magnet": bool(flags & 0x01),
        "over_speed": bool(flags & 0x02),
        "sample_count": sample_count,
        "error_count": error_count,
        "last_attempt_millis": last_attempt_millis,
    }
    if len(body) == ENCODER_STATUS_V2_BODY.size:
        (
            _schema,
            _status,
            _transport_status,
            _angle_raw,
            _flags,
            _sample_count,
            _error_count,
            _last_attempt_millis,
            estimator_flags,
            position_q16_16,
            velocity_q16_16,
            estimator_timestamp_us,
            estimator_fault_flags,
            alignment_zero_raw,
            alignment_direction,
            electrical_phase_q32,
            estimator_sample_interval_us,
            estimator_maximum_sample_interval_us,
        ) = ENCODER_STATUS_V2_BODY.unpack(body)
        result["estimator"] = {
            "flags": active_names(
                estimator_flags, ENCODER_ESTIMATOR_FLAG_NAMES
            ),
            "flags_hex": f"0x{estimator_flags:02X}",
            "faults": active_names(
                estimator_fault_flags,
                ENCODER_ESTIMATOR_FAULT_NAMES,
            ),
            "fault_flags_hex": f"0x{estimator_fault_flags:08X}",
            "position_revolutions": round(position_q16_16 / 65536.0, 6),
            "velocity_revolutions_per_second": round(
                velocity_q16_16 / 65536.0, 6
            ),
            "timestamp_us": estimator_timestamp_us,
            "sample_interval_us": estimator_sample_interval_us,
            "maximum_sample_interval_us":
                estimator_maximum_sample_interval_us,
        }
        result["alignment"] = {
            "valid": bool(
                estimator_flags & (1 << 1)
            ),
            "electrical_zero_raw": alignment_zero_raw,
            "encoder_direction": alignment_direction,
            "electrical_phase_turns": round(
                electrical_phase_q32 / 4294967296.0, 8
            ),
            "electrical_phase_degrees": round(
                electrical_phase_q32 * 360.0 / 4294967296.0, 4
            ),
        }
    return result


def query_current_trace_sample(client: Client, index: int) -> dict[str, Any]:
    body = client.transact(
        COMMAND_GET_CURRENT_TRACE,
        struct.pack(">H", index),
    )
    if len(body) != CURRENT_TRACE_BODY.size:
        raise ProtocolError("current-trace response has an unexpected length")
    (
        schema,
        captured_sample_count,
        sample_index,
        loop_sample_count,
        reference_a,
        reference_b,
        measured_a,
        measured_b,
        voltage_a,
        voltage_b,
    ) = CURRENT_TRACE_BODY.unpack(body)
    if sample_index != index:
        raise ProtocolError("current-trace response index does not match request")
    return {
        "schema": schema,
        "captured_sample_count": captured_sample_count,
        "sample_index": sample_index,
        "loop_sample_count": loop_sample_count,
        "reference_counts": {"a": reference_a, "b": reference_b},
        "measured_counts": {"a": measured_a, "b": measured_b},
        "phase_voltage_permille": {"a": voltage_a, "b": voltage_b},
    }


def read_current_trace(client: Client) -> list[dict[str, Any]]:
    first = query_current_trace_sample(client, 0)
    samples = [first]
    expected_count = first["captured_sample_count"]
    for index in range(1, expected_count):
        sample = query_current_trace_sample(client, index)
        if sample["captured_sample_count"] != expected_count:
            raise ProtocolError("current-trace sample count changed while reading")
        samples.append(sample)
    first_loop_sample = samples[0]["loop_sample_count"]
    for sample in samples:
        sample["time_seconds"] = round(
            (sample["loop_sample_count"] - first_loop_sample) / 20000.0,
            7,
        )
    return samples


def query_alignment(client: Client) -> dict[str, Any]:
    body = client.transact(COMMAND_GET_ALIGNMENT_STATUS)
    if len(body) != ALIGNMENT_STATUS_BODY.size:
        raise ProtocolError("alignment-status response has an unexpected length")
    (
        schema,
        state,
        result,
        flags,
        alignment_current_counts,
        phase_zero_raw,
        phase_quarter_raw,
        return_zero_raw,
        observed_quarter_step_counts,
        quarter_step_error_counts,
        closure_error_counts,
        encoder_direction,
        active_sample_count,
        elapsed_millis,
        remaining_millis,
        minimum_current_counts,
        maximum_current_counts,
        expected_quarter_step_counts,
        maximum_quarter_step_error_counts,
        settle_duration_millis,
        sample_duration_millis,
        maximum_duration_millis,
        minimum_sample_count,
        maximum_sample_span_counts,
        maximum_closure_error_counts,
        maximum_current_error_counts,
    ) = ALIGNMENT_STATUS_BODY.unpack(body)
    return {
        "schema": schema,
        "state": ALIGNMENT_STATE_NAMES.get(state, f"state_{state}"),
        "result": ALIGNMENT_RESULT_NAMES.get(result, f"result_{result}"),
        "flags_hex": f"0x{flags:02X}",
        "flags": active_names(flags, ALIGNMENT_FLAG_NAMES),
        "alignment_current_counts": alignment_current_counts,
        "alignment_current_nominal_milliamperes": round(
            alignment_current_counts * COUNTS_TO_MILLIAMPERES, 1
        ),
        "phase_zero_raw": phase_zero_raw,
        "phase_quarter_raw": phase_quarter_raw,
        "return_zero_raw": return_zero_raw,
        "observed_quarter_step_counts": observed_quarter_step_counts,
        "quarter_step_error_counts": quarter_step_error_counts,
        "closure_error_counts": closure_error_counts,
        "encoder_direction": encoder_direction,
        "active_sample_count": active_sample_count,
        "elapsed_millis": elapsed_millis,
        "remaining_millis": remaining_millis,
        "policy": {
            "minimum_current_counts": minimum_current_counts,
            "minimum_current_nominal_milliamperes": round(
                minimum_current_counts * COUNTS_TO_MILLIAMPERES, 1
            ),
            "maximum_current_counts": maximum_current_counts,
            "maximum_current_nominal_milliamperes": round(
                maximum_current_counts * COUNTS_TO_MILLIAMPERES, 1
            ),
            "expected_quarter_step_counts": expected_quarter_step_counts,
            "maximum_quarter_step_error_counts": (
                maximum_quarter_step_error_counts
            ),
            "settle_duration_millis": settle_duration_millis,
            "sample_duration_millis": sample_duration_millis,
            "maximum_duration_millis": maximum_duration_millis,
            "minimum_sample_count": minimum_sample_count,
            "maximum_sample_span_counts": maximum_sample_span_counts,
            "maximum_closure_error_counts": (
                maximum_closure_error_counts
            ),
            "maximum_current_error_counts": maximum_current_error_counts,
        },
    }


def query_configuration(client: Client) -> dict[str, Any]:
    body = client.transact(COMMAND_GET_CONFIGURATION_STATUS)
    if len(body) != CONFIGURATION_STATUS_BODY.size:
        raise ProtocolError(
            "configuration-status response has an unexpected length"
        )
    (
        schema,
        flags,
        last_result,
        active_slot,
        record_schema,
        generation,
        stored_counts_per_revolution,
        stored_electrical_cycles,
        stored_zero,
        stored_quarter_step,
        stored_quarter_error,
        stored_direction,
        active_counts_per_revolution,
        active_electrical_cycles,
        active_zero,
        active_quarter_step,
        active_quarter_error,
        active_direction,
    ) = CONFIGURATION_STATUS_BODY.unpack(body)
    names = active_names(flags, CONFIGURATION_FLAG_NAMES)
    return {
        "schema": schema,
        "flags": names,
        "last_result": CONFIGURATION_RESULT_NAMES.get(
            last_result, f"result_{last_result}"
        ),
        "active_slot": None if active_slot == 0xFF else active_slot,
        "record_schema": record_schema,
        "generation": generation,
        "dirty": (
            "active_calibration_valid" in names
            and "active_matches_record" not in names
        ),
        "stored": {
            "encoder_counts_per_revolution": stored_counts_per_revolution,
            "electrical_cycles_per_revolution": stored_electrical_cycles,
            "electrical_zero_raw": stored_zero,
            "observed_quarter_step_counts": stored_quarter_step,
            "quarter_step_error_counts": stored_quarter_error,
            "encoder_direction": stored_direction,
        },
        "active": {
            "encoder_counts_per_revolution": active_counts_per_revolution,
            "electrical_cycles_per_revolution": active_electrical_cycles,
            "electrical_zero_raw": active_zero,
            "observed_quarter_step_counts": active_quarter_step,
            "quarter_step_error_counts": active_quarter_error,
            "encoder_direction": active_direction,
        },
    }


def query_aligned_torque(client: Client) -> dict[str, Any]:
    body = client.transact(COMMAND_GET_ALIGNED_TORQUE_STATUS)
    if len(body) != ALIGNED_TORQUE_STATUS_BODY.size:
        raise ProtocolError(
            "aligned-torque-status response has an unexpected length"
        )
    (
        schema,
        state,
        result,
        flags,
        fault_flags,
        requested_q_current_counts,
        applied_q_current_counts,
        current_a_reference_counts,
        current_b_reference_counts,
        electrical_phase_q32,
        velocity_q16_16,
        acceleration_q16_16,
        elapsed_millis,
        remaining_millis,
        maximum_current_counts,
        maximum_current_slew_counts_per_second,
        maximum_velocity_q16_16,
        maximum_acceleration_q16_16,
        maximum_feedback_interval_us,
        minimum_duration_millis,
        maximum_duration_millis,
        backend_fault_flags,
    ) = ALIGNED_TORQUE_STATUS_BODY.unpack(body)
    return {
        "schema": schema,
        "state": TORQUE_STATE_NAMES.get(state, f"state_{state}"),
        "result": TORQUE_RESULT_NAMES.get(result, f"result_{result}"),
        "flags_hex": f"0x{flags:02X}",
        "flags": active_names(flags, TORQUE_FLAG_NAMES),
        "fault_flags_hex": f"0x{fault_flags:08X}",
        "faults": active_names(fault_flags, TORQUE_FAULT_NAMES),
        "requested_q_current_counts": requested_q_current_counts,
        "requested_q_current_nominal_milliamperes": round(
            requested_q_current_counts * COUNTS_TO_MILLIAMPERES, 1
        ),
        "applied_q_current_counts": applied_q_current_counts,
        "applied_q_current_nominal_milliamperes": round(
            applied_q_current_counts * COUNTS_TO_MILLIAMPERES, 1
        ),
        "phase_current_reference_counts": {
            "a": current_a_reference_counts,
            "b": current_b_reference_counts,
        },
        "electrical_phase_q32_hex": f"0x{electrical_phase_q32:08X}",
        "velocity_revolutions_per_second": velocity_q16_16 / 65536.0,
        "acceleration_revolutions_per_second2": (
            acceleration_q16_16 / 65536.0
        ),
        "elapsed_millis": elapsed_millis,
        "remaining_millis": remaining_millis,
        "backend_fault_flags_hex": f"0x{backend_fault_flags:08X}",
        "policy": {
            "maximum_current_counts": maximum_current_counts,
            "maximum_current_nominal_milliamperes": round(
                maximum_current_counts * COUNTS_TO_MILLIAMPERES, 1
            ),
            "maximum_current_slew_counts_per_second": (
                maximum_current_slew_counts_per_second
            ),
            "maximum_current_slew_amperes_per_second": round(
                maximum_current_slew_counts_per_second
                * COUNTS_TO_MILLIAMPERES
                / 1000.0,
                3,
            ),
            "maximum_velocity_revolutions_per_second": (
                maximum_velocity_q16_16 / 65536.0
            ),
            "maximum_acceleration_revolutions_per_second2": (
                maximum_acceleration_q16_16 / 65536.0
            ),
            "maximum_feedback_interval_us": maximum_feedback_interval_us,
            "minimum_duration_millis": minimum_duration_millis,
            "maximum_duration_millis": maximum_duration_millis,
        },
    }


def stop_drive(client: Client) -> None:
    try:
        client.transact(COMMAND_STOP_DRIVE)
    except ProtocolError as error:
        if "unknown_command" not in str(error):
            raise
        client.transact(COMMAND_STOP_CURRENT_TEST)


def print_json(value: Any) -> None:
    print(json.dumps(value, indent=2, sort_keys=True), flush=True)


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port, for example COM6")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--address", type=int, default=DEFAULT_ADDRESS)
    parser.add_argument("--timeout", type=float, default=0.75)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("list", help="list serial ports")
    commands.add_parser("identity", help="read product and firmware identity")
    commands.add_parser("status", help="read one drive-diagnostic snapshot")
    commands.add_parser("boot", help="read reset cause, panic, and uptime")
    commands.add_parser("encoder", help="read live encoder position and health")
    commands.add_parser("alignment", help="read alignment progress and result")
    commands.add_parser(
        "configuration", help="read persistent and active motor configuration"
    )
    commands.add_parser(
        "save-configuration", help="persist the active alignment configuration"
    )
    commands.add_parser(
        "clear-calibration", help="persistently invalidate motor alignment"
    )
    commands.add_parser(
        "torque-status", help="read aligned q-current progress and policy"
    )
    trace = commands.add_parser(
        "trace", help="read the completed 20 kHz current-loop startup trace"
    )
    trace.add_argument("--output", help="write JSON lines to this path")

    configure = commands.add_parser("configure", help="set test demand")
    configure.add_argument("--counts", type=int, required=True)
    configure.add_argument("--frequency-hz", type=float, required=True)

    start = commands.add_parser("start", help="start a bounded remote run")
    start.add_argument("--leg", choices=LEG_VALUES, default="A1")
    start.add_argument("--duration-ms", type=int, default=10000)

    commands.add_parser("stop", help="stop any active drive operation")

    align = commands.add_parser(
        "align", help="run the bounded production alignment procedure"
    )
    alignment_current = align.add_mutually_exclusive_group(required=True)
    alignment_current.add_argument("--counts", type=int)
    alignment_current.add_argument("--current-ma", type=float)
    align.add_argument("--interval", type=float, default=0.1)

    torque = commands.add_parser(
        "torque", help="run a bounded encoder-aligned q-current demand"
    )
    torque_current = torque.add_mutually_exclusive_group(required=True)
    torque_current.add_argument("--counts", type=int)
    torque_current.add_argument("--current-ma", type=float)
    torque.add_argument("--duration-ms", type=int, default=250)
    torque.add_argument("--interval", type=float, default=0.05)

    watch = commands.add_parser("watch", help="stream status as JSON lines")
    watch.add_argument("--interval", type=float, default=0.2)

    run = commands.add_parser("run", help="start and watch a bounded run")
    run.add_argument("--leg", choices=LEG_VALUES, default="A1")
    run.add_argument("--duration-ms", type=int, default=10000)
    run.add_argument("--interval", type=float, default=0.2)
    return parser


def open_serial(args: argparse.Namespace) -> Any:
    if not args.port:
        raise ProtocolError("--port is required for this command")
    try:
        import serial
    except ImportError as error:
        raise ProtocolError(
            "pyserial is required; install it with 'py -m pip install pyserial'"
        ) from error
    return serial.Serial(
        port=args.port,
        baudrate=args.baud,
        bytesize=8,
        parity="N",
        stopbits=1,
        timeout=args.timeout,
        write_timeout=args.timeout,
    )


def list_ports() -> None:
    try:
        from serial.tools import list_ports as serial_list_ports
    except ImportError as error:
        raise ProtocolError(
            "pyserial is required; install it with 'py -m pip install pyserial'"
        ) from error
    print_json(
        [
            {
                "port": port.device,
                "description": port.description,
                "hardware_id": port.hwid,
            }
            for port in serial_list_ports.comports()
        ]
    )


def main() -> int:
    args = make_parser().parse_args()
    if not (1 <= args.address <= 247):
        raise ProtocolError("address must be in the range 1..247")
    if args.command == "list":
        list_ports()
        return 0

    with open_serial(args) as port:
        client = Client(port, args.address)
        if args.command == "identity":
            body = client.transact(COMMAND_GET_IDENTITY)
            if len(body) != 10:
                raise ProtocolError("identity response has an unexpected length")
            product, major, minor, patch, proto_major, proto_minor = struct.unpack(
                ">IBBHBB", body
            )
            print_json(
                {
                    "product_id_hex": f"0x{product:08X}",
                    "firmware": f"{major}.{minor}.{patch}",
                    "protocol": f"{proto_major}.{proto_minor}",
                }
            )
        elif args.command == "status":
            print_json(query_status(client))
        elif args.command == "boot":
            body = client.transact(COMMAND_GET_BOOT_STATUS)
            if len(body) != 10:
                raise ProtocolError("boot-status response has an unexpected length")
            schema, reset_flags, retained_panic, uptime_millis = struct.unpack(
                ">BIBI", body
            )
            print_json(
                {
                    "schema": schema,
                    "reset_flags_hex": f"0x{reset_flags:08X}",
                    "reset_causes": active_names(reset_flags, RESET_FLAG_NAMES),
                    "retained_panic": retained_panic,
                    "uptime_millis": uptime_millis,
                }
            )
        elif args.command == "encoder":
            print_json(query_encoder(client))
        elif args.command == "alignment":
            print_json(query_alignment(client))
        elif args.command == "configuration":
            print_json(query_configuration(client))
        elif args.command == "save-configuration":
            client.transact(COMMAND_SAVE_CONFIGURATION)
            print_json(query_configuration(client))
        elif args.command == "clear-calibration":
            client.transact(COMMAND_CLEAR_CALIBRATION)
            print_json(query_configuration(client))
        elif args.command == "torque-status":
            print_json(query_aligned_torque(client))
        elif args.command == "trace":
            samples = read_current_trace(client)
            if args.output:
                with open(args.output, "w", encoding="utf-8") as stream:
                    for sample in samples:
                        stream.write(json.dumps(sample, sort_keys=True) + "\n")
                print_json(
                    {
                        "captured_sample_count": len(samples),
                        "duration_seconds": samples[-1]["time_seconds"],
                        "output": args.output,
                    }
                )
            else:
                for sample in samples:
                    print(json.dumps(sample, sort_keys=True), flush=True)
        elif args.command == "configure":
            frequency_millihz = round(args.frequency_hz * 1000.0)
            body = client.transact(
                COMMAND_CONFIGURE_CURRENT_TEST,
                struct.pack(">HI", args.counts, frequency_millihz),
            )
            if len(body) != 6:
                raise ProtocolError("configure response has an unexpected length")
            amplitude, frequency_millihz = struct.unpack(">HI", body)
            print_json(
                {
                    "amplitude_counts": amplitude,
                    "frequency_hz": frequency_millihz / 1000.0,
                }
            )
        elif args.command == "start":
            client.transact(
                COMMAND_START_CURRENT_TEST,
                struct.pack(">BI", LEG_VALUES[args.leg], args.duration_ms),
            )
            print_json(query_status(client))
        elif args.command == "stop":
            stop_drive(client)
            print_json(query_status(client))
        elif args.command == "align":
            if args.counts is not None:
                alignment_counts = args.counts
            else:
                if args.current_ma is None or args.current_ma <= 0.0:
                    raise ProtocolError("--current-ma must be positive")
                alignment_counts = round(
                    args.current_ma / COUNTS_TO_MILLIAMPERES
                )
            if not 1 <= alignment_counts <= 0xFFFF:
                raise ProtocolError(
                    "alignment current must encode as 1..65535 counts"
                )
            if not 0.01 <= args.interval <= 2.0:
                raise ProtocolError(
                    "--interval must be in the range 0.01..2.0 seconds"
                )
            alignment_status = query_alignment(client)
            policy = alignment_status["policy"]
            if not (
                policy["minimum_current_counts"]
                <= alignment_counts
                <= policy["maximum_current_counts"]
            ):
                raise ProtocolError(
                    "alignment current is outside the firmware-reported "
                    f"range {policy['minimum_current_counts']}.."
                    f"{policy['maximum_current_counts']} counts"
                )
            client.transact(
                COMMAND_START_ALIGNMENT,
                struct.pack(">H", alignment_counts),
            )
            try:
                while True:
                    alignment = query_alignment(client)
                    print(json.dumps(alignment, sort_keys=True), flush=True)
                    if alignment["state"] in {"complete", "failed", "aborted"}:
                        return 0 if alignment["result"] == "success" else 3
                    time.sleep(args.interval)
            except KeyboardInterrupt:
                stop_drive(client)
                print_json(query_alignment(client))
                print("stopped", file=sys.stderr)
                return 130
        elif args.command == "torque":
            if args.counts is not None:
                q_current_counts = args.counts
            else:
                if args.current_ma is None or args.current_ma == 0.0:
                    raise ProtocolError("--current-ma must be nonzero")
                q_current_counts = round(
                    args.current_ma / COUNTS_TO_MILLIAMPERES
                )
            if not -0x8000 <= q_current_counts <= 0x7FFF:
                raise ProtocolError(
                    "q-current must encode as a signed 16-bit count"
                )
            if q_current_counts == 0:
                raise ProtocolError("q-current demand must be nonzero")
            if not 0.01 <= args.interval <= 2.0:
                raise ProtocolError(
                    "--interval must be in the range 0.01..2.0 seconds"
                )
            torque_status = query_aligned_torque(client)
            policy = torque_status["policy"]
            if abs(q_current_counts) > policy["maximum_current_counts"]:
                raise ProtocolError(
                    "q-current is outside the firmware-reported range "
                    f"-{policy['maximum_current_counts']}.."
                    f"{policy['maximum_current_counts']} counts"
                )
            if not (
                policy["minimum_duration_millis"]
                <= args.duration_ms
                <= policy["maximum_duration_millis"]
            ):
                raise ProtocolError(
                    "duration is outside the firmware-reported range "
                    f"{policy['minimum_duration_millis']}.."
                    f"{policy['maximum_duration_millis']} ms"
                )
            client.transact(
                COMMAND_START_ALIGNED_TORQUE,
                struct.pack(">hI", q_current_counts, args.duration_ms),
            )
            try:
                while True:
                    torque_status = query_aligned_torque(client)
                    torque_status["drive"] = query_status(client)
                    torque_status["encoder"] = query_encoder(client)
                    print(json.dumps(torque_status, sort_keys=True), flush=True)
                    if torque_status["state"] in {
                        "complete",
                        "stopped",
                        "failed",
                    }:
                        return 0 if torque_status["state"] == "complete" else 3
                    time.sleep(args.interval)
            except KeyboardInterrupt:
                stop_drive(client)
                print_json(query_aligned_torque(client))
                print("stopped", file=sys.stderr)
                return 130
        elif args.command == "watch":
            while True:
                status = query_status(client)
                status["encoder"] = query_encoder(client)
                print(json.dumps(status, sort_keys=True), flush=True)
                time.sleep(args.interval)
        elif args.command == "run":
            client.transact(
                COMMAND_START_CURRENT_TEST,
                struct.pack(">BI", LEG_VALUES[args.leg], args.duration_ms),
            )
            try:
                while True:
                    status = query_status(client)
                    status["encoder"] = query_encoder(client)
                    print(json.dumps(status, sort_keys=True), flush=True)
                    if "remote_authority" not in status["flags"] and (
                        "remote_start_pending" not in status["flags"]
                    ):
                        break
                    time.sleep(args.interval)
            except KeyboardInterrupt:
                stop_drive(client)
                print("stopped", file=sys.stderr)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ProtocolError, OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
