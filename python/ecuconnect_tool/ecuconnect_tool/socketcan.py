from __future__ import annotations

import queue
import threading
import time
from dataclasses import dataclass
from typing import Iterable, Optional, Tuple

import socket
import can as python_can

CAN_EFF_FLAG = 0x80000000
CAN_EFF_MASK = 0x1FFFFFFF
CAN_SFF_MASK = 0x7FF


@dataclass
class CanFrame:
    can_id: int
    data: bytes
    extended: bool = False


class SocketCanBus:
    def __init__(
        self,
        interface: str,
        rx_buffer: int = 4 * 1024 * 1024,
        tx_buffer: int = 4 * 1024 * 1024,
        filters: Optional[Iterable[Tuple[int, int]]] = None,
        tx_retry_attempts: int = 20,
        tx_retry_sleep_ms: int = 2,
    ) -> None:
        self.interface = interface
        self.rx_buffer = rx_buffer
        self.tx_buffer = tx_buffer
        self.filters = list(filters) if filters else []
        self._tx_retry_attempts = max(tx_retry_attempts, 0)
        self._tx_retry_sleep_ms = max(tx_retry_sleep_ms, 0)
        self.bus = self._open_bus()
        self._rx_queue: queue.Queue[CanFrame] = queue.Queue()
        self._stop = threading.Event()
        self._reader: Optional[threading.Thread] = None

    def _open_bus(self) -> python_can.BusABC:
        try:
            bus = python_can.Bus(
                interface="socketcan",
                channel=self.interface,
                receive_own_messages=False,
            )
        except TypeError:
            bus = python_can.Bus(
                bustype="socketcan",
                channel=self.interface,
                receive_own_messages=False,
            )

        if self.filters:
            bus.set_filters([self._filter_to_dict(item) for item in self.filters])

        if hasattr(bus, "socket"):
            try:
                bus.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, self.rx_buffer)
                bus.socket.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, self.tx_buffer)
            except OSError:
                pass

        return bus

    def _filter_to_dict(self, entry: Tuple[int, int]) -> dict:
        can_id, mask = entry
        extended = bool(can_id & CAN_EFF_FLAG)
        if extended:
            can_id &= CAN_EFF_MASK
            mask &= CAN_EFF_MASK
        else:
            can_id &= CAN_SFF_MASK
            mask &= CAN_SFF_MASK
        return {"can_id": can_id, "can_mask": mask, "extended": extended}

    def close(self) -> None:
        self._stop.set()
        if self.bus:
            if hasattr(self.bus, "shutdown"):
                self.bus.shutdown()
            else:
                self.bus.close()

    def start_reader(self) -> None:
        if self._reader and self._reader.is_alive():
            return
        self._stop.clear()
        self._reader = threading.Thread(target=self._read_loop, name="socketcan-reader", daemon=True)
        self._reader.start()

    def get_frame(self, timeout: Optional[float] = None) -> Optional[CanFrame]:
        try:
            return self._rx_queue.get(timeout=timeout)
        except queue.Empty:
            return None

    def send_frame(self, can_id: int, data: bytes, extended: bool = False) -> None:
        msg = python_can.Message(
            arbitration_id=can_id,
            is_extended_id=extended,
            data=bytes(data[:8]),
        )
        for attempt in range(self._tx_retry_attempts + 1):
            try:
                self.bus.send(msg)
                return
            except Exception as exc:
                if "Transmit buffer full" not in str(exc) or attempt >= self._tx_retry_attempts:
                    raise
                time.sleep(self._tx_retry_sleep_ms / 1000.0)

    def recv_frame(self, timeout: Optional[float] = None) -> Optional[CanFrame]:
        msg = self.bus.recv(timeout=timeout)
        if msg is None:
            return None
        dlc = getattr(msg, "dlc", len(msg.data))
        return CanFrame(
            can_id=msg.arbitration_id,
            data=bytes(msg.data[:dlc]),
            extended=bool(msg.is_extended_id),
        )

    def _read_loop(self) -> None:
        while not self._stop.is_set():
            frame = self.recv_frame(timeout=0.1)
            if frame:
                self._rx_queue.put(frame)

    def actual_buffers(self) -> Tuple[int, int]:
        if hasattr(self.bus, "socket"):
            rx = self.bus.socket.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
            tx = self.bus.socket.getsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF)
            return rx, tx
        return 0, 0
