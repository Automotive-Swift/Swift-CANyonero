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
from .test_runner import (
    open_channel_with_retry,
    run_ecuconnect_test,
)

app = typer.Typer(help="ECUconnect tool (Python) for CANyonero adapters", add_completion=False)
console = Console()
config_app = typer.Typer(help="Configure ECUconnect via JSON-RPC.")
config_canvoy_app = typer.Typer(help="Configure CANvoy settings.")
config_app.add_typer(config_canvoy_app, name="canvoy")
app.add_typer(config_app, name="config")


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


def _parse_mode(value: str) -> tuple[int, str]:
    normalized = value.strip().lower()
    mapping = {
        "0": ("elm327", 0),
        "elm": ("elm327", 0),
        "elm327": ("elm327", 0),
        "1": ("logger", 1),
        "logger": ("logger", 1),
        "2": ("canvoy", 2),
        "canvoy": ("canvoy", 2),
        "3": ("ecos", 3),
        "ecos": ("ecos", 3),
    }
    if normalized not in mapping:
        raise typer.BadParameter("Mode must be ecos, elm327, logger, canvoy (or 0-3).")
    name, mode_id = mapping[normalized]
    return mode_id, name


def _format_mode(value: object) -> str:
    try:
        mode_id = int(value)
    except (TypeError, ValueError):
        return str(value)
    mapping = {0: "elm327", 1: "logger", 2: "canvoy", 3: "ecos"}
    return mapping.get(mode_id, f"unknown({mode_id})")


def _parse_canvoy_role(value: str) -> tuple[int, str]:
    normalized = value.strip().lower()
    mapping = {
        "0": ("unconfigured", 0),
        "unconfigured": ("unconfigured", 0),
        "none": ("unconfigured", 0),
        "1": ("vehicle", 1),
        "vehicle": ("vehicle", 1),
        "2": ("tester", 2),
        "tester": ("tester", 2),
    }
    if normalized not in mapping:
        raise typer.BadParameter("Role must be vehicle, tester, unconfigured (or 0-2).")
    name, role_id = mapping[normalized]
    return role_id, name


def _format_canvoy_role(value: object) -> str:
    try:
        role_id = int(value)
    except (TypeError, ValueError):
        return str(value)
    mapping = {0: "unconfigured", 1: "vehicle", 2: "tester"}
    return mapping.get(role_id, f"unknown({role_id})")


def _rpc_bool(value: object, default: bool = False) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, int):
        return value != 0
    return default


def _rpc_best_effort(client: EcuconnectClient, method: str, params: Optional[dict] = None) -> Optional[dict]:
    try:
        return client.rpc_call(method, params=params)
    except RuntimeError:
        return None
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


def _default_reply_wildcard(request_id: int) -> str:
    width = 8 if request_id > 0x7FF else 3
    return "x" * width


def _parse_addressing(text: str) -> Addressing | None:
    parts = text.split(",")
    if len(parts) not in {1, 2}:
        return None
    send = _parse_address_component(parts[0])
    if send is None:
        return None

    reply_raw = parts[1].strip() if len(parts) == 2 else ""
    if not reply_raw:
        reply_raw = _default_reply_wildcard(send[0])
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
        "--endpoint",
        "--url",
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


@config_app.command("mode")
def config_mode(
    ctx: typer.Context,
    mode: str = typer.Argument(..., help="Mode: ecos, elm327, logger, canvoy (or 0-3)."),
    timeout: float = typer.Option(2.0, help="RPC timeout in seconds."),
) -> None:
    """Set the ECUconnect operating mode (JSON-RPC app.set_mode)."""
    endpoint = ctx.obj["endpoint"]
    rx_buffer = ctx.obj["rx_buffer"]
    tx_buffer = ctx.obj["tx_buffer"]
    mode_id, mode_name = _parse_mode(mode)
    with EcuconnectClient(endpoint=endpoint, rx_buffer=rx_buffer, tx_buffer=tx_buffer) as client:
        result = client.rpc_call("app.set_mode", params={"mode": mode_id}, timeout=timeout)
    success = result.get("success")
    if success is False:
        message = result.get("error", "Failed to change mode.")
        console.print(f"[red]Error:[/red] {message}")
        raise typer.Exit(code=1)
    console.print(f"Mode change requested: {mode_name}. Adapter will reboot shortly.")


@config_app.command("reboot")
def config_reboot(
    ctx: typer.Context,
    timeout: float = typer.Option(2.0, help="RPC/reset timeout in seconds."),
) -> None:
    """Reboot the adapter (tries JSON-RPC system.reboot, falls back to reset)."""
    endpoint = ctx.obj["endpoint"]
    rx_buffer = ctx.obj["rx_buffer"]
    tx_buffer = ctx.obj["tx_buffer"]
    with EcuconnectClient(endpoint=endpoint, rx_buffer=rx_buffer, tx_buffer=tx_buffer) as client:
        try:
            _ = client.rpc_call("system.reboot", timeout=timeout)
            console.print("Reboot requested via JSON-RPC.")
            return
        except RuntimeError as exc:
            exc_text = str(exc)
            if "error_invalid_rpc" not in exc_text and "error_invalid_command" not in exc_text:
                raise
            console.print("JSON-RPC reboot not available, falling back to reset command.")
        client.reset(timeout=timeout)
    console.print("Reboot requested via reset command.")


@config_app.command("show")
def config_show(
    ctx: typer.Context,
    timeout: float = typer.Option(2.0, help="RPC timeout in seconds."),
) -> None:
    """Show current configuration (best-effort via JSON-RPC)."""
    endpoint = ctx.obj["endpoint"]
    rx_buffer = ctx.obj["rx_buffer"]
    tx_buffer = ctx.obj["tx_buffer"]
    with EcuconnectClient(endpoint=endpoint, rx_buffer=rx_buffer, tx_buffer=tx_buffer) as client:
        config = _rpc_best_effort(client, "app.config")
        if config and "mode" in config:
            mode_value = config.get("mode")
            console.print(f"App mode: {_format_mode(mode_value)}")
            if isinstance(config.get("appstate"), str):
                console.print(f"App state: {config['appstate']}")
            elif isinstance(config.get("state"), str):
                console.print(f"App state: {config['state']}")
        else:
            console.print("App mode: unavailable (RPC not supported)")

        canvoy = _rpc_best_effort(client, "canvoy.role")
        if canvoy:
            role_name = _format_canvoy_role(canvoy.get("role", 0))
            bitrate = canvoy.get("bitrate", 500_000)
            if isinstance(bitrate, float):
                bitrate = int(bitrate)
            termination = _rpc_bool(canvoy.get("termination"), default=False)
            console.print(f"CANvoy role: {role_name}")
            console.print(f"CANvoy bitrate: {bitrate}")
            console.print(f"CANvoy termination: {'enabled' if termination else 'disabled'}")


@config_canvoy_app.command("get")
def config_canvoy_get(
    ctx: typer.Context,
    timeout: float = typer.Option(2.0, help="RPC timeout in seconds."),
) -> None:
    """Fetch CANvoy configuration (JSON-RPC canvoy.role)."""
    endpoint = ctx.obj["endpoint"]
    rx_buffer = ctx.obj["rx_buffer"]
    tx_buffer = ctx.obj["tx_buffer"]
    with EcuconnectClient(endpoint=endpoint, rx_buffer=rx_buffer, tx_buffer=tx_buffer) as client:
        result = client.rpc_call("canvoy.role", timeout=timeout)

    role_value = result.get("role", 0)
    role_name = _format_canvoy_role(role_value)
    bitrate = result.get("bitrate", 500_000)
    if isinstance(bitrate, float):
        bitrate = int(bitrate)
    termination = _rpc_bool(result.get("termination"), default=False)
    console.print(f"CANvoy role: {role_name}")
    console.print(f"Bitrate: {bitrate}")
    console.print(f"Termination: {'enabled' if termination else 'disabled'}")


@config_canvoy_app.command("set")
def config_canvoy_set(
    ctx: typer.Context,
    role: str = typer.Argument(..., help="Role: vehicle, tester, unconfigured (or 0-2)."),
    bitrate: int = typer.Option(500_000, help="CAN bitrate (e.g. 500000)."),
    termination: bool = typer.Option(False, "--termination/--no-termination", help="Enable termination."),
    timeout: float = typer.Option(2.0, help="RPC timeout in seconds."),
) -> None:
    """Set CANvoy configuration (JSON-RPC canvoy.set_role)."""
    endpoint = ctx.obj["endpoint"]
    rx_buffer = ctx.obj["rx_buffer"]
    tx_buffer = ctx.obj["tx_buffer"]
    role_id, role_name = _parse_canvoy_role(role)
    params = {"role": role_id, "bitrate": bitrate, "termination": termination}
    with EcuconnectClient(endpoint=endpoint, rx_buffer=rx_buffer, tx_buffer=tx_buffer) as client:
        result = client.rpc_call("canvoy.set_role", params=params, timeout=timeout)

    success = result.get("success")
    if success is False:
        message = result.get("error", "Unable to set CANvoy role.")
        console.print(f"[red]Error:[/red] {message}")
        raise typer.Exit(code=1)
    applied_bitrate = result.get("bitrate", bitrate)
    if isinstance(applied_bitrate, float):
        applied_bitrate = int(applied_bitrate)
    applied_termination = _rpc_bool(result.get("termination"), default=termination)
    console.print(f"CANvoy role set to {role_name}.")
    console.print(f"Bitrate: {applied_bitrate}")
    console.print(f"Termination: {'enabled' if applied_termination else 'disabled'}")


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
        "raw", "--proto", "-p", help="Channel protocol (raw, isotp, kline, raw_fd, or isotp_fd)."
    ),
    data_bitrate: int = typer.Option(2000000, "--data-bitrate", help="CAN-FD data bitrate (used for raw_fd/isotp_fd)."),
    timeout: float = typer.Option(1.0, help="Response timeout in seconds."),
    idle_timeout: float = typer.Option(0.25, help="Idle timeout for multicast responses."),
) -> None:
    """Interactive CAN terminal."""
    endpoint = ctx.obj["endpoint"]
    rx_buffer = ctx.obj["rx_buffer"]
    tx_buffer = ctx.obj["tx_buffer"]

    proto_normalized = proto.lower()
    if proto_normalized == "raw":
        channel_proto = canyonero.ChannelProtocol.raw
    elif proto_normalized == "isotp":
        channel_proto = canyonero.ChannelProtocol.isotp
    elif proto_normalized == "kline":
        channel_proto = canyonero.ChannelProtocol.kline
    elif proto_normalized == "raw_fd":
        channel_proto = canyonero.ChannelProtocol.raw_fd
    elif proto_normalized == "isotp_fd":
        channel_proto = canyonero.ChannelProtocol.isotp_fd
    else:
        raise typer.BadParameter("Invalid protocol. Use raw, isotp, kline, raw_fd, or isotp_fd.")

    fd_protocols = {
        canyonero.ChannelProtocol.raw_fd,
        canyonero.ChannelProtocol.isotp_fd,
    }
    selected_data_bitrate = data_bitrate if channel_proto in fd_protocols else None

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
            data_bitrate=selected_data_bitrate,
            open_timeout=5.0,
            open_retries=3,
            open_retry_delay=1.0,
        )
        apply_addressing(client, channel, default_addressing)

        channel_desc = {
            canyonero.ChannelProtocol.raw: "Raw",
            canyonero.ChannelProtocol.isotp: "ISOTP",
            canyonero.ChannelProtocol.kline: "KLine",
            canyonero.ChannelProtocol.raw_fd: "Raw CAN-FD",
            canyonero.ChannelProtocol.isotp_fd: "ISOTP-FD",
        }.get(channel_proto, str(channel_proto))
        if selected_data_bitrate:
            console.print(f"{channel_desc} channel opened at {bitrate}/{selected_data_bitrate} bps.")
        else:
            console.print(f"{channel_desc} channel opened at {bitrate} bps.")
        console.print("Commands:")
        console.print("  :REQ[,REPLY]   - Set addressing (e.g. :7df or :7df,7e8)")
        console.print("  :6F1/12,612/F1 - Include CAN extended addressing bytes (EA/REA)")
        console.print("  0902           - Send hex data with current addressing")
        console.print("  quit           - Exit")
        console.print("")

        current_addressing = default_addressing

        try:
            while True:
                prompt = "> " if current_addressing else "> (set addressing first with :7df or :7df,7e8) "
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
                            "SyntaxError: Invalid addressing format. Use :7df or :7df,7e8 "
                            "(or :18DA33F1/10,18DAF110/20 for extended addressing)"
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
            data_bitrate=None,
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
            data_bitrate=None,
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
        ["5"],
        "--busload",
        "-b",
        help="Busload percentage(s) to test, or 'auto' to ramp up automatically.",
    ),
    auto_max: float = typer.Option(30.0, help="Max busload for 'auto' mode (percent)."),
    auto_step: float = typer.Option(5.0, help="Busload step size for 'auto' mode (percent)."),
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
    loss_threshold: float = typer.Option(2.0, help="Maximum loss percentage allowed."),
    ecu_tx_boost: float = typer.Option(
        1.2, help="ECU->CAN rate boost factor to compensate for TCP overhead."
    ),
    rr_count: int = typer.Option(100, help="Request/response transaction count (0 to skip)."),
    rr_timeout: float = typer.Option(1.0, help="Request/response per-transaction timeout."),
) -> None:
    """Run an automated ECUconnect test using SocketCAN.

    Tests both raw CAN frame throughput at the specified busload percentage,
    and request/response latency patterns. Randomly chooses between standard
    (11-bit) and extended (29-bit) CAN IDs."""
    endpoint = ctx.obj["endpoint"]
    rx_buffer = ctx.obj["rx_buffer"]
    tx_buffer = ctx.obj["tx_buffer"]

    busload_entries = [entry.strip().lower() for entry in busload]

    if "auto" in busload_entries:
        if len(busload_entries) > 1:
            raise typer.BadParameter("Use only 'auto' or numeric busload values, not both.")
        if auto_step <= 0 or auto_max <= 0:
            raise typer.BadParameter("auto_step and auto_max must be > 0.")
        busload_list = []
        current = auto_step
        while current <= auto_max:
            busload_list.append(current)
            current += auto_step
        if not busload_list:
            raise typer.BadParameter("auto ramp produced no steps; adjust auto_max/auto_step.")
    else:
        try:
            busload_list = [float(entry) for entry in busload_entries]
        except ValueError as exc:
            raise typer.BadParameter("Busload values must be numeric or 'auto'.") from exc

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
                data_bitrate=None,
                open_timeout=open_timeout,
                open_retries=open_retries,
                open_retry_delay=open_retry_delay,
            )
            # Use standard test IDs for traffic=none
            arbitration = canyonero.Arbitration(
                request=0x123,
                reply_pattern=0x321,
                reply_mask=0x7FF,
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
        traffic_mode=traffic_mode,
        open_timeout=open_timeout,
        open_retries=open_retries,
        open_retry_delay=open_retry_delay,
        duration=duration,
        settle_time=settle,
        rx_buffer=rx_buffer,
        tx_buffer=tx_buffer,
        loss_threshold=loss_threshold,
        ecu_tx_boost=ecu_tx_boost,
        rr_count=rr_count,
        rr_timeout=rr_timeout,
    )

    # Throughput results table
    table = Table(title="Throughput Test")
    table.add_column("Busload", justify="right")
    table.add_column("Direction")
    table.add_column("Sent")
    table.add_column("Recv")
    table.add_column("Loss %", justify="right")

    for summary in summaries:
        busload_pct = summary.busload
        if summary.can_to_ecu is None:
            table.add_row(f"{busload_pct:.0f}%", "CAN -> ECU", "-", "-", "skipped")
        else:
            m = summary.can_to_ecu
            table.add_row(
                f"{busload_pct:.0f}%",
                "CAN -> ECU",
                str(m.sent),
                str(m.received),
                f"{m.loss_pct:.2f}",
            )
        if summary.ecu_to_can is None:
            table.add_row(f"{busload_pct:.0f}%", "ECU -> CAN", "-", "-", "skipped")
        else:
            m = summary.ecu_to_can
            table.add_row(
                f"{busload_pct:.0f}%",
                "ECU -> CAN",
                str(m.sent),
                str(m.received),
                f"{m.loss_pct:.2f}",
            )

    console.print(table)

    # Request/response results table
    rr_table = Table(title="Request/Response Test")
    rr_table.add_column("Busload", justify="right")
    rr_table.add_column("OK", justify="right")
    rr_table.add_column("Fail", justify="right")
    rr_table.add_column("Min ms", justify="right")
    rr_table.add_column("Avg ms", justify="right")
    rr_table.add_column("Max ms", justify="right")

    for summary in summaries:
        rr = summary.request_response
        if rr is None:
            rr_table.add_row(f"{summary.busload:.0f}%", "-", "-", "-", "-", "-")
        else:
            rr_table.add_row(
                f"{summary.busload:.0f}%",
                str(rr.successful),
                str(rr.failed),
                f"{rr.min_latency_ms:.2f}" if rr.successful > 0 else "-",
                f"{rr.avg_latency_ms:.2f}" if rr.successful > 0 else "-",
                f"{rr.max_latency_ms:.2f}" if rr.successful > 0 else "-",
            )

    console.print(rr_table)

    # Filter test results (only in first summary)
    filter_result = next((s.filter_test for s in summaries if s.filter_test is not None), None)
    if filter_result is not None:
        console.print("")
        if filter_result.passed:
            console.print(
                f"Filter Test: PASSED "
                f"({filter_result.accepted_matching} accepted, {filter_result.rejected_nonmatching} rejected)"
            )
        else:
            console.print(f"Filter Test: FAILED")
            for error in filter_result.errors:
                console.print(f"  - {error}")

    if not all(summary.passed for summary in summaries):
        console.print("Test failed.")
        raise typer.Exit(code=1)
    console.print("Test passed.")


if __name__ == "__main__":
    try:
        app()
    except KeyboardInterrupt:
        console.print("Interrupted.")
        raise SystemExit(130)
