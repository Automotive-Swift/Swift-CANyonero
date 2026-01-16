from __future__ import annotations

import queue
import socket
import threading
import time
from dataclasses import dataclass
from typing import Callable, Optional, Tuple
from urllib.parse import urlparse

from . import canyonero

DEFAULT_ENDPOINT = "192.168.42.42:129"
MAX_PDU_SIZE = 0x10003


@dataclass
class Endpoint:
    host: str
    port: int
    scheme: str = "tcp"


def parse_endpoint(value: str) -> Endpoint:
    if "://" in value:
        parsed = urlparse(value)
        scheme = parsed.scheme.lower()
        if scheme in {"ecuconnect-l2cap", "l2cap", "ble"}:
            raise NotImplementedError("L2CAP transport is not implemented on Linux yet")
        if scheme in {"ecuconnect-wifi", "ecuconnect", "tcp"}:
            host = parsed.hostname or ""
            port = parsed.port or 0
            if not host or not port:
                raise ValueError(f"Invalid endpoint: {value}")
            return Endpoint(host=host, port=port, scheme="tcp")
        raise ValueError(f"Unsupported endpoint scheme: {scheme}")

    if ":" in value:
        host, port_str = value.rsplit(":", 1)
        return Endpoint(host=host, port=int(port_str), scheme="tcp")

    raise ValueError(f"Invalid endpoint: {value}")


class PDUStream:
    def __init__(self, max_pdu_size: int = MAX_PDU_SIZE) -> None:
        self._buffer = bytearray()
        self._max_pdu_size = max_pdu_size

    def feed(self, data: bytes) -> list[canyonero.PDU]:
        self._buffer.extend(data)
        pdus: list[canyonero.PDU] = []
        while self._buffer:
            if canyonero.PDU.exceeds_max_pdu_size(self._buffer, self._max_pdu_size):
                raise ValueError("Incoming PDU exceeds max size")
            scan = canyonero.PDU.scan_buffer(self._buffer)
            if scan > 0:
                frame = bytes(self._buffer[:scan])
                del self._buffer[:scan]
                pdus.append(canyonero.PDU.from_frame(frame))
            elif scan < 0:
                drop = abs(scan)
                del self._buffer[:drop]
            else:
                break
        return pdus


class EcuconnectClient:
    def __init__(
        self,
        endpoint: str = DEFAULT_ENDPOINT,
        rx_buffer: int = 4 * 1024 * 1024,
        tx_buffer: int = 4 * 1024 * 1024,
        max_pdu_size: int = MAX_PDU_SIZE,
    ) -> None:
        self.endpoint = parse_endpoint(endpoint)
        self.rx_buffer = rx_buffer
        self.tx_buffer = tx_buffer
        self.max_pdu_size = max_pdu_size
        self._sock: Optional[socket.socket] = None
        self._rx_queue: queue.Queue[canyonero.PDU] = queue.Queue()
        self._reader: Optional[threading.Thread] = None
        self._stop = threading.Event()
        self._stream = PDUStream(max_pdu_size=max_pdu_size)

    def connect(self) -> None:
        if self._sock:
            return
        sock = socket.create_connection((self.endpoint.host, self.endpoint.port), timeout=5.0)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, self.rx_buffer)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, self.tx_buffer)
        sock.settimeout(1.0)
        self._sock = sock
        self._stop.clear()
        self._reader = threading.Thread(target=self._read_loop, name="ecuconnect-reader", daemon=True)
        self._reader.start()

    def close(self) -> None:
        self._stop.set()
        # Wait for reader thread to exit before closing socket
        if self._reader is not None:
            self._reader.join(timeout=1.0)
            self._reader = None
        if self._sock:
            try:
                self._sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self._sock.close()
        self._sock = None

    def __enter__(self) -> "EcuconnectClient":
        self.connect()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def send_pdu(self, pdu: canyonero.PDU) -> None:
        if not self._sock:
            raise RuntimeError("Not connected")
        self._sock.sendall(pdu.frame())

    def get_pdu(self, timeout: Optional[float] = None) -> Optional[canyonero.PDU]:
        try:
            return self._rx_queue.get(timeout=timeout)
        except queue.Empty:
            return None

    def wait_for(self, predicate: Callable[[canyonero.PDU], bool], timeout: float) -> canyonero.PDU:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            remaining = max(0.0, deadline - time.monotonic())
            pdu = self.get_pdu(timeout=remaining)
            if pdu is None:
                continue
            if predicate(pdu):
                return pdu
        raise TimeoutError("Timed out waiting for ECUconnect response")

    def request_info(self, timeout: float = 2.0) -> canyonero.Info:
        self.send_pdu(canyonero.PDU.request_info())
        pdu = self.wait_for(lambda p: p.type == canyonero.PDUType.info, timeout)
        return pdu.information()

    def read_voltage(self, timeout: float = 2.0) -> float:
        self.send_pdu(canyonero.PDU.read_voltage())
        pdu = self.wait_for(lambda p: p.type == canyonero.PDUType.voltage, timeout)
        payload = pdu.payload()
        if len(payload) < 2:
            raise ValueError("Invalid voltage payload")
        millivolts = int.from_bytes(payload[:2], "big")
        return millivolts / 1000.0

    def ping(self, payload_size: int, timeout: float = 2.0) -> float:
        payload = bytes([0xA5] * payload_size)
        start = time.perf_counter()
        self.send_pdu(canyonero.PDU.ping(payload))
        _ = self.wait_for(lambda p: p.type == canyonero.PDUType.pong, timeout)
        return time.perf_counter() - start

    def open_channel(
        self,
        protocol: canyonero.ChannelProtocol,
        bitrate: int,
        rx_separation_us: int = 0,
        tx_separation_us: int = 0,
        timeout: float = 2.0,
    ) -> int:
        rx_code = canyonero.PDU.separation_time_code_from_microseconds(rx_separation_us)
        tx_code = canyonero.PDU.separation_time_code_from_microseconds(tx_separation_us)
        self.send_pdu(canyonero.PDU.open_channel(protocol, bitrate, rx_code, tx_code))
        pdu = self.wait_for(lambda p: p.type == canyonero.PDUType.channel_opened, timeout)
        payload = pdu.payload()
        if not payload:
            raise ValueError("Channel opened reply missing handle")
        return payload[0]

    def set_arbitration(self, handle: int, arbitration: canyonero.Arbitration, timeout: float = 2.0) -> None:
        self.send_pdu(canyonero.PDU.set_arbitration(handle, arbitration))
        _ = self.wait_for(lambda p: p.type == canyonero.PDUType.ok, timeout)

    def send(self, handle: int, data: bytes) -> None:
        self.send_pdu(canyonero.PDU.send(handle, data))

    def close_channel(self, handle: int, timeout: float = 2.0) -> None:
        self.send_pdu(canyonero.PDU.close_channel(handle))
        _ = self.wait_for(lambda p: p.type == canyonero.PDUType.channel_closed, timeout)

    def _read_loop(self) -> None:
        assert self._sock is not None
        while not self._stop.is_set():
            try:
                data = self._sock.recv(4096)
                if not data:
                    break
                for pdu in self._stream.feed(data):
                    self._rx_queue.put(pdu)
            except socket.timeout:
                continue
            except OSError:
                break

