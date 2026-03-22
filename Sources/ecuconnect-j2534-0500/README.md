# ECUconnect J2534 05.00 Driver

This directory contains the SAE J2534 API `05.00` driver line for ECUconnect.

It is intended to coexist with the legacy `04.04` driver under [`/Sources/ecuconnect-j2534`](C:\Users\DrMic\Documents\late\Automotive-Swift\Swift-CANyonero\Sources\ecuconnect-j2534).

## Packaging

The expected shipped DLL set is:

- `ecuconnect32.dll` for J2534 `04.04` 32-bit
- `ecuconnect64.dll` for J2534 `04.04` 64-bit
- `ecuconnect050032.dll` for J2534 `05.00` 32-bit
- `ecuconnect050064.dll` for J2534 `05.00` 64-bit

The two API versions should be registered independently:

- `HKLM\SOFTWARE\PassThruSupport.04.04\ECUconnect`
- `HKLM\SOFTWARE\WOW6432Node\PassThruSupport.04.04\ECUconnect`
- `HKLM\SOFTWARE\PassThruSupport.05.00\ECUconnect`
- `HKLM\SOFTWARE\WOW6432Node\PassThruSupport.05.00\ECUconnect`

## Implemented 05.00 Surface

- `PassThruScanForDevices`
- `PassThruGetNextDevice`
- `PassThruOpen`
- `PassThruClose`
- `PassThruConnect`
- `PassThruDisconnect`
- `PassThruLogicalConnect`
- `PassThruLogicalDisconnect`
- `PassThruSelect`
- `PassThruReadMsgs`
- `PassThruQueueMsgs`
- `PassThruStartPeriodicMsg`
- `PassThruStopPeriodicMsg`
- `PassThruStartMsgFilter`
- `PassThruStopMsgFilter`
- `PassThruReadVersion`
- `PassThruGetLastError`
- `PassThruIoctl`

## Protocol Coverage

- `CAN`
- `CAN_FD_PS`
- `ISO9141`
- `ISO14230`
- `ISO15765` logical channels over CAN
- `ISO15765_FD_PS` logical channels over CAN-FD

The implementation reuses the existing CANyonero protocol and transport code from the `04.04` driver instead of duplicating the adapter communication layer.

## Introspection IOCTLs

This driver exposes `05.00`-style discovery/introspection through:

- `GET_DEVICE_INFO`
- `GET_PROTOCOL_INFO`
- `GET_RESOURCE_INFO`

## Notes

- This workspace currently lacks a discoverable C++ compiler toolchain on `PATH`, so the new target has been prepared structurally but still needs a compile/test pass in a configured MSVC or MinGW environment.
- The next cleanup step should be extracting the shared helper logic used by `04.04` and `05.00` into common subfiles so both driver lines stay aligned.
