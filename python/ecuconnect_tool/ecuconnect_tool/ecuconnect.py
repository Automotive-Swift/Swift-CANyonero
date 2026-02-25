from __future__ import annotations

import json
import os
import queue
import socket
import sys
import threading
import time
import uuid
from dataclasses import dataclass
from typing import Any, Callable, Optional, Tuple
from urllib.parse import urlparse

from . import canyonero
from .macos_l2cap import connect_l2cap

DEFAULT_ENDPOINT = "192.168.42.42:129"
MAX_PDU_SIZE = 0x10003


@dataclass
class Endpoint:
    host: str
    port: int
    scheme: str = "tcp"
    peer_uuid: Optional[str] = None


def parse_endpoint(value: str) -> Endpoint:
    if "://" in value:
        parsed = urlparse(value)
        scheme = parsed.scheme.lower()
        if scheme in {"ecuconnect-l2cap", "l2cap", "ble"}:
            host = parsed.hostname or ""
            port = parsed.port or 129
            if not host:
                raise ValueError(f"Invalid endpoint: {value}")
            if not (1 <= int(port) <= 65535):
                raise ValueError(f"Invalid endpoint port: {port}")
            peer_uuid: Optional[str] = None
            if parsed.path and parsed.path != "/":
                candidate = parsed.path.lstrip("/")
                try:
                    peer_uuid = str(uuid.UUID(candidate))
                except ValueError as exc:
                    raise ValueError(f"Invalid BLE peer UUID in endpoint path: {candidate}") from exc
            return Endpoint(host=host.upper(), port=int(port), scheme="ble", peer_uuid=peer_uuid)
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
        self._sock: Optional[Any] = None
        self._rx_queue: queue.Queue[canyonero.PDU] = queue.Queue()
        self._reader: Optional[threading.Thread] = None
        self._stop = threading.Event()
        self._stream = PDUStream(max_pdu_size=max_pdu_size)
        self._rpc_id = 1
        self._debug_io = os.getenv("ECUCONNECT_DEBUG_IO", "").strip().lower() not in {"", "0", "false", "off"}
        self._inline_reads = False

    def _log_io(self, direction: str, payload: bytes) -> None:
        if not self._debug_io:
            return
        preview = payload[:64].hex(" ")
        suffix = " ..." if len(payload) > 64 else ""
        print(f"[pdu:{direction}] {len(payload)} bytes: {preview}{suffix}", file=sys.stderr, flush=True)

    def connect(self) -> None:
        if self._sock:
            return
        self._inline_reads = False
        if self.endpoint.scheme == "tcp":
            sock = socket.create_connection((self.endpoint.host, self.endpoint.port), timeout=5.0)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, self.rx_buffer)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, self.tx_buffer)
            sock.settimeout(1.0)
        elif self.endpoint.scheme == "ble":
            if sys.platform != "darwin":
                raise NotImplementedError("BLE/L2CAP transport is only supported on macOS.")
            sock = connect_l2cap(
                service_uuid=self.endpoint.host,
                psm=self.endpoint.port,
                timeout=5.0,
                peer_uuid=self.endpoint.peer_uuid,
            )
            sock.settimeout(0.2)
            self._inline_reads = True
        else:
            raise ValueError(f"Unsupported endpoint scheme: {self.endpoint.scheme}")
        self._sock = sock
        if self._debug_io:
            print(
                f"[pdu:connect] scheme={self.endpoint.scheme} host={self.endpoint.host} port={self.endpoint.port}",
                file=sys.stderr,
                flush=True,
            )
        self._stop.clear()
        if self._inline_reads:
            self._reader = None
        else:
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
            try:
                self._sock.close()
            except OSError:
                pass
        self._sock = None

    def __enter__(self) -> "EcuconnectClient":
        self.connect()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def send_pdu(self, pdu: canyonero.PDU) -> None:
        if not self._sock:
            raise RuntimeError("Not connected")
        frame = pdu.frame()
        self._log_io("tx", frame)
        self._sock.sendall(frame)

    def get_pdu(self, timeout: Optional[float] = None) -> Optional[canyonero.PDU]:
        if self._inline_reads:
            return self._get_pdu_inline(timeout)
        try:
            return self._rx_queue.get(timeout=timeout)
        except queue.Empty:
            return None

    def _get_pdu_inline(self, timeout: Optional[float]) -> Optional[canyonero.PDU]:
        if self._sock is None:
            return None
        deadline = None if timeout is None else (time.monotonic() + max(0.0, timeout))
        while True:
            try:
                return self._rx_queue.get_nowait()
            except queue.Empty:
                pass

            if deadline is not None and time.monotonic() >= deadline:
                return None

            try:
                data = self._sock.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                return None
            if not data:
                return None

            self._log_io("rx-raw", data)
            for pdu in self._stream.feed(data):
                try:
                    self._log_io("rx-pdu", pdu.frame())
                except Exception:
                    pass
                self._rx_queue.put(pdu)

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

    def reset(self, timeout: float = 2.0) -> None:
        self.send_pdu(canyonero.PDU.reset())
        _ = self.wait_for(lambda p: p.type == canyonero.PDUType.ok, timeout)

    def rpc_call(self, method: str, params: Optional[dict] = None, timeout: float = 2.0) -> dict:
        if params is None:
            params = {}
        rpc_id = self._rpc_id
        self._rpc_id += 1
        payload = json.dumps(
            {"method": method, "id": rpc_id, "params": params},
            separators=(",", ":"),
        )
        self.send_pdu(canyonero.PDU.rpc_call(payload))

        def _predicate(pdu: canyonero.PDU) -> bool:
            return pdu.type in (
                canyonero.PDUType.rpc_response,
                canyonero.PDUType.error_invalid_rpc,
                canyonero.PDUType.error_invalid_command,
                canyonero.PDUType.error_hardware,
                canyonero.PDUType.error_unspecified,
            )

        pdu = self.wait_for(_predicate, timeout)
        if pdu.type != canyonero.PDUType.rpc_response:
            raise RuntimeError(f"RPC {method} failed: {pdu.type}")
        raw_payload = pdu.payload()
        try:
            response = json.loads(raw_payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise ValueError("Invalid RPC response payload") from exc
        if response.get("id") != rpc_id:
            raise ValueError(f"Unexpected RPC response id: {response.get('id')}")
        result = response.get("result")
        if isinstance(result, dict):
            return result
        if result is None:
            return {}
        raise ValueError("RPC response result is not a JSON object")

    def open_channel(
        self,
        protocol: canyonero.ChannelProtocol,
        bitrate: int,
        data_bitrate: Optional[int] = None,
        rx_separation_us: int = 0,
        tx_separation_us: int = 0,
        timeout: float = 2.0,
    ) -> int:
        rx_code = canyonero.PDU.separation_time_code_from_microseconds(rx_separation_us)
        tx_code = canyonero.PDU.separation_time_code_from_microseconds(tx_separation_us)
        fd_protocols = {
            canyonero.ChannelProtocol.raw_fd,
            canyonero.ChannelProtocol.isotp_fd,
        }
        if protocol in fd_protocols:
            if data_bitrate is None or data_bitrate <= 0:
                raise ValueError("data_bitrate must be provided for FD channels")
            self.send_pdu(canyonero.PDU.open_fd_channel(protocol, bitrate, data_bitrate, rx_code, tx_code))
        else:
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
                self._log_io("rx-raw", data)
                for pdu in self._stream.feed(data):
                    try:
                        self._log_io("rx-pdu", pdu.frame())
                    except Exception:
                        pass
                    self._rx_queue.put(pdu)
            except socket.timeout:
                continue
            except OSError:
                break
