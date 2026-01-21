# ECUconnect L2CAP Profile Driver

## ⚠️ DEPRECATED - DO NOT USE ⚠️

**This driver is abandoned and causes BSOD. Do not install.**

### Reasons for Deprecation

1. **Fundamental Architecture Flaw**: The driver is installed as a root-enumerated software device but attempts to query the Bluetooth DDI profile interface via `WdfFdoQueryForInterface()`. This fails because root-enumerated devices have no hardware parent in the device stack. A real Bluetooth profile driver must attach to the Bluetooth stack PDO.

2. **Critical Memory Bugs**: Multiple use-after-free bugs in async BRB handling (`WdfObjectDelete(memory)` called while BRBs are still in flight) cause heap corruption.

3. **BSOD on Boot**: Installing this driver causes Blue Screen of Death during Windows startup due to the above issues.

4. **No User-Mode L2CAP on Windows**: Windows does not expose raw L2CAP PSM connections to user-mode. Only GATT (BLE) and RFCOMM (Classic) are available via WinRT APIs. Custom L2CAP PSMs require a properly architected kernel profile driver with WHQL signing.

### Recommended Alternatives

- **TCP Transport** (Recommended): Use `transport_tcp.cpp` which connects to ECUconnect over Wi-Fi at `192.168.42.42:129`. This is the primary supported transport.

- **BLE GATT Transport**: For BLE-capable devices, use `transport_gatt.cpp` with C++/WinRT APIs.

---

## Original Documentation (Historical)

A Windows kernel-mode driver that provides L2CAP communication with ECUconnect devices using the CANyonero protocol (PSM 129).

## Overview

Windows does not expose BLE L2CAP Connection-Oriented Channels (CoC) at the application level. The L2CAP APIs are only available through kernel-mode Bluetooth profile drivers. This driver bridges that gap by providing a simple device interface that user-mode applications can use for read/write operations.

### Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    User-Mode (J2534 DLL)                        │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │              transport_l2cap.cpp                            ││
│  │  - SetupDiGetClassDevs() to find driver devices             ││
│  │  - CreateFile() to open device handle                       ││
│  │  - DeviceIoControl() for connect/disconnect                 ││
│  │  - ReadFile()/WriteFile() for data transfer                 ││
│  └───────────────────────────┬─────────────────────────────────┘│
└──────────────────────────────│──────────────────────────────────┘
                               │ Device Interface
                               │ {E7B3C5A1-4D2F-4E8B-9A1C-3F5D7E9B2C4A}
┌──────────────────────────────▼──────────────────────────────────┐
│                  Kernel-Mode Driver                             │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │           ecuconnect_l2cap.sys (KMDF)                       ││
│  │                                                             ││
│  │  IOCTLs:                                                    ││
│  │    IOCTL_ECUCONNECT_CONNECT   - Connect to device           ││
│  │    IOCTL_ECUCONNECT_DISCONNECT - Disconnect                 ││
│  │    IOCTL_ECUCONNECT_GET_STATUS - Query status               ││
│  │                                                             ││
│  │  Read/Write: BRB_L2CA_ACL_TRANSFER                          ││
│  └───────────────────────────┬─────────────────────────────────┘│
└──────────────────────────────│──────────────────────────────────┘
                               │ BRB (Bluetooth Request Block)
                               ▼
┌─────────────────────────────────────────────────────────────────┐
│              Windows Bluetooth Stack                            │
│  bthport.sys → bthusb.sys                                       │
└─────────────────────────────────────────────────────────────────┘
```

## Building the Driver

### Prerequisites

1. **Windows Driver Kit (WDK)**
   - Download from: https://docs.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk
   - Requires WDK 10.0.19041.0 or later

2. **Visual Studio 2019/2022**
   - With "Desktop development with C++" workload
   - Windows Driver Kit Visual Studio Extension

### Build Steps

1. Open `ecuconnect_l2cap.sln` in Visual Studio
2. Select configuration (Debug/Release) and platform (x64/ARM64)
3. Build → Build Solution

Or from command line:
```powershell
msbuild ecuconnect_l2cap.sln /p:Configuration=Release /p:Platform=x64
```

### Output Files

- `ecuconnect_l2cap.sys` - The driver binary
- `ecuconnect_l2cap.inf` - Installation information
- `ecuconnect_l2cap.cat` - Catalog file (after signing)

## Installation

### Test Signing (Development)

1. Enable test signing:
   ```powershell
   bcdedit /set testsigning on
   # Reboot required
   ```

2. Install the driver:
   ```powershell
   pnputil /add-driver ecuconnect_l2cap.inf /install
   ```

### Production Signing

For production deployment, the driver must be signed with an EV code signing certificate and submitted to Microsoft for attestation signing.

## Usage

### From User-Mode Application

```cpp
#include "../driver/public.h"
#include <windows.h>
#include <setupapi.h>

// Find and open the driver device
HDEVINFO deviceInfoSet = SetupDiGetClassDevs(
    &GUID_DEVINTERFACE_ECUCONNECT_L2CAP,
    NULL, NULL,
    DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
);

// ... enumerate and get device path ...

HANDLE device = CreateFile(
    devicePath,
    GENERIC_READ | GENERIC_WRITE,
    0, NULL, OPEN_EXISTING,
    FILE_FLAG_OVERLAPPED, NULL
);

// Connect to ECUconnect device
ECUCONNECT_CONNECT_REQUEST request = {
    .BluetoothAddress = 0x001122334455,  // ECUconnect BT address
    .Psm = 129,                          // CANyonero PSM
    .TimeoutMs = 10000
};

DeviceIoControl(device, IOCTL_ECUCONNECT_CONNECT,
                &request, sizeof(request),
                NULL, 0, &bytesReturned, NULL);

// Send data
WriteFile(device, data, dataLen, &bytesWritten, NULL);

// Receive data
ReadFile(device, buffer, bufferSize, &bytesRead, NULL);

// Disconnect
DeviceIoControl(device, IOCTL_ECUCONNECT_DISCONNECT,
                NULL, 0, NULL, 0, &bytesReturned, NULL);

CloseHandle(device);
```

### From J2534 DLL

The `transport_l2cap.cpp` implementation handles all of the above automatically. Simply use:

```cpp
#include "transport.h"

auto transport = ecuconnect::createL2capTransport({
    .deviceNameOrAddress = "00:11:22:33:44:55",
    .psm = 129
});

if (transport->connect()) {
    transport->send({0x1F, 0x01, ...});
    auto response = transport->receive(1000);
}
```

## IOCTLs Reference

| IOCTL | Code | Input | Output | Description |
|-------|------|-------|--------|-------------|
| CONNECT | 0x80002000 | ECUCONNECT_CONNECT_REQUEST | - | Connect to device |
| DISCONNECT | 0x80002004 | - | - | Disconnect |
| GET_STATUS | 0x80002008 | - | ECUCONNECT_STATUS | Query status |
| SET_CONFIG | 0x8000200C | ECUCONNECT_CONFIG | - | Configure |
| ENUMERATE | 0x80002010 | - | ECUCONNECT_ENUMERATE_RESPONSE | List devices |

## Debugging

### Enable Debug Output

```powershell
# Enable verbose kernel debug output
reg add "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Debug Print Filter" /v DEFAULT /t REG_DWORD /d 0xFFFFFFFF /f
```

### View Debug Messages

Use DbgView or WinDbg:
```
kd> ed nt!Kd_DEFAULT_Mask 0xFFFFFFFF
kd> g
```

### Common Issues

1. **Driver not loading**: Check test signing is enabled
2. **Connection timeout**: Ensure ECUconnect is paired and in range
3. **Access denied**: Run application as Administrator

## References

- [Bluetooth L2CAP Profile Driver](https://learn.microsoft.com/en-us/windows-hardware/drivers/bluetooth/creating-a-l2cap-client-connection-to-a-remote-device)
- [Bluetooth Echo Sample](https://github.com/microsoft/windows-driver-samples/tree/main/bluetooth/bthecho)
- [KMDF Driver Development](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/)
