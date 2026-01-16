from __future__ import annotations

import secrets
import threading
import time
from dataclasses import dataclass
from typing import Callable, Iterable, Optional

from . import canyonero
from .ecuconnect import EcuconnectClient
from .socketcan import SocketCanBus


@dataclass
class DirectionMetrics:
    name: str
    sent: int
    received: int
    duplicates: int
    duration: float
    pps: float
    loss_pct: float


@dataclass
class TestSummary:
    can_to_ecu: Optional[DirectionMetrics]
    ecu_to_can: Optional[DirectionMetrics]
    loss_threshold: float

    @property
    def passed(self) -> bool:
        can_ok = self.can_to_ecu is None or self.can_to_ecu.loss_pct <= self.loss_threshold
        ecu_ok = self.ecu_to_can is None or self.ecu_to_can.loss_pct <= self.loss_threshold
        return can_ok and ecu_ok


def _estimate_frame_bits(payload_len: int, extended: bool) -> int:
    base = 47 + payload_len * 8
    if extended:
        base += 18
    return int(base * 1.2)


def busload_to_pps(bitrate: int, busload_pct: float, payload_len: int, extended: bool) -> float:
    frame_bits = _estimate_frame_bits(payload_len, extended)
    return (bitrate * (busload_pct / 100.0)) / frame_bits


def pps_to_busload(bitrate: int, pps: float, payload_len: int, extended: bool) -> float:
    frame_bits = _estimate_frame_bits(payload_len, extended)
    return (pps * frame_bits / bitrate) * 100.0


def _sleep_until(deadline: float) -> None:
    while True:
        now = time.perf_counter()
        remaining = deadline - now
        if remaining <= 0:
            return
        if remaining > 0.001:
            time.sleep(remaining - 0.0005)
        else:
            time.sleep(0)


def _build_payload(payload_len: int, direction: int, seq: int, run_id: int) -> bytes:
    payload = bytearray(payload_len)
    if payload_len >= 1:
        payload[0] = direction
    if payload_len >= 2:
        payload[1] = (seq >> 8) & 0xFF
    if payload_len >= 3:
        payload[2] = seq & 0xFF
    if payload_len >= 4:
        payload[3] = run_id
    for i in range(4, payload_len):
        payload[i] = (i + seq) & 0xFF
    return bytes(payload)


def _parse_seq(payload: bytes, run_id: int, direction: int) -> Optional[int]:
    if len(payload) < 3:
        return None
    if payload[0] != direction:
        return None
    if len(payload) >= 4 and payload[3] != run_id:
        return None
    return (payload[1] << 8) | payload[2]


def _extract_received_pdu(pdu: canyonero.PDU) -> Optional[tuple[int, bytes]]:
    payload = pdu.payload()
    if len(payload) < 6:
        return None
    can_id = int.from_bytes(payload[1:5], "big")
    if pdu.type == canyonero.PDUType.received:
        data = payload[6:]
    elif pdu.type == canyonero.PDUType.received_compressed:
        data = pdu.uncompressed_data()
    else:
        return None
    return can_id, data


def _run_direction(
    name: str,
    send_fn: Callable[[bytes], None],
    receive_iter: Callable[[float], Optional[bytes]],
    payload_len: int,
    pps: float,
    duration: float,
    settle_time: float,
    direction_marker: int,
    run_id: int,
) -> DirectionMetrics:
    interval = 1.0 / pps if pps > 0 else 0.0
    expected = int(pps * duration)
    received: set[int] = set()
    duplicates = 0
    stop = threading.Event()

    def receiver_loop() -> None:
        nonlocal duplicates
        while not stop.is_set():
            payload = receive_iter(0.1)
            if payload is None:
                continue
            seq = _parse_seq(payload, run_id, direction_marker)
            if seq is None:
                continue
            if seq in received:
                duplicates += 1
            else:
                received.add(seq)
        # Drain any remaining items from the queue after stop is signaled
        drain_deadline = time.perf_counter() + 0.5
        empty_count = 0
        while time.perf_counter() < drain_deadline:
            payload = receive_iter(0.05)
            if payload is None:
                empty_count += 1
                # Only exit after several consecutive empty results
                if empty_count >= 3:
                    break
                continue
            empty_count = 0
            seq = _parse_seq(payload, run_id, direction_marker)
            if seq is None:
                continue
            if seq in received:
                duplicates += 1
            else:
                received.add(seq)

    receiver = threading.Thread(target=receiver_loop, name=f"{name}-rx", daemon=True)
    receiver.start()

    start = time.perf_counter()
    for seq in range(expected):
        payload = _build_payload(payload_len, direction_marker, seq, run_id)
        send_fn(payload)
        if interval > 0:
            _sleep_until(start + (seq + 1) * interval)
    # Dynamic settle time: at least 1 second, plus extra time for high PPS
    effective_settle = max(settle_time, 1.0 + expected / max(pps, 1) * 0.05)
    _sleep_until(time.perf_counter() + effective_settle)
    stop.set()
    receiver.join(timeout=2.0)

    elapsed = max(time.perf_counter() - start, 0.001)
    received_count = len(received)
    loss_pct = 100.0 * max(expected - received_count, 0) / max(expected, 1)
    return DirectionMetrics(
        name=name,
        sent=expected,
        received=received_count,
        duplicates=duplicates,
        duration=elapsed,
        pps=pps,
        loss_pct=loss_pct,
    )


def run_ecuconnect_test(
    endpoint: str,
    can_interface: str,
    bitrate: int,
    payload_len: int,
    busloads: Iterable[float],
    pps_overrides: Optional[list[float]],
    traffic_mode: str,
    open_timeout: float,
    open_retries: int,
    open_retry_delay: float,
    duration: float,
    settle_time: float,
    tx_id: int,
    rx_id: int,
    rx_mask: int,
    tx_extended: bool,
    rx_extended: bool,
    rx_buffer: int,
    tx_buffer: int,
    loss_threshold: float,
) -> list[TestSummary]:
    summaries: list[TestSummary] = []
    run_id = secrets.randbelow(256)

    ecu = EcuconnectClient(endpoint=endpoint, rx_buffer=rx_buffer, tx_buffer=tx_buffer)
    ecu.connect()
    channel: Optional[int] = None
    bus: Optional[SocketCanBus] = None
    try:
        channel = open_channel_with_retry(
            ecu,
            bitrate=bitrate,
            protocol=canyonero.ChannelProtocol.raw,
            open_timeout=open_timeout,
            open_retries=open_retries,
            open_retry_delay=open_retry_delay,
        )

        arbitration = canyonero.Arbitration(
            request=tx_id,
            reply_pattern=rx_id,
            reply_mask=rx_mask,
            request_extension=0,
            reply_extension=0,
        )
        ecu.set_arbitration(channel, arbitration)

        do_rx = traffic_mode in {"rx", "all"}
        do_tx = traffic_mode in {"tx", "all"}

        if traffic_mode != "none":
            filters = []
            if rx_extended:
                filters.append((rx_id | 0x80000000, rx_mask | 0x80000000))
            else:
                filters.append((rx_id, rx_mask))
            if tx_extended:
                filters.append((tx_id | 0x80000000, 0x1FFFFFFF | 0x80000000))
            else:
                filters.append((tx_id, 0x7FF))

            bus = SocketCanBus(
                interface=can_interface,
                rx_buffer=rx_buffer,
                tx_buffer=tx_buffer,
                filters=filters,
            )
            bus.start_reader()

        busload_list = list(busloads)
        if pps_overrides is not None and len(pps_overrides) != len(busload_list):
            raise ValueError("pps_overrides length must match busloads length")

        for idx, busload in enumerate(busload_list):
            if pps_overrides is not None:
                pps_can_to_ecu = pps_overrides[idx]
                pps_ecu_to_can = pps_overrides[idx]
            else:
                pps_can_to_ecu = busload_to_pps(bitrate, busload, payload_len, rx_extended)
                pps_ecu_to_can = busload_to_pps(bitrate, busload, payload_len, tx_extended)
            if (do_rx and pps_can_to_ecu <= 0) or (do_tx and pps_ecu_to_can <= 0):
                raise ValueError("Calculated PPS is zero; lower payload length or increase busload")

            can_to_ecu = None
            ecu_to_can = None
            if do_rx and bus is not None:
                def ecu_receive(timeout: float) -> Optional[bytes]:
                    pdu = ecu.get_pdu(timeout=timeout)
                    if pdu is None:
                        return None
                    extracted = _extract_received_pdu(pdu)
                    if not extracted:
                        return None
                    can_id, data = extracted
                    if can_id != rx_id:
                        return None
                    return data

                can_to_ecu = _run_direction(
                    name=f"can-to-ecu@{busload}",
                    send_fn=lambda payload: bus.send_frame(rx_id, payload, extended=rx_extended),
                    receive_iter=ecu_receive,
                    payload_len=payload_len,
                    pps=pps_can_to_ecu,
                    duration=duration,
                    settle_time=settle_time,
                    direction_marker=0xA1,
                    run_id=run_id,
                )

            if do_tx and bus is not None:
                def can_receive(timeout: float) -> Optional[bytes]:
                    frame = bus.get_frame(timeout=timeout)
                    if frame is None:
                        return None
                    if frame.can_id != tx_id:
                        return None
                    return frame.data

                ecu_to_can = _run_direction(
                    name=f"ecu-to-can@{busload}",
                    send_fn=lambda payload: ecu.send(channel, payload),
                    receive_iter=can_receive,
                    payload_len=payload_len,
                    pps=pps_ecu_to_can,
                    duration=duration,
                    settle_time=settle_time,
                    direction_marker=0xB2,
                    run_id=run_id,
                )

            summaries.append(
                TestSummary(
                    can_to_ecu=can_to_ecu,
                    ecu_to_can=ecu_to_can,
                    loss_threshold=loss_threshold,
                )
            )
    finally:
        if bus is not None:
            bus.close()
        if channel is not None:
            try:
                ecu.close_channel(channel, timeout=open_timeout)
            except (TimeoutError, RuntimeError):
                pass
        ecu.close()
    return summaries


def open_channel_with_retry(
    ecu: EcuconnectClient,
    bitrate: int,
    protocol: canyonero.ChannelProtocol,
    open_timeout: float,
    open_retries: int,
    open_retry_delay: float,
) -> int:
    channel: Optional[int] = None
    for _ in range(max(open_retries, 0) + 1):
        try:
            channel = ecu.open_channel(
                protocol,
                bitrate=bitrate,
                timeout=open_timeout,
            )
            break
        except TimeoutError:
            ecu.close()
            time.sleep(open_retry_delay)
            ecu.connect()
    if channel is None:
        raise TimeoutError("Timed out waiting for ECUconnect channel open")
    return channel
