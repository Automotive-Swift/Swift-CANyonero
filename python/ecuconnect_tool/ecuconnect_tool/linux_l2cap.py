"""Linux BLE/L2CAP transport for ECUconnect.

Phase 1 — Discovery via BlueZ D-Bus (python3-dbus + python3-gi).
Phase 2 — L2CAP CoC socket via kernel socket API (ctypes).
"""

from __future__ import annotations

import ctypes
import ctypes.util
import os
import select
import socket
import sys
import time
from typing import Optional, Tuple

# ---------------------------------------------------------------------------
# BlueZ D-Bus discovery (system packages: python3-dbus, python3-gi)
# ---------------------------------------------------------------------------

_BLUEZ_BUS_NAME = "org.bluez"
_BLUEZ_ADAPTER_IFACE = "org.bluez.Adapter1"
_BLUEZ_DEVICE_IFACE = "org.bluez.Device1"
_DBUS_PROPERTIES_IFACE = "org.freedesktop.DBus.Properties"
_DBUS_OBJECT_MANAGER_IFACE = "org.freedesktop.DBus.ObjectManager"


def _discover_device(
    service_uuid: str,
    timeout: float,
    peer_filter: Optional[str] = None,
) -> Tuple[str, int]:
    """Discover a BLE device advertising *service_uuid*.

    Returns ``(bdaddr, addr_type)`` where *addr_type* is one of the
    ``BDADDR_LE_*`` constants (1 = public, 2 = random).

    *peer_filter*, when given, is matched as a case-insensitive substring
    against the device BD_ADDR (e.g. ``"DC:DA:0C"``).
    """
    import dbus  # type: ignore[import-untyped]
    from gi.repository import GLib  # type: ignore[import-untyped]

    bus = dbus.SystemBus()

    # Normalise the service UUID to the full 128-bit lowercase form that
    # BlueZ exposes in UUIDs properties.
    service_uuid_lower = _normalise_uuid(service_uuid)

    # ------------------------------------------------------------------
    # Helper: check a D-Bus device object for a matching service UUID
    # ------------------------------------------------------------------
    def _device_matches(props: dict) -> bool:
        uuids = [str(u).lower() for u in props.get("UUIDs", [])]
        if service_uuid_lower not in uuids:
            return False
        if peer_filter is not None:
            addr = str(props.get("Address", ""))
            if peer_filter.upper() not in addr.upper():
                return False
        return True

    def _addr_type_int(props: dict) -> int:
        at = str(props.get("AddressType", "public")).lower()
        return 2 if at == "random" else 1  # BDADDR_LE_RANDOM / PUBLIC

    # ------------------------------------------------------------------
    # Phase 1a — check already-known devices
    # ------------------------------------------------------------------
    om = dbus.Interface(
        bus.get_object(_BLUEZ_BUS_NAME, "/"),
        _DBUS_OBJECT_MANAGER_IFACE,
    )
    objects = om.GetManagedObjects()
    for path, ifaces in objects.items():
        if _BLUEZ_DEVICE_IFACE not in ifaces:
            continue
        props = ifaces[_BLUEZ_DEVICE_IFACE]
        if _device_matches(props):
            return str(props["Address"]), _addr_type_int(props)

    # ------------------------------------------------------------------
    # Phase 1b — active LE scan with GLib mainloop timeout
    # ------------------------------------------------------------------
    adapter_path = _find_adapter(bus)
    adapter = dbus.Interface(
        bus.get_object(_BLUEZ_BUS_NAME, adapter_path),
        _BLUEZ_ADAPTER_IFACE,
    )

    result: list[Tuple[str, int]] = []
    loop = GLib.MainLoop()

    def _on_ifaces_added(path, ifaces):
        if _BLUEZ_DEVICE_IFACE not in ifaces:
            return
        props = ifaces[_BLUEZ_DEVICE_IFACE]
        if _device_matches(props):
            result.append((str(props["Address"]), _addr_type_int(props)))
            loop.quit()

    bus.add_signal_receiver(
        _on_ifaces_added,
        dbus_interface=_DBUS_OBJECT_MANAGER_IFACE,
        signal_name="InterfacesAdded",
    )

    # Also listen for property changes on already-created device objects
    # whose UUIDs list gets populated after service resolution.
    def _on_properties_changed(interface, changed, invalidated, path=None):
        if interface != _BLUEZ_DEVICE_IFACE:
            return
        if "UUIDs" not in changed:
            return
        try:
            dev_obj = bus.get_object(_BLUEZ_BUS_NAME, path)
            dev_props = dbus.Interface(dev_obj, _DBUS_PROPERTIES_IFACE)
            all_props = dev_props.GetAll(_BLUEZ_DEVICE_IFACE)
            if _device_matches(all_props):
                result.append((str(all_props["Address"]), _addr_type_int(all_props)))
                loop.quit()
        except dbus.DBusException:
            pass

    bus.add_signal_receiver(
        _on_properties_changed,
        dbus_interface=_DBUS_PROPERTIES_IFACE,
        signal_name="PropertiesChanged",
        path_keyword="path",
    )

    scan_filter = {
        "UUIDs": dbus.Array([service_uuid_lower], signature="s"),
        "Transport": "le",
    }
    try:
        adapter.SetDiscoveryFilter(scan_filter)
    except dbus.DBusException:
        pass  # Older BlueZ may not support filters; scan anyway.
    adapter.StartDiscovery()

    GLib.timeout_add(int(timeout * 1000), loop.quit)
    loop.run()

    try:
        adapter.StopDiscovery()
    except dbus.DBusException:
        pass

    if result:
        return result[0]

    raise TimeoutError(
        f"No BLE device found advertising service {service_uuid} "
        f"within {timeout:.1f}s"
    )


def _find_adapter(bus) -> str:
    """Return the D-Bus object path for the first Bluetooth adapter."""
    import dbus  # type: ignore[import-untyped]

    om = dbus.Interface(
        bus.get_object(_BLUEZ_BUS_NAME, "/"),
        _DBUS_OBJECT_MANAGER_IFACE,
    )
    for path, ifaces in om.GetManagedObjects().items():
        if _BLUEZ_ADAPTER_IFACE in ifaces:
            return str(path)
    raise RuntimeError("No Bluetooth adapter found via BlueZ D-Bus")


def _normalise_uuid(short_or_full: str) -> str:
    """Expand a short (4/8 hex char) or full UUID to lowercase 128-bit form."""
    s = short_or_full.strip().lower()
    if len(s) == 4:
        return f"0000{s}-0000-1000-8000-00805f9b34fb"
    if len(s) == 8:
        return f"{s}-0000-1000-8000-00805f9b34fb"
    return s


# ---------------------------------------------------------------------------
# L2CAP CoC socket via kernel API (ctypes)
# ---------------------------------------------------------------------------

# Bluetooth address family & protocol constants from <bluetooth/bluetooth.h>
AF_BLUETOOTH = 31
BTPROTO_L2CAP = 0

# bdaddr_type values for LE
BDADDR_LE_PUBLIC = 1
BDADDR_LE_RANDOM = 2

# Security level constants
SOL_BLUETOOTH = 274
BT_SECURITY = 4
BT_SECURITY_LOW = 1


class _sockaddr_l2(ctypes.Structure):
    """``struct sockaddr_l2`` from <bluetooth/l2cap.h>."""
    _fields_ = [
        ("l2_family", ctypes.c_ushort),   # sa_family_t
        ("l2_psm", ctypes.c_ushort),      # __le16 (little-endian)
        ("l2_bdaddr", ctypes.c_ubyte * 6), # bdaddr_t
        ("l2_cid", ctypes.c_ushort),      # __le16
        ("l2_bdaddr_type", ctypes.c_ubyte),
    ]


class _bt_security(ctypes.Structure):
    """``struct bt_security`` from <bluetooth/bluetooth.h>."""
    _fields_ = [
        ("level", ctypes.c_ubyte),
        ("key_size", ctypes.c_ubyte),
    ]


def _bdaddr_bytes(addr_str: str) -> bytes:
    """Convert ``"AA:BB:CC:DD:EE:FF"`` to 6 bytes in BlueZ wire order (reversed)."""
    parts = addr_str.split(":")
    if len(parts) != 6:
        raise ValueError(f"Invalid BD_ADDR: {addr_str}")
    return bytes(int(p, 16) for p in reversed(parts))


def _l2cap_connect(
    bdaddr: str,
    addr_type: int,
    psm: int,
    timeout: float,
) -> socket.socket:
    """Create a BLE L2CAP CoC connection and return a standard Python socket."""
    # Load libc for low-level socket ops we need before wrapping with
    # Python's socket module.
    libc_name = ctypes.util.find_library("c")
    if libc_name is None:
        raise RuntimeError("Cannot locate libc")
    libc = ctypes.CDLL(libc_name, use_errno=True)

    # Create raw Bluetooth L2CAP socket
    fd = libc.socket(AF_BLUETOOTH, socket.SOCK_SEQPACKET, BTPROTO_L2CAP)
    if fd < 0:
        errno = ctypes.get_errno()
        raise OSError(errno, f"socket(AF_BLUETOOTH) failed: {os.strerror(errno)}")

    try:
        # Bind with BDADDR_LE_PUBLIC — this is critical for data flow.
        bind_addr = _sockaddr_l2()
        bind_addr.l2_family = AF_BLUETOOTH
        bind_addr.l2_psm = 0
        bind_addr.l2_bdaddr = (ctypes.c_ubyte * 6)(*([0] * 6))
        bind_addr.l2_cid = 0
        bind_addr.l2_bdaddr_type = BDADDR_LE_PUBLIC
        ret = libc.bind(fd, ctypes.byref(bind_addr), ctypes.sizeof(bind_addr))
        if ret < 0:
            errno = ctypes.get_errno()
            raise OSError(errno, f"bind() failed: {os.strerror(errno)}")

        # Set BT_SECURITY_LOW for LE CoC
        sec = _bt_security()
        sec.level = BT_SECURITY_LOW
        sec.key_size = 0
        ret = libc.setsockopt(
            fd, SOL_BLUETOOTH, BT_SECURITY,
            ctypes.byref(sec), ctypes.sizeof(sec),
        )
        if ret < 0:
            errno = ctypes.get_errno()
            raise OSError(errno, f"setsockopt(BT_SECURITY) failed: {os.strerror(errno)}")

        # Set non-blocking for connect with timeout
        import fcntl
        flags = fcntl.fcntl(fd, fcntl.F_GETFL)
        fcntl.fcntl(fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)

        # Connect to the remote device
        conn_addr = _sockaddr_l2()
        conn_addr.l2_family = AF_BLUETOOTH
        conn_addr.l2_psm = psm  # __le16: host byte order on LE machines
        conn_addr.l2_bdaddr = (ctypes.c_ubyte * 6)(*_bdaddr_bytes(bdaddr))
        conn_addr.l2_cid = 0
        conn_addr.l2_bdaddr_type = addr_type

        ret = libc.connect(fd, ctypes.byref(conn_addr), ctypes.sizeof(conn_addr))
        if ret < 0:
            errno = ctypes.get_errno()
            import errno as errno_module
            if errno != errno_module.EINPROGRESS:
                raise OSError(errno, f"connect() failed: {os.strerror(errno)}")

            # Wait for connection with select
            _, wlist, xlist = select.select([], [fd], [fd], timeout)
            if not wlist and not xlist:
                raise TimeoutError(f"L2CAP connect to {bdaddr} PSM {psm} timed out")

            # Check socket error
            SO_ERROR = socket.SO_ERROR
            err = ctypes.c_int(0)
            err_len = ctypes.c_uint(ctypes.sizeof(err))
            libc.getsockopt(fd, socket.SOL_SOCKET, SO_ERROR, ctypes.byref(err), ctypes.byref(err_len))
            if err.value != 0:
                raise OSError(err.value, f"L2CAP connect failed: {os.strerror(err.value)}")

        # Restore blocking mode
        fcntl.fcntl(fd, fcntl.F_SETFL, flags)

        # Wrap the raw fd in a Python socket object.  socket.fromfd() dups
        # the fd, so we close our original afterward.
        sock = socket.fromfd(fd, AF_BLUETOOTH, socket.SOCK_SEQPACKET, BTPROTO_L2CAP)

    except BaseException:
        libc.close(fd)
        raise
    else:
        libc.close(fd)  # fromfd() duped it

    return sock


# ---------------------------------------------------------------------------
# Public API — matches macOS signature
# ---------------------------------------------------------------------------

def connect_l2cap(
    service_uuid: str,
    psm: int,
    timeout: float = 5.0,
    peer_uuid: Optional[str] = None,
) -> socket.socket:
    """Discover a BLE device by GATT service UUID and open an L2CAP CoC channel.

    Returns a standard Python ``socket.socket`` (SEQPACKET over L2CAP).
    The same URL ``ecuconnect-l2cap://FFF1:129`` works — discovery resolves
    the BD_ADDR automatically via GATT service UUID scan.

    Parameters
    ----------
    service_uuid:
        Short (``"FFF1"``) or full 128-bit GATT service UUID.
    psm:
        L2CAP PSM (Protocol/Service Multiplexer) number.
    timeout:
        Discovery + connection timeout in seconds.
    peer_uuid:
        Optional BD_ADDR filter (substring match, e.g. ``"DC:DA:0C"``).
    """
    debug = os.getenv("ECUCONNECT_DEBUG_IO", "").strip().lower() not in {
        "", "0", "false", "off",
    }

    if debug:
        print(
            f"[l2cap:linux] discovering service_uuid={service_uuid} "
            f"psm={psm} peer={peer_uuid}",
            file=sys.stderr, flush=True,
        )

    discovery_deadline = time.monotonic() + timeout
    bdaddr, addr_type = _discover_device(
        service_uuid,
        timeout=timeout,
        peer_filter=peer_uuid,
    )

    if debug:
        at_name = "random" if addr_type == BDADDR_LE_RANDOM else "public"
        print(
            f"[l2cap:linux] discovered {bdaddr} (type={at_name})",
            file=sys.stderr, flush=True,
        )

    remaining = max(1.0, discovery_deadline - time.monotonic())
    sock = _l2cap_connect(bdaddr, addr_type, psm, timeout=remaining)

    if debug:
        print(
            f"[l2cap:linux] connected to {bdaddr} PSM {psm}",
            file=sys.stderr, flush=True,
        )

    return sock
