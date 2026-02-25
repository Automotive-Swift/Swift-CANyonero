from __future__ import annotations

import os
import socket
import threading
import time
from typing import Optional

try:
    import objc  # type: ignore
    import CoreBluetooth  # type: ignore
    import CoreFoundation  # type: ignore
    import Foundation  # type: ignore
except Exception:
    objc = None
    CoreBluetooth = None
    CoreFoundation = None
    Foundation = None


def _missing_runtime_error() -> RuntimeError:
    return RuntimeError(
        "BLE/L2CAP on macOS requires PyObjC CoreBluetooth bindings. "
        "Install with: pip install pyobjc-framework-CoreBluetooth"
    )


if objc is not None and CoreBluetooth is not None and Foundation is not None:

    class _Connector(Foundation.NSObject):  # type: ignore[misc]
        def initWithServicePSMPeer_(self, service_uuid: str, psm: int, peer_uuid: Optional[str]):  # noqa: N802
            self = objc.super(_Connector, self).init()
            if self is None:
                return None
            self._service_uuid = CoreBluetooth.CBUUID.UUIDWithString_(service_uuid)
            self._psm = int(psm)
            self._peer_uuid = peer_uuid.lower() if peer_uuid else None
            self._done = threading.Event()
            self._error: Optional[Exception] = None
            self._manager = None
            self._peripheral = None
            self._channel = None
            self._manager = CoreBluetooth.CBCentralManager.alloc().initWithDelegate_queue_(self, None)
            return self

        @property
        def manager(self):
            return self._manager

        @property
        def peripheral(self):
            return self._peripheral

        @property
        def channel(self):
            return self._channel

        def connect(self, timeout: float):
            run_loop = Foundation.NSRunLoop.currentRunLoop()
            deadline = time.monotonic() + timeout
            while not self._done.is_set() and time.monotonic() < deadline:
                run_loop.runUntilDate_(Foundation.NSDate.dateWithTimeIntervalSinceNow_(0.05))
            if not self._done.is_set():
                self._finish_error(TimeoutError("timed out while opening BLE/L2CAP channel"))
            if self._error is not None:
                raise self._error
            return self._channel

        def _finish_error(self, exc: Exception) -> None:
            if self._done.is_set():
                return
            self._error = exc
            self._done.set()

        def _finish_channel(self, channel) -> None:
            if self._done.is_set():
                return
            self._channel = channel
            self._done.set()

        # CBCentralManagerDelegate
        def centralManagerDidUpdateState_(self, central):  # noqa: N802
            state = int(central.state())
            if state == int(CoreBluetooth.CBManagerStatePoweredOn):
                options = {CoreBluetooth.CBCentralManagerScanOptionAllowDuplicatesKey: False}
                central.scanForPeripheralsWithServices_options_([self._service_uuid], options)
                return
            if state in (
                int(CoreBluetooth.CBManagerStateUnsupported),
                int(CoreBluetooth.CBManagerStateUnauthorized),
                int(CoreBluetooth.CBManagerStatePoweredOff),
            ):
                self._finish_error(RuntimeError(f"Bluetooth unavailable (state={state})"))

        def centralManager_didDiscoverPeripheral_advertisementData_RSSI_(  # noqa: N802
            self, central, peripheral, _advertisement_data, _rssi
        ):
            if self._peer_uuid is not None:
                identifier = str(peripheral.identifier().UUIDString()).lower()
                if identifier != self._peer_uuid:
                    return
            self._peripheral = peripheral
            central.stopScan()
            central.connectPeripheral_options_(peripheral, None)

        def centralManager_didConnectPeripheral_(self, _central, peripheral):  # noqa: N802
            self._peripheral = peripheral
            peripheral.setDelegate_(self)
            peripheral.discoverServices_([self._service_uuid])

        def centralManager_didFailToConnectPeripheral_error_(self, _central, _peripheral, error):  # noqa: N802
            message = str(error) if error is not None else "unknown error"
            self._finish_error(RuntimeError(f"BLE connection failed: {message}"))

        # CBPeripheralDelegate
        def peripheral_didDiscoverServices_(self, peripheral, error):  # noqa: N802
            if error is not None:
                self._finish_error(RuntimeError(f"BLE service discovery failed: {error}"))
                return
            peripheral.openL2CAPChannel_(self._psm)

        def peripheral_didOpenL2CAPChannel_error_(self, _peripheral, channel, error):  # noqa: N802
            if error is not None:
                self._finish_error(RuntimeError(f"L2CAP open failed: {error}"))
                return
            if channel is None:
                self._finish_error(RuntimeError("L2CAP open failed: channel is nil"))
                return
            self._finish_channel(channel)


class MacOSL2CAPSocket:
    def __init__(self, sock: socket.socket, manager, peripheral, input_stream, output_stream) -> None:
        self._sock = sock
        self._manager = manager
        self._peripheral = peripheral
        self._input_stream = input_stream
        self._output_stream = output_stream
        self._closed = False
        self._lock = threading.Lock()

    def settimeout(self, value: Optional[float]) -> None:
        self._sock.settimeout(value)

    def recv(self, size: int) -> bytes:
        return self._sock.recv(size)

    def sendall(self, data: bytes) -> None:
        with self._lock:
            self._sock.sendall(data)

    def shutdown(self, how: int) -> None:
        self._sock.shutdown(how)

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        try:
            self._sock.close()
        finally:
            try:
                self._input_stream.close()
            except Exception:
                pass
            try:
                self._output_stream.close()
            except Exception:
                pass
            try:
                if self._manager is not None and self._peripheral is not None:
                    self._manager.cancelPeripheralConnection_(self._peripheral)
            except Exception:
                pass


def connect_l2cap(service_uuid: str, psm: int, timeout: float = 5.0, peer_uuid: Optional[str] = None):
    if objc is None or CoreBluetooth is None or Foundation is None:
        raise _missing_runtime_error()

    connector = _Connector.alloc().initWithServicePSMPeer_(service_uuid, psm, peer_uuid)
    if connector is None:
        raise RuntimeError("Unable to initialize CoreBluetooth connector.")
    channel = connector.connect(timeout)
    input_stream = channel.inputStream()
    output_stream = channel.outputStream()
    if input_stream is None or output_stream is None:
        raise RuntimeError("BLE L2CAP channel has no usable streams.")
    input_stream.open()
    output_stream.open()

    native = input_stream.propertyForKey_(CoreFoundation.kCFStreamPropertySocketNativeHandle)
    if native is None:
        native = output_stream.propertyForKey_(CoreFoundation.kCFStreamPropertySocketNativeHandle)
    if native is None:
        raise RuntimeError("BLE L2CAP stream did not expose a socket file descriptor.")

    fd = int(native)
    dup_fd = os.dup(fd)
    sock = socket.socket(fileno=dup_fd)
    return MacOSL2CAPSocket(sock, connector.manager, connector.peripheral, input_stream, output_stream)
