# ecuconnect-tool (Python)

Python CLI for ECUconnect using libCANyonero bindings. The default transport is WiFi/TCP at `192.168.42.42:129` (override with `--endpoint` for mocks or other adapters).

## Build + install

```bash
python -m pip install -e ./python/ecuconnect_tool
```

## Quick start

```bash
ecuconnect-tool info
ecuconnect-tool ping 512 --count 10
ecuconnect-tool benchmark --count 32
ecuconnect-tool term 500000 --proto passthrough
ecuconnect-tool monitor --bitrate 500000
ecuconnect-tool send "02 3E 80" --tx-id 0x123 --rx-id 0x321
ecuconnect-tool test --can-interface can0 --busload 1 --duration 5
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

- L2CAP/BLE transport is not implemented yet on Linux.
- Socket buffers default to 4 MiB; override with `--rx-buffer/--tx-buffer`.
