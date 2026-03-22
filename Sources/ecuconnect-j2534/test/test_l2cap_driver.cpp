/**
 * L2CAP Driver Test Program
 *
 * Tests the ECUconnect L2CAP kernel driver interface:
 * 1. Opens the driver device interface
 * 2. Queries driver status
 * 3. Optionally connects to a device and sends/receives data
 *
 * Usage:
 *   test_l2cap_driver.exe                    # Just test driver interface
 *   test_l2cap_driver.exe AA:BB:CC:DD:EE:FF  # Connect to device
 */

#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#pragma comment(lib, "setupapi.lib")

// Device interface GUID (must match driver's public.h)
DEFINE_GUID(GUID_DEVINTERFACE_ECUCONNECT_L2CAP,
    0x8c2d9e1a, 0x5f3b, 0x4d7c, 0xa1, 0x2e, 0x6f, 0x8d, 0x4b, 0x9c, 0x3e, 0x7f);

// IOCTL codes (must match driver's public.h)
#define FILE_DEVICE_ECUCONNECT  0x8000
#define IOCTL_ECUCONNECT_CONNECT      CTL_CODE(FILE_DEVICE_ECUCONNECT, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ECUCONNECT_DISCONNECT   CTL_CODE(FILE_DEVICE_ECUCONNECT, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ECUCONNECT_GET_STATUS   CTL_CODE(FILE_DEVICE_ECUCONNECT, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Structures (must match driver's public.h)
#pragma pack(push, 1)
typedef struct _ECUCONNECT_CONNECT_REQUEST {
    uint64_t BluetoothAddress;
    uint16_t Psm;
    uint32_t TimeoutMs;
} ECUCONNECT_CONNECT_REQUEST;

typedef struct _ECUCONNECT_STATUS {
    uint32_t State;
    uint64_t RemoteAddress;
    uint16_t Psm;
    uint16_t OutMtu;
    uint16_t InMtu;
    uint32_t BytesSent;
    uint32_t BytesReceived;
} ECUCONNECT_STATUS;
#pragma pack(pop)

// Connection states
const char* StateNames[] = {
    "Uninitialized",
    "Connecting",
    "Connected",
    "Disconnecting",
    "Disconnected",
    "Failed"
};

// Parse MAC address string "AA:BB:CC:DD:EE:FF" to uint64_t
bool ParseMacAddress(const char* str, uint64_t* out) {
    unsigned int parts[6];
    if (sscanf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
               &parts[0], &parts[1], &parts[2], &parts[3], &parts[4], &parts[5]) != 6) {
        return false;
    }

    *out = 0;
    for (int i = 0; i < 6; i++) {
        *out = (*out << 8) | (uint8_t)parts[i];
    }
    return true;
}

// Format MAC address for display
void FormatMacAddress(uint64_t addr, char* buf) {
    sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X",
            (unsigned)((addr >> 40) & 0xFF),
            (unsigned)((addr >> 32) & 0xFF),
            (unsigned)((addr >> 24) & 0xFF),
            (unsigned)((addr >> 16) & 0xFF),
            (unsigned)((addr >> 8) & 0xFF),
            (unsigned)(addr & 0xFF));
}

// Find and open the driver device interface
HANDLE OpenDriverInterface() {
    HDEVINFO deviceInfo;
    SP_DEVICE_INTERFACE_DATA interfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W detailData = NULL;
    DWORD requiredSize;
    HANDLE handle = INVALID_HANDLE_VALUE;

    // Get device information set
    deviceInfo = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_ECUCONNECT_L2CAP,
        NULL,
        NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );

    if (deviceInfo == INVALID_HANDLE_VALUE) {
        printf("SetupDiGetClassDevs failed: %lu\n", GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    // Enumerate interfaces
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    if (!SetupDiEnumDeviceInterfaces(deviceInfo, NULL, &GUID_DEVINTERFACE_ECUCONNECT_L2CAP, 0, &interfaceData)) {
        DWORD err = GetLastError();
        if (err == ERROR_NO_MORE_ITEMS) {
            printf("No ECUconnect L2CAP device interface found.\n");
            printf("The driver may be installed but no Bluetooth adapter is present,\n");
            printf("or the driver hasn't created the device interface yet.\n");
        } else {
            printf("SetupDiEnumDeviceInterfaces failed: %lu\n", err);
        }
        SetupDiDestroyDeviceInfoList(deviceInfo);
        return INVALID_HANDLE_VALUE;
    }

    // Get interface detail size
    SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData, NULL, 0, &requiredSize, NULL);

    detailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)malloc(requiredSize);
    if (!detailData) {
        printf("Memory allocation failed\n");
        SetupDiDestroyDeviceInfoList(deviceInfo);
        return INVALID_HANDLE_VALUE;
    }

    detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

    // Get interface detail
    if (!SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData, detailData, requiredSize, NULL, NULL)) {
        printf("SetupDiGetDeviceInterfaceDetail failed: %lu\n", GetLastError());
        free(detailData);
        SetupDiDestroyDeviceInfoList(deviceInfo);
        return INVALID_HANDLE_VALUE;
    }

    printf("Device path: %ls\n", detailData->DevicePath);

    // Open the device
    handle = CreateFileW(
        detailData->DevicePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (handle == INVALID_HANDLE_VALUE) {
        printf("CreateFile failed: %lu\n", GetLastError());
    }

    free(detailData);
    SetupDiDestroyDeviceInfoList(deviceInfo);
    return handle;
}

// Query driver status
bool GetDriverStatus(HANDLE handle, ECUCONNECT_STATUS* status) {
    DWORD bytesReturned;

    if (!DeviceIoControl(
            handle,
            IOCTL_ECUCONNECT_GET_STATUS,
            NULL, 0,
            status, sizeof(*status),
            &bytesReturned,
            NULL)) {
        printf("IOCTL_ECUCONNECT_GET_STATUS failed: %lu\n", GetLastError());
        return false;
    }

    return true;
}

// Connect to a device
bool ConnectToDevice(HANDLE handle, uint64_t btAddress, uint16_t psm, uint32_t timeoutMs) {
    ECUCONNECT_CONNECT_REQUEST request;
    DWORD bytesReturned;

    request.BluetoothAddress = btAddress;
    request.Psm = psm;
    request.TimeoutMs = timeoutMs;

    char macStr[18];
    FormatMacAddress(btAddress, macStr);
    printf("Connecting to %s PSM %d (timeout %dms)...\n", macStr, psm, timeoutMs);

    if (!DeviceIoControl(
            handle,
            IOCTL_ECUCONNECT_CONNECT,
            &request, sizeof(request),
            NULL, 0,
            &bytesReturned,
            NULL)) {
        printf("IOCTL_ECUCONNECT_CONNECT failed: %lu\n", GetLastError());
        return false;
    }

    printf("Connect request sent successfully!\n");
    return true;
}

// Disconnect from device
bool DisconnectFromDevice(HANDLE handle) {
    DWORD bytesReturned;

    if (!DeviceIoControl(
            handle,
            IOCTL_ECUCONNECT_DISCONNECT,
            NULL, 0,
            NULL, 0,
            &bytesReturned,
            NULL)) {
        printf("IOCTL_ECUCONNECT_DISCONNECT failed: %lu\n", GetLastError());
        return false;
    }

    printf("Disconnected.\n");
    return true;
}

// Print status
void PrintStatus(const ECUCONNECT_STATUS* status) {
    char macStr[18];
    FormatMacAddress(status->RemoteAddress, macStr);

    const char* stateName = (status->State < 6) ? StateNames[status->State] : "Unknown";

    printf("\n=== Driver Status ===\n");
    printf("  State:          %s (%d)\n", stateName, status->State);
    printf("  Remote Address: %s\n", macStr);
    printf("  PSM:            %d\n", status->Psm);
    printf("  MTU (out/in):   %d / %d\n", status->OutMtu, status->InMtu);
    printf("  Bytes sent:     %u\n", status->BytesSent);
    printf("  Bytes received: %u\n", status->BytesReceived);
    printf("=====================\n\n");
}

// Simple read/write test
bool TestReadWrite(HANDLE handle) {
    // CANyonero protocol: send a simple ping/status request
    // PDU format: [ATT:1][TYP:1][LEN:2][payload]
    uint8_t pingRequest[] = {
        0x1F,       // ATT byte
        0x01,       // TYP: status request (example)
        0x00, 0x00  // LEN: 0 (no payload)
    };

    DWORD bytesWritten, bytesRead;
    uint8_t response[256];

    printf("Sending test packet...\n");

    if (!WriteFile(handle, pingRequest, sizeof(pingRequest), &bytesWritten, NULL)) {
        printf("WriteFile failed: %lu\n", GetLastError());
        return false;
    }

    printf("Wrote %lu bytes\n", bytesWritten);

    // Try to read response (with timeout via overlapped I/O would be better)
    printf("Reading response...\n");

    if (!ReadFile(handle, response, sizeof(response), &bytesRead, NULL)) {
        DWORD err = GetLastError();
        if (err == ERROR_NO_DATA) {
            printf("No data available (device may not have responded)\n");
        } else {
            printf("ReadFile failed: %lu\n", err);
        }
        return false;
    }

    printf("Read %lu bytes: ", bytesRead);
    for (DWORD i = 0; i < bytesRead && i < 32; i++) {
        printf("%02X ", response[i]);
    }
    if (bytesRead > 32) printf("...");
    printf("\n");

    return true;
}

int main(int argc, char* argv[]) {
    printf("ECUconnect L2CAP Driver Test\n");
    printf("============================\n\n");

    // Open driver interface
    printf("Opening driver interface...\n");
    HANDLE handle = OpenDriverInterface();

    if (handle == INVALID_HANDLE_VALUE) {
        printf("\nFailed to open driver interface.\n");
        printf("Make sure:\n");
        printf("  1. The driver is installed (make driver-reinstall)\n");
        printf("  2. A Bluetooth adapter is present and enabled\n");
        return 1;
    }

    printf("Driver interface opened successfully!\n");

    // Query status
    ECUCONNECT_STATUS status;
    if (GetDriverStatus(handle, &status)) {
        PrintStatus(&status);
    }

    // If BT address provided, try to connect
    if (argc >= 2) {
        uint64_t btAddress;
        if (ParseMacAddress(argv[1], &btAddress)) {
            uint16_t psm = 129;  // Default CANyonero PSM
            uint32_t timeout = 10000;  // 10 seconds

            if (argc >= 3) {
                psm = (uint16_t)atoi(argv[2]);
            }

            if (ConnectToDevice(handle, btAddress, psm, timeout)) {
                // Wait a moment for connection
                Sleep(1000);

                // Check status again
                if (GetDriverStatus(handle, &status)) {
                    PrintStatus(&status);
                }

                // If connected, try read/write
                if (status.State == 2) {  // Connected
                    TestReadWrite(handle);
                }

                // Disconnect
                printf("\nPress Enter to disconnect...");
                getchar();
                DisconnectFromDevice(handle);
            }
        } else {
            printf("Invalid MAC address format. Use: AA:BB:CC:DD:EE:FF\n");
        }
    } else {
        printf("No BT address provided. Use: %s AA:BB:CC:DD:EE:FF [psm]\n", argv[0]);
    }

    CloseHandle(handle);
    printf("\nTest complete.\n");
    return 0;
}
