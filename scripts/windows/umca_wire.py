"""Minimal UMCA wire helpers for Windows interoperability tests.

This module is intentionally used by the UART test tool only.  The transparent
COM/TCP bridge does not import it and does not inspect UMCA frames.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


MAGIC = b"\x55\x4d"
VERSION = 1
HEADER_SIZE = 36
CRC_SIZE = 4
FRAME_OVERHEAD = HEADER_SIZE + CRC_SIZE
MAX_PAYLOAD = 256
MAX_FRAME = FRAME_OVERHEAD + MAX_PAYLOAD
BROADCAST_DEVID = 0xFFFFFFFFFFFFFFFF
SYSTEM_TOPIC_ID = 0
DEFAULT_TTL = 8

MSG_DATA = 0x00
MSG_ANNOUNCE = 0x03
MSG_HEARTBEAT = 0x04
MSG_TEARDOWN = 0x05

TOPIC_TEMPERATURE = 0x0FC95CB0
TOPIC_TEMPERATURE_NAME = b"/sensors/temperature"
TOPIC_THRESHOLD = 0x5389C486
TOPIC_THRESHOLD_NAME = b"/events/temperature/threshold"
TOPIC_FAN_COMMAND = 0xC72FE51A
TOPIC_FAN_COMMAND_NAME = b"/actuators/fan/command"
TOPIC_FAN_STATE = 0x6D3F9EBE
TOPIC_FAN_STATE_NAME = b"/actuators/fan/state"
TOPIC_LLM_CHAT_REQUEST = 0x7985C52E
TOPIC_LLM_CHAT_REQUEST_NAME = b"/llm/chat/request"
TOPIC_LLM_CHAT_RESPONSE = 0x9D3E1F72
TOPIC_LLM_CHAT_RESPONSE_NAME = b"/llm/chat/response"

TOPIC_PUBLISHER = 1
TOPIC_SUBSCRIBER = 2

LLM_ORIGIN_TERMINAL = 0
LLM_ORIGIN_AUTOMATIC = 1
LLM_STATUS_OK = 0
LLM_STATUS_REJECTED = 1
LLM_STATUS_UNAVAILABLE = 2
LLM_STATUS_TIMEOUT = 3
LLM_STATUS_ERROR = 4
LLM_ACTION_NONE = 0
LLM_ACTION_SET_FAN = 1


class WireError(ValueError):
    """Raised when a byte string is not a valid UMCA MVP frame."""


@dataclass(frozen=True)
class Frame:
    message_type: int
    destination: int
    source: int
    topic_id: int
    sequence: int
    payload: bytes = b""
    ttl: int = DEFAULT_TTL
    flags: int = 0
    qos: int = 0


@dataclass(frozen=True)
class DecodedFrame:
    frame: Frame
    raw: bytes
    crc32c: int


def crc32c(data: bytes) -> int:
    """Return CRC32C Castagnoli using the reflected 0x82F63B78 form."""

    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 if crc & 1 else 0)
    return crc ^ 0xFFFFFFFF


def _validate_semantics(frame: Frame) -> None:
    if frame.flags != 0:
        raise WireError("unsupported flags")
    if frame.qos != 0:
        raise WireError("unsupported qos")
    if frame.destination == 0 or frame.source == 0 or frame.ttl == 0:
        raise WireError("invalid address or ttl")
    if frame.message_type not in (
        MSG_DATA,
        MSG_ANNOUNCE,
        MSG_HEARTBEAT,
        MSG_TEARDOWN,
    ):
        raise WireError("unsupported message type")
    if frame.message_type == MSG_DATA:
        if frame.topic_id == SYSTEM_TOPIC_ID:
            raise WireError("DATA requires a non-system topic")
    elif (
        frame.topic_id != SYSTEM_TOPIC_ID
        or frame.destination != BROADCAST_DEVID
    ):
        raise WireError("discovery frame requires broadcast and system topic")
    if len(frame.payload) > MAX_PAYLOAD:
        raise WireError("payload too large")


def encode_frame(frame: Frame) -> bytes:
    _validate_semantics(frame)
    header = bytearray(HEADER_SIZE)
    header[0:2] = MAGIC
    header[2] = VERSION
    header[3] = HEADER_SIZE
    header[4:6] = frame.flags.to_bytes(2, "big")
    header[6] = frame.message_type
    header[7] = frame.qos
    header[8] = frame.ttl
    header[9] = 0
    header[10:18] = frame.destination.to_bytes(8, "big")
    header[18:26] = frame.source.to_bytes(8, "big")
    header[26:30] = frame.topic_id.to_bytes(4, "big")
    header[30:34] = frame.sequence.to_bytes(4, "big")
    header[34:36] = len(frame.payload).to_bytes(2, "big")
    body = bytes(header) + frame.payload
    checksum = crc32c(body[2:]).to_bytes(4, "big")
    return body + checksum


def decode_frame(raw: bytes) -> DecodedFrame:
    if len(raw) < FRAME_OVERHEAD:
        raise WireError("frame too short")
    if raw[0:2] != MAGIC:
        raise WireError("bad magic")
    if raw[2] != VERSION:
        raise WireError("unsupported version")
    if raw[3] != HEADER_SIZE:
        raise WireError("bad header length")
    if raw[9] != 0:
        raise WireError("reserved byte is non-zero")
    payload_length = int.from_bytes(raw[34:36], "big")
    if payload_length > MAX_PAYLOAD:
        raise WireError("payload too large")
    if len(raw) != FRAME_OVERHEAD + payload_length:
        raise WireError("frame length mismatch")
    expected = int.from_bytes(raw[-CRC_SIZE:], "big")
    actual = crc32c(raw[2:-CRC_SIZE])
    if expected != actual:
        raise WireError(
            f"CRC mismatch expected=0x{expected:08x} actual=0x{actual:08x}"
        )
    frame = Frame(
        flags=int.from_bytes(raw[4:6], "big"),
        message_type=raw[6],
        qos=raw[7],
        ttl=raw[8],
        destination=int.from_bytes(raw[10:18], "big"),
        source=int.from_bytes(raw[18:26], "big"),
        topic_id=int.from_bytes(raw[26:30], "big"),
        sequence=int.from_bytes(raw[30:34], "big"),
        payload=raw[HEADER_SIZE:-CRC_SIZE],
    )
    _validate_semantics(frame)
    return DecodedFrame(frame=frame, raw=raw, crc32c=expected)


class StreamDecoder:
    """Recover complete UMCA frames from arbitrary serial/TCP chunks."""

    def __init__(self) -> None:
        self._buffer = bytearray()
        self.discarded_bytes = 0
        self.invalid_frames = 0

    def clear(self) -> None:
        self._buffer.clear()

    def feed(self, data: bytes) -> List[DecodedFrame]:
        self._buffer.extend(data)
        frames: List[DecodedFrame] = []
        while True:
            position = self._buffer.find(MAGIC)
            if position < 0:
                keep = 1 if self._buffer.endswith(MAGIC[:1]) else 0
                discard = len(self._buffer) - keep
                if discard:
                    del self._buffer[:discard]
                    self.discarded_bytes += discard
                break
            if position:
                del self._buffer[:position]
                self.discarded_bytes += position
            if len(self._buffer) < HEADER_SIZE:
                break
            if self._buffer[2] != VERSION or self._buffer[3] != HEADER_SIZE:
                del self._buffer[0]
                self.discarded_bytes += 1
                continue
            payload_length = int.from_bytes(self._buffer[34:36], "big")
            if payload_length > MAX_PAYLOAD:
                del self._buffer[0]
                self.discarded_bytes += 1
                continue
            length = FRAME_OVERHEAD + payload_length
            if len(self._buffer) < length:
                break
            raw = bytes(self._buffer[:length])
            del self._buffer[:length]
            try:
                frames.append(decode_frame(raw))
            except WireError:
                self.invalid_frames += 1
        return frames


class SequenceTracker:
    """Classify received sequences without changing wire data."""

    def __init__(self) -> None:
        self._last: Dict[Tuple[int, int], int] = {}
        self._boot_ids: Dict[int, int] = {}

    def observe(self, decoded: DecodedFrame) -> str:
        frame = decoded.frame
        if frame.message_type == MSG_ANNOUNCE and len(frame.payload) >= 8:
            boot_id = int.from_bytes(frame.payload[4:8], "big")
            previous = self._boot_ids.get(frame.source)
            if previous is not None and previous != boot_id:
                for key in list(self._last):
                    if key[0] == frame.source:
                        del self._last[key]
            self._boot_ids[frame.source] = boot_id

        key = (frame.source, frame.topic_id)
        previous = self._last.get(key)
        if previous is None:
            self._last[key] = frame.sequence
            return "baseline"
        delta = (frame.sequence - previous) & 0xFFFFFFFF
        if delta == 0:
            return "duplicate"
        if delta & 0x80000000:
            return "stale"
        self._last[key] = frame.sequence
        return "in-order" if delta == 1 else f"gap:{delta - 1}"


def announce_frame(
    source: int,
    boot_id: int,
    sequence: int = 1,
    topics: Optional[Sequence[Tuple[int, int, bytes]]] = None,
    ttl: int = DEFAULT_TTL,
) -> bytes:
    if boot_id == 0:
        raise WireError("boot_id must be non-zero")
    if topics is None:
        topics = ((TOPIC_TEMPERATURE, TOPIC_PUBLISHER,
                   TOPIC_TEMPERATURE_NAME),)
    if len(topics) > 8:
        raise WireError("too many announced topics")
    payload = bytearray(
        (1).to_bytes(4, "big")
        + boot_id.to_bytes(4, "big")
        + bytes((len(topics),))
    )
    for topic_id, direction, topic_name in topics:
        if not topic_name or len(topic_name) > 63:
            raise WireError("invalid topic name")
        if direction == 0 or direction & ~(TOPIC_PUBLISHER | TOPIC_SUBSCRIBER):
            raise WireError("invalid topic direction")
        payload.extend(topic_id.to_bytes(4, "big"))
        payload.extend((direction, len(topic_name)))
        payload.extend(topic_name)
    return encode_frame(
        Frame(
            message_type=MSG_ANNOUNCE,
            destination=BROADCAST_DEVID,
            source=source,
            topic_id=SYSTEM_TOPIC_ID,
            sequence=sequence,
            payload=bytes(payload),
            ttl=ttl,
        )
    )


def heartbeat_frame(
    source: int,
    boot_id: int,
    sequence: int,
    uptime_ms: int,
    status: int = 0,
    ttl: int = DEFAULT_TTL,
) -> bytes:
    payload = (
        boot_id.to_bytes(4, "big")
        + uptime_ms.to_bytes(4, "big")
        + bytes((status,))
    )
    return encode_frame(
        Frame(
            message_type=MSG_HEARTBEAT,
            destination=BROADCAST_DEVID,
            source=source,
            topic_id=SYSTEM_TOPIC_ID,
            sequence=sequence,
            payload=payload,
            ttl=ttl,
        )
    )


def temperature_frame(
    source: int,
    destination: int,
    sequence: int,
    milli_celsius: int = 30500,
    sample_time_ms: int = 1000,
    quality: int = 1,
    ttl: int = DEFAULT_TTL,
) -> bytes:
    payload = (
        milli_celsius.to_bytes(4, "big", signed=True)
        + sample_time_ms.to_bytes(4, "big")
        + bytes((quality,))
    )
    return encode_frame(
        Frame(
            message_type=MSG_DATA,
            destination=destination,
            source=source,
            topic_id=TOPIC_TEMPERATURE,
            sequence=sequence,
            payload=payload,
            ttl=ttl,
        )
    )


def data_frame(
    source: int,
    destination: int,
    topic_id: int,
    sequence: int,
    payload: bytes,
    ttl: int = DEFAULT_TTL,
) -> bytes:
    return encode_frame(
        Frame(
            message_type=MSG_DATA,
            destination=destination,
            source=source,
            topic_id=topic_id,
            sequence=sequence,
            payload=payload,
            ttl=ttl,
        )
    )


def threshold_payload(
    milli_celsius: int,
    threshold_mc: int,
    direction: int,
    event_time_ms: int,
) -> bytes:
    if direction not in (0, 1):
        raise WireError("invalid threshold direction")
    return (
        milli_celsius.to_bytes(4, "big", signed=True)
        + threshold_mc.to_bytes(4, "big", signed=True)
        + bytes((direction,))
        + event_time_ms.to_bytes(4, "big")
    )


def decode_fan_command(payload: bytes) -> Tuple[int, int, int]:
    if len(payload) != 6:
        raise WireError("invalid fan command length")
    request_id = int.from_bytes(payload[0:4], "big")
    command = payload[4]
    source = payload[5]
    if request_id == 0 or command > 1 or source > 2:
        raise WireError("invalid fan command")
    return request_id, command, source


def fan_state_payload(
    request_id: int, state: int, result: int, applied_time_ms: int
) -> bytes:
    if request_id == 0 or state not in (0, 1) or not 0 <= result <= 255:
        raise WireError("invalid fan state")
    return (
        request_id.to_bytes(4, "big")
        + bytes((state, result))
        + applied_time_ms.to_bytes(4, "big")
    )


def decode_chat_request(payload: bytes) -> Tuple[int, int, int, int, bytes]:
    if len(payload) < 15:
        raise WireError("chat request is too short")
    boot_id = int.from_bytes(payload[0:4], "big")
    request_id = int.from_bytes(payload[4:8], "big")
    timeout_ms = int.from_bytes(payload[8:12], "big")
    origin = payload[12]
    text_length = int.from_bytes(payload[13:15], "big")
    if (
        boot_id == 0
        or request_id == 0
        or timeout_ms == 0
        or origin not in (LLM_ORIGIN_TERMINAL, LLM_ORIGIN_AUTOMATIC)
        or text_length == 0
        or text_length > 241
        or len(payload) != 15 + text_length
    ):
        raise WireError("invalid chat request")
    return boot_id, request_id, timeout_ms, origin, payload[15:]


def chat_response_payload(
    requester_boot_id: int,
    request_id: int,
    status: int,
    action_type: int,
    action_value: int,
    text: bytes,
) -> bytes:
    if (
        requester_boot_id == 0
        or request_id == 0
        or status not in range(LLM_STATUS_ERROR + 1)
        or action_type not in (LLM_ACTION_NONE, LLM_ACTION_SET_FAN)
        or action_value not in (0, 1)
        or (status != LLM_STATUS_OK and action_type != LLM_ACTION_NONE)
        or (action_type == LLM_ACTION_NONE and action_value != 0)
        or len(text) > 243
    ):
        raise WireError("invalid chat response")
    return (
        requester_boot_id.to_bytes(4, "big")
        + request_id.to_bytes(4, "big")
        + bytes((status, action_type, action_value))
        + len(text).to_bytes(2, "big")
        + text
    )


def type_name(message_type: int) -> str:
    return {
        MSG_DATA: "DATA",
        MSG_ANNOUNCE: "ANNOUNCE",
        MSG_HEARTBEAT: "HEARTBEAT",
        MSG_TEARDOWN: "TEARDOWN",
    }.get(message_type, f"TYPE_{message_type:02x}")


def hex_bytes(data: Iterable[int]) -> str:
    return " ".join(f"{value:02x}" for value in data)
