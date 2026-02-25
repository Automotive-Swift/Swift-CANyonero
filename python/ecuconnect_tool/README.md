# ecuconnect-tool (Python)

Python CLI for ECUconnect using libCANyonero bindings. The default transport is WiFi/TCP at `192.168.42.42:129` (override with `--endpoint` for mocks or other adapters).

## Build + install

```bash
python -m pip install -e ./python/ecuconnect_tool
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

## macOS BLE/L2CAP

Install CoreBluetooth bindings:

```bash
python3 -m pip install pyobjc-framework-CoreBluetooth
```

Use the ECUconnect L2CAP endpoint format:

```bash
ecuconnect-tool --url ecuconnect-l2cap://FFF1:129 info
```

Optionally target a specific peripheral UUID:

```bash
ecuconnect-tool --url ecuconnect-l2cap://FFF1:129/12345678-1234-1234-1234-123456789abc info
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

- `ecuconnect-l2cap://` is supported on macOS via CoreBluetooth (L2CAP).
- Linux and Windows currently support TCP endpoints only.
- Socket buffers default to `4M`; override with `--rx-buffer/--tx-buffer` using bytes or `K/M/G` suffixes.
- Set `ECUCONNECT_DEBUG_IO=1` to print raw TX/RX frame traces while debugging transport issues.
