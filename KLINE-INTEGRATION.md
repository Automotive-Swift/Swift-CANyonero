## K-Line Integration Plan for ECOS

Goal: mirror ISOTP integration by using the shared K-Line transceiver for framing (TX/RX) while keeping ESPenlaub’s synchronous K-Line transport for now.

### 1) Channel representation
- Add K-Line protocol mode to `Channel` (kwp vs iso9141).
- Store optional TX chunk size (default 4, matches current VIN framing) and ISO tester byte (default 0xF1).
- Keep default target/source derived from arbitration (0x33/0xF1).

### 2) Outbound path
- In `Channel::send` for K-Line:
  - ISO mode: build frame with `makeIso9141Frame(target, source, tester, payload)`, then hand raw bytes to ESPenlaub `sendFrameWithResponse`.
  - KWP mode: if payload ≤ 0x0F, use `makeKwpFrame`; otherwise use `makeKwpFrames(target, source, payload, 0x80, chunkSize)` to emit a sequence. Send frames one by one via ESPenlaub transport; insert delays only if needed (channel separation time available).

### 3) Inbound path
- Replace/wrap `KWPDecoder` with shared K-Line transceiver:
  - `splitFrames(raw, mode)` to slice the echo-stripped buffer from ESPenlaub.
  - Feed frames into `Transceiver` (constructed with mode + expected target/source). On `process`, return payload to `ProtocolMachine`. On `protocolViolation`, log/drop. On `waitForMore`, finalize after windows exhausted.

### 4) Transport glue (ESPenlaub K-Line)
- Keep `sendFrameWithResponse` for now; it handles echo stripping and response windows.
- Use returned response target/source to build response ID (as today). Ensure `txBuf` is large enough (current 64 bytes OK for multi-frame).
- Leave echo/timeout handling in ESPenlaub; framing now lives in shared K-Line.

### 5) API/config exposure
- When opening a `.kline` channel, accept: mode (kwp/iso9141), target/source, tester byte (ISO), chunk size (optional).
- Persist in `Channel` and use in TX/RX paths above.

### 6) Regression/testing
- Unit: reuse K-Line builder/splitter tests; add firmware-level tests for VIN multi-frame and ISO9141 PID via `decodeStream`.
- Bench: run VIN and ISO9141 PID against known ECU/adapter; verify payload matches host-side decode.
- Ensure CAN/ISOTP channels remain unchanged.

### Risks/notes
- ESPenlaub transport is synchronous; if moved to async later, framing stays compatible (stateless per frame).
- Builders currently handle short-format KWP; extend for extended-length/functional addressing if needed.
