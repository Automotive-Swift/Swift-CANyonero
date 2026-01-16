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
    """Run a directional throughput test at the specified PPS rate."""
    interval = 1.0 / pps if pps > 0 else 0.0
    expected = int(pps * duration)
    received: set[int] = set()
    duplicates = 0
    stop = threading.Event()
    received_lock = threading.Lock()

    def receiver_loop() -> None:
        nonlocal duplicates
        while not stop.is_set():
            payload = receive_iter(0.1)
            if payload is None:
                continue
            seq = _parse_seq(payload, run_id, direction_marker)
            if seq is None:
                continue
            with received_lock:
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
            with received_lock:
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
    with received_lock:
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
    traffic_mode: str,
    open_timeout: float,
    open_retries: int,
    open_retry_delay: float,
    duration: float,
    settle_time: float,
    rx_buffer: int,
    tx_buffer: int,
    loss_threshold: float,
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

        busload_list = list(busloads)

        for busload in busload_list:
            pps_can_to_ecu = busload_to_pps(bitrate, busload, payload_len, extended)
            pps_ecu_to_can = busload_to_pps(bitrate, busload, payload_len, extended)
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


# -----------------------------------------------------------------------------
# ISOTP Test Functions - realistic request/response testing
# -----------------------------------------------------------------------------

@dataclass
class ISOTPTransactionResult:
    """Result of a single ISOTP request/response transaction."""
    payload_size: int
    success: bool
    latency_ms: float
    error: Optional[str] = None


@dataclass
class ISOTPTestMetrics:
    """Aggregated metrics for ISOTP testing."""
    payload_size: int
    transactions: int
    successful: int
    failed: int
    min_latency_ms: float
    avg_latency_ms: float
    max_latency_ms: float
    throughput_bytes_per_sec: float

    @property
    def success_rate(self) -> float:
        return 100.0 * self.successful / max(self.transactions, 1)


@dataclass
class ISOTPTestSummary:
    """Summary of ISOTP test run."""
    metrics: list[ISOTPTestMetrics]
    total_transactions: int
    total_successful: int
    total_failed: int

    @property
    def passed(self) -> bool:
        return self.total_failed == 0


def _isotp_echo_responder(
    bus: SocketCanBus,
    request_id: int,
    response_id: int,
    stop: threading.Event,
) -> None:
    """
    SocketCAN-side ISOTP echo responder.

    Listens for ISOTP frames on request_id and echoes the payload back on response_id.
    This simulates an ECU responding to diagnostic requests.

    For simplicity, this handles single-frame ISOTP only (payloads up to 7 bytes).
    Multi-frame would require full ISOTP state machine.
    """
    while not stop.is_set():
        frame = bus.get_frame(timeout=0.1)
        if frame is None:
            continue
        if frame.can_id != request_id:
            continue

        data = frame.data
        if len(data) < 1:
            continue

        pci = data[0]
        frame_type = (pci >> 4) & 0x0F

        if frame_type == 0:  # Single Frame
            sf_dl = pci & 0x0F
            if sf_dl > 0 and len(data) > sf_dl:
                payload = data[1:1+sf_dl]
                # Echo back as single frame
                response = bytes([sf_dl]) + payload
                response = response.ljust(8, b'\xAA')
                bus.send_frame(response_id, response)
        elif frame_type == 1:  # First Frame - send Flow Control
            # Send FC with CTS (Continue To Send)
            fc_frame = bytes([0x30, 0x00, 0x00, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA])
            bus.send_frame(response_id, fc_frame)
        # Consecutive frames (type 2) are handled by ISOTP layer on ECUconnect


def run_isotp_transaction(
    ecu: EcuconnectClient,
    channel: int,
    payload: bytes,
    timeout: float,
) -> ISOTPTransactionResult:
    """
    Run a single ISOTP request/response transaction.

    Sends payload via ECUconnect ISOTP channel and waits for response.
    """
    start = time.perf_counter()

    try:
        # Send the ISOTP payload
        ecu.send(channel, payload)

        # Wait for response (received PDU)
        deadline = time.perf_counter() + timeout
        while time.perf_counter() < deadline:
            pdu = ecu.get_pdu(timeout=min(0.1, deadline - time.perf_counter()))
            if pdu is None:
                continue
            if pdu.type == canyonero.PDUType.received:
                # Got response
                latency = (time.perf_counter() - start) * 1000
                return ISOTPTransactionResult(
                    payload_size=len(payload),
                    success=True,
                    latency_ms=latency,
                )
            elif pdu.type in (canyonero.PDUType.error_no_response,
                              canyonero.PDUType.error_hardware,
                              canyonero.PDUType.error_unspecified):
                latency = (time.perf_counter() - start) * 1000
                return ISOTPTransactionResult(
                    payload_size=len(payload),
                    success=False,
                    latency_ms=latency,
                    error=f"ECU error: {pdu.type}",
                )

        # Timeout
        latency = (time.perf_counter() - start) * 1000
        return ISOTPTransactionResult(
            payload_size=len(payload),
            success=False,
            latency_ms=latency,
            error="Timeout waiting for response",
        )
    except Exception as e:
        latency = (time.perf_counter() - start) * 1000
        return ISOTPTransactionResult(
            payload_size=len(payload),
            success=False,
            latency_ms=latency,
            error=str(e),
        )


def run_isotp_test(
    endpoint: str,
    can_interface: str,
    bitrate: int,
    payload_sizes: Iterable[int],
    transactions_per_size: int,
    tx_id: int,
    rx_id: int,
    open_timeout: float,
    open_retries: int,
    open_retry_delay: float,
    transaction_timeout: float,
    rx_buffer: int,
    tx_buffer: int,
) -> ISOTPTestSummary:
    """
    Run ISOTP request/response test with various payload sizes.

    This tests realistic diagnostic communication patterns:
    - Small payloads (1-7 bytes): Single-frame UDS requests
    - Medium payloads (8-100 bytes): Multi-frame requests
    - Large payloads (100-4095 bytes): Large transfers (e.g., flash data)
    """
    all_metrics: list[ISOTPTestMetrics] = []
    total_transactions = 0
    total_successful = 0
    total_failed = 0

    ecu = EcuconnectClient(endpoint=endpoint, rx_buffer=rx_buffer, tx_buffer=tx_buffer)
    ecu.connect()

    bus: Optional[SocketCanBus] = None
    channel: Optional[int] = None
    stop_responder = threading.Event()
    responder_thread: Optional[threading.Thread] = None

    try:
        # Open ISOTP channel
        channel = open_channel_with_retry(
            ecu,
            bitrate=bitrate,
            protocol=canyonero.ChannelProtocol.isotp,
            open_timeout=open_timeout,
            open_retries=open_retries,
            open_retry_delay=open_retry_delay,
        )

        # Set arbitration
        arbitration = canyonero.Arbitration(
            request=tx_id,
            reply_pattern=rx_id,
            reply_mask=0x7FF,
            request_extension=0,
            reply_extension=0,
        )
        ecu.set_arbitration(channel, arbitration)

        # Setup SocketCAN with echo responder
        bus = SocketCanBus(
            interface=can_interface,
            filters=[(tx_id, 0x7FF)],
        )
        bus.start_reader()

        # Start echo responder thread
        responder_thread = threading.Thread(
            target=_isotp_echo_responder,
            args=(bus, tx_id, rx_id, stop_responder),
            name="isotp-responder",
            daemon=True,
        )
        responder_thread.start()

        # Run tests for each payload size
        for payload_size in payload_sizes:
            results: list[ISOTPTransactionResult] = []

            for i in range(transactions_per_size):
                # Create test payload with sequence number
                payload = bytes([(i >> 8) & 0xFF, i & 0xFF]) + bytes(
                    [(j + i) & 0xFF for j in range(payload_size - 2)]
                ) if payload_size > 2 else bytes([i & 0xFF] * payload_size)

                result = run_isotp_transaction(
                    ecu, channel, payload, transaction_timeout
                )
                results.append(result)

                total_transactions += 1
                if result.success:
                    total_successful += 1
                else:
                    total_failed += 1

            # Calculate metrics for this payload size
            successful_results = [r for r in results if r.success]
            latencies = [r.latency_ms for r in successful_results]

            if latencies:
                total_bytes = sum(r.payload_size for r in successful_results)
                total_time_sec = sum(r.latency_ms for r in successful_results) / 1000.0
                throughput = total_bytes / max(total_time_sec, 0.001)

                metrics = ISOTPTestMetrics(
                    payload_size=payload_size,
                    transactions=len(results),
                    successful=len(successful_results),
                    failed=len(results) - len(successful_results),
                    min_latency_ms=min(latencies),
                    avg_latency_ms=sum(latencies) / len(latencies),
                    max_latency_ms=max(latencies),
                    throughput_bytes_per_sec=throughput,
                )
            else:
                metrics = ISOTPTestMetrics(
                    payload_size=payload_size,
                    transactions=len(results),
                    successful=0,
                    failed=len(results),
                    min_latency_ms=0,
                    avg_latency_ms=0,
                    max_latency_ms=0,
                    throughput_bytes_per_sec=0,
                )

            all_metrics.append(metrics)

    finally:
        stop_responder.set()
        if responder_thread:
            responder_thread.join(timeout=1.0)
        if bus is not None:
            bus.close()
        if channel is not None:
            try:
                ecu.close_channel(channel, timeout=open_timeout)
            except (TimeoutError, RuntimeError):
                pass
        ecu.close()

    return ISOTPTestSummary(
        metrics=all_metrics,
        total_transactions=total_transactions,
        total_successful=total_successful,
        total_failed=total_failed,
    )
