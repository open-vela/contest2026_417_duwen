"""Simulate Sensor, Actuator and ESP32-LM nodes on one Windows COM port."""

from __future__ import annotations

import argparse
import json
import re
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Optional, Tuple

from umca_wire import (
    BROADCAST_DEVID,
    LLM_ACTION_NONE,
    LLM_ACTION_SET_FAN,
    LLM_STATUS_OK,
    LLM_STATUS_REJECTED,
    MSG_DATA,
    TOPIC_FAN_COMMAND,
    TOPIC_FAN_COMMAND_NAME,
    TOPIC_FAN_STATE,
    TOPIC_FAN_STATE_NAME,
    TOPIC_LLM_CHAT_REQUEST,
    TOPIC_LLM_CHAT_REQUEST_NAME,
    TOPIC_LLM_CHAT_RESPONSE,
    TOPIC_LLM_CHAT_RESPONSE_NAME,
    TOPIC_PUBLISHER,
    TOPIC_SUBSCRIBER,
    TOPIC_TEMPERATURE,
    TOPIC_TEMPERATURE_NAME,
    TOPIC_THRESHOLD,
    TOPIC_THRESHOLD_NAME,
    SequenceTracker,
    StreamDecoder,
    WireError,
    announce_frame,
    chat_response_payload,
    data_frame,
    decode_chat_request,
    decode_fan_command,
    fan_state_payload,
    heartbeat_frame,
    hex_bytes,
    temperature_frame,
    threshold_payload,
    type_name,
)


DEFAULT_GD32_DEVID = 0x4700000000000001
DEFAULT_SENSOR_DEVID = 0x0000000000001001
DEFAULT_ACTUATOR_DEVID = 0x0000000000003001
DEFAULT_ESP32_DEVID = 0x3300000000000001
FORWARDED_TTL = 7

# The real ESP32/MiMo provider must send this policy as its system prompt.
# The deterministic Mock enforces the same C1 contract locally.
MIMO_SYSTEM_PROMPT_C1 = (
    "C1 is a trusted GD32 device snapshot. tv=0 means temperature is "
    "unavailable and must not be invented. f=0/1/2 means off/on/unknown. "
    "Keep reply text concise. Device action may only be none or set_fan."
)

_UINT = r"(?:0|[1-9][0-9]*)"
_C1_VALID = re.compile(
    rf"^C1;tv=1;t=(-?{_UINT});age=({_UINT});q=({_UINT});"
    rf"so=([01]);ao=([01]);f=([012])\nQ:(.+)$"
)
_C1_INVALID = re.compile(
    r"^C1;tv=0;so=([01]);ao=([01]);f=([012])\nQ:(.+)$"
)


def parse_int(value: str) -> int:
    return int(value, 0)


def load_serial_module():
    try:
        import serial  # type: ignore
    except ImportError as exc:
        raise SystemExit(
            "pyserial is required: py -m pip install -r "
            "scripts\\windows\\requirements.txt"
        ) from exc
    return serial


class EvidenceLog:
    def __init__(self, path: Optional[str]) -> None:
        self._path = Path(path) if path else None

    def emit(self, event: str, **fields: Any) -> None:
        record: Dict[str, Any] = {
            "time": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
            "event": event,
            **fields,
        }
        # ASCII JSON avoids Windows GBK console failures while preserving text.
        line = json.dumps(record, ensure_ascii=True, sort_keys=True)
        print(line, flush=True)
        if self._path is not None:
            with self._path.open("a", encoding="utf-8") as stream:
                stream.write(line + "\n")


@dataclass
class LogicalNode:
    role: str
    dev_id: int
    boot_id: int
    topics: Tuple[Tuple[int, int, bytes], ...]
    forwarded: bool
    system_sequence: int = 1
    data_sequences: Dict[int, int] = field(default_factory=dict)

    @property
    def ttl(self) -> int:
        return FORWARDED_TTL if self.forwarded else 8

    def next_system_sequence(self) -> int:
        sequence = self.system_sequence
        self.system_sequence = (sequence + 1) & 0xFFFFFFFF
        return sequence

    def next_data_sequence(self, topic_id: int) -> int:
        sequence = self.data_sequences.get(topic_id, 1)
        self.data_sequences[topic_id] = (sequence + 1) & 0xFFFFFFFF
        return sequence


class MockLlmProvider:
    """Deterministic stand-in for the future Mimo HTTPS provider."""

    def respond(self, text: str) -> Tuple[int, int, int, str]:
        context = self._parse_c1(text)
        if context is None:
            return (LLM_STATUS_REJECTED, LLM_ACTION_NONE, 0,
                    "Mock: invalid C1 context.")
        question = str(context["question"])
        normalized = question.casefold()
        turn_on = ("fan on" in normalized or "open fan" in normalized or
                   "turn on" in normalized or "\u6253\u5f00\u98ce\u6247" in normalized)
        turn_off = ("fan off" in normalized or "close fan" in normalized or
                    "turn off" in normalized or "\u5173\u95ed\u98ce\u6247" in normalized)
        if turn_on and not turn_off:
            return LLM_STATUS_OK, LLM_ACTION_SET_FAN, 1, "Mock: request fan on."
        if turn_off and not turn_on:
            return LLM_STATUS_OK, LLM_ACTION_SET_FAN, 0, "Mock: request fan off."
        asks_temperature = ("temperature" in normalized or
                            "\u6e29\u5ea6" in normalized)
        if asks_temperature:
            if context["temperature_valid"]:
                return (LLM_STATUS_OK, LLM_ACTION_NONE, 0,
                        f"Mock: temperature is {context['temperature_mC']} mC.")
            return (LLM_STATUS_OK, LLM_ACTION_NONE, 0,
                    "Mock: temperature is unavailable.")
        return LLM_STATUS_OK, LLM_ACTION_NONE, 0, f"Mock: {question}"

    @staticmethod
    def _parse_c1(text: str) -> Optional[Dict[str, Any]]:
        if any(ord(char) <= 0x1F or 0x7F <= ord(char) <= 0x9F
               for char in text.replace("\n", "", 1)):
            return None
        match = _C1_VALID.fullmatch(text)
        if match is not None:
            temperature, age, quality, sensor, actuator, fan, question = (
                match.groups()
            )
            values = (int(temperature), int(age), int(quality))
            if not (-0x80000000 <= values[0] <= 0x7FFFFFFF and
                    0 <= values[1] <= 0xFFFFFFFF and
                    0 <= values[2] <= 0xFF):
                return None
            return {
                "temperature_valid": True,
                "temperature_mC": values[0],
                "age_ms": values[1],
                "quality": values[2],
                "sensor_online": sensor == "1",
                "actuator_online": actuator == "1",
                "fan_state": int(fan),
                "question": question,
            }
        match = _C1_INVALID.fullmatch(text)
        if match is None:
            return None
        sensor, actuator, fan, question = match.groups()
        return {
            "temperature_valid": False,
            "sensor_online": sensor == "1",
            "actuator_online": actuator == "1",
            "fan_state": int(fan),
            "question": question,
        }


def utf8_prefix(text: str, limit: int) -> bytes:
    encoded = text.encode("utf-8")
    if len(encoded) <= limit:
        return encoded
    return encoded[:limit].decode("utf-8", errors="ignore").encode("utf-8")


class ThreeNodeSimulator:
    def __init__(self, port, args, log: EvidenceLog) -> None:
        self.port = port
        self.args = args
        self.log = log
        self.decoder = StreamDecoder()
        self.tracker = SequenceTracker()
        self.started = time.monotonic()
        self.provider = MockLlmProvider()
        self.fan_on = False
        self.sensor_high: Optional[bool] = None
        self.sensor = LogicalNode(
            "sensor",
            args.sensor_devid,
            args.sensor_boot_id,
            (
                (TOPIC_TEMPERATURE, TOPIC_PUBLISHER,
                 TOPIC_TEMPERATURE_NAME),
                (TOPIC_THRESHOLD, TOPIC_PUBLISHER, TOPIC_THRESHOLD_NAME),
            ),
            True,
        )
        self.actuator = LogicalNode(
            "actuator",
            args.actuator_devid,
            args.actuator_boot_id,
            (
                (TOPIC_FAN_COMMAND, TOPIC_SUBSCRIBER,
                 TOPIC_FAN_COMMAND_NAME),
                (TOPIC_FAN_STATE, TOPIC_PUBLISHER, TOPIC_FAN_STATE_NAME),
            ),
            True,
        )
        self.esp32 = LogicalNode(
            "esp32",
            args.esp32_devid,
            args.esp32_boot_id,
            (
                (TOPIC_LLM_CHAT_REQUEST, TOPIC_SUBSCRIBER,
                 TOPIC_LLM_CHAT_REQUEST_NAME),
                (TOPIC_LLM_CHAT_RESPONSE, TOPIC_PUBLISHER,
                 TOPIC_LLM_CHAT_RESPONSE_NAME),
            ),
            False,
        )
        self.nodes = (self.sensor, self.actuator, self.esp32)

    def uptime_ms(self) -> int:
        return int((time.monotonic() - self.started) * 1000) & 0xFFFFFFFF

    def send(self, node: LogicalNode, case: str, raw: bytes) -> None:
        offset = 0
        while offset < len(raw):
            written = self.port.write(raw[offset:])
            if written is None or written <= 0:
                raise OSError("serial write made no progress")
            offset += written
        self.port.flush()
        self.log.emit(
            "tx",
            role=node.role,
            case=case,
            length=len(raw),
            hex=hex_bytes(raw) if self.args.log_hex else None,
        )

    def announce(self, node: LogicalNode) -> None:
        self.send(
            node,
            "announce",
            announce_frame(
                node.dev_id,
                node.boot_id,
                node.next_system_sequence(),
                node.topics,
                node.ttl,
            ),
        )

    def heartbeat(self, node: LogicalNode) -> None:
        self.send(
            node,
            "heartbeat",
            heartbeat_frame(
                node.dev_id,
                node.boot_id,
                node.next_system_sequence(),
                self.uptime_ms(),
                ttl=node.ttl,
            ),
        )

    def publish_sensor(self) -> None:
        phase = int((time.monotonic() - self.started) /
                    self.args.temperature_phase_seconds) & 1
        high = phase == 1
        temperature = (self.args.temperature_high_mc if high else
                       self.args.temperature_low_mc)
        now = self.uptime_ms()
        self.send(
            self.sensor,
            "temperature",
            temperature_frame(
                self.sensor.dev_id,
                self.args.gd32_devid,
                self.sensor.next_data_sequence(TOPIC_TEMPERATURE),
                temperature,
                now,
                ttl=self.sensor.ttl,
            ),
        )
        if self.sensor_high is None:
            self.sensor_high = high
        elif self.sensor_high != high:
            payload = threshold_payload(
                temperature,
                self.args.on_threshold_mc if high else self.args.off_threshold_mc,
                1 if high else 0,
                now,
            )
            self.send(
                self.sensor,
                "threshold",
                data_frame(
                    self.sensor.dev_id,
                    self.args.gd32_devid,
                    TOPIC_THRESHOLD,
                    self.sensor.next_data_sequence(TOPIC_THRESHOLD),
                    payload,
                    self.sensor.ttl,
                ),
            )
            self.sensor_high = high

    def handle_fan_command(self, payload: bytes) -> None:
        request_id, command, source = decode_fan_command(payload)
        self.fan_on = command == 1
        response = fan_state_payload(
            request_id, command, 0, self.uptime_ms()
        )
        self.send(
            self.actuator,
            "fan_state",
            data_frame(
                self.actuator.dev_id,
                self.args.gd32_devid,
                TOPIC_FAN_STATE,
                self.actuator.next_data_sequence(TOPIC_FAN_STATE),
                response,
                self.actuator.ttl,
            ),
        )
        self.log.emit(
            "fan_applied", request_id=request_id, state=command, source=source
        )

    def handle_chat_request(self, payload: bytes) -> None:
        boot_id, request_id, timeout_ms, origin, text_bytes = (
            decode_chat_request(payload)
        )
        try:
            request_text = text_bytes.decode("utf-8")
        except UnicodeDecodeError:
            request_text = text_bytes.decode("utf-8", errors="replace")
        status, action_type, action_value, response_text = (
            self.provider.respond(request_text)
        )
        encoded_text = utf8_prefix(response_text, 243)
        response = chat_response_payload(
            boot_id,
            request_id,
            status,
            action_type,
            action_value,
            encoded_text,
        )
        self.send(
            self.esp32,
            "chat_response",
            data_frame(
                self.esp32.dev_id,
                self.args.gd32_devid,
                TOPIC_LLM_CHAT_RESPONSE,
                self.esp32.next_data_sequence(TOPIC_LLM_CHAT_RESPONSE),
                response,
                self.esp32.ttl,
            ),
        )
        self.log.emit(
            "chat_handled",
            request_id=request_id,
            requester_boot_id=f"0x{boot_id:08x}",
            timeout_ms=timeout_ms,
            origin=origin,
            action_type=action_type,
            action_value=action_value,
            request_text=request_text,
        )

    def handle_frame(self, decoded) -> None:
        frame = decoded.frame
        sequence_state = self.tracker.observe(decoded)
        self.log.emit(
            "rx_frame",
            type=type_name(frame.message_type),
            source=f"0x{frame.source:016x}",
            destination=f"0x{frame.destination:016x}",
            topic=f"0x{frame.topic_id:08x}",
            sequence=frame.sequence,
            sequence_state=sequence_state,
            ttl=frame.ttl,
            payload_length=len(frame.payload),
        )
        if frame.message_type != MSG_DATA:
            return
        if frame.destination not in (
            BROADCAST_DEVID,
            self.actuator.dev_id,
            self.esp32.dev_id,
        ):
            return
        try:
            if frame.topic_id == TOPIC_FAN_COMMAND:
                self.handle_fan_command(frame.payload)
            elif frame.topic_id == TOPIC_LLM_CHAT_REQUEST:
                self.handle_chat_request(frame.payload)
        except WireError as exc:
            self.log.emit("application_reject", error=str(exc))

    def run(self) -> None:
        for node in self.nodes:
            self.announce(node)
        next_announce = time.monotonic() + self.args.announce_seconds
        next_heartbeat = time.monotonic() + self.args.heartbeat_seconds
        next_temperature = time.monotonic()
        while True:
            now = time.monotonic()
            if now >= next_announce:
                for node in self.nodes:
                    self.announce(node)
                next_announce = now + self.args.announce_seconds
            if now >= next_heartbeat:
                for node in self.nodes:
                    self.heartbeat(node)
                next_heartbeat = now + self.args.heartbeat_seconds
            if now >= next_temperature:
                self.publish_sensor()
                next_temperature = now + self.args.sample_seconds

            raw = self.port.read(4096)
            if raw:
                self.log.emit(
                    "rx_bytes",
                    length=len(raw),
                    hex=hex_bytes(raw) if self.args.log_hex else None,
                )
                for decoded in self.decoder.feed(raw):
                    self.handle_frame(decoded)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--gd32-devid", type=parse_int, default=DEFAULT_GD32_DEVID)
    parser.add_argument("--sensor-devid", type=parse_int, default=DEFAULT_SENSOR_DEVID)
    parser.add_argument("--actuator-devid", type=parse_int,
                        default=DEFAULT_ACTUATOR_DEVID)
    parser.add_argument("--esp32-devid", type=parse_int, default=DEFAULT_ESP32_DEVID)
    parser.add_argument("--sensor-boot-id", type=parse_int, default=0x10010001)
    parser.add_argument("--actuator-boot-id", type=parse_int, default=0x30010001)
    parser.add_argument("--esp32-boot-id", type=parse_int, default=0x33000001)
    parser.add_argument("--heartbeat-seconds", type=float, default=5.0)
    parser.add_argument("--announce-seconds", type=float, default=30.0)
    parser.add_argument("--sample-seconds", type=float, default=1.0)
    parser.add_argument("--temperature-phase-seconds", type=float, default=5.0)
    parser.add_argument("--temperature-low-mc", type=int, default=27000)
    parser.add_argument("--temperature-high-mc", type=int, default=31000)
    parser.add_argument("--on-threshold-mc", type=int, default=30000)
    parser.add_argument("--off-threshold-mc", type=int, default=28000)
    parser.add_argument("--evidence")
    parser.add_argument("--log-hex", action="store_true")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if min(args.heartbeat_seconds, args.announce_seconds, args.sample_seconds,
           args.temperature_phase_seconds) <= 0:
        raise SystemExit("periods must be positive")
    serial = load_serial_module()
    log = EvidenceLog(args.evidence)
    with serial.Serial(args.port, args.baud, timeout=0.05) as port:
        port.reset_input_buffer()
        port.reset_output_buffer()
        log.emit(
            "port_open",
            port=args.port,
            baud=args.baud,
            nodes=[
                f"0x{args.sensor_devid:016x}",
                f"0x{args.actuator_devid:016x}",
                f"0x{args.esp32_devid:016x}",
            ],
            llm_provider="mock",
            c1_system_prompt=MIMO_SYSTEM_PROMPT_C1,
        )
        simulator = ThreeNodeSimulator(port, args, log)
        try:
            simulator.run()
        except KeyboardInterrupt:
            log.emit(
                "stopped",
                invalid_frames=simulator.decoder.invalid_frames,
                discarded_bytes=simulator.decoder.discarded_bytes,
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
