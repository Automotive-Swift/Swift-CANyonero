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
ecuconnect-tool term 500000 --proto tp20
ecuconnect-tool --url ecuconnect-l2cap://FFF1:129 term 500000 --proto raw
ecuconnect-tool monitor --bitrate 500000
ecuconnect-tool send "02 3E 80" --tx-id 0x123 --rx-id 0x321
ecuconnect-tool test --can-interface can0 --busload 1 --duration 5
ecuconnect-tool update firmware.bin
```

## Firmware update

`ecuconnect-tool update <image>` uploads a firmware image and reboots the
adapter:

```
Connected to ECUconnect: ACME ECUconnect (1.2.3).
Reported system voltage is 12.60V.
Uploading firmware.bin (245760 bytes) in 4000-byte chunks.
Uploading complete, resetting hardware.
Connected to ECUconnect: ACME ECUconnect (1.3.0).
```

Every chunk is acknowledged before the next one is sent, and a refusal from the
adapter aborts the upload instead of running into the timeout. Use
`--chunk-size` if the transport needs smaller PDUs, and `--reconnect-delay` if
the adapter takes longer than three seconds to come back.

Do not interrupt an update in progress: the image is written as it arrives, so
an adapter that is reset midway needs a repeat update before it is usable.

## TP2.0 terminal behavior

`ecuconnect-tool term --proto tp20` performs the fixed-ID TP2.0 setup handshake against target `0x01` by default, which matches the common VAG `TPOPEN` flow.

Use these options when you need different behavior:

- `--tp20-target 0xNN` to negotiate with a different TP2.0 target address.
- `--no-tp20-setup` to skip fixed-ID setup and open only the dynamic TP2.0 channel.
- `--tp20-reply-id` and `--tp20-application-type` to override the tester-side setup parameters.

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

## Health diagnostics

Health-enabled S3 firmware (including 0.9.536) exposes schema 1 through the
normal BLE/L2CAP or TCP connection. Older firmware, including 0.5.x and 0.8.1,
does not provide this RPC: these commands report an error and close their
connection; other commands do not acquire a new health requirement.

```bash
ecuconnect-tool-py health
ecuconnect-tool-py health --json
ecuconnect-tool-py health --watch --interval 1 --count 10 --json
ecuconnect-tool-py --url ecuconnect-l2cap://FFF1:129 health
ecuconnect-tool-py diagnostics export --output ./adapter-health
```

When installed from this Python package alone, its executable is named
`ecuconnect-tool`; the combined installation uses `ecuconnect-tool-py` for
Python and `ecuconnect-tool` for Swift.

Health reports heap free/minimum/largest block, PSRAM, radio ownership, retained
shutdown samples, firmware ELF hash, allocation failures and crash-dump count.
`--json` writes unmodified schema-1 snapshots as JSON Lines, without connection
banners. A watch reuses one connection and keeps an S3 adapter awake until its
count is reached or Ctrl-C is pressed. A single sample closes the connection.

Opening a BLE protocol connection on S3 stops WiFi/Ethernet; TCP stops BLE.
For S31 management, explicitly use
`--url ecuconnect-l2cap://FFF3:131/<peripheral-uuid>`. It uses the same default
health schema 1 without taking diagnostic ownership. Do not fall back to
diagnosis if management is unavailable.

History export requires `system.health.events` on the chosen endpoint (the S3
ECOS diagnostic endpoint provides it; S31 management currently does not).
Export creates a new directory containing `health.json`, frozen paged
`events.json` and, only on completion, `manifest.json`. It never deletes
device evidence or overwrites an existing directory. Python export does not
download core dumps; the Swift variant supports `--include-coredumps`.
