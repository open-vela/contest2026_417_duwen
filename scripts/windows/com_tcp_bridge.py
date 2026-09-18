"""Pure byte-transparent Windows COM-to-TCP bridge.

The bridge deliberately does not import UMCA code or inspect message types,
topics, headers, CRCs, sequences, Boot IDs or request IDs.
"""

from __future__ import annotations

import argparse
import socket
import sys
import threading
import time
from dataclasses import dataclass, field
from typing import Optional, Tuple


class FixedByteQueue:
    """Thread-safe fixed-capacity byte queue that drops new overflow bytes."""

    def __init__(self, capacity: int) -> None:
        if capacity <= 0:
            raise ValueError("capacity must be positive")
        self.capacity = capacity
        self._data = bytearray()
        self._closed = False
        self._condition = threading.Condition()

    def put(self, data: bytes) -> Tuple[int, int]:
        with self._condition:
            if self._closed:
                return 0, len(data)
            available = self.capacity - len(self._data)
            accepted = min(available, len(data))
            if accepted:
                self._data.extend(data[:accepted])
                self._condition.notify()
            return accepted, len(data) - accepted

    def get(self, maximum: int, timeout: float = 0.2) -> bytes:
        with self._condition:
            if maximum <= 0:
                raise ValueError("maximum must be positive")
            if not self._data and not self._closed:
                self._condition.wait(timeout)
            if not self._data:
                return b""
            length = min(maximum, len(self._data))
            result = bytes(self._data[:length])
            del self._data[:length]
            return result

    def close_and_clear(self) -> int:
        with self._condition:
            self._closed = True
            cleared = len(self._data)
            self._data.clear()
            self._condition.notify_all()
            return cleared

    def __len__(self) -> int:
        with self._condition:
            return len(self._data)

    def is_closed(self) -> bool:
        with self._condition:
            return self._closed


@dataclass
class DirectionStats:
    read_bytes: int = 0
    written_bytes: int = 0
    overflow_events: int = 0
    overflow_bytes: int = 0
    disconnected_drop_bytes: int = 0
    cleared_bytes: int = 0


@dataclass
class BridgeStats:
    uart_to_tcp: DirectionStats = field(default_factory=DirectionStats)
    tcp_to_uart: DirectionStats = field(default_factory=DirectionStats)
    connections: int = 0
    disconnects: int = 0
    serial_purge_failures: int = 0
    _lock: threading.Lock = field(default_factory=threading.Lock, repr=False)

    def update(self, direction: str, **changes: int) -> None:
        with self._lock:
            target = getattr(self, direction)
            for name, value in changes.items():
                setattr(target, name, getattr(target, name) + value)

    def increment(self, name: str, value: int = 1) -> None:
        with self._lock:
            setattr(self, name, getattr(self, name) + value)

    def line(self) -> str:
        with self._lock:
            u = self.uart_to_tcp
            t = self.tcp_to_uart
            return (
                f"connections={self.connections} disconnects={self.disconnects} "
                f"uart->tcp read={u.read_bytes} sent={u.written_bytes} "
                f"overflow={u.overflow_events}/{u.overflow_bytes} "
                f"offline_drop={u.disconnected_drop_bytes} cleared={u.cleared_bytes} "
                f"tcp->uart read={t.read_bytes} sent={t.written_bytes} "
                f"overflow={t.overflow_events}/{t.overflow_bytes} "
                f"cleared={t.cleared_bytes} purge_fail={self.serial_purge_failures}"
            )


class ConnectionSession:
    def __init__(self, sock: socket.socket, capacity: int, generation: int) -> None:
        self.sock = sock
        self.generation = generation
        self.stop = threading.Event()
        self.uart_to_tcp = FixedByteQueue(capacity)
        self.tcp_to_uart = FixedByteQueue(capacity)

    def close_and_clear(self) -> Tuple[int, int]:
        self.stop.set()
        uart_cleared = self.uart_to_tcp.close_and_clear()
        tcp_cleared = self.tcp_to_uart.close_and_clear()
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        try:
            self.sock.close()
        except OSError:
            pass
        return uart_cleared, tcp_cleared


class TransparentBridge:
    def __init__(self, serial_port, capacity: int, io_chunk: int) -> None:
        self.serial = serial_port
        self.capacity = capacity
        self.io_chunk = io_chunk
        self.stats = BridgeStats()
        self.stop = threading.Event()
        self._lock = threading.Lock()
        self._session: Optional[ConnectionSession] = None
        self._generation = 0

    def current_session(self) -> Optional[ConnectionSession]:
        with self._lock:
            return self._session

    def attach(self, sock: socket.socket) -> ConnectionSession:
        sock.settimeout(0.5)
        # Purge while no session is published.  The serial reader therefore
        # keeps dropping bytes until the best-effort cleanup is complete.
        self._best_effort_serial_purge()
        with self._lock:
            if self._session is not None:
                raise RuntimeError("a TCP session is already attached")
            self._generation += 1
            session = ConnectionSession(sock, self.capacity, self._generation)
            self._session = session
        self.stats.increment("connections")
        for target, name in (
            (self._tcp_reader, "tcp-reader"),
            (self._tcp_writer, "tcp-writer"),
            (self._serial_writer, "serial-writer"),
        ):
            threading.Thread(
                target=target,
                args=(session,),
                name=f"{name}-{session.generation}",
                daemon=True,
            ).start()
        return session

    def disconnect(self, session: ConnectionSession, reason: str) -> None:
        with self._lock:
            if self._session is not session:
                return
            self._session = None
            cleared_uart, cleared_tcp = session.close_and_clear()
        self.stats.update("uart_to_tcp", cleared_bytes=cleared_uart)
        self.stats.update("tcp_to_uart", cleared_bytes=cleared_tcp)
        self.stats.increment("disconnects")
        self._best_effort_serial_purge()
        print(
            f"TCP disconnected generation={session.generation} reason={reason}; "
            "old socket destroyed and both application buffers cleared",
            flush=True,
        )

    def _best_effort_serial_purge(self) -> None:
        try:
            self.serial.reset_input_buffer()
            self.serial.reset_output_buffer()
        except Exception:
            self.stats.increment("serial_purge_failures")

    def serial_reader_loop(self) -> None:
        while not self.stop.is_set():
            try:
                data = self.serial.read(self.io_chunk)
            except Exception as exc:
                print(f"serial read failed: {exc}", file=sys.stderr, flush=True)
                self.stop.set()
                break
            if not data:
                continue
            self.stats.update("uart_to_tcp", read_bytes=len(data))
            session = self.current_session()
            if session is None or session.stop.is_set():
                self.stats.update(
                    "uart_to_tcp", disconnected_drop_bytes=len(data)
                )
                continue
            accepted, dropped = session.uart_to_tcp.put(data)
            if dropped:
                if session.stop.is_set() or self.current_session() is not session:
                    self.stats.update(
                        "uart_to_tcp", disconnected_drop_bytes=dropped
                    )
                else:
                    self.stats.update(
                        "uart_to_tcp", overflow_events=1, overflow_bytes=dropped
                    )

    def _tcp_reader(self, session: ConnectionSession) -> None:
        try:
            while not self.stop.is_set() and not session.stop.is_set():
                try:
                    data = session.sock.recv(self.io_chunk)
                except socket.timeout:
                    continue
                if not data:
                    self.disconnect(session, "peer closed")
                    return
                self.stats.update("tcp_to_uart", read_bytes=len(data))
                _, dropped = session.tcp_to_uart.put(data)
                if dropped and not session.stop.is_set():
                    self.stats.update(
                        "tcp_to_uart", overflow_events=1, overflow_bytes=dropped
                    )
        except OSError as exc:
            self.disconnect(session, f"recv error: {exc}")

    def _tcp_writer(self, session: ConnectionSession) -> None:
        try:
            while not self.stop.is_set() and not session.stop.is_set():
                data = session.uart_to_tcp.get(self.io_chunk)
                if not data:
                    continue
                session.sock.sendall(data)
                self.stats.update("uart_to_tcp", written_bytes=len(data))
        except OSError as exc:
            self.disconnect(session, f"send error: {exc}")

    def _serial_writer(self, session: ConnectionSession) -> None:
        try:
            while not self.stop.is_set() and not session.stop.is_set():
                data = session.tcp_to_uart.get(self.io_chunk)
                if not data:
                    continue
                offset = 0
                while offset < len(data):
                    if self.stop.is_set() or session.stop.is_set():
                        return
                    written = self.serial.write(data[offset:])
                    if written is None or written <= 0:
                        raise OSError("serial write made no progress")
                    offset += written
                    self.stats.update("tcp_to_uart", written_bytes=written)
        except Exception as exc:
            self.disconnect(session, f"serial write error: {exc}")

    def shutdown(self) -> None:
        self.stop.set()
        session = self.current_session()
        if session is not None:
            self.disconnect(session, "bridge shutdown")


def load_serial_module():
    try:
        import serial  # type: ignore
    except ImportError as exc:
        raise SystemExit(
            "pyserial is required: py -m pip install -r "
            "scripts\\windows\\requirements.txt"
        ) from exc
    return serial


def open_serial(args):
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


def create_server(args):
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((args.host, args.port_number))
    listener.listen(1)
    listener.settimeout(0.5)
    return listener


def connect_client(args) -> Optional[socket.socket]:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(args.connect_timeout)
    try:
        sock.connect((args.host, args.port_number))
    except OSError as exc:
        print(f"TCP connect failed: {exc}", flush=True)
        sock.close()
        return None
    return sock


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--port", default="COM7", help="Windows COM port")
    result.add_argument("--baud", type=int, default=115200)
    result.add_argument("--tcp-mode", choices=("server", "client"), default="server")
    result.add_argument("--host", default="127.0.0.1")
    result.add_argument("--port-number", type=int, default=47000)
    result.add_argument("--buffer-size", type=int, default=4096)
    result.add_argument("--io-chunk", type=int, default=512)
    result.add_argument("--reconnect-delay", type=float, default=1.0)
    result.add_argument("--connect-timeout", type=float, default=3.0)
    result.add_argument("--stats-interval", type=float, default=5.0)
    return result


def main() -> int:
    args = parser().parse_args()
    if args.buffer_size <= 0 or args.io_chunk <= 0:
        raise SystemExit("buffer-size and io-chunk must be positive")
    serial_port = open_serial(args)
    bridge = TransparentBridge(serial_port, args.buffer_size, args.io_chunk)
    threading.Thread(
        target=bridge.serial_reader_loop,
        name="serial-reader",
        daemon=True,
    ).start()
    listener = create_server(args) if args.tcp_mode == "server" else None
    last_stats = time.monotonic()
    print(
        f"bridge started COM={args.port}@{args.baud} TCP={args.tcp_mode} "
        f"{args.host}:{args.port_number} buffer={args.buffer_size}; "
        "pure byte mode, no UMCA parsing",
        flush=True,
    )
    try:
        while not bridge.stop.is_set():
            if bridge.current_session() is None:
                sock: Optional[socket.socket]
                if listener is not None:
                    try:
                        sock, peer = listener.accept()
                        print(f"TCP accepted peer={peer}", flush=True)
                    except socket.timeout:
                        sock = None
                else:
                    sock = connect_client(args)
                if sock is not None:
                    bridge.attach(sock)
                elif args.tcp_mode == "client":
                    time.sleep(args.reconnect_delay)
            else:
                time.sleep(0.05)
            now = time.monotonic()
            if now - last_stats >= args.stats_interval:
                print(bridge.stats.line(), flush=True)
                last_stats = now
    except KeyboardInterrupt:
        print("bridge interrupted", flush=True)
    finally:
        bridge.shutdown()
        if listener is not None:
            listener.close()
        serial_port.close()
        print(bridge.stats.line(), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
