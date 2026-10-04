# Swift-CANyonero

Swift bindings for the CANyonero CAN/ENET adapter stack. The package contains the `libCANyonero` core, Objective‑C and Swift facades, and the `ecuconnect-tool` CLI that can talk to production hardware.

## Hardware

Our proprietary CAN(fd)/ENET adapter is currently in pre-production and will be announced soon.

## Software

The adapter runs CANyonerOS (FreeRTOS based) and exposes the CANyonero protocol over BLE/WiFi. The Swift package implements the full protocol encoder/decoder, transport helpers, and a collection of utilities that can be embedded in diagnostics tooling.

## Protocol Overview

* **PDU framing** – Every frame on the wire follows `[ ATT:UInt8 | TYP:UInt8 | LEN:UInt16 | payload… ]`. `ATT` is fixed to `0x1F`, `TYP` defines the command/response, and `LEN` is the payload in bytes (max `0xFFFF`). The maximum PDU size is therefore `0x10003`.
* **Channel protocols** – Logical channels can transport `raw` CAN, `isotp`, `kline`, `can_fd`, `isotp_fd`, or `enet` payloads. `openChannel` picks the protocol, bitrate, and the RX/TX separation times (encoded as nibble values mapping to 0 µs, 100 µs, …, 5000 µs).
* **Addressing and arbitration** – `setArbitration` configures request/reply IDs, reply masks, and optional CAN extended addressing bytes. Each logical channel therefore retains its own addressing context which the adapter automatically applies for subsequent `send` PDUs.
* **Commands** – `PING`, `REQUEST INFO`, `READ VOLTAGE`, and the automotive commands (`OPEN`, `CLOSE`, `SEND`, `SET ARBITRATION`, `START/END PERIODIC`, `SEND COMPRESSED`) follow the `TYP` codes defined in `Sources/libCANyonero/include/Protocol.hpp`. Firmware update helpers (`PREPARE/SEND/COMMIT/RESET`) and RPC (`RPC CALL`, `RPC SEND BINARY`) live in their own ranges.
* **Replies** – Positive replies start at `0x80` (`OK`, `PONG`, `INFO`, `VOLTAGE`) and channel events (`channelOpened`, `received`, `periodicMessageStarted`, …) occupy `0xB0–0xD1`. Firmware lifecycle commands are acknowledged with generic `OK` (`0x80`) rather than dedicated update-specific reply PDUs. Errors use `0xE0+` to differentiate protocol violations, hardware failures, invalid channel handles, and missing responses.
* **Typical flow** – `requestInfo` gathers adapter metadata, `openChannel` with `setArbitration` creates the diagnostic link, `send`/`received` PDUs transport payloads, and `startPeriodicMessage` automates tester-present traffic. Most production stacks keep a control channel (for RPC/update) and one or more transport channels in parallel.

### Command and response ranges

| Range | Purpose | Notes |
| --- | --- | --- |
| `0x10–0x12` | Ping & adapter telemetry | `ping`, `requestInfo`, `readVoltage` |
| `0x30–0x37` | Automotive transport | Channel lifecycle, arbitration, periodic jobs, and compressed payloads |
| `0x40–0x43` | Firmware lifecycle | Update state machine and device resets |
| `0x50–0x51` | RPC | JSON RPC payloads and binary attachments |
| `0x80–0x92` | Generic replies | `ok`, `pong`, hardware info, and voltage readings |
| `0xB0–0xD1` | Channel replies | Open/close confirmation, received payloads, periodic message ack, RPC responses |
| `0xE0–0xEF` | Error classes | Distinguishes unspecified, hardware, invalid-channel, periodic, missing-response, invalid RPC, and invalid command errors |

You can explore the exact field names and helper factories inside `Protocol.hpp`, which mirrors the documentation above in code form.

### Streaming ISO-TP transmission

Embedded transports should use `Transceiver::didReceiveFrameStreaming()` or
`TransceiverFD::didReceiveFrameStreaming()` when handling a flow-control frame.
The callback receives each consecutive frame as soon as it is encoded, together
with the negotiated separation time and a flag indicating whether another frame
follows in the current FC window. This lets a controller queue the first CF
immediately instead of waiting for the complete block-size window to be
materialized. The original `didReceiveFrame()` API remains compatible and
collects the same frames for clients that require an owning action object.

## Windows J2534 Driver

The `Sources/ecuconnect-j2534` directory contains a Windows J2534 (Pass-Thru) driver that enables any J2534-compatible application to communicate with ECUconnect hardware. This includes professional diagnostic tools, flash programmers, and custom applications.

### Features

- **SAE J2534-1 API v04.04** compliance for broad application compatibility
- **Dual architecture**: Both 32-bit (`ecuconnect32.dll`) and 64-bit (`ecuconnect64.dll`) DLLs
- **Raw CAN** protocol with pass-all filtering for diagnostic communication
- **CANyonero protocol** over TCP (connects to ECUconnect at `192.168.42.42:129`)
- **Single-channel constraint** matching ECUconnect hardware capabilities

### Quick Start (Windows, Administrator required)

```bash
cd Sources/ecuconnect-j2534
make install      # Build, install to Program Files, register in Windows Registry
```

The driver registers at:
- `HKLM\SOFTWARE\PassThruSupport.04.04\ECUconnect` (for 64-bit applications)
- `HKLM\SOFTWARE\WOW6432Node\PassThruSupport.04.04\ECUconnect` (for 32-bit applications)

See `Sources/ecuconnect-j2534/README.md` for detailed build instructions, API usage examples, and architecture documentation.

## Linux SocketCAN Bridge

The `Sources/ecuconnect-socketcan` directory contains a Linux user-space bridge that exposes ECUconnect over SocketCAN for local tools (`candump`, `cansend`, `isotpdump`, ...).

### Quick Start (Linux)

```bash
cd Sources/ecuconnect-socketcan
make vcan-up
make run ENDPOINT=192.168.42.42:129 CAN_IF=vcan0
```

Useful targets include:

- `make wifi-connect-sudo` to connect to the first available ECUconnect SSID
- `make run-fd` for CAN FD mode
- `make up` for one-shot Wi-Fi connect + `vcan` setup + bridge start

See `Sources/ecuconnect-socketcan/README.md` for full options and deployment notes (`vcan` vs `canX`).

## Tools

`Sources/ecuconnect-tool` provides an interactive CLI for working with CANyonero hardware from macOS/Linux terminals. Launch it with:

```bash
swift run ecuconnect-tool --help
swift run ecuconnect-tool term --channel-protocol isotp --addressing :7df,7e8
swift run ecuconnect-tool login --port 4242
```

For VW TP2.0 diagnostics, `term --proto tp20` now runs the fixed-ID setup handshake against target `0x01` by default, matching the common `TPOPEN` workflow. Use `--tp20-target` to select a different ECU target, or `--no-tp20-setup` to skip fixed-ID setup and attach only to an already-known dynamic TP2.0 channel.

The tool uses the Swift bindings in this package so it is a convenient way to validate PDUs, poll ECU information, or keep a periodic tester-present running without writing your own app.

The `benchmark` subcommand uses ECUconnect `ping`, which echoes the full payload and verifies it byte-for-byte; reported bandwidth is round-trip (payload out + payload back).

Python users can also use the rebuilt CLI in `python/ecuconnect_tool` (default WiFi endpoint `192.168.42.42:129`, override with `--endpoint` for mocks):

```bash
python -m pip install -e ./python/ecuconnect_tool
ecuconnect-tool info
ecuconnect-tool --url ecuconnect-l2cap://FFF1:129 info
ecuconnect-tool login
ecuconnect-tool term 500000 --proto tp20
ecuconnect-tool test --can-interface can0 --busload 20 --duration 5
```

For VW TP2.0, `ecuconnect-tool term --proto tp20` performs the fixed-ID setup handshake for target `0x01` by default. Override the target with `--tp20-target 0xNN`, or pass `--no-tp20-setup` if you want to skip setup and work with negotiated dynamic CAN IDs manually.

macOS BLE/L2CAP endpoints use the scheme `ecuconnect-l2cap://<service-uuid>:<psm>`, for example `ecuconnect-l2cap://FFF1:129`.  
An optional peripheral UUID selector can be appended as the URL path: `ecuconnect-l2cap://FFF1:129/<peripheral-uuid>`.

Python CLI socket buffers default to `4M` and accept friendly sizes (`200`, `10K`, `4M`, `1G`) via `--rx-buffer` and `--tx-buffer`.

## Health diagnostics (Swift CLI)

With health-enabled ECOS firmware, the Swift `ecuconnect-tool` reads diagnostics
over its existing BLE L2CAP or TCP connection:

```sh
ecuconnect-tool health
ecuconnect-tool health --watch --interval 1 --count 10 --json
ecuconnect-tool health --url ecuconnect-tcp://192.168.42.42:129
ecuconnect-tool diagnostics export --output ./adapter-health
ecuconnect-tool diagnostics export --include-coredumps --output ./adapter-crashes
```

The default endpoint is `ecuconnect-l2cap://FFF1:129`. Health displays internal heap
free/minimum/largest block, PSRAM, radio ownership and the retained before/after
samples of the last BLE and network shutdown. Values are bytes and KiB. A watch
holds the protocol connection until its count is reached or Ctrl-C is pressed.
`--json` writes one JSON object per sample; errors go to stderr.

Export creates a new directory with `health.json`, paged `events.json`, and a final
`manifest.json`. Optional crash dumps download in bounded chunks and remain on the
device. An interrupted dump retains a `.partial` suffix; an incomplete bundle has
no manifest. The snapshot contains the exact firmware ELF hash for symbol matching.

Opening BLE stops WiFi/network in ECOS; opening TCP stops BLE. Both return after the
configured quiet period (default ten seconds). The historical stop samples preserve
the opposite transport's measurement. Current free heap includes protocol buffers;
the immediate shutdown measurement precedes their allocation. This is runtime
internal heap, not linked instruction RAM. Measurements reset on adapter reboot.

## PDU Examples

### Request Information

`1F 11 0000` – Request device information.

### Open Channel

`1F 30 0005 00 0007A120 02` – Open ISOTP channel w/ bitrate `500000`, `0 ms` separation for RX, `2 ms` for TX.

### Send Periodic Message

`1F 35 0012 02 000007E0 00 000007E8 FFFFFFFF 00 023E80` – Send every 1 second `023E80` (*UDS Tester Present, do not send response*) to `0x7E0`.

### Stop Periodic Message

`1F 36 0001 00` – Stop periodic message with handle `0`.

## Diagnostic request/response benchmark (macOS)

The S31 debug firmware provides a synthetic diagnostic command: small request,
one complete response, then the next request. No vehicle bus is accessed.
Build and run the Swift tool from this checkout:

```sh
make benchmark-diagnostic
```

Defaults: serial `FFFEF3`, 32 samples per combination, output
`/tmp/s31-diagnostic-macos.json`. Override with `BENCHMARK_SERIAL`,
`BENCHMARK_COUNT` or `BENCHMARK_OUTPUT` as Make variables.
The equivalent direct command is:

```sh
swift run -c release ecuconnect-tool benchmark --diagnostic \
  --expected-serial FFFEF3 -n 32 \
  --output /tmp/s31-diagnostic-macos.json
```

The default endpoint is `ecuconnect-l2cap://FFF1:129`. The first peripheral
advertising that service is selected, as with the existing tool. The serial
check happens before benchmark requests; if another adapter was selected,
disconnect/switch it off and retry. No explicit CoreBluetooth UUID is required.
The ordinary `benchmark` command continues to measure PING echo throughput.

The diagnostic mode uses requests of 8/32 bytes and responses of
32/256/1024/4096 bytes, excluding the four-byte protocol header. It verifies the
response token and every pattern byte. It uses five seconds of unmeasured
warmup (`--warmup`) and 32 samples per combination (`-n`). Debug firmware with
`CONFIG_ECOS_DIAGNOSTIC_BENCHMARK` is required; unsupported firmware fails clearly.

The table reports median/p95 first receive and complete response times, plus
response-only KiB/s (including the 12-byte timing metadata). JSON retains all
samples, firmware dispatch/preparation times, identity and actual peer UUID.
First receive means the first positive read in the CoreBluetooth input stream
callback, not the first radio byte. The complete time ends at the read delivering
the full frame. Payload validation happens outside the timed interval. Firmware
dispatch excludes time waiting in the native callback queue. These definitions
match the Python diagnostic runner in the firmware repository.

macOS controls the link parameters. This command neither forces nor promises
2M PHY. Correlate the firmware's `BLELink` logs (PHY, interval, DLE and SDU MTU)
with each run before comparing throughput. A PING echo rate counts both payload
directions; this diagnostic rate counts only the response.
