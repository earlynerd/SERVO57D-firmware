#!/usr/bin/env python3
"""MKS57D native-v1 RS-485 commissioning console."""

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

STATUS_BODY = struct.Struct(">BIBBBBIIHHHHhhhhhhHHHHHHHHIIBB")
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
    if len(body) != 18:
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
    ) = struct.unpack(">BBBHBIII", body)
    return {
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
    commands.add_parser("status", help="read one commissioning snapshot")
    commands.add_parser("boot", help="read reset cause, panic, and uptime")
    commands.add_parser("encoder", help="read live encoder position and health")

    configure = commands.add_parser("configure", help="set test demand")
    configure.add_argument("--counts", type=int, required=True)
    configure.add_argument("--frequency-hz", type=float, required=True)

    start = commands.add_parser("start", help="start a bounded remote run")
    start.add_argument("--leg", choices=LEG_VALUES, default="A1")
    start.add_argument("--duration-ms", type=int, default=10000)

    commands.add_parser("stop", help="stop a local or remote run")

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
            client.transact(COMMAND_STOP_CURRENT_TEST)
            print_json(query_status(client))
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
                client.transact(COMMAND_STOP_CURRENT_TEST)
                print("stopped", file=sys.stderr)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ProtocolError, OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
