#!/usr/bin/env python3
"""MKS57D native-v1 RS-485 product service and diagnostic console."""

from __future__ import annotations

import argparse
import csv
import json
import math
import struct
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any


PROTOCOL_VERSION = 1
DEFAULT_ADDRESS = 1
MAX_WIRE_FRAME_SIZE = 92
MAX_RESPONSE_FRAMES_PER_TRANSACTION = 4
CURRENT_TRACE_SAMPLE_ATTEMPTS = 3
VELOCITY_MINIMUM_DURATION_MILLIS = 3
POSITION_MINIMUM_DURATION_MILLIS = 100
VELOCITY_CONSOLE_INTERVAL_SECONDS = 0.2
POSITION_CONSOLE_INTERVAL_SECONDS = 0.2
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
COMMAND_ARM_CURRENT_TRACE = 0x0107
COMMAND_START_ALIGNMENT = 0x0200
COMMAND_GET_ALIGNMENT_STATUS = 0x0201
COMMAND_STOP_DRIVE = 0x0202
COMMAND_CLEAR_FAULTS = 0x0203
COMMAND_GET_CONFIGURATION_STATUS = 0x0300
COMMAND_SAVE_CONFIGURATION = 0x0301
COMMAND_CLEAR_CALIBRATION = 0x0302
COMMAND_SET_CURRENT_LOOP_GAINS = 0x0303
COMMAND_REVERT_CURRENT_LOOP_GAINS = 0x0304
COMMAND_START_ALIGNED_TORQUE = 0x0400
COMMAND_GET_ALIGNED_TORQUE_STATUS = 0x0401
COMMAND_START_VELOCITY = 0x0500
COMMAND_GET_VELOCITY_STATUS = 0x0501
COMMAND_START_POSITION_RELATIVE = 0x0600
COMMAND_GET_POSITION_STATUS = 0x0601

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
    11: "vbus_snapshot_valid",
}

INPUT_BITS = {
    2: "left",
    0: "center",
    1: "right",
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
    20: "phase_prediction",
}

PHASE_PREDICTION_REJECT_NAMES = {
    0: "none",
    1: "observation_invalid",
    2: "stale",
    3: "reference_out_of_range",
    4: "reference_mapping_failed",
}

FAULT_RECOVERY_RESULT_NAMES = {
    0: "cleared",
    1: "no_fault",
    2: "blocked",
}

FAULT_RECOVERY_BLOCKER_NAMES = {
    0: "zero_failed",
    1: "backend_reset_failed",
    2: "runtime_reset_failed",
    3: "supervisor_reset_failed",
}

FAULT_SOURCE_NAMES = {
    0: "supervisor",
    1: "estimator",
    2: "alignment",
    3: "aligned_torque",
    4: "velocity",
    5: "position",
    6: "current_backend",
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

VELOCITY_STATE_NAMES = {
    0: "idle",
    1: "ramping",
    2: "tracking",
    3: "complete",
    4: "stopped",
    5: "failed",
}

VELOCITY_RESULT_NAMES = {
    0: "none",
    1: "deadline",
    2: "stopped",
    3: "invalid_feedback",
    4: "feedback_timing",
    5: "overspeed",
    6: "internal_numeric",
    7: "actuator_fault",
}

VELOCITY_FLAG_NAMES = {
    0: "active",
    1: "authority_active",
    2: "backend_active",
    3: "alignment_valid",
    4: "actuator_active",
    5: "reference_at_target",
    6: "current_at_limit",
}

VELOCITY_FAULT_NAMES = {
    0: "invalid_feedback",
    1: "feedback_timing",
    2: "overspeed",
    3: "internal_numeric",
    4: "actuator_fault",
}

POSITION_STATE_NAMES = {
    0: "idle",
    1: "moving",
    2: "settling",
    3: "complete",
    4: "stopped",
    5: "failed",
}

POSITION_RESULT_NAMES = {
    0: "none",
    1: "settled",
    2: "deadline",
    3: "stopped",
    4: "invalid_feedback",
    5: "feedback_timing",
    6: "following_error",
    7: "internal_numeric",
    8: "actuator_fault",
}

POSITION_FLAG_NAMES = {
    0: "active",
    1: "authority_active",
    2: "backend_active",
    3: "alignment_valid",
    4: "velocity_active",
    5: "profile_at_target",
    6: "target_settled",
    7: "current_at_limit",
}

POSITION_FAULT_NAMES = {
    0: "invalid_feedback",
    1: "feedback_timing",
    2: "following_error",
    3: "internal_numeric",
    4: "actuator_fault",
}

STATUS_V2_BODY = struct.Struct(">BIBBBBIIHHHHhhhhhhHHHHHHHHIIBB")
STATUS_V3_BODY = struct.Struct(">BIBBBBIIHHHHhhhhhhHHHHHHHHIIBBHI")
STATUS_V4_BODY = struct.Struct(">BIBBBBIIHHHHhhhhhhHHHHHHHHIIBBHIII")
CURRENT_TRACE_V1_BODY = struct.Struct(">BHHIhhhhhh")
CURRENT_TRACE_V2_BODY = struct.Struct(">BHHIhhhhhhIHHHHHH")
TRACE_CYCLE_COUNTER_HZ = 64_000_000
TRACE_PWM_TIMER_HZ = 32_000_000
ENCODER_STATUS_V1_BODY = struct.Struct(">BBBHBIII")
ENCODER_STATUS_V2_BODY = struct.Struct(">BBBHBIIIBiiIIHbIII")
ALIGNMENT_STATUS_BODY = struct.Struct(">BBBBHHHHHhhbHIIHHHHIIIHHHH")
CONFIGURATION_STATUS_V1_BODY = struct.Struct(">BBBBHIHHHHhbHHHHhb")
CONFIGURATION_STATUS_V2_BODY = struct.Struct(
    ">BBBBHIHHHHhbHHHHhbiiiiiiii"
)
ALIGNED_TORQUE_STATUS_V1_BODY = struct.Struct(">BBBBIhhhhIiiIIHHiiHIII")
ALIGNED_TORQUE_STATUS_V2_BODY = struct.Struct(">BBBBIhhhhIiiIIHHiiHIIIBIHH")
VELOCITY_STATUS_BODY = struct.Struct(">BBBBIiiihhHIIiiiHHiiI")
POSITION_STATUS_BODY = struct.Struct(">BBBBIiiiiiihhHIIiiii")
FAULT_RECOVERY_STATUS_BODY = struct.Struct(">BBIII")
VELOCITY_TELEMETRY_FIELDS = (
    "host_elapsed_seconds",
    "controller_elapsed_millis",
    "remaining_millis",
    "state",
    "result",
    "target_velocity_rps",
    "reference_velocity_rps",
    "measured_velocity_rps",
    "velocity_error_rps",
    "requested_q_current_amperes",
    "applied_q_current_amperes",
    "current_limit_amperes",
    "requested_q_current_counts",
    "applied_q_current_counts",
    "current_limit_counts",
    "velocity_flags_hex",
    "velocity_flags",
    "velocity_fault_flags_hex",
    "velocity_faults",
    "drive_flags_hex",
    "drive_flags",
    "loop_fault_flags_hex",
    "loop_faults",
    "loop_sample_count",
    "bus_voltage_volts",
    "phase_a_voltage_command_volts",
    "phase_b_voltage_command_volts",
    "phase_voltage_limit_volts",
    "encoder_status",
    "encoder_transport_status",
    "encoder_flags_hex",
    "encoder_error_count",
    "encoder_angle_raw",
    "encoder_position_revolutions",
    "encoder_velocity_rps",
    "encoder_sample_interval_us",
    "encoder_maximum_sample_interval_us",
    "estimator_flags_hex",
    "estimator_flags",
    "estimator_fault_flags_hex",
    "estimator_faults",
    "retained_panic",
    "watchdog_reset",
)
POSITION_TELEMETRY_FIELDS = (
    "host_elapsed_seconds",
    "controller_elapsed_millis",
    "remaining_millis",
    "state",
    "result",
    "target_position_revolutions",
    "reference_position_revolutions",
    "measured_position_revolutions",
    "profile_following_error_revolutions",
    "target_position_error_revolutions",
    "reference_velocity_rps",
    "target_velocity_rps",
    "measured_velocity_rps",
    "requested_q_current_amperes",
    "applied_q_current_amperes",
    "current_limit_amperes",
    "requested_q_current_counts",
    "applied_q_current_counts",
    "current_limit_counts",
    "position_flags_hex",
    "position_flags",
    "position_fault_flags_hex",
    "position_faults",
    "drive_flags_hex",
    "drive_flags",
    "loop_fault_flags_hex",
    "loop_faults",
    "loop_sample_count",
    "bus_voltage_volts",
    "phase_a_voltage_command_volts",
    "phase_b_voltage_command_volts",
    "phase_voltage_limit_volts",
    "encoder_status",
    "encoder_transport_status",
    "encoder_flags_hex",
    "encoder_error_count",
    "encoder_angle_raw",
    "encoder_position_revolutions",
    "encoder_velocity_rps",
    "encoder_sample_interval_us",
    "encoder_maximum_sample_interval_us",
    "estimator_flags_hex",
    "estimator_flags",
    "estimator_fault_flags_hex",
    "estimator_faults",
    "retained_panic",
    "watchdog_reset",
)
CURRENT_TRACE_CSV_FIELDS = (
    "schema",
    "sample_index",
    "captured_sample_count",
    "time_seconds",
    "loop_sample_count",
    "current_a_reference_counts",
    "current_b_reference_counts",
    "current_a_measured_counts",
    "current_b_measured_counts",
    "phase_a_voltage_permille",
    "phase_b_voltage_permille",
    "phase_a_voltage_command_volts",
    "phase_b_voltage_command_volts",
    "predicted_electrical_phase_q32",
    "predicted_electrical_phase_turns",
    "predicted_electrical_phase_degrees",
    "phase_prediction_age_us",
    "trigger_timer_count",
    "trigger_timer_us",
    "trigger_to_dma_timer_ticks",
    "trigger_to_dma_us",
    "dma_to_pwm_stage_cycles",
    "dma_to_pwm_stage_us",
    "dma_to_trace_record_cycles",
    "dma_to_trace_record_us",
    "pwm_preload_margin_ticks",
    "pwm_preload_margin_us",
    "bus_voltage_volts",
    "phase_voltage_limit_volts",
)
COUNTS_TO_MILLIAMPERES = (
    3.3 / 4095.0 / (6.65 * 0.020) * 1000.0
)
VBUS_VOLTS_PER_COUNT = 3.3 / 4095.0 * 16.4


def nominal_amperes_from_counts(counts: int) -> float:
    return counts * COUNTS_TO_MILLIAMPERES / 1000.0


def current_loop_gain_from_q16(value: int) -> float:
    return value / 65536.0


def current_loop_gain_to_q16(value: float, name: str = "gain") -> int:
    if not math.isfinite(value) or value < 0.0:
        raise ProtocolError(f"{name} must be a finite nonnegative value")
    scaled = round(value * 65536.0)
    if scaled > 0x7FFFFFFF:
        raise ProtocolError(f"{name} exceeds the signed Q16.16 range")
    return scaled


class ProtocolError(RuntimeError):
    pass


class TransportError(ProtocolError):
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
            raise TransportError("zero byte inside COBS frame")
        index += 1
        end = index + code - 1
        if end > len(data):
            raise TransportError("truncated COBS frame")
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
        raise TransportError("response has no delimiter")
    decoded = cobs_decode(wire[:-1])
    if len(decoded) < 10:
        raise TransportError("response is too short")
    version, address, sequence, message_type, command, length = struct.unpack(
        ">BBHBHB", decoded[:8]
    )
    if version != PROTOCOL_VERSION or message_type != MESSAGE_RESPONSE:
        raise TransportError("unexpected response version or message type")
    if len(decoded) != 8 + length + 2:
        raise TransportError("response length field does not match frame")
    expected_crc = struct.unpack(">H", decoded[-2:])[0]
    if crc16_ccitt_false(decoded[:-2]) != expected_crc:
        raise TransportError("response CRC mismatch")
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
        last_frame_error: ProtocolError | None = None
        response: Response | None = None
        wire = b""
        for _ in range(MAX_RESPONSE_FRAMES_PER_TRANSACTION):
            wire = self.serial.read_until(b"\x00", MAX_WIRE_FRAME_SIZE)
            if not wire:
                break
            try:
                response = decode_response(wire)
            except ProtocolError as error:
                last_frame_error = error
                continue
            if (
                response.address != self.address
                or response.sequence != sequence
                or response.command != command
            ):
                last_frame_error = TransportError(
                    "response identity does not match request"
                )
                continue
            break
        else:
            response = None
        if not wire or response is None or (
            response.address != self.address
            or response.sequence != sequence
            or response.command != command
        ):
            if last_frame_error is not None:
                raise last_frame_error
            raise TransportError("response timeout")
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


def query_identity(client: Client) -> dict[str, Any]:
    body = client.transact(COMMAND_GET_IDENTITY)
    if len(body) != 10:
        raise ProtocolError("identity response has an unexpected length")
    product, major, minor, patch, proto_major, proto_minor = struct.unpack(
        ">IBBHBB", body
    )
    return {
        "product_id_hex": f"0x{product:08X}",
        "firmware": f"{major}.{minor}.{patch}",
        "protocol": f"{proto_major}.{proto_minor}",
    }


def input_state(levels: int) -> dict[str, bool]:
    return {name: not bool(levels & (1 << bit)) for bit, name in INPUT_BITS.items()}


def parse_status(body: bytes) -> dict[str, Any]:
    if len(body) not in {
        STATUS_V2_BODY.size,
        STATUS_V3_BODY.size,
        STATUS_V4_BODY.size,
    }:
        raise ProtocolError(
            "commissioning status is "
            f"{len(body)} bytes, expected {STATUS_V2_BODY.size} or "
            f"{STATUS_V3_BODY.size} or {STATUS_V4_BODY.size}"
        )
    values = iter(STATUS_V2_BODY.unpack(body[: STATUS_V2_BODY.size]))
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
    vbus_raw = None
    vbus_sample_count = None
    missed_pwm_update_count = None
    maximum_consecutive_missed_pwm_updates = None
    if len(body) >= STATUS_V3_BODY.size:
        vbus_raw, vbus_sample_count = struct.unpack(
            ">HI", body[STATUS_V2_BODY.size : STATUS_V3_BODY.size]
        )
    if len(body) == STATUS_V4_BODY.size:
        (
            missed_pwm_update_count,
            maximum_consecutive_missed_pwm_updates,
        ) = struct.unpack(">II", body[STATUS_V3_BODY.size :])
    vbus_valid = bool(flags & (1 << 11)) and vbus_raw is not None
    bus_voltage_unrounded = (
        vbus_raw * VBUS_VOLTS_PER_COUNT
        if vbus_valid
        else None
    )
    bus_voltage = (
        round(bus_voltage_unrounded, 3)
        if bus_voltage_unrounded is not None
        else None
    )
    phase_voltage_command = {
        "a": round(bus_voltage_unrounded * voltage_a / 1000.0, 3),
        "b": round(bus_voltage_unrounded * voltage_b / 1000.0, 3),
    } if bus_voltage_unrounded is not None else {"a": None, "b": None}

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
            "vbus_raw": vbus_raw,
            "vbus_sample_count": vbus_sample_count,
            "bus_voltage_volts": bus_voltage,
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
            "missed_pwm_update_count": missed_pwm_update_count,
            "maximum_consecutive_missed_pwm_updates":
                maximum_consecutive_missed_pwm_updates,
            "reference_counts": {"a": reference_a, "b": reference_b},
            "reference_nominal_milliamperes": {
                "a": round(reference_a * COUNTS_TO_MILLIAMPERES, 1),
                "b": round(reference_b * COUNTS_TO_MILLIAMPERES, 1),
            },
            "measured_counts": {"a": measured_a, "b": measured_b},
            "measured_nominal_milliamperes": {
                "a": round(measured_a * COUNTS_TO_MILLIAMPERES, 1),
                "b": round(measured_b * COUNTS_TO_MILLIAMPERES, 1),
            },
            "phase_voltage_permille": {"a": voltage_a, "b": voltage_b},
            "phase_voltage_command_volts": phase_voltage_command,
            "duties_permille": {
                "a1": duties[0],
                "a2": duties[1],
                "b1": duties[2],
                "b2": duties[3],
            },
            "hard_current_limit_counts": hard_limit,
            "hard_current_limit_nominal_amperes": round(
                nominal_amperes_from_counts(hard_limit), 3
            ),
            "phase_voltage_limit_permille": voltage_limit,
            "phase_voltage_limit_volts": (
                round(bus_voltage_unrounded * voltage_limit / 1000.0, 3)
                if bus_voltage_unrounded is not None
                else None
            ),
        },
    }


def query_status(client: Client) -> dict[str, Any]:
    return parse_status(client.transact(COMMAND_GET_COMMISSIONING_STATUS))


def parse_fault_recovery_status(body: bytes) -> dict[str, Any]:
    if len(body) != FAULT_RECOVERY_STATUS_BODY.size:
        raise ProtocolError(
            "fault-recovery response has an unexpected length"
        )
    schema, result, blockers, cleared, remaining = (
        FAULT_RECOVERY_STATUS_BODY.unpack(body)
    )
    return {
        "schema": schema,
        "result": FAULT_RECOVERY_RESULT_NAMES.get(
            result, f"result_{result}"
        ),
        "blocker_flags_hex": f"0x{blockers:08X}",
        "blockers": active_names(blockers, FAULT_RECOVERY_BLOCKER_NAMES),
        "cleared_fault_flags_hex": f"0x{cleared:08X}",
        "cleared_faults": active_names(cleared, FAULT_SOURCE_NAMES),
        "remaining_fault_flags_hex": f"0x{remaining:08X}",
        "remaining_faults": active_names(remaining, FAULT_SOURCE_NAMES),
    }


def clear_faults(client: Client) -> dict[str, Any]:
    return parse_fault_recovery_status(
        client.transact(COMMAND_CLEAR_FAULTS)
    )


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
    if len(body) == CURRENT_TRACE_V1_BODY.size:
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
        ) = CURRENT_TRACE_V1_BODY.unpack(body)
        if schema != 1:
            raise ProtocolError(
                "current-trace schema does not match its response length"
            )
        predicted_phase_q32 = None
        phase_prediction_age_us = None
        trigger_timer_count = None
        trigger_to_dma_timer_ticks = None
        dma_to_pwm_stage_cycles = None
        dma_to_trace_record_cycles = None
        pwm_preload_margin_ticks = None
    elif len(body) == CURRENT_TRACE_V2_BODY.size:
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
            predicted_phase_q32,
            phase_prediction_age_us,
            trigger_timer_count,
            trigger_to_dma_timer_ticks,
            dma_to_pwm_stage_cycles,
            dma_to_trace_record_cycles,
            pwm_preload_margin_ticks,
        ) = CURRENT_TRACE_V2_BODY.unpack(body)
        if schema != 2:
            raise ProtocolError(
                "current-trace schema does not match its response length"
            )
    else:
        raise ProtocolError("current-trace response has an unexpected length")
    if sample_index != index:
        raise ProtocolError("current-trace response index does not match request")
    phase_turns = (
        predicted_phase_q32 / 4294967296.0
        if predicted_phase_q32 is not None
        else None
    )
    return {
        "schema": schema,
        "captured_sample_count": captured_sample_count,
        "sample_index": sample_index,
        "loop_sample_count": loop_sample_count,
        "reference_counts": {"a": reference_a, "b": reference_b},
        "measured_counts": {"a": measured_a, "b": measured_b},
        "phase_voltage_permille": {"a": voltage_a, "b": voltage_b},
        "phase_prediction": {
            "electrical_phase_q32": predicted_phase_q32,
            "electrical_phase_turns": (
                round(phase_turns, 9) if phase_turns is not None else None
            ),
            "electrical_phase_degrees": (
                round(phase_turns * 360.0, 6)
                if phase_turns is not None
                else None
            ),
            "age_us": phase_prediction_age_us,
        },
        "timing": {
            "cycle_counter_hz": (
                TRACE_CYCLE_COUNTER_HZ if schema >= 2 else None
            ),
            "pwm_timer_hz": TRACE_PWM_TIMER_HZ if schema >= 2 else None,
            "trigger_timer_count": trigger_timer_count,
            "trigger_to_dma_timer_ticks": trigger_to_dma_timer_ticks,
            "dma_to_pwm_stage_cycles": dma_to_pwm_stage_cycles,
            "dma_to_trace_record_cycles": dma_to_trace_record_cycles,
            "pwm_preload_margin_ticks": pwm_preload_margin_ticks,
        },
    }


def arm_current_trace(client: Client) -> None:
    client.transact(COMMAND_ARM_CURRENT_TRACE)


def _query_current_trace_sample_with_retry(
    client: Client, index: int
) -> dict[str, Any]:
    last_error: ProtocolError | None = None
    for attempt in range(1, CURRENT_TRACE_SAMPLE_ATTEMPTS + 1):
        try:
            return query_current_trace_sample(client, index)
        except TransportError as error:
            last_error = error
            if attempt < CURRENT_TRACE_SAMPLE_ATTEMPTS:
                print(
                    f"warning: retrying current-trace sample {index} "
                    f"after {error}",
                    file=sys.stderr,
                )
    assert last_error is not None
    raise last_error


def read_current_trace(client: Client) -> list[dict[str, Any]]:
    drive_status = query_status(client)
    bus_voltage = drive_status.get("adc", {}).get("bus_voltage_volts")
    phase_voltage_limit = drive_status.get("loop", {}).get(
        "phase_voltage_limit_volts"
    )
    first = _query_current_trace_sample_with_retry(client, 0)
    samples = [first]
    expected_count = first["captured_sample_count"]
    for index in range(1, expected_count):
        sample = _query_current_trace_sample_with_retry(client, index)
        if sample["captured_sample_count"] != expected_count:
            raise ProtocolError("current-trace sample count changed while reading")
        samples.append(sample)
    first_loop_sample = samples[0]["loop_sample_count"]
    for sample in samples:
        sample["time_seconds"] = round(
            (sample["loop_sample_count"] - first_loop_sample) / 20000.0,
            7,
        )
        sample["bus_voltage_volts"] = bus_voltage
        sample["phase_voltage_limit_volts"] = phase_voltage_limit
        trigger_timer_count = sample["timing"]["trigger_timer_count"]
        sample["timing"]["trigger_timer_us"] = (
            round(
                trigger_timer_count * 1_000_000.0 / TRACE_PWM_TIMER_HZ,
                6,
            )
            if trigger_timer_count is not None
            else None
        )
        trigger_to_dma_ticks = sample["timing"][
            "trigger_to_dma_timer_ticks"
        ]
        sample["timing"]["trigger_to_dma_us"] = (
            round(
                trigger_to_dma_ticks * 1_000_000.0 / TRACE_PWM_TIMER_HZ,
                6,
            )
            if trigger_to_dma_ticks is not None
            else None
        )
        for key in (
            "dma_to_pwm_stage_cycles",
            "dma_to_trace_record_cycles",
        ):
            cycles = sample["timing"][key]
            sample["timing"][key.removesuffix("_cycles") + "_us"] = (
                round(cycles * 1_000_000.0 / TRACE_CYCLE_COUNTER_HZ, 6)
                if cycles is not None
                else None
            )
        margin_ticks = sample["timing"]["pwm_preload_margin_ticks"]
        sample["timing"]["pwm_preload_margin_us"] = (
            round(margin_ticks * 1_000_000.0 / TRACE_PWM_TIMER_HZ, 6)
            if margin_ticks is not None
            else None
        )
        raw_voltage = sample["phase_voltage_permille"]
        sample["phase_voltage_command_volts"] = {
            phase: (
                round(bus_voltage * value / 1000.0, 3)
                if bus_voltage is not None
                else None
            )
            for phase, value in raw_voltage.items()
        }
    return samples


def _current_trace_csv_row(sample: dict[str, Any]) -> dict[str, Any]:
    prediction = sample["phase_prediction"]
    timing = sample["timing"]
    reference = sample["reference_counts"]
    measured = sample["measured_counts"]
    voltage = sample["phase_voltage_permille"]
    voltage_command = sample["phase_voltage_command_volts"]
    return {
        "schema": sample["schema"],
        "sample_index": sample["sample_index"],
        "captured_sample_count": sample["captured_sample_count"],
        "time_seconds": sample["time_seconds"],
        "loop_sample_count": sample["loop_sample_count"],
        "current_a_reference_counts": reference["a"],
        "current_b_reference_counts": reference["b"],
        "current_a_measured_counts": measured["a"],
        "current_b_measured_counts": measured["b"],
        "phase_a_voltage_permille": voltage["a"],
        "phase_b_voltage_permille": voltage["b"],
        "phase_a_voltage_command_volts": voltage_command["a"],
        "phase_b_voltage_command_volts": voltage_command["b"],
        "predicted_electrical_phase_q32": prediction[
            "electrical_phase_q32"
        ],
        "predicted_electrical_phase_turns": prediction[
            "electrical_phase_turns"
        ],
        "predicted_electrical_phase_degrees": prediction[
            "electrical_phase_degrees"
        ],
        "phase_prediction_age_us": prediction["age_us"],
        "trigger_timer_count": timing["trigger_timer_count"],
        "trigger_timer_us": timing["trigger_timer_us"],
        "trigger_to_dma_timer_ticks": timing[
            "trigger_to_dma_timer_ticks"
        ],
        "trigger_to_dma_us": timing["trigger_to_dma_us"],
        "dma_to_pwm_stage_cycles": timing["dma_to_pwm_stage_cycles"],
        "dma_to_pwm_stage_us": timing["dma_to_pwm_stage_us"],
        "dma_to_trace_record_cycles": timing[
            "dma_to_trace_record_cycles"
        ],
        "dma_to_trace_record_us": timing["dma_to_trace_record_us"],
        "pwm_preload_margin_ticks": timing["pwm_preload_margin_ticks"],
        "pwm_preload_margin_us": timing["pwm_preload_margin_us"],
        "bus_voltage_volts": sample["bus_voltage_volts"],
        "phase_voltage_limit_volts": sample["phase_voltage_limit_volts"],
    }


def write_current_trace_csv(
    path: Path, samples: list[dict[str, Any]]
) -> None:
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=CURRENT_TRACE_CSV_FIELDS)
        writer.writeheader()
        writer.writerows(_current_trace_csv_row(sample) for sample in samples)


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
    if len(body) not in {
        CONFIGURATION_STATUS_V1_BODY.size,
        CONFIGURATION_STATUS_V2_BODY.size,
    }:
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
    ) = CONFIGURATION_STATUS_V1_BODY.unpack(
        body[: CONFIGURATION_STATUS_V1_BODY.size]
    )
    names = active_names(flags, CONFIGURATION_FLAG_NAMES)
    result = {
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
    if len(body) == CONFIGURATION_STATUS_V2_BODY.size:
        if schema < 2:
            raise ProtocolError(
                "configuration schema does not match its response length"
            )
        gain_values = struct.unpack(
            ">iiiiiiii", body[CONFIGURATION_STATUS_V1_BODY.size :]
        )
        (
            default_kp,
            default_ki,
            stored_kp,
            stored_ki,
            active_kp,
            active_ki,
            maximum_kp,
            maximum_ki,
        ) = gain_values

        def gains(kp_q16: int, ki_q16: int) -> dict[str, Any]:
            return {
                "proportional_q16_per_count": kp_q16,
                "integral_q16_per_count_per_step": ki_q16,
                "proportional_per_count": round(
                    current_loop_gain_from_q16(kp_q16), 9
                ),
                "integral_per_count_per_step": round(
                    current_loop_gain_from_q16(ki_q16), 9
                ),
            }

        result["tuning_supported"] = True
        result["default"] = {
            "current_loop_gains": gains(default_kp, default_ki)
        }
        result["stored"]["current_loop_gains"] = gains(
            stored_kp, stored_ki
        )
        result["active"]["current_loop_gains"] = gains(
            active_kp, active_ki
        )
        result["limits"] = {
            "maximum_current_loop_gains": gains(maximum_kp, maximum_ki)
        }
    else:
        if schema != 1:
            raise ProtocolError(
                "configuration schema does not match its response length"
            )
        result["tuning_supported"] = False
    return result


def _validate_current_loop_gains_against_configuration(
    configuration: dict[str, Any], kp_q16: int, ki_q16: int
) -> None:
    if not configuration.get("tuning_supported"):
        raise ProtocolError(
            "firmware does not expose volatile current-loop tuning"
        )
    limits = configuration["limits"]["maximum_current_loop_gains"]
    maximum_kp = int(limits["proportional_q16_per_count"])
    maximum_ki = int(limits["integral_q16_per_count_per_step"])
    if kp_q16 > maximum_kp:
        raise ProtocolError(
            "Kp exceeds the firmware-reported maximum of "
            f"{current_loop_gain_from_q16(maximum_kp):g}"
        )
    if ki_q16 > maximum_ki:
        raise ProtocolError(
            "Ki exceeds the firmware-reported maximum of "
            f"{current_loop_gain_from_q16(maximum_ki):g}"
        )


def set_current_loop_gains_q16(
    client: Client, kp_q16: int, ki_q16: int
) -> None:
    if not 0 <= kp_q16 <= 0x7FFFFFFF:
        raise ProtocolError("Kp Q16.16 value is outside the supported range")
    if not 0 <= ki_q16 <= 0x7FFFFFFF:
        raise ProtocolError("Ki Q16.16 value is outside the supported range")
    client.transact(
        COMMAND_SET_CURRENT_LOOP_GAINS,
        struct.pack(">ii", kp_q16, ki_q16),
    )


def set_current_loop_gains(
    client: Client, kp: float, ki: float,
    configuration: dict[str, Any] | None = None,
) -> dict[str, Any]:
    kp_q16 = current_loop_gain_to_q16(kp, "Kp")
    ki_q16 = current_loop_gain_to_q16(ki, "Ki")
    before = configuration or query_configuration(client)
    _validate_current_loop_gains_against_configuration(
        before, kp_q16, ki_q16
    )
    set_current_loop_gains_q16(client, kp_q16, ki_q16)
    return query_configuration(client)


def revert_current_loop_gains(client: Client) -> dict[str, Any]:
    client.transact(COMMAND_REVERT_CURRENT_LOOP_GAINS)
    return query_configuration(client)


def print_current_loop_gain_summary(configuration: dict[str, Any]) -> None:
    if not configuration.get("tuning_supported"):
        print("Current-loop tuning: unavailable (configuration schema 1)")
        return

    def format_set(label: str, values: dict[str, Any]) -> str:
        gains = values["current_loop_gains"]
        return (
            f"{label} Kp={gains['proportional_per_count']:g} "
            f"(Q16={gains['proportional_q16_per_count']}), "
            f"Ki={gains['integral_per_count_per_step']:g} "
            f"(Q16={gains['integral_q16_per_count_per_step']})"
        )

    print(
        "Current-loop gains: "
        + "; ".join(
            (
                format_set("active", configuration["active"]),
                format_set("stored", configuration["stored"]),
                format_set("default", configuration["default"]),
            )
        )
    )


def query_aligned_torque(client: Client) -> dict[str, Any]:
    body = client.transact(COMMAND_GET_ALIGNED_TORQUE_STATUS)
    if len(body) not in {
        ALIGNED_TORQUE_STATUS_V1_BODY.size,
        ALIGNED_TORQUE_STATUS_V2_BODY.size,
    }:
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
    ) = ALIGNED_TORQUE_STATUS_V1_BODY.unpack(
        body[: ALIGNED_TORQUE_STATUS_V1_BODY.size]
    )
    phase_prediction_reject_reason = 0
    rejected_phase_prediction_age_us = None
    maximum_observed_phase_prediction_age_us = None
    maximum_phase_prediction_age_us = None
    if len(body) == ALIGNED_TORQUE_STATUS_V2_BODY.size:
        (
            phase_prediction_reject_reason,
            rejected_phase_prediction_age_us,
            maximum_observed_phase_prediction_age_us,
            maximum_phase_prediction_age_us,
        ) = struct.unpack(
            ">BIHH", body[ALIGNED_TORQUE_STATUS_V1_BODY.size :]
        )
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
        "phase_prediction": {
            "reject_reason": PHASE_PREDICTION_REJECT_NAMES.get(
                phase_prediction_reject_reason,
                f"reason_{phase_prediction_reject_reason}",
            ),
            "rejected_age_us": rejected_phase_prediction_age_us,
            "maximum_observed_age_us": (
                maximum_observed_phase_prediction_age_us
            ),
            "maximum_age_us": maximum_phase_prediction_age_us,
        },
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


def query_velocity(client: Client) -> dict[str, Any]:
    body = client.transact(COMMAND_GET_VELOCITY_STATUS)
    if len(body) != VELOCITY_STATUS_BODY.size:
        raise ProtocolError("velocity-status response has an unexpected length")
    (
        schema,
        state,
        result,
        flags,
        fault_flags,
        target_velocity_q16_16,
        reference_velocity_q16_16,
        measured_velocity_q16_16,
        requested_q_current_counts,
        applied_q_current_counts,
        current_limit_counts,
        elapsed_millis,
        remaining_millis,
        maximum_target_velocity_q16_16,
        maximum_target_acceleration_q16_16,
        maximum_feedback_velocity_q16_16,
        maximum_current_counts,
        maximum_feedback_interval_us,
        proportional_gain_q16_16,
        integral_gain_q16_16,
        maximum_duration_millis,
    ) = VELOCITY_STATUS_BODY.unpack(body)
    return {
        "schema": schema,
        "state": VELOCITY_STATE_NAMES.get(state, f"state_{state}"),
        "result": VELOCITY_RESULT_NAMES.get(result, f"result_{result}"),
        "flags_hex": f"0x{flags:02X}",
        "flags": active_names(flags, VELOCITY_FLAG_NAMES),
        "fault_flags_hex": f"0x{fault_flags:08X}",
        "faults": active_names(fault_flags, VELOCITY_FAULT_NAMES),
        "target_velocity_revolutions_per_second": (
            target_velocity_q16_16 / 65536.0
        ),
        "reference_velocity_revolutions_per_second": (
            reference_velocity_q16_16 / 65536.0
        ),
        "measured_velocity_revolutions_per_second": (
            measured_velocity_q16_16 / 65536.0
        ),
        "requested_q_current_counts": requested_q_current_counts,
        "requested_q_current_nominal_milliamperes": round(
            requested_q_current_counts * COUNTS_TO_MILLIAMPERES, 1
        ),
        "applied_q_current_counts": applied_q_current_counts,
        "applied_q_current_nominal_milliamperes": round(
            applied_q_current_counts * COUNTS_TO_MILLIAMPERES, 1
        ),
        "current_limit_counts": current_limit_counts,
        "current_limit_nominal_milliamperes": round(
            current_limit_counts * COUNTS_TO_MILLIAMPERES, 1
        ),
        "elapsed_millis": elapsed_millis,
        "remaining_millis": remaining_millis,
        "policy": {
            "maximum_target_velocity_revolutions_per_second": (
                maximum_target_velocity_q16_16 / 65536.0
            ),
            "maximum_target_acceleration_revolutions_per_second2": (
                maximum_target_acceleration_q16_16 / 65536.0
            ),
            "maximum_feedback_velocity_revolutions_per_second": (
                maximum_feedback_velocity_q16_16 / 65536.0
            ),
            "maximum_current_counts": maximum_current_counts,
            "maximum_current_nominal_milliamperes": round(
                maximum_current_counts * COUNTS_TO_MILLIAMPERES, 1
            ),
            "maximum_feedback_interval_us": maximum_feedback_interval_us,
            "proportional_gain_current_counts_per_velocity": (
                proportional_gain_q16_16 / 65536.0
            ),
            "integral_gain_current_counts_per_position": (
                integral_gain_q16_16 / 65536.0
            ),
            "minimum_duration_millis": VELOCITY_MINIMUM_DURATION_MILLIS,
            "maximum_duration_millis": maximum_duration_millis,
        },
    }


def query_position(client: Client) -> dict[str, Any]:
    body = client.transact(COMMAND_GET_POSITION_STATUS)
    if len(body) != POSITION_STATUS_BODY.size:
        raise ProtocolError("position-status response has an unexpected length")
    (
        schema,
        state,
        result,
        flags,
        fault_flags,
        target_position_q16_16,
        reference_position_q16_16,
        measured_position_q16_16,
        reference_velocity_q16_16,
        target_velocity_q16_16,
        measured_velocity_q16_16,
        requested_q_current_counts,
        applied_q_current_counts,
        current_limit_counts,
        elapsed_millis,
        remaining_millis,
        maximum_relative_travel_q16_16,
        maximum_velocity_q16_16,
        maximum_acceleration_q16_16,
        maximum_following_error_q16_16,
    ) = POSITION_STATUS_BODY.unpack(body)
    return {
        "schema": schema,
        "state": POSITION_STATE_NAMES.get(state, f"state_{state}"),
        "result": POSITION_RESULT_NAMES.get(result, f"result_{result}"),
        "flags_hex": f"0x{flags:02X}",
        "flags": active_names(flags, POSITION_FLAG_NAMES),
        "fault_flags_hex": f"0x{fault_flags:08X}",
        "faults": active_names(fault_flags, POSITION_FAULT_NAMES),
        "target_position_revolutions": target_position_q16_16 / 65536.0,
        "reference_position_revolutions": reference_position_q16_16 / 65536.0,
        "measured_position_revolutions": measured_position_q16_16 / 65536.0,
        "reference_velocity_revolutions_per_second": (
            reference_velocity_q16_16 / 65536.0
        ),
        "target_velocity_revolutions_per_second": (
            target_velocity_q16_16 / 65536.0
        ),
        "measured_velocity_revolutions_per_second": (
            measured_velocity_q16_16 / 65536.0
        ),
        "requested_q_current_counts": requested_q_current_counts,
        "requested_q_current_nominal_milliamperes": round(
            requested_q_current_counts * COUNTS_TO_MILLIAMPERES, 1
        ),
        "applied_q_current_counts": applied_q_current_counts,
        "applied_q_current_nominal_milliamperes": round(
            applied_q_current_counts * COUNTS_TO_MILLIAMPERES, 1
        ),
        "current_limit_counts": current_limit_counts,
        "current_limit_nominal_milliamperes": round(
            current_limit_counts * COUNTS_TO_MILLIAMPERES, 1
        ),
        "elapsed_millis": elapsed_millis,
        "remaining_millis": remaining_millis,
        "policy": {
            "maximum_relative_travel_revolutions": (
                maximum_relative_travel_q16_16 / 65536.0
            ),
            "maximum_velocity_revolutions_per_second": (
                maximum_velocity_q16_16 / 65536.0
            ),
            "maximum_acceleration_revolutions_per_second2": (
                maximum_acceleration_q16_16 / 65536.0
            ),
            "maximum_following_error_revolutions": (
                maximum_following_error_q16_16 / 65536.0
            ),
            "minimum_duration_millis": POSITION_MINIMUM_DURATION_MILLIS,
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


def _write_json(path: Path, value: Any) -> None:
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def _joined_names(value: Any) -> str:
    return "|".join(str(item) for item in (value or []))


def _velocity_without_policy(status: dict[str, Any]) -> dict[str, Any]:
    return {key: value for key, value in status.items() if key != "policy"}


def _make_velocity_run_directory(
    root: Path,
    target_velocity_rps: float,
    current_limit_counts: int,
    duration_millis: int,
) -> Path:
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    target_label = f"{target_velocity_rps:+.3f}".replace("+", "p")
    target_label = target_label.replace("-", "m").replace(".", "p")
    label = (
        f"{timestamp}-{target_label}rps-"
        f"{current_limit_counts:05d}cnt-{duration_millis}ms"
    )
    path = root / label
    suffix = 2
    while path.exists():
        path = root / f"{label}-{suffix}"
        suffix += 1
    path.mkdir(parents=True)
    return path


def _query_velocity_capture_snapshot(
    client: Client,
    capture_start: float,
) -> dict[str, Any]:
    return {
        "host_elapsed_seconds": round(time.monotonic() - capture_start, 6),
        "velocity": query_velocity(client),
        "drive": query_status(client),
        "encoder": query_encoder(client),
    }


def _velocity_csv_row(snapshot: dict[str, Any]) -> dict[str, Any]:
    velocity = snapshot["velocity"]
    drive = snapshot["drive"]
    encoder = snapshot["encoder"]
    loop = drive.get("loop", {})
    adc = drive.get("adc", {})
    reset = drive.get("reset", {})
    estimator = encoder.get("estimator", {})
    reference = velocity["reference_velocity_revolutions_per_second"]
    measured = velocity["measured_velocity_revolutions_per_second"]

    return {
        "host_elapsed_seconds": snapshot["host_elapsed_seconds"],
        "controller_elapsed_millis": velocity["elapsed_millis"],
        "remaining_millis": velocity["remaining_millis"],
        "state": velocity["state"],
        "result": velocity["result"],
        "target_velocity_rps": (
            velocity["target_velocity_revolutions_per_second"]
        ),
        "reference_velocity_rps": reference,
        "measured_velocity_rps": measured,
        "velocity_error_rps": round(reference - measured, 6),
        "requested_q_current_amperes": nominal_amperes_from_counts(
            velocity["requested_q_current_counts"]
        ),
        "applied_q_current_amperes": nominal_amperes_from_counts(
            velocity["applied_q_current_counts"]
        ),
        "current_limit_amperes": nominal_amperes_from_counts(
            velocity["current_limit_counts"]
        ),
        "requested_q_current_counts": velocity["requested_q_current_counts"],
        "applied_q_current_counts": velocity["applied_q_current_counts"],
        "current_limit_counts": velocity["current_limit_counts"],
        "velocity_flags_hex": velocity["flags_hex"],
        "velocity_flags": _joined_names(velocity["flags"]),
        "velocity_fault_flags_hex": velocity["fault_flags_hex"],
        "velocity_faults": _joined_names(velocity["faults"]),
        "drive_flags_hex": drive.get("flags_hex", ""),
        "drive_flags": _joined_names(drive.get("flags")),
        "loop_fault_flags_hex": loop.get("fault_flags_hex", ""),
        "loop_faults": _joined_names(loop.get("faults")),
        "loop_sample_count": loop.get("sample_count", ""),
        "bus_voltage_volts": adc.get("bus_voltage_volts", ""),
        "phase_a_voltage_command_volts": loop.get(
            "phase_voltage_command_volts", {}
        ).get("a", ""),
        "phase_b_voltage_command_volts": loop.get(
            "phase_voltage_command_volts", {}
        ).get("b", ""),
        "phase_voltage_limit_volts": loop.get(
            "phase_voltage_limit_volts", ""
        ),
        "encoder_status": encoder.get("status", ""),
        "encoder_transport_status": encoder.get("transport_status", ""),
        "encoder_flags_hex": encoder.get("flags_hex", ""),
        "encoder_error_count": encoder.get("error_count", ""),
        "encoder_angle_raw": encoder.get("angle_raw", ""),
        "encoder_position_revolutions": estimator.get(
            "position_revolutions", ""
        ),
        "encoder_velocity_rps": estimator.get(
            "velocity_revolutions_per_second", ""
        ),
        "encoder_sample_interval_us": estimator.get("sample_interval_us", ""),
        "encoder_maximum_sample_interval_us": estimator.get(
            "maximum_sample_interval_us", ""
        ),
        "estimator_flags_hex": estimator.get("flags_hex", ""),
        "estimator_flags": _joined_names(estimator.get("flags")),
        "estimator_fault_flags_hex": estimator.get("fault_flags_hex", ""),
        "estimator_faults": _joined_names(estimator.get("faults")),
        "retained_panic": reset.get("retained_panic", ""),
        "watchdog_reset": reset.get("watchdog_reset", ""),
    }


def _new_velocity_capture_analysis() -> dict[str, Any]:
    return {
        "sample_count": 0,
        "sum_squared_velocity_error_rps2": 0.0,
        "maximum_absolute_velocity_error_rps": 0.0,
        "maximum_absolute_requested_q_current_counts": 0,
        "maximum_absolute_applied_q_current_counts": 0,
        "current_limit_sample_count": 0,
        "maximum_encoder_sample_interval_us": 0,
        "observed_states": set(),
        "faults": set(),
    }


def _update_velocity_capture_analysis(
    analysis: dict[str, Any],
    row: dict[str, Any],
) -> None:
    error = float(row["velocity_error_rps"])
    analysis["sample_count"] += 1
    analysis["sum_squared_velocity_error_rps2"] += error * error
    analysis["maximum_absolute_velocity_error_rps"] = max(
        analysis["maximum_absolute_velocity_error_rps"], abs(error)
    )
    analysis["maximum_absolute_requested_q_current_counts"] = max(
        analysis["maximum_absolute_requested_q_current_counts"],
        abs(int(row["requested_q_current_counts"])),
    )
    analysis["maximum_absolute_applied_q_current_counts"] = max(
        analysis["maximum_absolute_applied_q_current_counts"],
        abs(int(row["applied_q_current_counts"])),
    )
    if "current_at_limit" in str(row["velocity_flags"]).split("|"):
        analysis["current_limit_sample_count"] += 1
    encoder_interval = row["encoder_sample_interval_us"]
    if encoder_interval != "":
        analysis["maximum_encoder_sample_interval_us"] = max(
            analysis["maximum_encoder_sample_interval_us"],
            int(encoder_interval),
        )
    analysis["observed_states"].add(str(row["state"]))
    for prefix, field in (
        ("velocity", "velocity_faults"),
        ("current_loop", "loop_faults"),
        ("estimator", "estimator_faults"),
    ):
        for fault in filter(None, str(row[field]).split("|")):
            analysis["faults"].add(f"{prefix}_{fault}")
    if "fault_present" in str(row["drive_flags"]).split("|"):
        analysis["faults"].add("drive_supervisor_fault")
    if row["retained_panic"] not in {"", 0, "0"}:
        analysis["faults"].add("retained_panic")
    if row["watchdog_reset"] not in {"", False, 0, "0"}:
        analysis["faults"].add("watchdog_reset")


def _finalize_velocity_capture_analysis(
    analysis: dict[str, Any],
) -> dict[str, Any]:
    count = int(analysis["sample_count"])
    return {
        "sample_count": count,
        "rms_velocity_error_rps": (
            math.sqrt(analysis["sum_squared_velocity_error_rps2"] / count)
            if count
            else None
        ),
        "maximum_absolute_velocity_error_rps": analysis[
            "maximum_absolute_velocity_error_rps"
        ],
        "maximum_absolute_requested_q_current_counts": analysis[
            "maximum_absolute_requested_q_current_counts"
        ],
        "maximum_absolute_applied_q_current_counts": analysis[
            "maximum_absolute_applied_q_current_counts"
        ],
        "current_limit_sample_count": analysis["current_limit_sample_count"],
        "maximum_encoder_sample_interval_us": analysis[
            "maximum_encoder_sample_interval_us"
        ],
        "observed_states": sorted(analysis["observed_states"]),
        "faults": sorted(analysis["faults"]),
    }


def _velocity_live_line(
    row: dict[str, Any],
    duration_millis: int,
) -> str:
    faults = row["velocity_faults"] or row["loop_faults"] or "none"
    voltage_text = "Vbus=unavailable"
    if row["bus_voltage_volts"] not in {"", None}:
        phase_voltage = max(
            abs(float(row["phase_a_voltage_command_volts"])),
            abs(float(row["phase_b_voltage_command_volts"])),
        )
        voltage_text = (
            f"Vbus={float(row['bus_voltage_volts']):5.2f} V  "
            f"|Vph|={phase_voltage:5.2f}/"
            f"{float(row['phase_voltage_limit_volts']):5.2f} V"
        )
    return (
        f"{float(row['host_elapsed_seconds']):6.2f}/"
        f"{duration_millis / 1000.0:.2f} s  "
        f"{str(row['state']):8s}  "
        f"ref={float(row['reference_velocity_rps']):+7.3f}  "
        f"meas={float(row['measured_velocity_rps']):+7.3f} rps  "
        f"Iq={nominal_amperes_from_counts(int(row['applied_q_current_counts'])):+6.3f}/"
        f"{nominal_amperes_from_counts(int(row['current_limit_counts'])):.3f} A  "
        f"{voltage_text}  "
        f"enc={row['encoder_sample_interval_us']} us  "
        f"faults={faults}"
    )


def _run_velocity_capture(
    client: Client,
    args: argparse.Namespace,
    target_velocity_q16_16: int,
    current_limit_counts: int,
    initial_velocity_status: dict[str, Any],
) -> int:
    run_directory = _make_velocity_run_directory(
        args.output_root,
        args.rps,
        current_limit_counts,
        args.duration_ms,
    )
    metadata_path = run_directory / "metadata.json"
    telemetry_path = run_directory / "telemetry.csv"
    full_jsonl_path = run_directory / "telemetry.jsonl"
    current_trace_path = run_directory / "current_trace.csv"
    initial_drive = query_status(client)
    initial_encoder = query_encoder(client)
    metadata: dict[str, Any] = {
        "schema": 1,
        "generated_at": datetime.now().astimezone().isoformat(timespec="seconds"),
        "request": {
            "target_velocity_revolutions_per_second": args.rps,
            "target_velocity_q16_16": target_velocity_q16_16,
            "current_limit_counts": current_limit_counts,
            "current_limit_nominal_milliamperes": round(
                current_limit_counts * COUNTS_TO_MILLIAMPERES, 1
            ),
            "duration_millis": args.duration_ms,
            "capture_interval_seconds": args.interval,
            "scheduled_stop_after_seconds": args.stop_after_seconds,
            "trace_at_seconds": args.trace_at_seconds,
        },
        "transport": {
            "port": args.port,
            "baud": args.baud,
            "address": args.address,
            "timeout_seconds": args.timeout,
        },
        "identity": query_identity(client),
        "configuration": query_configuration(client),
        "policy": initial_velocity_status["policy"],
        "initial": {
            "velocity": _velocity_without_policy(initial_velocity_status),
            "drive": initial_drive,
            "encoder": initial_encoder,
        },
        "capture": {
            "telemetry_csv": telemetry_path.name,
            "full_jsonl": full_jsonl_path.name if args.jsonl else None,
            "current_trace_csv": (
                current_trace_path.name
                if args.trace_at_seconds is not None
                else None
            ),
            "terminal_update_interval_seconds": (
                None if args.quiet else VELOCITY_CONSOLE_INTERVAL_SECONDS
            ),
            "status": "prepared",
        },
    }
    _write_json(metadata_path, metadata)
    print(f"Capture: {run_directory.resolve()}")
    if not args.quiet:
        print("Press Ctrl+C at any time to send STOP.")

    analysis = _new_velocity_capture_analysis()
    capture_start = time.monotonic()
    host_deadline = capture_start + args.duration_ms / 1000.0 + 5.0
    next_console_update = capture_start
    start_attempted = False
    terminal = False
    interrupted = False
    console_active = False
    stop_error: str | None = None
    capture_error: Exception | None = None
    final_snapshot: dict[str, Any] | None = None
    scheduled_stop_sent = False
    trace_arm_sent = False
    trace_arm_host_elapsed_seconds: float | None = None
    exit_code = 2

    with telemetry_path.open("w", encoding="utf-8", newline="") as csv_stream:
        writer = csv.DictWriter(csv_stream, fieldnames=VELOCITY_TELEMETRY_FIELDS)
        writer.writeheader()
        csv_stream.flush()
        jsonl_stream = (
            full_jsonl_path.open("w", encoding="utf-8") if args.jsonl else None
        )
        try:
            try:
                start_attempted = True
                client.transact(
                    COMMAND_START_VELOCITY,
                    struct.pack(
                        ">iHI",
                        target_velocity_q16_16,
                        current_limit_counts,
                        args.duration_ms,
                    ),
                )
                while True:
                    snapshot = _query_velocity_capture_snapshot(
                        client, capture_start
                    )
                    final_snapshot = snapshot
                    row = _velocity_csv_row(snapshot)
                    writer.writerow(row)
                    csv_stream.flush()
                    if jsonl_stream is not None:
                        jsonl_stream.write(
                            json.dumps(snapshot, sort_keys=True) + "\n"
                        )
                        jsonl_stream.flush()
                    _update_velocity_capture_analysis(analysis, row)
                    terminal = row["state"] in {"complete", "stopped", "failed"}
                    now = time.monotonic()
                    if (
                        not terminal
                        and args.trace_at_seconds is not None
                        and not trace_arm_sent
                        and now - capture_start >= args.trace_at_seconds
                    ):
                        arm_current_trace(client)
                        trace_arm_sent = True
                        trace_arm_host_elapsed_seconds = round(
                            now - capture_start, 6
                        )
                    if not args.quiet and (
                        terminal or now >= next_console_update
                    ):
                        print(
                            "\r"
                            + _velocity_live_line(
                                row, args.duration_ms
                            ).ljust(120),
                            end="",
                            flush=True,
                        )
                        console_active = True
                        next_console_update = (
                            now + VELOCITY_CONSOLE_INTERVAL_SECONDS
                        )
                    if terminal:
                        exit_code = (
                            0
                            if row["state"] == "complete"
                            or (
                                row["state"] == "stopped"
                                and scheduled_stop_sent
                            )
                            else 3
                        )
                        break
                    if (
                        args.stop_after_seconds is not None
                        and not scheduled_stop_sent
                        and now - capture_start >= args.stop_after_seconds
                    ):
                        stop_drive(client)
                        scheduled_stop_sent = True
                    if time.monotonic() > host_deadline:
                        raise ProtocolError(
                            "velocity command did not release authority "
                            "before the host deadline"
                        )
                    time.sleep(args.interval)
            except KeyboardInterrupt:
                interrupted = True
                exit_code = 130
            except (ProtocolError, OSError, ValueError) as error:
                capture_error = error
                exit_code = 2
            finally:
                if start_attempted and not terminal:
                    try:
                        stop_drive(client)
                    except (ProtocolError, OSError) as error:
                        stop_error = str(error)
                    try:
                        final_snapshot = _query_velocity_capture_snapshot(
                            client, capture_start
                        )
                        row = _velocity_csv_row(final_snapshot)
                        writer.writerow(row)
                        csv_stream.flush()
                        if jsonl_stream is not None:
                            jsonl_stream.write(
                                json.dumps(final_snapshot, sort_keys=True) + "\n"
                            )
                            jsonl_stream.flush()
                        _update_velocity_capture_analysis(analysis, row)
                    except (ProtocolError, OSError, ValueError) as error:
                        if capture_error is None:
                            capture_error = error
                            exit_code = 2
        finally:
            if jsonl_stream is not None:
                jsonl_stream.close()

    if console_active:
        print()
    trace_samples: list[dict[str, Any]] = []
    if trace_arm_sent:
        try:
            trace_samples = read_current_trace(client)
            write_current_trace_csv(current_trace_path, trace_samples)
        except (ProtocolError, OSError, ValueError) as error:
            if capture_error is None:
                capture_error = error
                exit_code = 2
    final_analysis = _finalize_velocity_capture_analysis(analysis)
    metadata["completed_at"] = datetime.now().astimezone().isoformat(
        timespec="seconds"
    )
    metadata["final"] = (
        {
            **final_snapshot,
            "velocity": _velocity_without_policy(final_snapshot["velocity"]),
        }
        if final_snapshot is not None
        else None
    )
    metadata["analysis"] = final_analysis
    metadata["capture"].update(
        {
            "status": (
                "interrupted"
                if interrupted
                else "error"
                if capture_error is not None
                else "complete"
                if exit_code == 0
                else "device_failed"
            ),
            "exit_code": exit_code,
            "stop_error": stop_error,
            "scheduled_stop_sent": scheduled_stop_sent,
            "trace_arm_sent": trace_arm_sent,
            "trace_arm_host_elapsed_seconds": (
                trace_arm_host_elapsed_seconds
            ),
            "trace_sample_count": len(trace_samples),
            "trace_duration_seconds": (
                trace_samples[-1]["time_seconds"]
                if trace_samples
                else None
            ),
            "error": str(capture_error) if capture_error is not None else None,
        }
    )
    _write_json(metadata_path, metadata)
    rms_error = final_analysis["rms_velocity_error_rps"]
    rms_text = "unavailable" if rms_error is None else f"{rms_error:.4f} rps"
    final_state = (
        final_snapshot["velocity"]["state"]
        if final_snapshot is not None
        else "unavailable"
    )
    print(
        f"Result: state={final_state}, samples={final_analysis['sample_count']}, "
        f"RMS error={rms_text}, faults={final_analysis['faults'] or 'none'}"
    )
    if stop_error is not None:
        print(
            "error: STOP was not acknowledged; rely on the firmware deadline "
            f"or press the Right button: {stop_error}",
            file=sys.stderr,
        )
    if capture_error is not None:
        raise capture_error
    return exit_code


def _position_without_policy(status: dict[str, Any]) -> dict[str, Any]:
    return {key: value for key, value in status.items() if key != "policy"}


def _make_position_run_directory(
    root: Path,
    displacement_revolutions: float,
    maximum_velocity_rps: float,
    current_limit_counts: int,
    duration_millis: int,
) -> Path:
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    displacement_label = f"{displacement_revolutions:+.3f}".replace(
        "+", "p"
    )
    displacement_label = displacement_label.replace("-", "m").replace(
        ".", "p"
    )
    velocity_label = f"{maximum_velocity_rps:.3f}".replace(".", "p")
    label = (
        f"{timestamp}-{displacement_label}rev-"
        f"{velocity_label}rps-{current_limit_counts:05d}cnt-"
        f"{duration_millis}ms"
    )
    path = root / label
    suffix = 2
    while path.exists():
        path = root / f"{label}-{suffix}"
        suffix += 1
    path.mkdir(parents=True)
    return path


def _query_position_capture_snapshot(
    client: Client,
    capture_start: float,
) -> dict[str, Any]:
    return {
        "host_elapsed_seconds": round(time.monotonic() - capture_start, 6),
        "position": query_position(client),
        "drive": query_status(client),
        "encoder": query_encoder(client),
    }


def _position_csv_row(snapshot: dict[str, Any]) -> dict[str, Any]:
    position = snapshot["position"]
    drive = snapshot["drive"]
    encoder = snapshot["encoder"]
    loop = drive.get("loop", {})
    adc = drive.get("adc", {})
    reset = drive.get("reset", {})
    estimator = encoder.get("estimator", {})
    target = position["target_position_revolutions"]
    reference = position["reference_position_revolutions"]
    measured = position["measured_position_revolutions"]

    return {
        "host_elapsed_seconds": snapshot["host_elapsed_seconds"],
        "controller_elapsed_millis": position["elapsed_millis"],
        "remaining_millis": position["remaining_millis"],
        "state": position["state"],
        "result": position["result"],
        "target_position_revolutions": target,
        "reference_position_revolutions": reference,
        "measured_position_revolutions": measured,
        "profile_following_error_revolutions": round(
            reference - measured, 6
        ),
        "target_position_error_revolutions": round(target - measured, 6),
        "reference_velocity_rps": position[
            "reference_velocity_revolutions_per_second"
        ],
        "target_velocity_rps": position[
            "target_velocity_revolutions_per_second"
        ],
        "measured_velocity_rps": position[
            "measured_velocity_revolutions_per_second"
        ],
        "requested_q_current_amperes": nominal_amperes_from_counts(
            position["requested_q_current_counts"]
        ),
        "applied_q_current_amperes": nominal_amperes_from_counts(
            position["applied_q_current_counts"]
        ),
        "current_limit_amperes": nominal_amperes_from_counts(
            position["current_limit_counts"]
        ),
        "requested_q_current_counts": position["requested_q_current_counts"],
        "applied_q_current_counts": position["applied_q_current_counts"],
        "current_limit_counts": position["current_limit_counts"],
        "position_flags_hex": position["flags_hex"],
        "position_flags": _joined_names(position["flags"]),
        "position_fault_flags_hex": position["fault_flags_hex"],
        "position_faults": _joined_names(position["faults"]),
        "drive_flags_hex": drive.get("flags_hex", ""),
        "drive_flags": _joined_names(drive.get("flags")),
        "loop_fault_flags_hex": loop.get("fault_flags_hex", ""),
        "loop_faults": _joined_names(loop.get("faults")),
        "loop_sample_count": loop.get("sample_count", ""),
        "bus_voltage_volts": adc.get("bus_voltage_volts", ""),
        "phase_a_voltage_command_volts": loop.get(
            "phase_voltage_command_volts", {}
        ).get("a", ""),
        "phase_b_voltage_command_volts": loop.get(
            "phase_voltage_command_volts", {}
        ).get("b", ""),
        "phase_voltage_limit_volts": loop.get(
            "phase_voltage_limit_volts", ""
        ),
        "encoder_status": encoder.get("status", ""),
        "encoder_transport_status": encoder.get("transport_status", ""),
        "encoder_flags_hex": encoder.get("flags_hex", ""),
        "encoder_error_count": encoder.get("error_count", ""),
        "encoder_angle_raw": encoder.get("angle_raw", ""),
        "encoder_position_revolutions": estimator.get(
            "position_revolutions", ""
        ),
        "encoder_velocity_rps": estimator.get(
            "velocity_revolutions_per_second", ""
        ),
        "encoder_sample_interval_us": estimator.get("sample_interval_us", ""),
        "encoder_maximum_sample_interval_us": estimator.get(
            "maximum_sample_interval_us", ""
        ),
        "estimator_flags_hex": estimator.get("flags_hex", ""),
        "estimator_flags": _joined_names(estimator.get("flags")),
        "estimator_fault_flags_hex": estimator.get("fault_flags_hex", ""),
        "estimator_faults": _joined_names(estimator.get("faults")),
        "retained_panic": reset.get("retained_panic", ""),
        "watchdog_reset": reset.get("watchdog_reset", ""),
    }


def _new_position_capture_analysis() -> dict[str, Any]:
    return {
        "sample_count": 0,
        "sum_squared_profile_following_error_revolutions2": 0.0,
        "maximum_absolute_profile_following_error_revolutions": 0.0,
        "maximum_absolute_target_position_error_revolutions": 0.0,
        "final_target_position_error_revolutions": None,
        "maximum_absolute_reference_velocity_rps": 0.0,
        "maximum_absolute_target_velocity_rps": 0.0,
        "maximum_absolute_measured_velocity_rps": 0.0,
        "maximum_absolute_requested_q_current_counts": 0,
        "maximum_absolute_applied_q_current_counts": 0,
        "current_limit_sample_count": 0,
        "maximum_encoder_sample_interval_us": 0,
        "observed_states": set(),
        "faults": set(),
    }


def _update_position_capture_analysis(
    analysis: dict[str, Any],
    row: dict[str, Any],
) -> None:
    following_error = float(row["profile_following_error_revolutions"])
    target_error = float(row["target_position_error_revolutions"])
    analysis["sample_count"] += 1
    analysis["sum_squared_profile_following_error_revolutions2"] += (
        following_error * following_error
    )
    analysis["maximum_absolute_profile_following_error_revolutions"] = max(
        analysis["maximum_absolute_profile_following_error_revolutions"],
        abs(following_error),
    )
    analysis["maximum_absolute_target_position_error_revolutions"] = max(
        analysis["maximum_absolute_target_position_error_revolutions"],
        abs(target_error),
    )
    analysis["final_target_position_error_revolutions"] = target_error
    for analysis_field, row_field in (
        ("maximum_absolute_reference_velocity_rps", "reference_velocity_rps"),
        ("maximum_absolute_target_velocity_rps", "target_velocity_rps"),
        ("maximum_absolute_measured_velocity_rps", "measured_velocity_rps"),
    ):
        analysis[analysis_field] = max(
            analysis[analysis_field], abs(float(row[row_field]))
        )
    analysis["maximum_absolute_requested_q_current_counts"] = max(
        analysis["maximum_absolute_requested_q_current_counts"],
        abs(int(row["requested_q_current_counts"])),
    )
    analysis["maximum_absolute_applied_q_current_counts"] = max(
        analysis["maximum_absolute_applied_q_current_counts"],
        abs(int(row["applied_q_current_counts"])),
    )
    if "current_at_limit" in str(row["position_flags"]).split("|"):
        analysis["current_limit_sample_count"] += 1
    encoder_interval = row["encoder_sample_interval_us"]
    if encoder_interval != "":
        analysis["maximum_encoder_sample_interval_us"] = max(
            analysis["maximum_encoder_sample_interval_us"],
            int(encoder_interval),
        )
    analysis["observed_states"].add(str(row["state"]))
    for prefix, field in (
        ("position", "position_faults"),
        ("current_loop", "loop_faults"),
        ("estimator", "estimator_faults"),
    ):
        for fault in filter(None, str(row[field]).split("|")):
            analysis["faults"].add(f"{prefix}_{fault}")
    if "fault_present" in str(row["drive_flags"]).split("|"):
        analysis["faults"].add("drive_supervisor_fault")
    if row["retained_panic"] not in {"", 0, "0"}:
        analysis["faults"].add("retained_panic")
    if row["watchdog_reset"] not in {"", False, 0, "0"}:
        analysis["faults"].add("watchdog_reset")


def _finalize_position_capture_analysis(
    analysis: dict[str, Any],
) -> dict[str, Any]:
    count = int(analysis["sample_count"])
    return {
        "sample_count": count,
        "rms_profile_following_error_revolutions": (
            math.sqrt(
                analysis[
                    "sum_squared_profile_following_error_revolutions2"
                ] / count
            )
            if count
            else None
        ),
        "maximum_absolute_profile_following_error_revolutions": analysis[
            "maximum_absolute_profile_following_error_revolutions"
        ],
        "maximum_absolute_target_position_error_revolutions": analysis[
            "maximum_absolute_target_position_error_revolutions"
        ],
        "final_target_position_error_revolutions": analysis[
            "final_target_position_error_revolutions"
        ],
        "maximum_absolute_reference_velocity_rps": analysis[
            "maximum_absolute_reference_velocity_rps"
        ],
        "maximum_absolute_target_velocity_rps": analysis[
            "maximum_absolute_target_velocity_rps"
        ],
        "maximum_absolute_measured_velocity_rps": analysis[
            "maximum_absolute_measured_velocity_rps"
        ],
        "maximum_absolute_requested_q_current_counts": analysis[
            "maximum_absolute_requested_q_current_counts"
        ],
        "maximum_absolute_applied_q_current_counts": analysis[
            "maximum_absolute_applied_q_current_counts"
        ],
        "current_limit_sample_count": analysis["current_limit_sample_count"],
        "maximum_encoder_sample_interval_us": analysis[
            "maximum_encoder_sample_interval_us"
        ],
        "observed_states": sorted(analysis["observed_states"]),
        "faults": sorted(analysis["faults"]),
    }


def _position_live_line(
    row: dict[str, Any],
    duration_millis: int,
) -> str:
    faults = row["position_faults"] or row["loop_faults"] or "none"
    voltage_text = "Vbus=unavailable"
    if row["bus_voltage_volts"] not in {"", None}:
        phase_voltage = max(
            abs(float(row["phase_a_voltage_command_volts"])),
            abs(float(row["phase_b_voltage_command_volts"])),
        )
        voltage_text = (
            f"Vbus={float(row['bus_voltage_volts']):5.2f} V  "
            f"|Vph|={phase_voltage:5.2f}/"
            f"{float(row['phase_voltage_limit_volts']):5.2f} V"
        )
    return (
        f"{float(row['host_elapsed_seconds']):6.2f}/"
        f"{duration_millis / 1000.0:.2f} s  "
        f"{str(row['state']):8s}  "
        f"tgt={float(row['target_position_revolutions']):+8.4f}  "
        f"ref={float(row['reference_position_revolutions']):+8.4f}  "
        f"meas={float(row['measured_position_revolutions']):+8.4f} rev  "
        f"err={float(row['profile_following_error_revolutions']):+7.4f}  "
        f"Iq={nominal_amperes_from_counts(int(row['applied_q_current_counts'])):+6.3f}/"
        f"{nominal_amperes_from_counts(int(row['current_limit_counts'])):.3f} A  "
        f"{voltage_text}  "
        f"faults={faults}"
    )


def _run_position_capture(
    client: Client,
    args: argparse.Namespace,
    displacement_q16_16: int,
    maximum_velocity_q16_16: int,
    maximum_acceleration_q16_16: int,
    current_limit_counts: int,
    initial_position_status: dict[str, Any],
    initial_velocity_status: dict[str, Any],
) -> int:
    maximum_velocity_rps = maximum_velocity_q16_16 / 65536.0
    run_directory = _make_position_run_directory(
        args.output_root,
        args.revolutions,
        maximum_velocity_rps,
        current_limit_counts,
        args.duration_ms,
    )
    metadata_path = run_directory / "metadata.json"
    telemetry_path = run_directory / "telemetry.csv"
    full_jsonl_path = run_directory / "telemetry.jsonl"
    initial_drive = query_status(client)
    initial_encoder = query_encoder(client)
    metadata: dict[str, Any] = {
        "schema": 1,
        "generated_at": datetime.now().astimezone().isoformat(
            timespec="seconds"
        ),
        "request": {
            "relative_displacement_revolutions": args.revolutions,
            "relative_displacement_q16_16": displacement_q16_16,
            "maximum_velocity_revolutions_per_second": maximum_velocity_rps,
            "maximum_velocity_q16_16": maximum_velocity_q16_16,
            "maximum_acceleration_revolutions_per_second2": (
                maximum_acceleration_q16_16 / 65536.0
            ),
            "maximum_acceleration_q16_16": maximum_acceleration_q16_16,
            "current_limit_counts": current_limit_counts,
            "current_limit_nominal_milliamperes": round(
                current_limit_counts * COUNTS_TO_MILLIAMPERES, 1
            ),
            "duration_millis": args.duration_ms,
            "capture_interval_seconds": args.interval,
            "scheduled_stop_after_seconds": args.stop_after_seconds,
        },
        "transport": {
            "port": args.port,
            "baud": args.baud,
            "address": args.address,
            "timeout_seconds": args.timeout,
        },
        "identity": query_identity(client),
        "configuration": query_configuration(client),
        "policy": {
            "position": initial_position_status["policy"],
            "velocity_actuator": initial_velocity_status["policy"],
        },
        "initial": {
            "position": _position_without_policy(initial_position_status),
            "drive": initial_drive,
            "encoder": initial_encoder,
        },
        "capture": {
            "telemetry_csv": telemetry_path.name,
            "full_jsonl": full_jsonl_path.name if args.jsonl else None,
            "terminal_update_interval_seconds": (
                None if args.quiet else POSITION_CONSOLE_INTERVAL_SECONDS
            ),
            "status": "prepared",
        },
    }
    _write_json(metadata_path, metadata)
    print(f"Capture: {run_directory.resolve()}")
    if not args.quiet:
        print("Press Ctrl+C at any time to send STOP.")

    analysis = _new_position_capture_analysis()
    capture_start = time.monotonic()
    host_deadline = capture_start + args.duration_ms / 1000.0 + 5.0
    next_console_update = capture_start
    start_attempted = False
    terminal = False
    interrupted = False
    console_active = False
    stop_error: str | None = None
    capture_error: Exception | None = None
    final_snapshot: dict[str, Any] | None = None
    scheduled_stop_sent = False
    exit_code = 2

    with telemetry_path.open("w", encoding="utf-8", newline="") as csv_stream:
        writer = csv.DictWriter(csv_stream, fieldnames=POSITION_TELEMETRY_FIELDS)
        writer.writeheader()
        csv_stream.flush()
        jsonl_stream = (
            full_jsonl_path.open("w", encoding="utf-8") if args.jsonl else None
        )
        try:
            try:
                start_attempted = True
                client.transact(
                    COMMAND_START_POSITION_RELATIVE,
                    struct.pack(
                        ">iiiHI",
                        displacement_q16_16,
                        maximum_velocity_q16_16,
                        maximum_acceleration_q16_16,
                        current_limit_counts,
                        args.duration_ms,
                    ),
                )
                while True:
                    snapshot = _query_position_capture_snapshot(
                        client, capture_start
                    )
                    final_snapshot = snapshot
                    row = _position_csv_row(snapshot)
                    writer.writerow(row)
                    csv_stream.flush()
                    if jsonl_stream is not None:
                        jsonl_stream.write(
                            json.dumps(snapshot, sort_keys=True) + "\n"
                        )
                        jsonl_stream.flush()
                    _update_position_capture_analysis(analysis, row)
                    terminal = row["state"] in {
                        "complete",
                        "stopped",
                        "failed",
                    }
                    now = time.monotonic()
                    if not args.quiet and (
                        terminal or now >= next_console_update
                    ):
                        print(
                            "\r"
                            + _position_live_line(
                                row, args.duration_ms
                            ).ljust(140),
                            end="",
                            flush=True,
                        )
                        console_active = True
                        next_console_update = (
                            now + POSITION_CONSOLE_INTERVAL_SECONDS
                        )
                    if terminal:
                        exit_code = (
                            0
                            if row["result"] == "settled"
                            or (
                                row["state"] == "stopped"
                                and scheduled_stop_sent
                            )
                            else 3
                        )
                        break
                    if (
                        args.stop_after_seconds is not None
                        and not scheduled_stop_sent
                        and now - capture_start >= args.stop_after_seconds
                    ):
                        stop_drive(client)
                        scheduled_stop_sent = True
                    if time.monotonic() > host_deadline:
                        raise ProtocolError(
                            "position command did not release authority "
                            "before the host deadline"
                        )
                    time.sleep(args.interval)
            except KeyboardInterrupt:
                interrupted = True
                exit_code = 130
            except (ProtocolError, OSError, ValueError) as error:
                capture_error = error
                exit_code = 2
            finally:
                if start_attempted and not terminal:
                    try:
                        stop_drive(client)
                    except (ProtocolError, OSError) as error:
                        stop_error = str(error)
                    try:
                        final_snapshot = _query_position_capture_snapshot(
                            client, capture_start
                        )
                        row = _position_csv_row(final_snapshot)
                        writer.writerow(row)
                        csv_stream.flush()
                        if jsonl_stream is not None:
                            jsonl_stream.write(
                                json.dumps(final_snapshot, sort_keys=True)
                                + "\n"
                            )
                            jsonl_stream.flush()
                        _update_position_capture_analysis(analysis, row)
                    except (ProtocolError, OSError, ValueError) as error:
                        if capture_error is None:
                            capture_error = error
                            exit_code = 2
        finally:
            if jsonl_stream is not None:
                jsonl_stream.close()

    if console_active:
        print()
    final_analysis = _finalize_position_capture_analysis(analysis)
    metadata["completed_at"] = datetime.now().astimezone().isoformat(
        timespec="seconds"
    )
    metadata["final"] = (
        {
            **final_snapshot,
            "position": _position_without_policy(final_snapshot["position"]),
        }
        if final_snapshot is not None
        else None
    )
    metadata["analysis"] = final_analysis
    metadata["capture"].update(
        {
            "status": (
                "interrupted"
                if interrupted
                else "error"
                if capture_error is not None
                else "complete"
                if exit_code == 0
                else "device_failed"
            ),
            "exit_code": exit_code,
            "stop_error": stop_error,
            "scheduled_stop_sent": scheduled_stop_sent,
            "error": str(capture_error) if capture_error is not None else None,
        }
    )
    _write_json(metadata_path, metadata)
    endpoint_error = final_analysis["final_target_position_error_revolutions"]
    endpoint_text = (
        "unavailable"
        if endpoint_error is None
        else f"{endpoint_error:+.6f} rev"
    )
    final_state = (
        final_snapshot["position"]["state"]
        if final_snapshot is not None
        else "unavailable"
    )
    final_result = (
        final_snapshot["position"]["result"]
        if final_snapshot is not None
        else "unavailable"
    )
    print(
        f"Result: state={final_state}, result={final_result}, "
        f"samples={final_analysis['sample_count']}, "
        f"endpoint error={endpoint_text}, "
        f"faults={final_analysis['faults'] or 'none'}"
    )
    if stop_error is not None:
        print(
            "error: STOP was not acknowledged; rely on the firmware deadline "
            f"or press the Right button: {stop_error}",
            file=sys.stderr,
        )
    if capture_error is not None:
        raise capture_error
    return exit_code


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
        "save-configuration", help="persist the full active motor configuration"
    )
    commands.add_parser(
        "clear-calibration", help="persistently invalidate motor alignment"
    )
    set_gains = commands.add_parser(
        "set-current-loop-gains",
        help="apply volatile current-loop gains without saving them",
    )
    set_gains.add_argument(
        "--kp", type=float, required=True,
        help="proportional gain in PWM-permille Q16 units per ADC count",
    )
    set_gains.add_argument(
        "--ki", type=float, required=True,
        help="integral gain in PWM-permille Q16 units per count per 20 kHz step",
    )
    commands.add_parser(
        "revert-current-loop-gains",
        help="restore volatile current-loop gains from stored configuration",
    )
    commands.add_parser(
        "torque-status", help="read aligned q-current progress and policy"
    )
    commands.add_parser(
        "velocity-status", help="read velocity-loop progress and policy"
    )
    commands.add_parser(
        "position-status", help="read position-loop progress and policy"
    )
    trace = commands.add_parser(
        "trace", help="read the completed 20 kHz current-loop burst trace"
    )
    trace.add_argument("--output", help="write JSON lines to this path")
    commands.add_parser(
        "arm-trace", help="re-arm the one-shot trace during active motion"
    )

    configure = commands.add_parser("configure", help="set test demand")
    configure_current = configure.add_mutually_exclusive_group(required=True)
    configure_current.add_argument("--current-ma", type=float)
    configure_current.add_argument("--counts", type=int)
    configure.add_argument("--frequency-hz", type=float, required=True)

    start = commands.add_parser("start", help="start a bounded remote run")
    start.add_argument("--leg", choices=LEG_VALUES, default="A1")
    start.add_argument("--duration-ms", type=int, default=10000)
    start.add_argument(
        "--ramp-duration-ms",
        type=int,
        default=0,
        help="frequency ramp before the full duration-ms hold window",
    )

    commands.add_parser("stop", help="stop any active drive operation")
    commands.add_parser(
        "clear-faults",
        help="acknowledge and reset latched drive faults in place",
    )

    align = commands.add_parser(
        "align", help="run the bounded production alignment procedure"
    )
    alignment_current = align.add_mutually_exclusive_group(required=True)
    alignment_current.add_argument("--current-ma", type=float)
    alignment_current.add_argument("--counts", type=int)
    align.add_argument("--interval", type=float, default=0.1)

    torque = commands.add_parser(
        "torque", help="run a bounded encoder-aligned q-current demand"
    )
    torque_current = torque.add_mutually_exclusive_group(required=True)
    torque_current.add_argument("--current-ma", type=float)
    torque_current.add_argument("--counts", type=int)
    torque.add_argument("--duration-ms", type=int, default=250)
    torque.add_argument("--interval", type=float, default=0.05)

    velocity = commands.add_parser(
        "velocity", help="run a bounded closed-loop velocity demand"
    )
    velocity_speed = velocity.add_mutually_exclusive_group(required=True)
    velocity_speed.add_argument("--rps", type=float)
    velocity_speed.add_argument("--rpm", type=float)
    velocity_current = velocity.add_mutually_exclusive_group(required=True)
    velocity_current.add_argument("--current-limit-ma", type=float)
    velocity_current.add_argument("--current-limit-counts", type=int)
    velocity.add_argument("--duration-ms", type=int, default=5000)
    velocity.add_argument("--interval", type=float, default=0.05)
    velocity.add_argument(
        "--stop-after-seconds",
        type=float,
        help=(
            "send generic STOP over the active connection after this many "
            "seconds, for deterministic shutdown qualification"
        ),
    )
    velocity.add_argument(
        "--trace-at-seconds",
        type=float,
        help=(
            "re-arm the 256-sample timing/current burst this many seconds "
            "after the velocity command starts"
        ),
    )
    velocity.add_argument(
        "--output-root",
        type=Path,
        default=Path("scratch/velocity-runs"),
        help="parent directory for timestamped capture directories",
    )
    velocity.add_argument(
        "--jsonl",
        action="store_true",
        help="also retain full nested snapshots as JSON lines",
    )
    velocity.add_argument(
        "--quiet",
        action="store_true",
        help="suppress the live terminal status line",
    )

    position = commands.add_parser(
        "position", help="run a bounded relative closed-loop position move"
    )
    position.add_argument("--revolutions", type=float, required=True)
    position_speed = position.add_mutually_exclusive_group(required=True)
    position_speed.add_argument("--max-rps", type=float)
    position_speed.add_argument("--max-rpm", type=float)
    position.add_argument(
        "--acceleration-rps2",
        type=float,
        required=True,
        help="positive trajectory acceleration in revolutions/second^2",
    )
    position_current = position.add_mutually_exclusive_group(required=True)
    position_current.add_argument("--current-limit-ma", type=float)
    position_current.add_argument("--current-limit-counts", type=int)
    position.add_argument("--duration-ms", type=int, default=10000)
    position.add_argument("--interval", type=float, default=0.05)
    position.add_argument(
        "--stop-after-seconds",
        type=float,
        help=(
            "send generic STOP over the active connection after this many "
            "seconds, for deterministic shutdown qualification"
        ),
    )
    position.add_argument(
        "--output-root",
        type=Path,
        default=Path("scratch/position-runs"),
        help="parent directory for timestamped capture directories",
    )
    position.add_argument(
        "--jsonl",
        action="store_true",
        help="also retain full nested snapshots as JSON lines",
    )
    position.add_argument(
        "--quiet",
        action="store_true",
        help="suppress the live terminal status line",
    )

    watch = commands.add_parser("watch", help="stream status as JSON lines")
    watch.add_argument("--interval", type=float, default=0.2)

    run = commands.add_parser("run", help="start and watch a bounded run")
    run.add_argument("--leg", choices=LEG_VALUES, default="A1")
    run.add_argument("--duration-ms", type=int, default=10000)
    run.add_argument(
        "--ramp-duration-ms",
        type=int,
        default=0,
        help="frequency ramp before the full duration-ms hold window",
    )
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
            print_json(query_identity(client))
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
        elif args.command == "set-current-loop-gains":
            configuration = query_configuration(client)
            updated = set_current_loop_gains(
                client, args.kp, args.ki, configuration
            )
            print_current_loop_gain_summary(updated)
        elif args.command == "revert-current-loop-gains":
            print_current_loop_gain_summary(
                revert_current_loop_gains(client)
            )
        elif args.command == "torque-status":
            print_json(query_aligned_torque(client))
        elif args.command == "velocity-status":
            print_json(query_velocity(client))
        elif args.command == "position-status":
            print_json(query_position(client))
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
        elif args.command == "arm-trace":
            arm_current_trace(client)
            print("current trace armed")
        elif args.command == "configure":
            if args.counts is not None:
                amplitude_counts = args.counts
            else:
                if (
                    args.current_ma is None
                    or not math.isfinite(args.current_ma)
                    or args.current_ma <= 0.0
                ):
                    raise ProtocolError("--current-ma must be positive")
                amplitude_counts = round(
                    args.current_ma / COUNTS_TO_MILLIAMPERES
                )
            if not 1 <= amplitude_counts <= 0xFFFF:
                raise ProtocolError(
                    "test current must encode as 1..65535 counts"
                )
            frequency_millihz = round(args.frequency_hz * 1000.0)
            body = client.transact(
                COMMAND_CONFIGURE_CURRENT_TEST,
                struct.pack(">HI", amplitude_counts, frequency_millihz),
            )
            if len(body) != 6:
                raise ProtocolError("configure response has an unexpected length")
            amplitude, frequency_millihz = struct.unpack(">HI", body)
            print_json(
                {
                    "amplitude_counts": amplitude,
                    "amplitude_nominal_milliamperes": round(
                        amplitude * COUNTS_TO_MILLIAMPERES, 1
                    ),
                    "frequency_hz": frequency_millihz / 1000.0,
                }
            )
        elif args.command == "start":
            if args.ramp_duration_ms < 0:
                raise ProtocolError("--ramp-duration-ms must be nonnegative")
            client.transact(
                COMMAND_START_CURRENT_TEST,
                (
                    struct.pack(
                        ">BII",
                        LEG_VALUES[args.leg],
                        args.ramp_duration_ms,
                        args.duration_ms,
                    )
                    if args.ramp_duration_ms > 0
                    else struct.pack(
                        ">BI", LEG_VALUES[args.leg], args.duration_ms
                    )
                ),
            )
            print_json(query_status(client))
        elif args.command == "stop":
            stop_drive(client)
            print_json(query_status(client))
        elif args.command == "clear-faults":
            recovery = clear_faults(client)
            print_json(recovery)
            if recovery["result"] == "blocked":
                return 3
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
        elif args.command == "velocity":
            if args.rpm is not None:
                args.rps = args.rpm / 60.0
            if not math.isfinite(args.rps) or args.rps == 0.0:
                raise ProtocolError("velocity must be finite and nonzero")
            target_velocity_q16_16 = round(args.rps * 65536.0)
            if not -0x80000000 <= target_velocity_q16_16 <= 0x7FFFFFFF:
                raise ProtocolError("--rps does not encode as signed Q16.16")
            if target_velocity_q16_16 == 0:
                raise ProtocolError("--rps is too small to encode as Q16.16")
            if args.current_limit_counts is not None:
                current_limit_counts = args.current_limit_counts
            else:
                if (
                    args.current_limit_ma is None
                    or not math.isfinite(args.current_limit_ma)
                    or args.current_limit_ma <= 0.0
                ):
                    raise ProtocolError("--current-limit-ma must be positive")
                current_limit_counts = round(
                    args.current_limit_ma / COUNTS_TO_MILLIAMPERES
                )
            if not 1 <= current_limit_counts <= 0xFFFF:
                raise ProtocolError(
                    "current limit must encode as 1..65535 counts"
                )
            if not 0.01 <= args.interval <= 2.0:
                raise ProtocolError(
                    "--interval must be in the range 0.01..2.0 seconds"
                )
            if args.stop_after_seconds is not None and (
                not math.isfinite(args.stop_after_seconds)
                or args.stop_after_seconds <= 0.0
                or args.stop_after_seconds >= args.duration_ms / 1000.0
            ):
                raise ProtocolError(
                    "--stop-after-seconds must be positive and earlier than "
                    "the firmware deadline"
                )
            trace_deadline_seconds = (
                args.stop_after_seconds
                if args.stop_after_seconds is not None
                else args.duration_ms / 1000.0
            )
            if args.trace_at_seconds is not None and (
                not math.isfinite(args.trace_at_seconds)
                or args.trace_at_seconds < 0.0
                or args.trace_at_seconds >= trace_deadline_seconds
            ):
                raise ProtocolError(
                    "--trace-at-seconds must be nonnegative and earlier than "
                    "the scheduled STOP or firmware deadline"
                )
            velocity_status = query_velocity(client)
            policy = velocity_status["policy"]
            if abs(args.rps) > policy[
                "maximum_target_velocity_revolutions_per_second"
            ]:
                raise ProtocolError(
                    "velocity is outside the firmware-reported range "
                    f"±{policy['maximum_target_velocity_revolutions_per_second']} "
                    "revolutions per second"
                )
            if current_limit_counts > policy["maximum_current_counts"]:
                raise ProtocolError(
                    "current limit exceeds the firmware-reported maximum "
                    f"of {policy['maximum_current_counts']} counts"
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
            return _run_velocity_capture(
                client,
                args,
                target_velocity_q16_16,
                current_limit_counts,
                velocity_status,
            )
        elif args.command == "position":
            maximum_velocity_rps = (
                args.max_rps
                if args.max_rps is not None
                else args.max_rpm / 60.0
            )
            requested_values = (
                args.revolutions,
                maximum_velocity_rps,
                args.acceleration_rps2,
            )
            if not all(math.isfinite(value) for value in requested_values):
                raise ProtocolError("position parameters must be finite")
            if args.revolutions == 0.0:
                raise ProtocolError("--revolutions must be nonzero")
            if maximum_velocity_rps <= 0.0:
                raise ProtocolError("maximum velocity must be positive")
            if args.acceleration_rps2 <= 0.0:
                raise ProtocolError("--acceleration-rps2 must be positive")
            displacement_q16_16 = round(args.revolutions * 65536.0)
            maximum_velocity_q16_16 = round(maximum_velocity_rps * 65536.0)
            maximum_acceleration_q16_16 = round(
                args.acceleration_rps2 * 65536.0
            )
            for name, value in (
                ("relative position", displacement_q16_16),
                ("maximum velocity", maximum_velocity_q16_16),
                ("maximum acceleration", maximum_acceleration_q16_16),
            ):
                if not -0x80000000 <= value <= 0x7FFFFFFF:
                    raise ProtocolError(f"{name} does not encode as Q16.16")
                if value == 0:
                    raise ProtocolError(f"{name} is too small to encode as Q16.16")
            if args.current_limit_counts is not None:
                current_limit_counts = args.current_limit_counts
            else:
                if (
                    args.current_limit_ma is None
                    or not math.isfinite(args.current_limit_ma)
                    or args.current_limit_ma <= 0.0
                ):
                    raise ProtocolError("--current-limit-ma must be positive")
                current_limit_counts = round(
                    args.current_limit_ma / COUNTS_TO_MILLIAMPERES
                )
            if not 1 <= current_limit_counts <= 0xFFFF:
                raise ProtocolError(
                    "current limit must encode as 1..65535 counts"
                )
            if not 0.01 <= args.interval <= 2.0:
                raise ProtocolError(
                    "--interval must be in the range 0.01..2.0 seconds"
                )
            if args.stop_after_seconds is not None and (
                not math.isfinite(args.stop_after_seconds)
                or args.stop_after_seconds <= 0.0
                or args.stop_after_seconds >= args.duration_ms / 1000.0
            ):
                raise ProtocolError(
                    "--stop-after-seconds must be positive and earlier than "
                    "the firmware deadline"
                )

            position_status = query_position(client)
            position_policy = position_status["policy"]
            velocity_status = query_velocity(client)
            velocity_policy = velocity_status["policy"]
            if abs(args.revolutions) > position_policy[
                "maximum_relative_travel_revolutions"
            ]:
                raise ProtocolError(
                    "relative travel exceeds the firmware-reported maximum "
                    f"of ±{position_policy['maximum_relative_travel_revolutions']} "
                    "revolutions"
                )
            if maximum_velocity_rps > position_policy[
                "maximum_velocity_revolutions_per_second"
            ]:
                raise ProtocolError(
                    "maximum velocity exceeds the firmware-reported maximum "
                    f"of {position_policy['maximum_velocity_revolutions_per_second']} "
                    "revolutions per second"
                )
            if args.acceleration_rps2 > position_policy[
                "maximum_acceleration_revolutions_per_second2"
            ]:
                raise ProtocolError(
                    "acceleration exceeds the firmware-reported maximum of "
                    f"{position_policy['maximum_acceleration_revolutions_per_second2']} "
                    "revolutions per second squared"
                )
            if current_limit_counts > velocity_policy["maximum_current_counts"]:
                raise ProtocolError(
                    "current limit exceeds the firmware-reported maximum "
                    f"of {velocity_policy['maximum_current_counts']} counts"
                )
            if not (
                POSITION_MINIMUM_DURATION_MILLIS
                <= args.duration_ms
                <= velocity_policy["maximum_duration_millis"]
            ):
                raise ProtocolError(
                    "duration is outside the firmware-reported range "
                    f"{POSITION_MINIMUM_DURATION_MILLIS}.."
                    f"{velocity_policy['maximum_duration_millis']} ms"
                )

            return _run_position_capture(
                client,
                args,
                displacement_q16_16,
                maximum_velocity_q16_16,
                maximum_acceleration_q16_16,
                current_limit_counts,
                position_status,
                velocity_status,
            )
        elif args.command == "watch":
            while True:
                status = query_status(client)
                status["encoder"] = query_encoder(client)
                print(json.dumps(status, sort_keys=True), flush=True)
                time.sleep(args.interval)
        elif args.command == "run":
            if args.ramp_duration_ms < 0:
                raise ProtocolError("--ramp-duration-ms must be nonnegative")
            client.transact(
                COMMAND_START_CURRENT_TEST,
                (
                    struct.pack(
                        ">BII",
                        LEG_VALUES[args.leg],
                        args.ramp_duration_ms,
                        args.duration_ms,
                    )
                    if args.ramp_duration_ms > 0
                    else struct.pack(
                        ">BI", LEG_VALUES[args.leg], args.duration_ms
                    )
                ),
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
