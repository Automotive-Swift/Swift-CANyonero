# ECUconnect J2534 Driver

A Windows J2534 (Pass-Thru) driver for ECUconnect devices using the CANyonero protocol.

## Features

- **J2534-1 API v04.04** compliance
- **Dual architecture**: Both 32-bit and 64-bit DLLs
- **Raw CAN** protocol support
- **Transport abstraction** for TCP (with future BLE/L2CAP support)
- **Modern C++20** implementation

## Building

### Prerequisites

- CMake 3.16+
- Visual Studio 2019+ or MinGW-w64 with C++20 support
- Windows SDK

### Using Make (MSYS2/Git Bash)

```bash
make release        # Build both 32-bit and 64-bit
make release32      # Build 32-bit only
make release64      # Build 64-bit only
make debug          # Build debug versions
make clean          # Clean all builds
make rebuild        # Clean + build
```

### Using CMake Directly

```bash
# 32-bit build
cmake -S . -B build32 -A Win32 -DDLL_NAME=ecuconnect32
cmake --build build32 --config Release

# 64-bit build
cmake -S . -B build64 -A x64 -DDLL_NAME=ecuconnect64
cmake --build build64 --config Release
```

## Installation

J2534 drivers must be registered in the Windows Registry to be discoverable by applications.

### Quick Install (Recommended)

**Option 1: Using Make** (requires Administrator terminal)
```bash
make install        # Build both, install, and register
make uninstall      # Unregister and remove all
```

**Option 2: Using PowerShell** (Run as Administrator)
```powershell
.\scripts\install.ps1               # Install both 32 and 64-bit
.\scripts\install.ps1 -Only32       # Install 32-bit only
.\scripts\install.ps1 -Only64       # Install 64-bit only
.\scripts\install.ps1 -Uninstall    # Uninstall
```

**Option 3: Using Batch Files** (Right-click -> Run as Administrator)
- `scripts\install.bat` - Install driver
- `scripts\uninstall.bat` - Uninstall driver

### What Gets Installed

The installer performs the following:

1. **Copies DLLs** to `C:\Program Files\ECUconnect\J2534\`:
   - `ecuconnect32.dll` - For 32-bit applications
   - `ecuconnect64.dll` - For 64-bit applications

2. **Creates registry entries**:
   - `HKLM\SOFTWARE\PassThruSupport.04.04\ECUconnect` -> `ecuconnect64.dll`
   - `HKLM\SOFTWARE\WOW6432Node\PassThruSupport.04.04\ECUconnect` -> `ecuconnect32.dll`

### Registry Details

On 64-bit Windows, applications read from different registry paths based on their architecture:

| Application | Registry Path | DLL |
|-------------|---------------|-----|
| 64-bit apps | `HKLM\SOFTWARE\PassThruSupport.04.04\ECUconnect` | ecuconnect64.dll |
| 32-bit apps | `HKLM\SOFTWARE\WOW6432Node\PassThruSupport.04.04\ECUconnect` | ecuconnect32.dll |

Required registry values:

| Value | Type | Description |
|-------|------|-------------|
| `Name` | REG_SZ | Display name |
| `Vendor` | REG_SZ | Vendor name |
| `FunctionLibrary` | REG_SZ | **Full path** to DLL (architecture-specific!) |
| `CAN` | REG_DWORD | 1 if CAN supported |
| `ISO15765` | REG_DWORD | 1 if ISO-TP supported |
| `ISO9141` | REG_DWORD | 1 if K-Line ISO9141 supported |
| `ISO14230` | REG_DWORD | 1 if K-Line ISO14230 supported |

### Useful Make Targets

```bash
make help           # Show all targets
make status         # Check registration status
make list-drivers   # List all installed J2534 drivers
make dev-register   # Register from build dirs (development)
make register       # Register installed DLLs
make unregister     # Remove registry entries only
```

## Usage

The driver connects to ECUconnect devices via TCP at the default address `192.168.42.42:129`.

### Connection String

Pass a connection string to `PassThruOpen()` to override defaults:
- `NULL` or empty: Uses default `192.168.42.42:129`
- `"host:port"`: Connects to specified host and port
- `"host"`: Connects to specified host on default port 129

### Example

```c
#include "j2534.h"

unsigned long deviceId, channelId;

// Open device (default connection)
PassThruOpen(NULL, &deviceId);

// Connect CAN channel at 500kbps
PassThruConnect(deviceId, CAN, 0, 500000, &channelId);

// Set up a pass filter for all messages
PASSTHRU_MSG maskMsg = {0}, patternMsg = {0};
maskMsg.ProtocolID = CAN;
maskMsg.DataSize = 4;
patternMsg.ProtocolID = CAN;
patternMsg.DataSize = 4;

unsigned long filterId;
PassThruStartMsgFilter(channelId, PASS_FILTER, &maskMsg, &patternMsg, NULL, &filterId);

// Send a CAN message
PASSTHRU_MSG txMsg = {0};
txMsg.ProtocolID = CAN;
txMsg.DataSize = 12;  // 4-byte ID + 8-byte data
txMsg.Data[0] = 0x00; txMsg.Data[1] = 0x00;
txMsg.Data[2] = 0x07; txMsg.Data[3] = 0xDF;  // ID = 0x7DF
txMsg.Data[4] = 0x02; txMsg.Data[5] = 0x01;  // OBD2 request
txMsg.Data[6] = 0x00; // ...

unsigned long numMsgs = 1;
PassThruWriteMsgs(channelId, &txMsg, &numMsgs, 1000);

// Read responses
PASSTHRU_MSG rxMsg[10];
numMsgs = 10;
PassThruReadMsgs(channelId, rxMsg, &numMsgs, 1000);

// Cleanup
PassThruDisconnect(channelId);
PassThruClose(deviceId);
```

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     J2534 Application                       │
│                    (32-bit or 64-bit)                       │
├─────────────────────────────────────────────────────────────┤
│            ecuconnect32.dll / ecuconnect64.dll              │
├─────────────────────────────────────────────────────────────┤
│                    j2534_api.cpp                            │
│                 (J2534 API Exports)                         │
├─────────────────────────────────────────────────────────────┤
│                    ecuconnect.cpp                           │
│               (Device/Channel Manager)                      │
├─────────────────────────────────────────────────────────────┤
│                canyonero_protocol.cpp                       │
│                (CANyonero Protocol)                         │
├─────────────────────────────────────────────────────────────┤
│                  transport_tcp.cpp                          │
│                  (TCP Transport)                            │
├─────────────────────────────────────────────────────────────┤
│                    TCP/IP Stack                             │
└─────────────────────────────────────────────────────────────┘
```

## Protocol

The driver communicates with ECUconnect devices using the CANyonero protocol:

- **Wire format**: `[ATT:0x1F | TYP:UInt8 | LEN:UInt16 | payload...]`
- **Big-endian** byte order for multi-byte values
- **Default port**: 129

### Supported Commands

| Command | Code | Description |
|---------|------|-------------|
| OpenChannel | 0x30 | Open a CAN channel |
| CloseChannel | 0x31 | Close a channel |
| Send | 0x33 | Send CAN frame |
| SetArbitration | 0x34 | Set CAN IDs |

## Supported Protocols

| Protocol | Status |
|----------|--------|
| CAN (Raw) | Supported |
| ISO 15765 | Not implemented |
| ISO 9141 | Not implemented |
| ISO 14230 | Not implemented |
| J1850 VPW | Not implemented |
| J1850 PWM | Not implemented |

## File Structure

```
ecuconnect-j2534/
├── CMakeLists.txt              # CMake build configuration
├── Makefile                    # Make targets for build/install
├── README.md                   # This file
├── include/
│   ├── j2534.h                 # J2534 API definitions
│   ├── transport.h             # Transport abstraction
│   ├── canyonero_protocol.h    # Protocol layer
│   └── ecuconnect.h            # Device manager
├── src/
│   ├── j2534_api.cpp           # J2534 exports
│   ├── ecuconnect.cpp          # Device manager
│   ├── ecuconnect.def          # DLL export definitions
│   ├── canyonero_protocol.cpp  # Protocol implementation
│   └── transport_tcp.cpp       # TCP transport
└── scripts/
    ├── install.ps1             # PowerShell installer
    ├── install.bat             # Batch wrapper
    ├── uninstall.bat           # Batch uninstaller
    ├── register.reg.template   # Registry template
    └── unregister.reg          # Registry removal
```

## Future Enhancements

- BLE/L2CAP transport support
- ISO-TP (ISO 15765) protocol
- K-Line protocols (ISO 9141/14230)

## License

MIT License
