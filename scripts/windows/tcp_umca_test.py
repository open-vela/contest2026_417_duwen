"""TCP-side UMCA endpoint used to validate the transparent Windows bridge."""

from __future__ import annotations

import argparse
import json
import socket
import sys
import time
from typing import Any, Dict

from umca_wire import (
    SequenceTracker,
    StreamDecoder,
    announce_frame,
    heartbeat_frame,
    hex_bytes,
    temperature_frame,
    type_name,
)


DEFAULT_TCP_DEVID = 0x3300000000000001
DEFAULT_GD32_DEVID = 0x4700000000000001


def parse_int(value: str) -> int:
    return int(value, 0)


def emit(event: str, **fields: Any) -> None:
    record: Dict[str, Any] = {
        "time": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "event": event,
        **fields,
    }
    print(json.dumps(record, ensure_ascii=False, sort_keys=True), flush=True)


def connect(args) -> socket.socket:
    sock = socket.create_connection((args.host, args.port_number), args.timeout)
    sock.settimeout(0.2)
    emit("connected", host=args.host, port=args.port_number)
    return sock


def receive_for(sock: socket.socket, seconds: float) -> None:
    decoder = StreamDecoder()
    tracker = SequenceTracker()
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            data = sock.recv(4096)
        except socket.timeout:
            continue
        if not data:
            emit("peer_closed")
            return
        emit("rx_bytes", length=len(data), hex=hex_bytes(data))
        for decoded in decoder.feed(data):
            frame = decoded.frame
            emit(
                "rx_frame",
                type=type_name(frame.message_type),
                source=f"0x{frame.source:016x}",
                destination=f"0x{frame.destination:016x}",
                topic=f"0x{frame.topic_id:08x}",
                sequence=frame.sequence,
                payload_length=len(frame.payload),
                crc32c=f"0x{decoded.crc32c:08x}",
                sequence_state=tracker.observe(decoded),
            )
    emit(
        "rx_summary",
        discarded_bytes=decoder.discarded_bytes,
        invalid_frames=decoder.invalid_frames,
    )


def send(sock: socket.socket, case: str, data: bytes) -> None:
    sock.sendall(data)
    emit("tx", case=case, length=len(data), hex=hex_bytes(data))


def run_monitor(args) -> int:
    with connect(args) as sock:
        receive_for(sock, args.seconds)
    return 0


def run_exercise(args) -> int:
    boot_id = args.boot_id
    sock = connect(args)
    try:
        send(sock, "announce", announce_frame(args.source, boot_id))
        send(
            sock,
            "heartbeat",
            heartbeat_frame(args.source, boot_id, 2, 5000),
        )
        send(
            sock,
            "data",
            temperature_frame(args.source, args.gd32_id, 1),
        )

        half = temperature_frame(args.source, args.gd32_id, 2, 31000, 2000)
        split = min(max(args.half_split, 1), len(half) - 1)
        sock.sendall(half[:split])
        emit("tx_chunk", case="tcp_half_first", length=split)
        time.sleep(args.half_delay)
        sock.sendall(half[split:])
        emit("tx_chunk", case="tcp_half_second", length=len(half) - split)

        sticky = (
            temperature_frame(args.source, args.gd32_id, 3, 31500, 3000)
            + temperature_frame(args.source, args.gd32_id, 4, 32000, 4000)
        )
        send(sock, "tcp_sticky_data_3_4", sticky)
        receive_for(sock, args.observe_seconds)
    finally:
        sock.close()
        emit("intentional_disconnect")

    time.sleep(args.disconnect_seconds)
    boot_id = (boot_id + 1) & 0xFFFFFFFF or 1
    with connect(args) as sock:
        # New session data is created only after reconnect.  The bridge must not
        # prepend bytes retained from the old socket generation.
        send(
            sock,
            "reconnect_new_boot_announce",
            announce_frame(args.source, boot_id),
        )
        send(
            sock,
            "reconnect_heartbeat",
            heartbeat_frame(args.source, boot_id, 2, 100),
        )
        send(
            sock,
            "reconnect_data_sequence_1",
            temperature_frame(args.source, args.gd32_id, 1, 30000, 100),
        )
        receive_for(sock, args.observe_seconds)
    emit("exercise_complete")
    return 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--host", default="127.0.0.1")
    result.add_argument("--port-number", type=int, default=47000)
    result.add_argument("--timeout", type=float, default=3.0)
    subparsers = result.add_subparsers(dest="command", required=True)

    monitor = subparsers.add_parser("monitor")
    monitor.add_argument("--seconds", type=float, default=40.0)
    monitor.set_defaults(func=run_monitor)

    exercise = subparsers.add_parser("exercise")
    exercise.add_argument("--source", type=parse_int, default=DEFAULT_TCP_DEVID)
    exercise.add_argument("--gd32-id", type=parse_int, default=DEFAULT_GD32_DEVID)
    exercise.add_argument("--boot-id", type=parse_int, default=0x33000001)
    exercise.add_argument("--half-split", type=int, default=17)
    exercise.add_argument("--half-delay", type=float, default=0.2)
    exercise.add_argument("--observe-seconds", type=float, default=6.0)
    exercise.add_argument("--disconnect-seconds", type=float, default=2.0)
    exercise.set_defaults(func=run_exercise)
    return result


def main() -> int:
    args = parser().parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
