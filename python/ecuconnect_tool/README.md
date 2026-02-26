# ecuconnect-tool (Python)

Python CLI for ECUconnect using libCANyonero bindings. The default transport is WiFi/TCP at `192.168.42.42:129` (override with `--endpoint` for mocks or other adapters).

## Install

From PyPI (TCP transport only):

```bash
pipx install ecuconnect-tool
```

With BLE/L2CAP support on Linux (needs system `python3-dbus` and `python3-gi`):

```bash
sudo apt install python3-dbus python3-gi
pipx install --system-site-packages ecuconnect-tool
```

From the repo (editable, for development):

```bash
python3 -m pip install -e ./python/ecuconnect_tool
```

## Quick start

```bash
ecuconnect-tool info
ecuconnect-tool login
ecuconnect-tool ping 512 --count 10
ecuconnect-tool benchmark --count 32
ecuconnect-tool term 500000 --proto raw
ecuconnect-tool --url ecuconnect-l2cap://FFF1:129 term 500000 --proto raw
ecuconnect-tool monitor --bitrate 500000
ecuconnect-tool send "02 3E 80" --tx-id 0x123 --rx-id 0x321
ecuconnect-tool test --can-interface can0 --busload 1 --duration 5
```

## BLE/L2CAP

Use the ECUconnect L2CAP endpoint format (works on both macOS and Linux):

```bash
ecuconnect-tool --url ecuconnect-l2cap://FFF1:129 info
```

### macOS

Install CoreBluetooth bindings:

```bash
python3 -m pip install pyobjc-framework-CoreBluetooth
```

Optionally target a specific peripheral UUID:

```bash
ecuconnect-tool --url ecuconnect-l2cap://FFF1:129/12345678-1234-1234-1234-123456789abc info
```

### Linux

See install section above for dependencies. Discovery uses the BlueZ D-Bus API; the L2CAP connection uses the kernel socket API directly.

Optionally filter by BD_ADDR:

```bash
ecuconnect-tool --url ecuconnect-l2cap://FFF1:129/DC:DA:0C:3A:E3:06 info
```

## Dev flow (no install)

This builds the native extension in-place and runs the CLI from the repo:

```bash
python3 -m pip install --user pybind11 typer rich python-can
python3 ./python/ecuconnect_tool/scripts/run_dev.py test --can-interface can0 --busload 20 --duration 5
```

Auto ramp mode (start at 1 pps, step 1 pps until max busload):

```bash
ecuconnect-tool test --can-interface can0 --busload auto --auto-max-busload 1
```

Preflight info + ping happens before the load test by default. Disable with `--preflight false`.
Use `--preflight-only` to stop after the connectivity checks (no CAN open).
Use `--traffic none` to open the CAN channel + set arbitration, then exit without data transfer.
Use `--traffic rx` for CAN->ECU only, `--traffic tx` for ECU->CAN only.

## Notes

- `ecuconnect-l2cap://` is supported on macOS (CoreBluetooth) and Linux (BlueZ + kernel L2CAP).
- Windows currently supports TCP endpoints only.
- Socket buffers default to `4M`; override with `--rx-buffer/--tx-buffer` using bytes or `K/M/G` suffixes.
- Set `ECUCONNECT_DEBUG_IO=1` to print raw TX/RX frame traces while debugging transport issues.
