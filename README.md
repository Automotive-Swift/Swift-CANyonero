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
* **Commands** – `PING`, `REQUEST INFO`, `READ VOLTAGE`, and the automotive commands (`OPEN`, `CLOSE`, `SEND`, `SET ARBITRATION`, `START/END PERIODIC`, `SEND COMPRESSED`) follow the `TYP` codes defined in `Sources/libCANyonero/include/Protocol.hpp`. Firmware update helpers (`PREPARE/SEND/COMMIT`) and RPC (`RPC CALL`, `RPC SEND BINARY`) live in their own ranges.
* **Replies** – Positive replies start at `0x80` (`OK`, `PONG`, `INFO`, `VOLTAGE`) and channel events (`channelOpened`, `received`, `periodicMessageStarted`, …) occupy `0xB0–0xD1`. Errors use `0xE0+` to differentiate protocol violations, hardware failures, invalid channel handles, and missing responses.
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

## Tools

`Sources/ecuconnect-tool` provides an interactive CLI for working with CANyonero hardware from macOS/Linux terminals. Launch it with:

```bash
swift run ecuconnect-tool --help
swift run ecuconnect-tool term --channel-protocol isotp --addressing :7df,7e8
```

The tool uses the Swift bindings in this package so it is a convenient way to validate PDUs, poll ECU information, or keep a periodic tester-present running without writing your own app.

The `benchmark` subcommand uses ECUconnect `ping`, which echoes the full payload and verifies it byte-for-byte; reported bandwidth is round-trip (payload out + payload back).

## PDU Examples

### Request Information

`1F 10 0000` – Request device information.

### Open Channel

`1F 30 0005 00 0007A120 02` – Open ISOTP channel w/ bitrate `500000`, `0 ms` separation for RX, `2 ms` for TX.

### Send Periodic Message

`1F 35 0012 02 000007E0 00 000007E8 FFFFFFFF 00 023E80` – Send every 1 second `023E80` (*UDS Tester Present, do not send response*) to `0x7E0`.

### Stop Periodic Message

`1F 36 0001 00` – Stop periodic message with handle `0`.
