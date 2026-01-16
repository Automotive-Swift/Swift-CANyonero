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
    target_pps: float
    send_pps: float  # Rate we sent commands
    recv_pps: float  # Rate frames were received (actual bus rate for verification)
    target_busload: float
    send_busload: float  # Busload based on send rate
    recv_busload: float  # Busload based on receive rate (actual bus measurement)
    loss_pct: float


@dataclass
class TestSummary:
    busload: float
    can_to_ecu: Optional[DirectionMetrics]
    ecu_to_can: Optional[DirectionMetrics]
    request_response: Optional["ISOTPMetrics"]
    filter_test: Optional["FilterTestResult"]
    loss_threshold: float

    @property
    def passed(self) -> bool:
        can_ok = self.can_to_ecu is None or self.can_to_ecu.loss_pct <= self.loss_threshold
        ecu_ok = self.ecu_to_can is None or self.ecu_to_can.loss_pct <= self.loss_threshold
        rr_ok = self.request_response is None or self.request_response.failed == 0
        filter_ok = self.filter_test is None or self.filter_test.passed
        return can_ok and ecu_ok and rr_ok and filter_ok


def _estimate_frame_bits(payload_len: int, extended: bool) -> int:
    # CAN 2.0 frame: SOF(1) + ID(11/29) + RTR(1) + IDE(1) + r0(1) + DLC(4) +
    #               Data(8*n) + CRC(15) + CRCdel(1) + ACK(1) + ACKdel(1) + EOF(7) + IFS(3)
    # Standard: 47 + 8*payload_len, Extended: +18 for 29-bit ID
    # Note: Using nominal bits without stuffing estimate. Actual busload depends
    # on data patterns. Use canbusload -e for exact measurement.
    base = 47 + payload_len * 8
    if extended:
        base += 18
    return base


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
    bitrate: int,
    extended: bool,
    target_busload: float,
) -> DirectionMetrics:
    """Run a directional throughput test at the specified PPS rate."""
    interval = 1.0 / pps if pps > 0 else 0.0
    expected = int(pps * duration)
    received: set[int] = set()
    duplicates = 0
    stop = threading.Event()
    received_lock = threading.Lock()
    first_recv_time: Optional[float] = None
    last_recv_time: Optional[float] = None

    def receiver_loop() -> None:
        nonlocal duplicates, first_recv_time, last_recv_time
        while not stop.is_set():
            payload = receive_iter(0.1)
            if payload is None:
                continue
            seq = _parse_seq(payload, run_id, direction_marker)
            if seq is None:
                continue
            now = time.perf_counter()
            with received_lock:
                if seq in received:
                    duplicates += 1
                else:
                    received.add(seq)
                    if first_recv_time is None:
                        first_recv_time = now
                    last_recv_time = now
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
            now = time.perf_counter()
            with received_lock:
                if seq in received:
                    duplicates += 1
                else:
                    received.add(seq)
                    if first_recv_time is None:
                        first_recv_time = now
                    last_recv_time = now

    receiver = threading.Thread(target=receiver_loop, name=f"{name}-rx", daemon=True)
    receiver.start()

    start = time.perf_counter()
    for seq in range(expected):
        payload = _build_payload(payload_len, direction_marker, seq, run_id)
        send_fn(payload)
        if interval > 0:
            _sleep_until(start + (seq + 1) * interval)

    send_elapsed = max(time.perf_counter() - start, 0.001)

    # Dynamic settle time: at least 1 second, plus extra time for high PPS
    effective_settle = max(settle_time, 1.0 + expected / max(pps, 1) * 0.05)
    _sleep_until(time.perf_counter() + effective_settle)
    stop.set()
    receiver.join(timeout=2.0)

    elapsed = max(time.perf_counter() - start, 0.001)
    send_pps = expected / send_elapsed if send_elapsed > 0 else 0
    send_busload = pps_to_busload(bitrate, send_pps, payload_len, extended)
    with received_lock:
        received_count = len(received)
        recv_first = first_recv_time
        recv_last = last_recv_time
    # Calculate actual receive rate (true bus measurement)
    if recv_first is not None and recv_last is not None and received_count > 1:
        recv_duration = recv_last - recv_first
        recv_pps = (received_count - 1) / recv_duration if recv_duration > 0 else 0
    else:
        recv_pps = 0
    recv_busload = pps_to_busload(bitrate, recv_pps, payload_len, extended)
    loss_pct = 100.0 * max(expected - received_count, 0) / max(expected, 1)
    return DirectionMetrics(
        name=name,
        sent=expected,
        received=received_count,
        duplicates=duplicates,
        duration=elapsed,
        target_pps=pps,
        send_pps=send_pps,
        recv_pps=recv_pps,
        target_busload=target_busload,
        send_busload=send_busload,
        recv_busload=recv_busload,
        loss_pct=loss_pct,
    )


def run_ecuconnect_test(
    endpoint: str,
    can_interface: str,
    bitrate: int,
    payload_len: int,
    busloads: Iterable[float],
    traffic_mode: str,
    open_timeout: float,
    open_retries: int,
    open_retry_delay: float,
    duration: float,
    settle_time: float,
    rx_buffer: int,
    tx_buffer: int,
    loss_threshold: float,
    ecu_tx_boost: float = 1.2,  # Compensation for ECU->CAN TCP overhead
    rr_count: int = 100,  # Request/response transaction count
    rr_timeout: float = 1.0,  # Request/response per-transaction timeout
) -> list[TestSummary]:
    summaries: list[TestSummary] = []
    run_id = secrets.randbelow(256)

    # Randomly choose between standard and extended CAN IDs
    extended = secrets.randbelow(2) == 1
    if extended:
        tx_id = 0x18DA00F1  # ISO-TP style extended ID
        rx_id = 0x18DAF100
        rx_mask = 0x1FFFFFFF
    else:
        tx_id = 0x123
        rx_id = 0x321
        rx_mask = 0x7FF

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
            if extended:
                filters.append((rx_id | 0x80000000, rx_mask | 0x80000000))
            else:
                filters.append((rx_id, rx_mask))
            if extended:
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

        # Run filter test once at the beginning
        filter_result: Optional[FilterTestResult] = None
        if bus is not None:
            # Temporarily remove filters to receive all test IDs
            bus.close()
            bus = SocketCanBus(
                interface=can_interface,
                rx_buffer=rx_buffer,
                tx_buffer=tx_buffer,
                filters=[],  # No filters for filter test
            )
            bus.start_reader()
            filter_result = run_filter_test(ecu, bus, channel, extended, timeout=0.5)
            # Restore original filters
            bus.close()
            bus = SocketCanBus(
                interface=can_interface,
                rx_buffer=rx_buffer,
                tx_buffer=tx_buffer,
                filters=filters,
            )
            bus.start_reader()
            # Restore original arbitration after filter test
            arbitration = canyonero.Arbitration(
                request=tx_id,
                reply_pattern=rx_id,
                reply_mask=rx_mask,
                request_extension=0,
                reply_extension=0,
            )
            ecu.set_arbitration(channel, arbitration)

        busload_list = list(busloads)
        first_summary = True

        for busload in busload_list:
            pps_can_to_ecu = busload_to_pps(bitrate, busload, payload_len, extended)
            # Apply boost factor to ECU->CAN to compensate for TCP command overhead
            pps_ecu_to_can = busload_to_pps(bitrate, busload, payload_len, extended) * ecu_tx_boost
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
                    send_fn=lambda payload: bus.send_frame(rx_id, payload, extended=extended),
                    receive_iter=ecu_receive,
                    payload_len=payload_len,
                    pps=pps_can_to_ecu,
                    duration=duration,
                    settle_time=settle_time,
                    direction_marker=0xA1,
                    run_id=run_id,
                    bitrate=bitrate,
                    extended=extended,
                    target_busload=busload,
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
                    bitrate=bitrate,
                    extended=extended,
                    target_busload=busload,
                )

            # Run request/response test after throughput tests
            rr_metrics: Optional[ISOTPMetrics] = None
            if bus is not None and rr_count > 0:
                rr_metrics = run_request_response_test(
                    ecu=ecu,
                    bus=bus,
                    channel=channel,
                    tx_id=tx_id,
                    rx_id=rx_id,
                    extended=extended,
                    count=rr_count,
                    timeout=rr_timeout,
                )

            summaries.append(
                TestSummary(
                    busload=busload,
                    can_to_ecu=can_to_ecu,
                    ecu_to_can=ecu_to_can,
                    request_response=rr_metrics,
                    filter_test=filter_result if first_summary else None,
                    loss_threshold=loss_threshold,
                )
            )
            first_summary = False
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


# -----------------------------------------------------------------------------
# ISOTP Request/Response Test - realistic diagnostic patterns
# -----------------------------------------------------------------------------

@dataclass
class ISOTPMetrics:
    """Metrics for ISOTP request/response testing."""
    transactions: int
    successful: int
    failed: int
    min_latency_ms: float
    avg_latency_ms: float
    max_latency_ms: float

    @property
    def success_rate(self) -> float:
        return 100.0 * self.successful / max(self.transactions, 1)


def _raw_echo_responder(
    bus: SocketCanBus,
    request_id: int,
    response_id: int,
    extended: bool,
    stop: threading.Event,
) -> None:
    """Echo responder for raw CAN frames - echoes payload back on response_id."""
    while not stop.is_set():
        frame = bus.get_frame(timeout=0.1)
        if frame is None:
            continue
        if frame.can_id != request_id:
            continue
        bus.send_frame(response_id, frame.data, extended=extended)


@dataclass
class FilterTestResult:
    """Result of arbitration filter/mask testing."""
    passed: bool
    accepted_matching: int
    rejected_nonmatching: int
    errors: list[str]


def run_filter_test(
    ecu: EcuconnectClient,
    bus: SocketCanBus,
    channel: int,
    extended: bool,
    timeout: float,
) -> FilterTestResult:
    """
    Test that arbitration masks work correctly.

    Sets various patterns/masks and verifies:
    - Frames matching the pattern ARE received
    - Frames NOT matching the pattern are rejected
    """
    errors: list[str] = []
    accepted = 0
    rejected = 0

    # Test cases: (pattern, mask, should_match_ids, should_reject_ids)
    if extended:
        test_cases = [
            # Exact match
            (0x18DA00F1, 0x1FFFFFFF, [0x18DA00F1], [0x18DA00F2, 0x18DB00F1]),
            # Wildcard last nibble: accept 0x18DA00F0-0x18DA00FF
            (0x18DA00F0, 0x1FFFFFF0, [0x18DA00F0, 0x18DA00F5, 0x18DA00FF], [0x18DA0100, 0x18DA00EF]),
        ]
    else:
        test_cases = [
            # Exact match
            (0x7E8, 0x7FF, [0x7E8], [0x7E9, 0x7E0]),
            # Accept 0x7E0-0x7EF (mask off low nibble)
            (0x7E0, 0x7F0, [0x7E0, 0x7E8, 0x7EF], [0x7F0, 0x7D0]),
            # Accept 0x600-0x6FF (mask off low byte)
            (0x600, 0x700, [0x600, 0x650, 0x6FF], [0x700, 0x5FF]),
        ]

    for pattern, mask, should_match, should_reject in test_cases:
        # Set the arbitration with this pattern/mask
        arbitration = canyonero.Arbitration(
            request=0x123 if not extended else 0x18DA00F1,  # TX doesn't matter for this test
            reply_pattern=pattern,
            reply_mask=mask,
            request_extension=0,
            reply_extension=0,
        )
        ecu.set_arbitration(channel, arbitration)

        # Drain any pending PDUs
        while ecu.get_pdu(timeout=0.05) is not None:
            pass

        # Send frames that SHOULD match
        for can_id in should_match:
            payload = bytes([0x02, 0x3E, 0x00, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA])
            bus.send_frame(can_id, payload, extended=extended)
            time.sleep(0.01)

            # Check if ECUconnect received it
            pdu = ecu.get_pdu(timeout=timeout)
            if pdu is not None and pdu.type in (canyonero.PDUType.received, canyonero.PDUType.received_compressed):
                accepted += 1
            else:
                errors.append(f"Filter {pattern:#x}/{mask:#x}: ID {can_id:#x} should match but wasn't received")

        # Send frames that should NOT match
        for can_id in should_reject:
            payload = bytes([0x02, 0x3E, 0x00, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA])
            bus.send_frame(can_id, payload, extended=extended)
            time.sleep(0.01)

            # Should NOT receive this
            pdu = ecu.get_pdu(timeout=0.1)  # Short timeout - we expect nothing
            if pdu is None:
                rejected += 1
            elif pdu.type in (canyonero.PDUType.received, canyonero.PDUType.received_compressed):
                errors.append(f"Filter {pattern:#x}/{mask:#x}: ID {can_id:#x} should NOT match but was received")
            else:
                rejected += 1  # Got some other PDU type, that's fine

    return FilterTestResult(
        passed=len(errors) == 0,
        accepted_matching=accepted,
        rejected_nonmatching=rejected,
        errors=errors,
    )


def run_request_response_test(
    ecu: EcuconnectClient,
    bus: SocketCanBus,
    channel: int,
    tx_id: int,
    rx_id: int,
    extended: bool,
    count: int,
    timeout: float,
) -> ISOTPMetrics:
    """
    Run request/response test pattern.

    ECUconnect sends a frame, SocketCAN echo responder sends it back.
    This tests realistic diagnostic communication latency.
    """
    stop = threading.Event()
    responder = threading.Thread(
        target=_raw_echo_responder,
        args=(bus, tx_id, rx_id, extended, stop),
        daemon=True,
    )
    responder.start()

    latencies: list[float] = []
    failed = 0

    try:
        for i in range(count):
            payload = bytes([0x02, 0x3E, (i & 0xFF), 0xAA, 0xAA, 0xAA, 0xAA, 0xAA])
            start = time.perf_counter()
            ecu.send(channel, payload)

            # Wait for echo response
            deadline = start + timeout
            got_response = False
            while time.perf_counter() < deadline:
                pdu = ecu.get_pdu(timeout=min(0.05, deadline - time.perf_counter()))
                if pdu is None:
                    continue
                if pdu.type in (canyonero.PDUType.received, canyonero.PDUType.received_compressed):
                    latencies.append((time.perf_counter() - start) * 1000)
                    got_response = True
                    break

            if not got_response:
                failed += 1
    finally:
        stop.set()
        responder.join(timeout=1.0)

    successful = len(latencies)
    return ISOTPMetrics(
        transactions=count,
        successful=successful,
        failed=failed,
        min_latency_ms=min(latencies) if latencies else 0,
        avg_latency_ms=sum(latencies) / len(latencies) if latencies else 0,
        max_latency_ms=max(latencies) if latencies else 0,
    )
