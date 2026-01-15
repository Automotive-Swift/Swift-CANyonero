from __future__ import annotations

import statistics
import socket as py_socket
import sys
from dataclasses import dataclass
from urllib.parse import urlparse
import time
from typing import List, Optional

import typer
from rich.console import Console
from rich.table import Table

from . import canyonero
from .ecuconnect import DEFAULT_ENDPOINT, EcuconnectClient
from .test_runner import open_channel_with_retry, pps_to_busload, run_ecuconnect_test

app = typer.Typer(help="ECUconnect tool (Python) for CANyonero adapters", add_completion=False)
console = Console()


def _parse_int(value: str) -> int:
    try:
        return int(value, 0)
    except ValueError as exc:
        raise typer.BadParameter(f"Invalid integer value: {value}") from exc


def _parse_hex_bytes(data: str) -> bytes:
    cleaned = data.replace(" ", "").replace(":", "")
    if len(cleaned) % 2 != 0:
        cleaned = "0" + cleaned
    try:
        return bytes.fromhex(cleaned)
    except ValueError as exc:
        raise typer.BadParameter("Invalid hex payload") from exc


@dataclass
class Addressing:
    mode: str
    request_id: int
    request_ext: int
    reply_pattern: int
    reply_mask: int
    reply_ext: int


@dataclass
class BenchmarkResult:
    payload_size: int
    average: float
    minimum: float
    maximum: float
    bandwidth_bytes_per_second: float


def _parse_hex(text: str) -> int | None:
    cleaned = text.strip()
    if cleaned.lower().startswith("0x"):
        cleaned = cleaned[2:]
    if not cleaned:
        return None
    return int(cleaned, 16)


def _parse_address_component(component: str) -> tuple[int, int] | None:
    trimmed = component.strip()
    if not trimmed:
        return None
    parts = trimmed.split("/")
    if len(parts) > 2:
        return None
    header = _parse_hex(parts[0])
    if header is None:
        return None
    ext = 0
    if len(parts) == 2:
        ext_val = _parse_hex(parts[1])
        if ext_val is None:
            return None
        ext = ext_val
    return header, ext


def _parse_addressing(text: str) -> Addressing | None:
    parts = text.split(",")
    if len(parts) != 2:
        return None
    send = _parse_address_component(parts[0])
    if send is None:
        return None

    reply_raw = parts[1].strip()
    reply_parts = reply_raw.split("/")
    reply_id_str = reply_parts[0]
    reply_ext = 0
    if len(reply_parts) == 2:
        ext_val = _parse_hex(reply_parts[1])
        if ext_val is None:
            return None
        reply_ext = ext_val

    if "x" in reply_id_str.lower():
        id_text = reply_id_str
        if id_text.lower().startswith("0x"):
            id_text = id_text[2:]
        if len(id_text) % 2 == 1:
            id_text = "0" + id_text
        pattern_str = id_text.replace("x", "0").replace("X", "0")
        mask_str = "".join("0" if ch in {"x", "X"} else "F" for ch in id_text)
        pattern = _parse_hex(pattern_str)
        mask = _parse_hex(mask_str)
        if pattern is None or mask is None:
            return None
        if pattern <= 0x7FF:
            mask &= 0x7FF
        return Addressing(
            mode="multicast",
            request_id=send[0],
            request_ext=send[1],
            reply_pattern=pattern,
            reply_mask=mask,
            reply_ext=reply_ext,
        )

    reply = _parse_address_component(reply_raw)
    if reply is None:
        return None
    reply_id = reply[0]
    reply_mask = 0x1FFFFFFF if reply_id > 0x7FF else 0x7FF
    return Addressing(
        mode="unicast",
        request_id=send[0],
        request_ext=send[1],
        reply_pattern=reply_id,
        reply_mask=reply_mask,
        reply_ext=reply[1],
    )


def _format_addressing(addressing: Addressing) -> str:
    def fmt_id(value: int, ext: int) -> str:
        base = f"{value:X}" if value > 0x7FF else f"{value:03X}"
        if ext:
            return f"{base}/{ext:02X}"
        return base

    if addressing.mode == "multicast":
        return (
            f"multicast {fmt_id(addressing.request_id, addressing.request_ext)} -> "
            f"{fmt_id(addressing.reply_pattern, addressing.reply_ext)} "
            f"mask=0x{addressing.reply_mask:X}"
        )
    return (
        f"unicast {fmt_id(addressing.request_id, addressing.request_ext)} -> "
        f"{fmt_id(addressing.reply_pattern, addressing.reply_ext)}"
    )


def _decode_received_pdu(pdu: canyonero.PDU) -> tuple[int, int, bytes] | None:
    payload = pdu.payload()
    if len(payload) < 6:
        return None
    can_id = int.from_bytes(payload[1:5], "big")
    ext = payload[5]
    if pdu.type == canyonero.PDUType.received:
        data = pdu.data()
    elif pdu.type == canyonero.PDUType.received_compressed:
        data = pdu.uncompressed_data()
    else:
        return None
    return can_id, ext, data


def _format_message(can_id: int, data: bytes) -> str:
    width = 3 if can_id <= 0x7FF else 8
    header = f"{can_id:0{width}X}"
    data_hex = " ".join(f"{value:02X}" for value in data)
    ascii_text = "".join(chr(value) if 0x20 <= value < 0x7F else "." for value in data)
    return f"{header}   {data_hex}\n ->  '{ascii_text}'"


def _interpret_response(data: bytes) -> str | None:
    if not data:
        return None
    sid = data[0]
    if sid == 0x7F:
        if len(data) < 3:
            return "Negative Response (incomplete)"
        requested_service = data[1]
        nrc = data[2]
        service_name = _service_id_name(requested_service)
        nrc_desc = _negative_response_description(nrc)
        return f"Negative Response to {service_name}: {nrc_desc}"
    if 0x41 <= sid <= 0x4A:
        service = sid - 0x40
        service_name = _obd2_service_name(service)
        if service in (0x01, 0x02) and len(data) >= 2:
            pid_name = _obd2_pid_name(data[1])
            return f"{service_name} - {pid_name}"
        if service == 0x09 and len(data) >= 2:
            info_name = _obd2_mode09_name(data[1])
            return f"{service_name} - {info_name}"
        return service_name
    if sid >= 0x50:
        service_name = _service_id_name(sid - 0x40)
        return f"Positive Response to {service_name}"
    return None


def _service_id_name(sid: int) -> str:
    services = {
        0x01: "Show Current Data",
        0x02: "Show Freeze Frame Data",
        0x03: "Show Stored DTCs",
        0x04: "Clear DTCs",
        0x05: "O2 Sensor Monitoring",
        0x06: "On-Board Monitoring",
        0x07: "Show Pending DTCs",
        0x08: "Control On-Board System",
        0x09: "Request Vehicle Information",
        0x0A: "Permanent DTCs",
        0x10: "Diagnostic Session Control",
        0x11: "ECU Reset",
        0x14: "Clear Diagnostic Information",
        0x19: "Read DTC Information",
        0x22: "Read Data By Identifier",
        0x23: "Read Memory By Address",
        0x24: "Read Scaling Data By Identifier",
        0x27: "Security Access",
        0x28: "Communication Control",
        0x2E: "Write Data By Identifier",
        0x2F: "Input/Output Control By Identifier",
        0x31: "Routine Control",
        0x34: "Request Download",
        0x35: "Request Upload",
        0x36: "Transfer Data",
        0x37: "Request Transfer Exit",
        0x3E: "Tester Present",
        0x85: "Control DTC Setting",
    }
    return services.get(sid, f"Service 0x{sid:02X}")


def _obd2_service_name(service: int) -> str:
    names = {
        0x01: "Mode 01: Current Data",
        0x02: "Mode 02: Freeze Frame Data",
        0x03: "Mode 03: Stored DTCs",
        0x04: "Mode 04: Clear DTCs",
        0x09: "Mode 09: Vehicle Information",
    }
    return names.get(service, f"Mode 0x{service:02X}")


def _obd2_pid_name(pid: int) -> str:
    names = {
        0x00: "Supported PIDs [01-20]",
        0x01: "Monitor Status",
        0x02: "Freeze DTC",
        0x03: "Fuel System Status",
        0x04: "Calculated Engine Load",
        0x05: "Engine Coolant Temperature",
        0x06: "Short Term Fuel Trim Bank 1",
        0x0C: "Engine RPM",
        0x0D: "Vehicle Speed",
        0x0F: "Intake Air Temperature",
        0x10: "MAF Air Flow Rate",
        0x11: "Throttle Position",
        0x1C: "OBD Standards Compliance",
        0x1F: "Engine Run Time",
        0x20: "Supported PIDs [21-40]",
        0x21: "Distance with MIL On",
        0x2F: "Fuel Tank Level",
        0x33: "Barometric Pressure",
        0x40: "Supported PIDs [41-60]",
        0x42: "Control Module Voltage",
        0x46: "Ambient Air Temperature",
        0x51: "Fuel Type",
        0x60: "Supported PIDs [61-80]",
    }
    return names.get(pid, f"PID 0x{pid:02X}")


def _obd2_mode09_name(info_type: int) -> str:
    names = {
        0x00: "Supported Info Types",
        0x01: "VIN Message Count",
        0x02: "Vehicle Identification Number (VIN)",
        0x03: "Calibration ID Message Count",
        0x04: "Calibration IDs",
        0x05: "Calibration Verification Numbers Message Count",
        0x06: "Calibration Verification Numbers (CVN)",
        0x08: "In-use Performance Tracking Message Count",
        0x09: "ECU Name Message Count",
        0x0A: "ECU Name",
    }
    return names.get(info_type, f"Info Type 0x{info_type:02X}")


def _negative_response_description(nrc: int) -> str:
    descriptions = {
        0x10: "General Reject",
        0x11: "Service Not Supported",
        0x12: "Sub-Function Not Supported",
        0x13: "Incorrect Message Length or Invalid Format",
        0x14: "Response Too Long",
        0x21: "Busy - Repeat Request",
        0x22: "Conditions Not Correct",
        0x24: "Request Sequence Error",
        0x31: "Request Out of Range",
        0x33: "Security Access Denied",
        0x35: "Invalid Key",
        0x36: "Exceeded Number of Attempts",
        0x37: "Required Time Delay Not Expired",
        0x78: "Request Correctly Received - Response Pending",
        0x7E: "Sub-Function Not Supported in Active Session",
        0x7F: "Service Not Supported in Active Session",
        0x81: "RPM Too High",
        0x82: "RPM Too Low",
        0x83: "Engine Is Running",
        0x84: "Engine Is Not Running",
        0x92: "Voltage Too High",
        0x93: "Voltage Too Low",
    }
    return descriptions.get(nrc, f"NRC 0x{nrc:02X}")


def _is_ble_endpoint(endpoint: str) -> bool:
    if "://" not in endpoint:
        return False
    parsed = urlparse(endpoint)
    scheme = (parsed.scheme or "").lower()
    if "ble" in scheme:
        return True
    return "ecuconnect-ble" in endpoint.lower()


def _is_tcp_endpoint(endpoint: str) -> bool:
    if "://" not in endpoint:
        return True
    parsed = urlparse(endpoint)
    scheme = (parsed.scheme or "").lower()
    return "tcp" in scheme or scheme in {"ecuconnect", "ecuconnect-wifi"}


def _format_rate(bytes_per_second: float) -> str:
    kib = 1024.0
    mib = kib * 1024.0
    if bytes_per_second >= mib:
        return f"{bytes_per_second / mib:.2f} MiB/s"
    if bytes_per_second >= kib:
        return f"{bytes_per_second / kib:.2f} KiB/s"
    return f"{bytes_per_second:.0f} B/s"


def _default_payload_sizes(max_payload_size: int) -> list[int]:
    if max_payload_size <= 0:
        return []
    base = [8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768]
    sizes = [size for size in base if size <= max_payload_size]
    if not sizes:
        return [max_payload_size]
    if sizes[-1] < max_payload_size:
        sizes.append(max_payload_size)
    return sizes


def _recommend_payload_size(results: list[BenchmarkResult]) -> BenchmarkResult | None:
    if not results:
        return None
    max_bw = max(result.bandwidth_bytes_per_second for result in results)
    if max_bw <= 0:
        return min(results, key=lambda result: result.average)
    threshold = max_bw * 0.98
    candidates = [result for result in results if result.bandwidth_bytes_per_second >= threshold]
    latency_epsilon = 0.0005
    def _key(result: BenchmarkResult) -> tuple[float, int]:
        return (result.average, result.payload_size)
    best = min(candidates, key=_key)
    if abs(best.average - min(result.average for result in candidates)) <= latency_epsilon:
        return min(candidates, key=lambda result: (result.average, result.payload_size))
    return best


@app.callback(invoke_without_command=True)
def main(
    ctx: typer.Context,
    endpoint: str = typer.Option(
        DEFAULT_ENDPOINT,
        help="ECUconnect endpoint (default: 192.168.42.42:129). Use host:port for mocks.",
    ),
    rx_buffer: int = typer.Option(4 * 1024 * 1024, help="Socket receive buffer size."),
    tx_buffer: int = typer.Option(4 * 1024 * 1024, help="Socket send buffer size."),
) -> None:
    ctx.ensure_object(dict)
    ctx.obj["endpoint"] = endpoint
    ctx.obj["rx_buffer"] = rx_buffer
    ctx.obj["tx_buffer"] = tx_buffer
    if ctx.invoked_subcommand is None:
        console.print(ctx.get_help())
        raise typer.Exit()


@app.command()
def info(ctx: typer.Context) -> None:
    """Query adapter info and voltage."""
    endpoint = ctx.obj["endpoint"]
    rx_buffer = ctx.obj["rx_buffer"]
    tx_buffer = ctx.obj["tx_buffer"]
    with EcuconnectClient(endpoint=endpoint, rx_buffer=rx_buffer, tx_buffer=tx_buffer) as client:
        info = client.request_info()
        voltage = client.read_voltage()
    console.print(f"Connected to ECUconnect {info.vendor} {info.model}")
    console.print(f"Hardware: {info.hardware}")
    console.print(f"Serial: {info.serial}")
    console.print(f"Firmware: {info.firmware}")
    console.print(f"Voltage: {voltage:.2f} V")


@app.command()
def ping(
    ctx: typer.Context,
    payload_size: int = typer.Argument(512, help="Payload size (0-65535)."),
    count: int = typer.Option(10, "--count", "-c", help="Number of pings."),
    timeout: float = typer.Option(2.0, help="Timeout per ping (seconds)."),
) -> None:
    """Send ping PDUs and report latency stats."""
    if payload_size < 0 or payload_size > 65535:
        raise typer.BadParameter("payload_size must be between 0 and 65535")
    endpoint = ctx.obj["endpoint"]
    rx_buffer = ctx.obj["rx_buffer"]
    tx_buffer = ctx.obj["tx_buffer"]
    with EcuconnectClient(endpoint=endpoint, rx_buffer=rx_buffer, tx_buffer=tx_buffer) as client:
        durations = []
        for _ in range(count):
            durations.append(client.ping(payload_size=payload_size, timeout=timeout))
    durations_ms = [d * 1000.0 for d in durations]
    console.print(
        f"Ping payload={payload_size}B count={count} min={min(durations_ms):.2f}ms "
        f"median={statistics.median(durations_ms):.2f}ms max={max(durations_ms):.2f}ms"
    )


@app.command()
def benchmark(
    ctx: typer.Context,
    count: int = typer.Option(32, "--count", "-c", help="Pings per payload size."),
    sizes: list[str] = typer.Option(
        [],
        "--sizes",
        help=(
            "Payload sizes in bytes (repeatable or comma-separated). "
            "If omitted, a default range is used (BLE cap 5000 bytes, TCP cap 16384 bytes)."
        ),
    ),
    force: bool = typer.Option(False, "--force", help="Allow payload sizes exceeding transport caps."),
) -> None:
    """Benchmark latency and bandwidth using PING."""
    endpoint = ctx.obj["endpoint"]
    rx_buffer = ctx.obj["rx_buffer"]
    tx_buffer = ctx.obj["tx_buffer"]

    if count <= 0:
        raise typer.BadParameter("Count must be greater than 0.")

    ble_payload_cap = 5000
    tcp_payload_cap = 16384
    protocol_payload_cap = 0xFFFF

    is_ble = _is_ble_endpoint(endpoint)
    is_tcp = _is_tcp_endpoint(endpoint)
    default_max_payload = ble_payload_cap if is_ble else (tcp_payload_cap if is_tcp else 4096)

    raw_sizes: list[int] = []
    for entry in sizes:
        for part in entry.replace(",", " ").split():
            if not part:
                continue
            try:
                raw_sizes.append(int(part, 0))
            except ValueError as exc:
                raise typer.BadParameter(f"Invalid payload size: {part}") from exc

    payload_sizes = (
        _default_payload_sizes(default_max_payload) if not raw_sizes else sorted(set(raw_sizes))
    )

    if not payload_sizes:
        raise typer.BadParameter("Payload sizes are empty.")
    if any(size < 0 for size in payload_sizes):
        raise typer.BadParameter("Payload sizes must be >= 0.")
    too_large = [size for size in payload_sizes if size > protocol_payload_cap]
    if too_large:
        raise typer.BadParameter(
            f"Payload size {too_large[0]} exceeds protocol maximum of {protocol_payload_cap} bytes."
        )

    if is_ble:
        too_large_ble = [size for size in payload_sizes if size > ble_payload_cap]
        if too_large_ble and not force:
            raise typer.BadParameter(
                f"Payload size {too_large_ble[0]} exceeds BLE cap of {ble_payload_cap} bytes. "
                "Use --force to override."
            )
        if too_large_ble and force:
            console.print(
                f"Warning: Payload size {too_large_ble[0]} exceeds BLE cap of {ble_payload_cap} bytes."
            )

    if is_tcp:
        too_large_tcp = [size for size in payload_sizes if size > tcp_payload_cap]
        if too_large_tcp and not force:
            raise typer.BadParameter(
                f"Payload size {too_large_tcp[0]} exceeds TCP cap of {tcp_payload_cap} bytes. "
                "Use --force to override."
            )
        if too_large_tcp and force:
            console.print(
                f"Warning: Payload size {too_large_tcp[0]} exceeds TCP cap of {tcp_payload_cap} bytes."
            )

    with EcuconnectClient(endpoint=endpoint, rx_buffer=rx_buffer, tx_buffer=tx_buffer) as client:
        info = client.request_info()
        voltage = client.read_voltage()
        console.print(f"Connected to ECUconnect: {info.vendor} {info.model}.")
        console.print(f"Reported system voltage is {voltage:.2f} V.")

        console.print(f"Benchmarking PING with {count} pings per size.")
        console.print(f"Payload sizes: {', '.join(str(size) for size in payload_sizes)} bytes")
        if is_ble:
            console.print(f"BLE payload cap: {ble_payload_cap} bytes")
        if is_tcp:
            console.print(f"TCP payload cap: {tcp_payload_cap} bytes")

        header = f"{'SIZE':<10}{'AVG':<13}{'MIN':<13}{'MAX':<13}AVG BW"
        console.print(header)

        results: list[BenchmarkResult] = []
        total_duration = 0.0
        total_count = 0
        spinner_frames = ["|", "/", "-", "\\"]
        spinner_index = 0

        for payload_size in payload_sizes:
            min_ping = float("inf")
            max_ping = 0.0
            sum_ping = 0.0
            progress_len = 0

            for index in range(count):
                spinner = spinner_frames[spinner_index]
                spinner_index = (spinner_index + 1) % len(spinner_frames)
                progress_plain = f"{spinner} {payload_size} bytes  ping {index + 1}/{count}"
                progress_len = max(progress_len, len(progress_plain))
                sys.stdout.write(progress_plain.ljust(progress_len) + "\r")
                sys.stdout.flush()
                duration = client.ping(payload_size=payload_size)
                sum_ping += duration
                min_ping = min(min_ping, duration)
                max_ping = max(max_ping, duration)

            if progress_len > 0:
                sys.stdout.write(" " * progress_len + "\r")
                sys.stdout.flush()

            avg_ping = sum_ping / float(count)
            round_trip_bw = (payload_size * 2.0 / avg_ping) if payload_size > 0 else 0.0
            results.append(
                BenchmarkResult(
                    payload_size=payload_size,
                    average=avg_ping,
                    minimum=min_ping,
                    maximum=max_ping,
                    bandwidth_bytes_per_second=round_trip_bw,
                )
            )
            total_duration += sum_ping
            total_count += count

            size_str = f"{payload_size:6d} bytes"
            avg_str = f"avg {avg_ping * 1000:7.2f} ms"
            min_str = f"min {min_ping * 1000:7.2f} ms"
            max_str = f"max {max_ping * 1000:7.2f} ms"
            bw_str = f"avg rt bw {_format_rate(round_trip_bw)}"
            console.print(f"{size_str:<10} {avg_str:<13} {min_str:<13} {max_str:<13} {bw_str}")

        overall_avg = total_duration / float(total_count) if total_count else 0.0
        best = max(results, key=lambda item: item.bandwidth_bytes_per_second, default=None)
        if best is not None:
            console.print(
                f"Max avg rt bandwidth: {_format_rate(best.bandwidth_bytes_per_second)} @ {best.payload_size} bytes"
            )
        recommended = _recommend_payload_size(results)
        if recommended is not None:
            console.print(
                "Recommended payload size: "
                f"{recommended.payload_size} bytes (avg rt bw "
                f"{_format_rate(recommended.bandwidth_bytes_per_second)}, "
                f"avg latency {recommended.average * 1000:.2f} ms)"
            )
        console.print(f"Average latency: {overall_avg * 1000:.2f} ms")


@app.command()
def term(
    ctx: typer.Context,
    bitrate: int = typer.Argument(500000, help="Bitrate."),
    proto: str = typer.Option(
        "passthrough", "--proto", "-p", help="Channel protocol (passthrough, isotp, or kline)."
    ),
    timeout: float = typer.Option(1.0, help="Response timeout in seconds."),
    idle_timeout: float = typer.Option(0.25, help="Idle timeout for multicast responses."),
) -> None:
    """Interactive CAN terminal."""
    endpoint = ctx.obj["endpoint"]
    rx_buffer = ctx.obj["rx_buffer"]
    tx_buffer = ctx.obj["tx_buffer"]

    proto_normalized = proto.lower()
    if proto_normalized in {"passthrough", "raw"}:
        channel_proto = canyonero.ChannelProtocol.raw
    elif proto_normalized == "isotp":
        channel_proto = canyonero.ChannelProtocol.isotp
    elif proto_normalized == "kline":
        channel_proto = canyonero.ChannelProtocol.kline
    else:
        raise typer.BadParameter("Invalid protocol. Use passthrough, isotp, or kline.")

    if channel_proto == canyonero.ChannelProtocol.kline:
        default_addressing = Addressing(
            mode="unicast",
            request_id=0x33,
            request_ext=0,
            reply_pattern=0xF1,
            reply_mask=0x7FF,
            reply_ext=0,
        )
    else:
        default_addressing = Addressing(
            mode="unicast",
            request_id=0x7DF,
            request_ext=0,
            reply_pattern=0x7E8,
            reply_mask=0x7FF,
            reply_ext=0,
        )

    def apply_addressing(client: EcuconnectClient, channel: int, addressing: Addressing) -> None:
        arbitration = canyonero.Arbitration(
            request=addressing.request_id,
            reply_pattern=addressing.reply_pattern,
            reply_mask=addressing.reply_mask,
            request_extension=addressing.request_ext,
            reply_extension=addressing.reply_ext,
        )
        client.set_arbitration(channel, arbitration)

    with EcuconnectClient(endpoint=endpoint, rx_buffer=rx_buffer, tx_buffer=tx_buffer) as client:
        info = client.request_info()
        voltage = client.read_voltage()
        console.print(f"Connected to ECUconnect: {info.vendor} {info.model}.")
        console.print(f"Reported system voltage is {voltage:.2f} V.")

        channel = open_channel_with_retry(
            client,
            bitrate=bitrate,
            protocol=channel_proto,
            open_timeout=5.0,
            open_retries=3,
            open_retry_delay=1.0,
        )
        apply_addressing(client, channel, default_addressing)

        channel_desc = {
            canyonero.ChannelProtocol.raw: "Passthrough",
            canyonero.ChannelProtocol.isotp: "ISOTP",
            canyonero.ChannelProtocol.kline: "KLine",
        }.get(channel_proto, str(channel_proto))
        console.print(f"{channel_desc} channel opened at {bitrate} bps.")
        console.print("Commands:")
        console.print("  :REQ,REPLY     - Set addressing (e.g. :7df,7e8 or :33,F1)")
        console.print("  :6F1/12,612/F1 - Include CAN extended addressing bytes (EA/REA)")
        console.print("  0902           - Send hex data with current addressing")
        console.print("  quit           - Exit")
        console.print("")

        current_addressing = default_addressing

        try:
            while True:
                prompt = "> " if current_addressing else "> (set addressing first with :7df,7e8) "
                try:
                    line = input(prompt)
                except (EOFError, KeyboardInterrupt):
                    console.print("Goodbye!")
                    return
                trimmed = line.strip()
                if not trimmed:
                    continue
                if trimmed.lower().startswith("quit") or trimmed.lower().startswith("exit"):
                    console.print("Goodbye!")
                    return
                if trimmed.startswith(":"):
                    addressing = _parse_addressing(trimmed[1:])
                    if addressing is None:
                        console.print(
                            "SyntaxError: Invalid addressing format. Use :7df,7e8 or "
                            ":18DA33F1/10,18DAF110/20 (send[/ea],reply[/rea])"
                        )
                        continue
                    current_addressing = addressing
                    apply_addressing(client, channel, addressing)
                    console.print(f"Addressing set to: {_format_addressing(addressing)}")
                    continue

                payload = _parse_hex_bytes(trimmed)
                if not payload:
                    console.print("SyntaxError: Invalid message format. Use hex bytes like: 0902")
                    continue

                client.send(channel, payload)

                if current_addressing.mode == "multicast":
                    responses: list[tuple[int, int, bytes]] = []
                    deadline = time.time() + timeout
                    while time.time() < deadline:
                        remaining = max(0.0, deadline - time.time())
                        pdu = client.get_pdu(timeout=min(idle_timeout, remaining))
                        if pdu is None:
                            if responses:
                                break
                            continue
                        if pdu.type not in (canyonero.PDUType.received, canyonero.PDUType.received_compressed):
                            continue
                        if pdu.channel() != channel:
                            continue
                        decoded = _decode_received_pdu(pdu)
                        if decoded:
                            responses.append(decoded)
                    for can_id, _ext, data in responses:
                        print(_format_message(can_id, data))
                        interpretation = _interpret_response(data)
                        if interpretation:
                            print(f" info: {interpretation}")
                    continue

                # Unicast
                response = None
                deadline = time.time() + timeout
                while time.time() < deadline:
                    remaining = max(0.0, deadline - time.time())
                    pdu = client.get_pdu(timeout=remaining)
                    if pdu is None:
                        break
                    if pdu.type not in (canyonero.PDUType.received, canyonero.PDUType.received_compressed):
                        continue
                    if pdu.channel() != channel:
                        continue
                    response = _decode_received_pdu(pdu)
                    if response:
                        break
                if response is None:
                    console.print("Timeout waiting for response.")
                    continue
                can_id, _ext, data = response
                print(_format_message(can_id, data))
                interpretation = _interpret_response(data)
                if interpretation:
                    print(f" info: {interpretation}")
        finally:
            try:
                client.close_channel(channel, timeout=timeout)
            except (TimeoutError, RuntimeError):
                pass


@app.command()
def monitor(
    ctx: typer.Context,
    bitrate: int = typer.Option(500000, help="CAN bitrate."),
    rx_id: Optional[str] = typer.Option(None, help="Filter RX ID (hex or int)."),
    rx_mask: Optional[str] = typer.Option(None, help="Filter RX mask (hex or int)."),
    rx_extended: bool = typer.Option(False, help="Use extended RX ID."),
) -> None:
    """Monitor CAN frames from ECUconnect."""
    endpoint = ctx.obj["endpoint"]
    rx_buffer = ctx.obj["rx_buffer"]
    tx_buffer = ctx.obj["tx_buffer"]
    rx_id_value = _parse_int(rx_id) if rx_id is not None else 0
    if rx_mask is None:
        if rx_id is None:
            rx_mask_value = 0
        else:
            rx_mask_value = 0x1FFFFFFF if rx_extended else 0x7FF
    else:
        rx_mask_value = _parse_int(rx_mask)

    with EcuconnectClient(endpoint=endpoint, rx_buffer=rx_buffer, tx_buffer=tx_buffer) as client:
        channel = open_channel_with_retry(
            client,
            bitrate=bitrate,
            protocol=canyonero.ChannelProtocol.raw,
            open_timeout=5.0,
            open_retries=3,
            open_retry_delay=1.0,
        )
        arbitration = canyonero.Arbitration(
            request=0,
            reply_pattern=rx_id_value,
            reply_mask=rx_mask_value,
            request_extension=0,
            reply_extension=0,
        )
        client.set_arbitration(channel, arbitration)
        if rx_id is None and rx_mask is None:
            console.print("Monitoring ECUconnect... (pass-all filter)")
        else:
            console.print("Monitoring ECUconnect... Ctrl+C to stop")
        last = time.perf_counter()
        try:
            while True:
                pdu = client.get_pdu(timeout=1.0)
                if pdu is None:
                    continue
                if pdu.type not in (canyonero.PDUType.received, canyonero.PDUType.received_compressed):
                    continue
                payload = pdu.payload()
                if len(payload) < 6:
                    continue
                can_id = int.from_bytes(payload[1:5], "big")
                data = pdu.data() if pdu.type == canyonero.PDUType.received else pdu.uncompressed_data()
                now = time.perf_counter()
                delta = now - last
                last = now
                data_hex = " ".join(f"{b:02X}" for b in data)
                console.print(f"{delta:10.6f} 0x{can_id:08X} [{len(data)}] {data_hex}")
        except KeyboardInterrupt:
            console.print("Stopped.")
        finally:
            try:
                client.close_channel(channel, timeout=2.0)
            except (TimeoutError, RuntimeError):
                pass


@app.command("send")
def send_frame(
    ctx: typer.Context,
    data: str = typer.Argument(..., help="Hex payload (e.g. '02 3E 80')."),
    bitrate: int = typer.Option(500000, help="CAN bitrate."),
    tx_id: str = typer.Option("0x123", help="TX CAN ID."),
    rx_id: str = typer.Option("0x321", help="RX CAN ID."),
    rx_mask: Optional[str] = typer.Option(None, help="RX mask."),
    count: int = typer.Option(1, help="Repeat count."),
    interval: float = typer.Option(0.1, help="Interval between frames (seconds)."),
) -> None:
    """Send a raw CAN frame over ECUconnect."""
    endpoint = ctx.obj["endpoint"]
    rx_buffer = ctx.obj["rx_buffer"]
    tx_buffer = ctx.obj["tx_buffer"]
    payload = _parse_hex_bytes(data)
    tx_id_value = _parse_int(tx_id)
    rx_id_value = _parse_int(rx_id)
    rx_mask_value = _parse_int(rx_mask) if rx_mask else 0x7FF

    with EcuconnectClient(endpoint=endpoint, rx_buffer=rx_buffer, tx_buffer=tx_buffer) as client:
        channel = open_channel_with_retry(
            client,
            bitrate=bitrate,
            protocol=canyonero.ChannelProtocol.raw,
            open_timeout=5.0,
            open_retries=3,
            open_retry_delay=1.0,
        )
        arbitration = canyonero.Arbitration(
            request=tx_id_value,
            reply_pattern=rx_id_value,
            reply_mask=rx_mask_value,
            request_extension=0,
            reply_extension=0,
        )
        client.set_arbitration(channel, arbitration)
        try:
            for _ in range(count):
                client.send(channel, payload)
                time.sleep(interval)
        finally:
            try:
                client.close_channel(channel, timeout=2.0)
            except (TimeoutError, RuntimeError):
                pass


@app.command()
def test(
    ctx: typer.Context,
    can_interface: str = typer.Option("can0", help="SocketCAN interface to use."),
    bitrate: int = typer.Option(500000, help="CAN bitrate."),
    payload_len: int = typer.Option(8, help="Payload length."),
    busload: List[str] = typer.Option(
        ["1"],
        "--busload",
        "-b",
        help="Busload percentage(s) to test, or 'auto' to ramp from low PPS.",
    ),
    pps: Optional[float] = typer.Option(None, help="Override with packets/sec."),
    auto_start_pps: float = typer.Option(1.0, help="Auto ramp start rate (pps)."),
    auto_step_pps: float = typer.Option(1.0, help="Auto ramp step size (pps)."),
    auto_max_busload: float = typer.Option(1.0, help="Auto ramp max busload percent."),
    preflight: bool = typer.Option(True, help="Run info + ping preflight before load test."),
    preflight_only: bool = typer.Option(
        False, help="Stop after preflight (no CAN channel open)."
    ),
    traffic: str = typer.Option(
        "all",
        help="Traffic mode: none, rx (CAN->ECU), tx (ECU->CAN), all.",
        show_default=True,
    ),
    preflight_pings: int = typer.Option(3, help="Number of preflight pings."),
    preflight_payload: int = typer.Option(8, help="Preflight ping payload size."),
    preflight_timeout: float = typer.Option(2.0, help="Timeout for preflight ping/info."),
    open_timeout: float = typer.Option(5.0, help="Timeout for open_channel (seconds)."),
    open_retries: int = typer.Option(3, help="Retries for open_channel timeouts."),
    open_retry_delay: float = typer.Option(1.0, help="Delay between open_channel retries."),
    duration: float = typer.Option(5.0, help="Duration per direction (seconds)."),
    settle: float = typer.Option(1.0, help="Settle time after sending (seconds)."),
    tx_id: str = typer.Option("0x123", help="TX CAN ID for ECUconnect->bus."),
    rx_id: str = typer.Option("0x321", help="RX CAN ID for bus->ECUconnect."),
    rx_mask: Optional[str] = typer.Option(None, help="RX mask for ECUconnect."),
    tx_extended: bool = typer.Option(False, help="Use extended TX ID."),
    rx_extended: bool = typer.Option(False, help="Use extended RX ID."),
    loss_threshold: float = typer.Option(2.0, help="Maximum loss percentage allowed."),
) -> None:
    """Run an automated ECUconnect loop test using SocketCAN."""
    endpoint = ctx.obj["endpoint"]
    rx_buffer = ctx.obj["rx_buffer"]
    tx_buffer = ctx.obj["tx_buffer"]
    tx_id_value = _parse_int(tx_id)
    rx_id_value = _parse_int(rx_id)

    if rx_mask is None:
        rx_mask_value = 0x1FFFFFFF if rx_extended else 0x7FF
    else:
        rx_mask_value = _parse_int(rx_mask)

    busload_entries = [entry.strip().lower() for entry in busload]
    pps_list: list[float] = []

    if "auto" in busload_entries:
        if len(busload_entries) > 1:
            raise typer.BadParameter("Use only 'auto' or numeric busload values, not both.")
        if pps is not None:
            raise typer.BadParameter("Use either pps override or auto ramp, not both.")
        if auto_start_pps <= 0 or auto_step_pps <= 0:
            raise typer.BadParameter("auto_start_pps and auto_step_pps must be > 0.")
        if auto_max_busload <= 0:
            raise typer.BadParameter("auto_max_busload must be > 0.")
        busload_list = []
        current_pps = auto_start_pps
        while True:
            current_busload = pps_to_busload(bitrate, current_pps, payload_len, rx_extended)
            if current_busload > auto_max_busload:
                break
            busload_list.append(current_busload)
            pps_list.append(current_pps)
            current_pps += auto_step_pps
        if not busload_list:
            raise typer.BadParameter("auto ramp produced no steps; adjust auto_max_busload.")
    else:
        try:
            busload_list = [float(entry) for entry in busload]
        except ValueError as exc:
            raise typer.BadParameter("Busload values must be numeric or 'auto'.") from exc

        if pps is not None:
            busload_list = [pps_to_busload(bitrate, pps, payload_len, rx_extended)]
            pps_list = [pps]

    if preflight:
        console.print("Preflight: request info + ping")
        with EcuconnectClient(endpoint=endpoint, rx_buffer=rx_buffer, tx_buffer=tx_buffer) as client:
            info = client.request_info(timeout=preflight_timeout)
            console.print(f"Adapter: {info.vendor} {info.model} ({info.hardware})")
            console.print(f"Serial: {info.serial} Firmware: {info.firmware}")
            ping_times = []
            for _ in range(max(preflight_pings, 0)):
                ping_times.append(
                    client.ping(payload_size=preflight_payload, timeout=preflight_timeout)
                )
        if ping_times:
            ping_ms = [value * 1000.0 for value in ping_times]
            console.print(
                f"Ping: min={min(ping_ms):.2f}ms median={statistics.median(ping_ms):.2f}ms "
                f"max={max(ping_ms):.2f}ms"
            )

    if preflight_only:
        console.print("Preflight-only mode requested; skipping load test.")
        return

    traffic_mode = traffic.strip().lower()
    if traffic_mode not in {"none", "rx", "tx", "all"}:
        raise typer.BadParameter("traffic must be one of: none, rx, tx, all")

    if traffic_mode != "none":
        try:
            py_socket.if_nametoindex(can_interface)
        except OSError as exc:
            raise typer.BadParameter(
                f"SocketCAN interface '{can_interface}' not found. "
                "Use --can-interface to set the correct device."
            ) from exc

    if traffic_mode == "none":
        console.print("Traffic=none: open channel and set arbitration only.")
        with EcuconnectClient(endpoint=endpoint, rx_buffer=rx_buffer, tx_buffer=tx_buffer) as client:
            channel = open_channel_with_retry(
                client,
                bitrate=bitrate,
                protocol=canyonero.ChannelProtocol.raw,
                open_timeout=open_timeout,
                open_retries=open_retries,
                open_retry_delay=open_retry_delay,
            )
            arbitration = canyonero.Arbitration(
                request=tx_id_value,
                reply_pattern=rx_id_value,
                reply_mask=rx_mask_value,
                request_extension=0,
                reply_extension=0,
            )
            client.set_arbitration(channel, arbitration, timeout=open_timeout)
            client.close_channel(channel, timeout=open_timeout)
        console.print("Traffic=none complete.")
        return

    summaries = run_ecuconnect_test(
        endpoint=endpoint,
        can_interface=can_interface,
        bitrate=bitrate,
        payload_len=payload_len,
        busloads=busload_list,
        pps_overrides=pps_list or None,
        traffic_mode=traffic_mode,
        open_timeout=open_timeout,
        open_retries=open_retries,
        open_retry_delay=open_retry_delay,
        duration=duration,
        settle_time=settle,
        tx_id=tx_id_value,
        rx_id=rx_id_value,
        rx_mask=rx_mask_value,
        tx_extended=tx_extended,
        rx_extended=rx_extended,
        rx_buffer=rx_buffer,
        tx_buffer=tx_buffer,
        loss_threshold=loss_threshold,
    )

    table = Table(title="ECUconnect Test Results")
    table.add_column("Busload %", justify="right")
    table.add_column("Direction")
    table.add_column("Sent")
    table.add_column("Recv")
    table.add_column("Loss %", justify="right")

    for summary, busload_pct in zip(summaries, busload_list):
        if summary.can_to_ecu is None:
            table.add_row(f"{busload_pct:.1f}", "CAN -> ECU", "-", "-", "skipped")
        else:
            table.add_row(
                f"{busload_pct:.1f}",
                "CAN -> ECU",
                str(summary.can_to_ecu.sent),
                str(summary.can_to_ecu.received),
                f"{summary.can_to_ecu.loss_pct:.2f}",
            )
        if summary.ecu_to_can is None:
            table.add_row(f"{busload_pct:.1f}", "ECU -> CAN", "-", "-", "skipped")
        else:
            table.add_row(
                f"{busload_pct:.1f}",
                "ECU -> CAN",
                str(summary.ecu_to_can.sent),
                str(summary.ecu_to_can.received),
                f"{summary.ecu_to_can.loss_pct:.2f}",
            )

    console.print(table)
    if not all(summary.passed for summary in summaries):
        console.print("Test failed: loss threshold exceeded.")
        raise typer.Exit(code=1)
    console.print("Test passed.")


if __name__ == "__main__":
    try:
        app()
    except KeyboardInterrupt:
        console.print("Interrupted.")
        raise SystemExit(130)
