"""Host-only tests for the Windows UMCA tools."""

from __future__ import annotations

import pathlib
import sys
import threading
import time
import unittest
from types import SimpleNamespace


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from com_tcp_bridge import FixedByteQueue, TransparentBridge  # noqa: E402
from umca_three_node_sim import (  # noqa: E402
    DEFAULT_ACTUATOR_DEVID,
    DEFAULT_ESP32_DEVID,
    DEFAULT_GD32_DEVID,
    DEFAULT_SENSOR_DEVID,
    MockLlmProvider,
    ThreeNodeSimulator,
)
from umca_wire import (  # noqa: E402
    Frame,
    MSG_DATA,
    SequenceTracker,
    StreamDecoder,
    TOPIC_TEMPERATURE,
    TOPIC_LLM_CHAT_REQUEST,
    TOPIC_LLM_CHAT_REQUEST_NAME,
    TOPIC_PUBLISHER,
    TOPIC_SUBSCRIBER,
    LLM_ACTION_NONE,
    LLM_ACTION_SET_FAN,
    LLM_ORIGIN_TERMINAL,
    LLM_STATUS_OK,
    LLM_STATUS_REJECTED,
    announce_frame,
    chat_response_payload,
    crc32c,
    data_frame,
    decode_chat_request,
    decode_fan_command,
    decode_frame,
    encode_frame,
)


EMPTY_DATA_VECTOR = bytes.fromhex(
    "55 4d 01 24 00 00 00 00 08 00 ff ff ff ff ff ff ff ff "
    "00 00 00 00 00 00 10 01 0f c9 5c b0 00 00 00 01 00 00 "
    "c8 c2 08 2a"
)


class WireTests(unittest.TestCase):
    def test_crc32c_vector(self) -> None:
        self.assertEqual(crc32c(b"123456789"), 0xE3069283)

    def test_empty_data_frozen_vector(self) -> None:
        frame = Frame(
            message_type=MSG_DATA,
            destination=0xFFFFFFFFFFFFFFFF,
            source=0x1001,
            topic_id=TOPIC_TEMPERATURE,
            sequence=1,
        )
        self.assertEqual(encode_frame(frame), EMPTY_DATA_VECTOR)
        self.assertEqual(decode_frame(EMPTY_DATA_VECTOR).frame, frame)

    def test_half_frame_and_sticky_frames(self) -> None:
        first = EMPTY_DATA_VECTOR
        second = encode_frame(
            Frame(
                message_type=MSG_DATA,
                destination=0xFFFFFFFFFFFFFFFF,
                source=0x1001,
                topic_id=TOPIC_TEMPERATURE,
                sequence=2,
            )
        )
        decoder = StreamDecoder()
        self.assertEqual(decoder.feed(first[:17]), [])
        self.assertEqual([x.raw for x in decoder.feed(first[17:] + second)], [first, second])

    def test_crc_failure_resynchronizes_to_next_magic(self) -> None:
        bad = bytearray(EMPTY_DATA_VECTOR)
        bad[-1] ^= 1
        decoder = StreamDecoder()
        decoded = decoder.feed(b"noise" + bytes(bad) + EMPTY_DATA_VECTOR)
        self.assertEqual([x.raw for x in decoded], [EMPTY_DATA_VECTOR])
        self.assertEqual(decoder.invalid_frames, 1)
        self.assertEqual(decoder.discarded_bytes, 5)

    def test_new_boot_announce_resets_system_sequence(self) -> None:
        tracker = SequenceTracker()
        old = decode_frame(announce_frame(0x1001, 1, 9))
        new = decode_frame(announce_frame(0x1001, 2, 1))
        self.assertEqual(tracker.observe(old), "baseline")
        self.assertEqual(tracker.observe(old), "duplicate")
        self.assertEqual(tracker.observe(new), "baseline")

    def test_custom_announce_topics_and_forwarded_ttl(self) -> None:
        decoded = decode_frame(
            announce_frame(
                0x3301,
                0x33000001,
                topics=((TOPIC_LLM_CHAT_REQUEST, TOPIC_SUBSCRIBER,
                         TOPIC_LLM_CHAT_REQUEST_NAME),),
                ttl=7,
            )
        )
        self.assertEqual(decoded.frame.ttl, 7)
        self.assertIn(TOPIC_LLM_CHAT_REQUEST_NAME, decoded.frame.payload)

    def test_chat_payload_round_trip(self) -> None:
        text = "打开风扇".encode("utf-8")
        payload = (
            (0x47000001).to_bytes(4, "big")
            + (9).to_bytes(4, "big")
            + (15000).to_bytes(4, "big")
            + bytes((LLM_ORIGIN_TERMINAL,))
            + len(text).to_bytes(2, "big")
            + text
        )
        self.assertEqual(
            decode_chat_request(payload),
            (0x47000001, 9, 15000, LLM_ORIGIN_TERMINAL, text),
        )
        response = chat_response_payload(
            0x47000001, 9, LLM_STATUS_OK, LLM_ACTION_SET_FAN, 1, b"ok"
        )
        frame = decode_frame(
            data_frame(0x3301, 0x4701, TOPIC_LLM_CHAT_REQUEST, 1, response)
        ).frame
        self.assertEqual(frame.payload, response)

    def test_mock_llm_has_bounded_actions(self) -> None:
        provider = MockLlmProvider()
        prefix = "C1;tv=1;t=31000;age=420;q=1;so=1;ao=1;f=0\nQ:"
        self.assertEqual(provider.respond(prefix + "turn fan on")[1:3],
                         (LLM_ACTION_SET_FAN, 1))
        self.assertEqual(provider.respond(prefix + "turn fan off")[1:3],
                         (LLM_ACTION_SET_FAN, 0))
        status, action, _, text = provider.respond(prefix + "temperature?")
        self.assertEqual((status, action), (LLM_STATUS_OK, LLM_ACTION_NONE))
        self.assertIn("31000", text)
        invalid = "C1;tv=0;so=0;ao=1;f=2\nQ:temperature?"
        self.assertIn("unavailable", provider.respond(invalid)[3])
        self.assertEqual(provider.respond("turn fan on")[0],
                         LLM_STATUS_REJECTED)


class FixedQueueTests(unittest.TestCase):
    def test_overflow_drops_new_bytes(self) -> None:
        queue = FixedByteQueue(4)
        self.assertEqual(queue.put(b"abc"), (3, 0))
        self.assertEqual(queue.put(b"def"), (1, 2))
        self.assertEqual(queue.get(8), b"abcd")

    def test_disconnect_clears_and_rejects_late_writer(self) -> None:
        queue = FixedByteQueue(8)
        self.assertEqual(queue.put(b"old"), (3, 0))
        self.assertEqual(queue.close_and_clear(), 3)
        self.assertEqual(queue.put(b"late"), (0, 4))
        self.assertEqual(queue.get(8), b"")


class FakeSerial:
    def __init__(self) -> None:
        self.input = bytearray()
        self.output = bytearray()
        self.lock = threading.Lock()

    def read(self, maximum: int) -> bytes:
        with self.lock:
            length = min(maximum, len(self.input))
            if length:
                result = bytes(self.input[:length])
                del self.input[:length]
                return result
        time.sleep(0.005)
        return b""

    def write(self, data: bytes) -> int:
        with self.lock:
            self.output.extend(data)
        return len(data)

    def reset_input_buffer(self) -> None:
        with self.lock:
            self.input.clear()

    def reset_output_buffer(self) -> None:
        with self.lock:
            self.output.clear()

    def flush(self) -> None:
        pass


class CapturingLog:
    def __init__(self) -> None:
        self.records = []

    def emit(self, event: str, **fields) -> None:
        self.records.append((event, fields))


class ThreeNodeSimulatorTests(unittest.TestCase):
    def make_simulator(self):
        serial = FakeSerial()
        log = CapturingLog()
        args = SimpleNamespace(
            gd32_devid=DEFAULT_GD32_DEVID,
            sensor_devid=DEFAULT_SENSOR_DEVID,
            actuator_devid=DEFAULT_ACTUATOR_DEVID,
            esp32_devid=DEFAULT_ESP32_DEVID,
            sensor_boot_id=0x10010001,
            actuator_boot_id=0x30010001,
            esp32_boot_id=0x33000001,
            log_hex=False,
            temperature_phase_seconds=5.0,
            temperature_high_mc=31000,
            temperature_low_mc=27000,
            on_threshold_mc=30000,
            off_threshold_mc=28000,
        )
        return ThreeNodeSimulator(serial, args, log), serial, log

    def test_actuator_returns_matching_fan_state(self) -> None:
        simulator, serial, log = self.make_simulator()
        command = (7).to_bytes(4, "big") + bytes((1, 0))
        self.assertEqual(decode_fan_command(command), (7, 1, 0))
        simulator.handle_fan_command(command)
        response = decode_frame(bytes(serial.output)).frame
        self.assertEqual(response.source, DEFAULT_ACTUATOR_DEVID)
        self.assertEqual(response.destination, DEFAULT_GD32_DEVID)
        self.assertEqual(response.topic_id, 0x6D3F9EBE)
        self.assertEqual(response.ttl, 7)
        self.assertIn("fan_applied", [event for event, _ in log.records])

    def test_esp32_returns_correlated_chat_action(self) -> None:
        simulator, serial, log = self.make_simulator()
        text = b"C1;tv=1;t=31000;age=420;q=1;so=1;ao=1;f=0\nQ:turn fan on"
        request = (
            (0x47000001).to_bytes(4, "big")
            + (11).to_bytes(4, "big")
            + (15000).to_bytes(4, "big")
            + bytes((LLM_ORIGIN_TERMINAL,))
            + len(text).to_bytes(2, "big")
            + text
        )
        simulator.handle_chat_request(request)
        response = decode_frame(bytes(serial.output)).frame
        self.assertEqual(response.source, DEFAULT_ESP32_DEVID)
        self.assertEqual(response.destination, DEFAULT_GD32_DEVID)
        self.assertEqual(response.topic_id, 0x9D3E1F72)
        self.assertEqual(response.ttl, 8)
        self.assertEqual(int.from_bytes(response.payload[0:4], "big"),
                         0x47000001)
        self.assertEqual(int.from_bytes(response.payload[4:8], "big"), 11)
        self.assertEqual(response.payload[9:11], bytes((LLM_ACTION_SET_FAN, 1)))
        self.assertIn("chat_handled", [event for event, _ in log.records])


class FakeSocket:
    def __init__(self) -> None:
        self.input = bytearray()
        self.output = bytearray()
        self.closed = False
        self.lock = threading.Lock()

    def settimeout(self, timeout: float) -> None:
        del timeout

    def peer_send(self, data: bytes) -> None:
        with self.lock:
            self.input.extend(data)

    def recv(self, maximum: int) -> bytes:
        with self.lock:
            if self.closed:
                return b""
            length = min(maximum, len(self.input))
            if length:
                result = bytes(self.input[:length])
                del self.input[:length]
                return result
        time.sleep(0.005)
        raise TimeoutError

    def sendall(self, data: bytes) -> None:
        with self.lock:
            if self.closed:
                raise OSError("socket closed")
            self.output.extend(data)

    def shutdown(self, how: int) -> None:
        del how
        with self.lock:
            self.closed = True

    def close(self) -> None:
        with self.lock:
            self.closed = True


class BridgeTests(unittest.TestCase):
    def wait_until(self, predicate, timeout: float = 1.0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(0.005)
        self.fail("condition not reached before timeout")

    def test_transparent_bidirectional_transfer_and_disconnect_drop(self) -> None:
        serial = FakeSerial()
        bridge = TransparentBridge(serial, capacity=32, io_chunk=8)
        reader = threading.Thread(target=bridge.serial_reader_loop, daemon=True)
        reader.start()
        bridge_sock = FakeSocket()
        session = bridge.attach(bridge_sock)

        bridge_sock.peer_send(b"tcp-bytes")
        self.wait_until(lambda: bytes(serial.output) == b"tcp-bytes")

        with serial.lock:
            serial.input.extend(b"uart-bytes")
        self.wait_until(lambda: bytes(bridge_sock.output) == b"uart-bytes")

        bridge.disconnect(session, "unit test")
        with serial.lock:
            serial.input.extend(b"offline")
        self.wait_until(
            lambda: bridge.stats.uart_to_tcp.disconnected_drop_bytes >= 7
        )
        self.assertIsNone(bridge.current_session())
        bridge.shutdown()

    def test_old_generation_queue_rejects_late_data(self) -> None:
        serial = FakeSerial()
        bridge = TransparentBridge(serial, capacity=8, io_chunk=8)
        bridge_sock = FakeSocket()
        session = bridge.attach(bridge_sock)
        self.assertEqual(session.uart_to_tcp.put(b"old"), (3, 0))
        bridge.disconnect(session, "unit test")
        self.assertEqual(session.uart_to_tcp.put(b"late"), (0, 4))
        bridge.shutdown()


if __name__ == "__main__":
    unittest.main()
