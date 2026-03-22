/**
 * ECUconnect L2CAP Profile Driver - Public Interface
 *
 * This header defines the interface between the user-mode J2534 DLL and
 * the kernel-mode L2CAP profile driver.
 *
 * Copyright (c) ECUconnect Project
 */

#ifndef ECUCONNECT_L2CAP_PUBLIC_H
#define ECUCONNECT_L2CAP_PUBLIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <initguid.h>

/**
 * Device Interface GUID
 * User-mode applications use this GUID with SetupDiGetClassDevs to enumerate
 * ECUconnect L2CAP driver instances.
 */
// {E7B3C5A1-4D2F-4E8B-9A1C-3F5D7E9B2C4A}
DEFINE_GUID(GUID_DEVINTERFACE_ECUCONNECT_L2CAP,
    0xe7b3c5a1, 0x4d2f, 0x4e8b, 0x9a, 0x1c, 0x3f, 0x5d, 0x7e, 0x9b, 0x2c, 0x4a);

/**
 * ECUconnect L2CAP Protocol Constants
 */
#define ECUCONNECT_L2CAP_PSM            129     // CANyonero protocol PSM
#define ECUCONNECT_L2CAP_MTU            1024    // Maximum transfer unit

/**
 * IOCTL Definitions
 *
 * Control codes for driver communication:
 * - CONNECT: Initiate L2CAP connection to specified Bluetooth address
 * - DISCONNECT: Close the L2CAP channel
 * - GET_STATUS: Query connection status
 * - SET_CONFIG: Configure connection parameters
 */

#define FILE_DEVICE_ECUCONNECT  0x8000  // Custom device type

#define IOCTL_ECUCONNECT_CONNECT \
    CTL_CODE(FILE_DEVICE_ECUCONNECT, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_ECUCONNECT_DISCONNECT \
    CTL_CODE(FILE_DEVICE_ECUCONNECT, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_ECUCONNECT_GET_STATUS \
    CTL_CODE(FILE_DEVICE_ECUCONNECT, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_ECUCONNECT_SET_CONFIG \
    CTL_CODE(FILE_DEVICE_ECUCONNECT, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_ECUCONNECT_ENUMERATE_DEVICES \
    CTL_CODE(FILE_DEVICE_ECUCONNECT, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)

/**
 * Connection Status
 */
typedef enum _ECUCONNECT_CONNECTION_STATE {
    EcuConnectionUninitialized = 0,
    EcuConnectionConnecting,
    EcuConnectionConnected,
    EcuConnectionDisconnecting,
    EcuConnectionDisconnected,
    EcuConnectionFailed
} ECUCONNECT_CONNECTION_STATE;

/**
 * Connect Request Structure
 * Used with IOCTL_ECUCONNECT_CONNECT
 */
#pragma pack(push, 1)
typedef struct _ECUCONNECT_CONNECT_REQUEST {
    ULONGLONG BluetoothAddress;     // 6-byte BT address as 64-bit value
    USHORT Psm;                     // Protocol/Service Multiplexer (default: 129)
    USHORT Mtu;                     // Maximum Transfer Unit (default: 1024)
    ULONG TimeoutMs;                // Connection timeout in milliseconds
} ECUCONNECT_CONNECT_REQUEST, *PECUCONNECT_CONNECT_REQUEST;
#pragma pack(pop)

/**
 * Connection Status Structure
 * Used with IOCTL_ECUCONNECT_GET_STATUS
 */
#pragma pack(push, 1)
typedef struct _ECUCONNECT_STATUS {
    ECUCONNECT_CONNECTION_STATE State;
    ULONGLONG RemoteAddress;        // Connected device address
    USHORT NegotiatedMtu;           // Actual MTU after negotiation
    ULONG BytesSent;                // Statistics
    ULONG BytesReceived;
    ULONG LastErrorCode;            // Last error if any
} ECUCONNECT_STATUS, *PECUCONNECT_STATUS;
#pragma pack(pop)

/**
 * Configuration Structure
 * Used with IOCTL_ECUCONNECT_SET_CONFIG
 */
#pragma pack(push, 1)
typedef struct _ECUCONNECT_CONFIG {
    USHORT Psm;                     // L2CAP PSM to use
    USHORT Mtu;                     // Requested MTU
    ULONG ConnectTimeoutMs;         // Connection timeout
    ULONG ReadTimeoutMs;            // Read operation timeout
    BOOLEAN AutoReconnect;          // Auto-reconnect on disconnect
} ECUCONNECT_CONFIG, *PECUCONNECT_CONFIG;
#pragma pack(pop)

/**
 * Device Enumeration Entry
 * Used with IOCTL_ECUCONNECT_ENUMERATE_DEVICES
 */
#pragma pack(push, 1)
typedef struct _ECUCONNECT_DEVICE_INFO {
    ULONGLONG BluetoothAddress;     // Device BT address
    WCHAR DeviceName[64];           // Device friendly name
    UCHAR ClassOfDevice[3];         // Bluetooth CoD
    BOOLEAN IsPaired;               // Whether device is paired
    BOOLEAN SupportsL2cap;          // Whether L2CAP PSM 129 is available
} ECUCONNECT_DEVICE_INFO, *PECUCONNECT_DEVICE_INFO;
#pragma pack(pop)

/**
 * Device Enumeration Response
 */
#pragma pack(push, 1)
typedef struct _ECUCONNECT_ENUMERATE_RESPONSE {
    ULONG DeviceCount;
    ECUCONNECT_DEVICE_INFO Devices[1];  // Variable length array
} ECUCONNECT_ENUMERATE_RESPONSE, *PECUCONNECT_ENUMERATE_RESPONSE;
#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif // ECUCONNECT_L2CAP_PUBLIC_H
