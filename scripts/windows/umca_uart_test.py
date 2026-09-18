"""Windows COM-port interoperability tool for the GD32 UMCA UART link."""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path
from typing import Any, Dict, Optional

from umca_wire import (
    SequenceTracker,
    StreamDecoder,
    announce_frame,
    heartbeat_frame,
    hex_bytes,
    temperature_frame,
    type_name,
)


DEFAULT_WINDOWS_DEVID = 0x3200000000000001
DEFAULT_GD32_DEVID = 0x4700000000000001


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
        line = json.dumps(record, ensure_ascii=False, sort_keys=True)
        print(line, flush=True)
        if self._path is not None:
            with self._path.open("a", encoding="utf-8") as stream:
                stream.write(line + "\n")


def write_all(port, data: bytes) -> None:
    offset = 0
    while offset < len(data):
        written = port.write(data[offset:])
        if written is None or written <= 0:
            raise OSError("serial write made no progress")
        offset += written
    port.flush()


def send_frame(port, log: EvidenceLog, case: str, data: bytes) -> None:
    write_all(port, data)
    log.emit("tx", case=case, length=len(data), hex=hex_bytes(data))


def receive_for(port, seconds: float, log: EvidenceLog) -> None:
    decoder = StreamDecoder()
    tracker = SequenceTracker()
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        data = port.read(4096)
        if not data:
            continue
        log.emit("rx_bytes", length=len(data), hex=hex_bytes(data))
        for decoded in decoder.feed(data):
            frame = decoded.frame
            log.emit(
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
    log.emit(
        "rx_summary",
        discarded_bytes=decoder.discarded_bytes,
        invalid_frames=decoder.invalid_frames,
    )


def open_port(args):
    serial = load_serial_module()
    return serial.Serial(
        port=args.port,
        baudrate=args.baud,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.05,
        write_timeout=1.0,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
    )


def best_effort_purge(port, log: EvidenceLog) -> None:
    try:
        port.reset_input_buffer()
        port.reset_output_buffer()
        log.emit("serial_purge", result="ok")
    except Exception as exc:  # Hardware/driver support differs by adapter.
        log.emit("serial_purge", result="best_effort_failed", error=str(exc))


def run_monitor(args) -> int:
    log = EvidenceLog(args.log)
    with open_port(args) as port:
        log.emit("port_open", port=args.port, baud=args.baud)
        receive_for(port, args.seconds, log)
    return 0


def run_exercise(args) -> int:
    log = EvidenceLog(args.log)
    boot_id = args.boot_id
    port = open_port(args)
    try:
        log.emit("port_open", port=args.port, baud=args.baud)
        best_effort_purge(port, log)

        send_frame(port, log, "announce", announce_frame(args.source, boot_id))
        send_frame(
            port,
            log,
            "heartbeat",
            heartbeat_frame(args.source, boot_id, 2, 5000),
        )
        send_frame(
            port,
            log,
            "data",
            temperature_frame(args.source, args.gd32_id, 1),
        )

        half = temperature_frame(args.source, args.gd32_id, 2, 31000, 2000)
        split = min(max(args.half_split, 1), len(half) - 1)
        write_all(port, half[:split])
        log.emit("tx_half", case="half_first", length=split)
        time.sleep(args.half_delay)
        write_all(port, half[split:])
        log.emit("tx_half", case="half_second", length=len(half) - split)

        sticky_a = temperature_frame(args.source, args.gd32_id, 3, 31500, 3000)
        sticky_b = temperature_frame(args.source, args.gd32_id, 4, 32000, 4000)
        send_frame(port, log, "sticky_data_3_4", sticky_a + sticky_b)

        bad_crc = bytearray(
            temperature_frame(args.source, args.gd32_id, 5, 32500, 5000)
        )
        bad_crc[-1] ^= 0x01
        send_frame(port, log, "bad_crc_sequence_5", bytes(bad_crc))
        valid_five = temperature_frame(
            args.source, args.gd32_id, 5, 32500, 5000
        )
        send_frame(port, log, "valid_sequence_5_after_bad_crc", valid_five)
        send_frame(port, log, "duplicate_sequence_5", valid_five)
        send_frame(
            port,
            log,
            "stale_sequence_3",
            temperature_frame(args.source, args.gd32_id, 3, 31500, 3000),
        )

        receive_for(port, args.observe_seconds, log)

        if not args.skip_disconnect:
            log.emit("disconnect_begin", seconds=args.disconnect_seconds)
            port.close()
            time.sleep(args.disconnect_seconds)
            port = open_port(args)
            best_effort_purge(port, log)
            boot_id = (boot_id + 1) & 0xFFFFFFFF
            if boot_id == 0:
                boot_id = 1
            send_frame(
                port,
                log,
                "reconnect_new_boot_announce",
                announce_frame(args.source, boot_id),
            )
            send_frame(
                port,
                log,
                "reconnect_heartbeat",
                heartbeat_frame(args.source, boot_id, 2, 100),
            )
            send_frame(
                port,
                log,
                "reconnect_data_sequence_1",
                temperature_frame(args.source, args.gd32_id, 1, 30000, 100),
            )
            receive_for(port, args.observe_seconds, log)

        log.emit(
            "exercise_complete",
            note=(
                "Use GD32 stats/GDB to confirm accepted, CRC, duplicate, stale, "
                "node-session and reconnect counters."
            ),
        )
        return 0
    finally:
        if port.is_open:
            port.close()


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--port", default="COM7")
    result.add_argument("--baud", type=int, default=115200)
    result.add_argument("--log", help="append JSONL evidence to this file")
    subparsers = result.add_subparsers(dest="command", required=True)

    monitor = subparsers.add_parser("monitor", help="validate received frames")
    monitor.add_argument("--seconds", type=float, default=20.0)
    monitor.set_defaults(func=run_monitor)

    exercise = subparsers.add_parser("exercise", help="run stage-one TX cases")
    exercise.add_argument("--source", type=parse_int, default=DEFAULT_WINDOWS_DEVID)
    exercise.add_argument("--gd32-id", type=parse_int, default=DEFAULT_GD32_DEVID)
    exercise.add_argument("--boot-id", type=parse_int, default=0x32000001)
    exercise.add_argument("--half-split", type=int, default=17)
    exercise.add_argument("--half-delay", type=float, default=0.2)
    exercise.add_argument("--observe-seconds", type=float, default=6.0)
    exercise.add_argument("--disconnect-seconds", type=float, default=2.0)
    exercise.add_argument("--skip-disconnect", action="store_true")
    exercise.set_defaults(func=run_exercise)
    return result


def main() -> int:
    args = parser().parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
