from __future__ import annotations

import os
import socket
import sys
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
        def initWithService_psm_peer_(self, service_uuid: str, psm: int, peer_uuid: Optional[str]):  # noqa: N802
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

        @objc.python_method
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

        @objc.python_method
        def _finish_error(self, exc: Exception) -> None:
            if self._done.is_set():
                return
            self._error = exc
            self._done.set()

        @objc.python_method
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

    class _StreamDelegate(Foundation.NSObject):  # type: ignore[misc]
        def initWithSocket_(self, socket_obj):  # noqa: N802
            self = objc.super(_StreamDelegate, self).init()
            if self is None:
                return None
            self._socket = socket_obj
            return self

        def stream_handleEvent_(self, stream, event):  # noqa: N802
            self._socket._on_stream_event(stream, int(event))


class MacOSL2CAPSocket:
    def __init__(self, manager, peripheral, channel, input_stream, output_stream) -> None:
        self._manager = manager
        self._peripheral = peripheral
        self._channel = channel
        self._input_stream = input_stream
        self._output_stream = output_stream
        self._closed = False
        self._lock = threading.Lock()
        self._timeout: Optional[float] = None
        self._debug = os.getenv("ECUCONNECT_DEBUG_IO", "").strip().lower() not in {"", "0", "false", "off"}
        self._space_event = threading.Event()
        self._bytes_event = threading.Event()
        self._delegate = None
        self._streams_opened = False
        if objc is not None and Foundation is not None:
            self._delegate = _StreamDelegate.alloc().initWithSocket_(self)
            self._input_stream.setDelegate_(self._delegate)
            self._output_stream.setDelegate_(self._delegate)

    def _log(self, direction: str, payload: bytes) -> None:
        if not self._debug:
            return
        preview = payload[:64].hex(" ")
        suffix = " ..." if len(payload) > 64 else ""
        print(f"[l2cap:{direction}] {len(payload)} bytes: {preview}{suffix}", file=sys.stderr, flush=True)

    def _log_text(self, text: str) -> None:
        if not self._debug:
            return
        print(f"[l2cap:debug] {text}", file=sys.stderr, flush=True)

    def _status_name(self, status: int) -> str:
        names = {
            int(Foundation.NSStreamStatusNotOpen): "not-open",
            int(Foundation.NSStreamStatusOpening): "opening",
            int(Foundation.NSStreamStatusOpen): "open",
            int(Foundation.NSStreamStatusReading): "reading",
            int(Foundation.NSStreamStatusWriting): "writing",
            int(Foundation.NSStreamStatusAtEnd): "at-end",
            int(Foundation.NSStreamStatusClosed): "closed",
            int(Foundation.NSStreamStatusError): "error",
        }
        return names.get(status, str(status))

    def _event_name(self, event: int) -> str:
        names = {
            int(Foundation.NSStreamEventNone): "none",
            int(Foundation.NSStreamEventOpenCompleted): "openCompleted",
            int(Foundation.NSStreamEventHasBytesAvailable): "hasBytesAvailable",
            int(Foundation.NSStreamEventHasSpaceAvailable): "hasSpaceAvailable",
            int(Foundation.NSStreamEventErrorOccurred): "errorOccurred",
            int(Foundation.NSStreamEventEndEncountered): "endEncountered",
        }
        return names.get(event, str(event))

    def _on_stream_event(self, stream, event: int) -> None:
        if stream == self._output_stream and event == int(Foundation.NSStreamEventHasSpaceAvailable):
            self._space_event.set()
        if stream == self._input_stream and event == int(Foundation.NSStreamEventHasBytesAvailable):
            self._bytes_event.set()
        if self._debug:
            side = "out" if stream == self._output_stream else "in"
            status = int(stream.streamStatus())
            self._log_text(f"event[{side}] {self._event_name(event)} status={self._status_name(status)}")

    def settimeout(self, value: Optional[float]) -> None:
        self._timeout = value

    def open(self, timeout: float = 2.0) -> None:
        if self._streams_opened:
            return

        run_loop = Foundation.NSRunLoop.currentRunLoop()
        self._input_stream.scheduleInRunLoop_forMode_(run_loop, Foundation.NSDefaultRunLoopMode)
        self._input_stream.scheduleInRunLoop_forMode_(run_loop, Foundation.NSRunLoopCommonModes)
        self._output_stream.scheduleInRunLoop_forMode_(run_loop, Foundation.NSDefaultRunLoopMode)
        self._output_stream.scheduleInRunLoop_forMode_(run_loop, Foundation.NSRunLoopCommonModes)
        self._input_stream.open()
        self._output_stream.open()

        deadline = time.monotonic() + max(0.0, timeout)
        while True:
            in_status = int(self._input_stream.streamStatus())
            out_status = int(self._output_stream.streamStatus())
            if in_status == int(Foundation.NSStreamStatusError):
                error = self._input_stream.streamError()
                raise OSError(str(error) if error is not None else "BLE input stream open failed")
            if out_status == int(Foundation.NSStreamStatusError):
                error = self._output_stream.streamError()
                raise OSError(str(error) if error is not None else "BLE output stream open failed")

            in_ready = in_status in (
                int(Foundation.NSStreamStatusOpen),
                int(Foundation.NSStreamStatusReading),
                int(Foundation.NSStreamStatusWriting),
            )
            out_ready = out_status in (
                int(Foundation.NSStreamStatusOpen),
                int(Foundation.NSStreamStatusReading),
                int(Foundation.NSStreamStatusWriting),
            )
            if in_ready and out_ready:
                self._streams_opened = True
                return

            if time.monotonic() >= deadline:
                raise socket.timeout(
                    "timed out while opening BLE streams "
                    f"(in={self._status_name(in_status)}, out={self._status_name(out_status)})"
                )
            run_loop.runUntilDate_(Foundation.NSDate.dateWithTimeIntervalSinceNow_(0.01))

    def recv(self, size: int) -> bytes:
        if self._closed:
            return b""
        timeout = self._timeout
        deadline = None if timeout is None else (time.monotonic() + max(0.0, timeout))
        run_loop = Foundation.NSRunLoop.currentRunLoop()

        while True:
            status = int(self._input_stream.streamStatus())
            if status == int(Foundation.NSStreamStatusAtEnd):
                return b""
            if status == int(Foundation.NSStreamStatusError):
                error = self._input_stream.streamError()
                raise OSError(str(error) if error is not None else "BLE input stream error")

            can_read = bool(self._input_stream.hasBytesAvailable()) or status in (
                int(Foundation.NSStreamStatusOpen),
                int(Foundation.NSStreamStatusReading),
            )
            if can_read:
                count, data = self._input_stream.read_maxLength_(None, max(1, size))
                if count > 0:
                    chunk = bytes(data[:count])
                    self._log("rx", chunk)
                    return chunk
                if count < 0:
                    error = self._input_stream.streamError()
                    raise OSError(str(error) if error is not None else "BLE input stream read failed")
                # count == 0 can be either temporary no-data or EOF; defer to stream status above.

            if timeout == 0.0:
                raise socket.timeout("timed out")
            if deadline is not None and time.monotonic() >= deadline:
                raise socket.timeout("timed out")

            run_loop.runUntilDate_(Foundation.NSDate.dateWithTimeIntervalSinceNow_(0.01))

    def sendall(self, data: bytes) -> None:
        with self._lock:
            offset = 0
            timeout = self._timeout
            deadline = None if timeout is None else (time.monotonic() + max(0.0, timeout))
            last_wait_log = 0.0
            while offset < len(data):
                status = int(self._output_stream.streamStatus())
                if status == int(Foundation.NSStreamStatusError):
                    error = self._output_stream.streamError()
                    raise OSError(str(error) if error is not None else "BLE output stream error")
                if status in (
                    int(Foundation.NSStreamStatusAtEnd),
                    int(Foundation.NSStreamStatusClosed),
                    int(Foundation.NSStreamStatusNotOpen),
                ):
                    raise OSError("BLE output stream is not writable")

                written = int(self._output_stream.write_maxLength_(data[offset:], len(data) - offset))
                if written > 0:
                    self._log("tx", data[offset:offset + written])
                    offset += written
                    continue
                if written < 0:
                    error = self._output_stream.streamError()
                    raise OSError(str(error) if error is not None else "BLE output stream write failed")

                if deadline is not None and time.monotonic() >= deadline:
                    raise socket.timeout(
                        f"timed out waiting for BLE output stream writable (status={self._status_name(status)})"
                    )
                error = self._output_stream.streamError()
                if error is not None:
                    raise OSError(str(error))
                now = time.monotonic()
                if now - last_wait_log >= 0.2:
                    self._log_text(
                        f"write returned 0; status={self._status_name(status)} hasSpace={bool(self._output_stream.hasSpaceAvailable())}"
                    )
                    last_wait_log = now
                self._space_event.clear()
                Foundation.NSRunLoop.currentRunLoop().runUntilDate_(
                    Foundation.NSDate.dateWithTimeIntervalSinceNow_(0.01)
                )

    def shutdown(self, how: int) -> None:
        _ = how

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        try:
            pass
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

    connector = _Connector.alloc().initWithService_psm_peer_(service_uuid, psm, peer_uuid)
    if connector is None:
        raise RuntimeError("Unable to initialize CoreBluetooth connector.")
    channel = connector.connect(timeout)
    input_stream = channel.inputStream()
    output_stream = channel.outputStream()
    if input_stream is None or output_stream is None:
        raise RuntimeError("BLE L2CAP channel has no usable streams.")
    socket_obj = MacOSL2CAPSocket(connector.manager, connector.peripheral, channel, input_stream, output_stream)
    socket_obj.open(timeout=max(1.0, timeout))
    return socket_obj
