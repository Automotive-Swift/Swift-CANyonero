/**
 * L2CAP Transport Implementation
 *
 * This transport communicates with ECUconnect devices via the L2CAP Profile Driver.
 * The driver provides a device interface that user-mode applications can open and
 * use for read/write operations.
 *
 * Architecture:
 * - Uses SetupDiGetClassDevs to enumerate driver device interfaces
 * - Opens device handle with CreateFile
 * - Uses DeviceIoControl for connect/disconnect operations
 * - Uses ReadFile/WriteFile for data transfer
 *
 * Requires: ecuconnect_l2cap.sys driver to be installed and running
 *
 * Copyright (c) ECUconnect Project
 */

#include "transport.h"
#include "../driver/public.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

// Debug logging macro
#define L2CAP_DBG(fmt, ...) fprintf(stderr, "[l2cap-transport] " fmt "\n", ##__VA_ARGS__)

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <setupapi.h>
#include <initguid.h>

// Link with SetupAPI
#pragma comment(lib, "setupapi.lib")

namespace ecuconnect {

namespace {

/**
 * Parse Bluetooth address from string
 * Supports formats: "XX:XX:XX:XX:XX:XX" or "XXXXXXXXXXXX" (12 hex digits)
 */
std::optional<uint64_t> parseBluetoothAddress(const std::string& str) {
    uint64_t addr = 0;

    // Try MAC format first (XX:XX:XX:XX:XX:XX)
    if (str.length() == 17) {
        int parts[6];
        if (sscanf(str.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
                   &parts[0], &parts[1], &parts[2], &parts[3], &parts[4], &parts[5]) == 6) {
            for (int i = 0; i < 6; i++) {
                addr = (addr << 8) | static_cast<uint8_t>(parts[i]);
            }
            return addr;
        }
    }

    // Try plain hex format (XXXXXXXXXXXX)
    if (str.length() == 12) {
        for (char c : str) {
            if (!isxdigit(static_cast<unsigned char>(c))) {
                return std::nullopt;
            }
        }
        addr = strtoull(str.c_str(), nullptr, 16);
        return addr;
    }

    return std::nullopt;
}

/**
 * Check if string looks like a Bluetooth address
 */
bool looksLikeBluetoothAddress(const std::string& str) {
    // MAC format
    if (str.length() == 17) {
        for (size_t i = 0; i < str.length(); i++) {
            if ((i + 1) % 3 == 0) {
                if (str[i] != ':') return false;
            } else {
                if (!isxdigit(static_cast<unsigned char>(str[i]))) return false;
            }
        }
        return true;
    }

    // Plain hex format
    if (str.length() == 12) {
        for (char c : str) {
            if (!isxdigit(static_cast<unsigned char>(c))) return false;
        }
        return true;
    }

    return false;
}

/**
 * Find ECUconnect L2CAP driver device path
 */
std::string findDriverDevicePath() {
    HDEVINFO deviceInfoSet;
    SP_DEVICE_INTERFACE_DATA interfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_A detailData = nullptr;
    DWORD requiredSize = 0;
    std::string devicePath;

    // Get device information set for our interface GUID
    deviceInfoSet = SetupDiGetClassDevsA(
        &GUID_DEVINTERFACE_ECUCONNECT_L2CAP,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );

    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        L2CAP_DBG("SetupDiGetClassDevs failed: %d", GetLastError());
        return "";
    }

    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    // Get first device interface
    if (!SetupDiEnumDeviceInterfaces(
            deviceInfoSet,
            nullptr,
            &GUID_DEVINTERFACE_ECUCONNECT_L2CAP,
            0,
            &interfaceData)) {
        DWORD err = GetLastError();
        if (err != ERROR_NO_MORE_ITEMS) {
            L2CAP_DBG("SetupDiEnumDeviceInterfaces failed: %d", err);
        } else {
            L2CAP_DBG("No ECUconnect L2CAP driver devices found");
        }
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return "";
    }

    // Get required size for detail data
    SetupDiGetDeviceInterfaceDetailA(
        deviceInfoSet,
        &interfaceData,
        nullptr,
        0,
        &requiredSize,
        nullptr
    );

    if (requiredSize == 0) {
        L2CAP_DBG("Failed to get interface detail size");
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return "";
    }

    // Allocate and populate detail data
    detailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)malloc(requiredSize);
    if (!detailData) {
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return "";
    }

    detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

    if (!SetupDiGetDeviceInterfaceDetailA(
            deviceInfoSet,
            &interfaceData,
            detailData,
            requiredSize,
            nullptr,
            nullptr)) {
        L2CAP_DBG("SetupDiGetDeviceInterfaceDetail failed: %d", GetLastError());
        free(detailData);
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return "";
    }

    devicePath = detailData->DevicePath;
    L2CAP_DBG("Found device: %s", devicePath.c_str());

    free(detailData);
    SetupDiDestroyDeviceInfoList(deviceInfoSet);

    return devicePath;
}

} // anonymous namespace

/**
 * L2CAP Transport Configuration
 */
struct L2capConfig {
    std::string deviceNameOrAddress;      // "ECUconnect-XXXX" or "XX:XX:XX:XX:XX:XX"
    uint16_t psm = ECUCONNECT_L2CAP_PSM;  // L2CAP PSM (default: 129)
    uint32_t connect_timeout_ms = 10000;
    uint32_t read_timeout_ms = 1000;
};

/**
 * L2CAP Transport Implementation
 */
class L2capTransport : public ITransport {
public:
    explicit L2capTransport(const L2capConfig& config);
    ~L2capTransport() override;

    // Non-copyable
    L2capTransport(const L2capTransport&) = delete;
    L2capTransport& operator=(const L2capTransport&) = delete;

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    int send(const std::vector<uint8_t>& data) override;
    std::vector<uint8_t> receive(uint32_t timeout_ms) override;
    std::string getLastError() const override;

private:
    L2capConfig config_;
    HANDLE deviceHandle_ = INVALID_HANDLE_VALUE;
    std::string lastError_;
    bool connected_ = false;

    bool openDriverDevice();
    bool connectToDevice(uint64_t bluetoothAddress);
    bool resolveDeviceAddress(uint64_t& outAddress);
};

L2capTransport::L2capTransport(const L2capConfig& config)
    : config_(config) {
    L2CAP_DBG("L2CAP transport created for: %s", config.deviceNameOrAddress.c_str());
}

L2capTransport::~L2capTransport() {
    disconnect();
}

bool L2capTransport::openDriverDevice() {
    std::string devicePath = findDriverDevicePath();
    if (devicePath.empty()) {
        lastError_ = "ECUconnect L2CAP driver not found. Is the driver installed?";
        return false;
    }

    deviceHandle_ = CreateFileA(
        devicePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,  // No sharing
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (deviceHandle_ == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        std::ostringstream oss;
        oss << "Failed to open driver device: error " << err;
        lastError_ = oss.str();
        L2CAP_DBG("CreateFile failed: %d", err);
        return false;
    }

    L2CAP_DBG("Driver device opened successfully");
    return true;
}

bool L2capTransport::resolveDeviceAddress(uint64_t& outAddress) {
    // If we have a direct address, use it
    if (looksLikeBluetoothAddress(config_.deviceNameOrAddress)) {
        auto addr = parseBluetoothAddress(config_.deviceNameOrAddress);
        if (addr) {
            outAddress = *addr;
            L2CAP_DBG("Using direct BT address: %012llX", (unsigned long long)outAddress);
            return true;
        }
    }

    // TODO: Device discovery by name would be implemented here
    // For now, we require a direct Bluetooth address
    // The driver could potentially expose an IOCTL for device enumeration

    lastError_ = "Device discovery by name not yet implemented. Please provide Bluetooth address.";
    return false;
}

bool L2capTransport::connectToDevice(uint64_t bluetoothAddress) {
    ECUCONNECT_CONNECT_REQUEST request = {};
    request.BluetoothAddress = bluetoothAddress;
    request.Psm = config_.psm;
    request.Mtu = ECUCONNECT_L2CAP_MTU;
    request.TimeoutMs = config_.connect_timeout_ms;

    DWORD bytesReturned = 0;
    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    if (!overlapped.hEvent) {
        lastError_ = "Failed to create event";
        return false;
    }

    L2CAP_DBG("Connecting to %012llX PSM %d...",
              (unsigned long long)bluetoothAddress, config_.psm);

    BOOL result = DeviceIoControl(
        deviceHandle_,
        IOCTL_ECUCONNECT_CONNECT,
        &request,
        sizeof(request),
        nullptr,
        0,
        &bytesReturned,
        &overlapped
    );

    if (!result) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            // Wait for completion with timeout
            DWORD waitResult = WaitForSingleObject(
                overlapped.hEvent,
                config_.connect_timeout_ms
            );

            if (waitResult == WAIT_TIMEOUT) {
                CancelIoEx(deviceHandle_, &overlapped);
                CloseHandle(overlapped.hEvent);
                lastError_ = "Connection timeout";
                L2CAP_DBG("Connection timeout");
                return false;
            }

            if (!GetOverlappedResult(deviceHandle_, &overlapped, &bytesReturned, FALSE)) {
                err = GetLastError();
                CloseHandle(overlapped.hEvent);
                std::ostringstream oss;
                oss << "Connection failed: error " << err;
                lastError_ = oss.str();
                L2CAP_DBG("Connection failed: %d", err);
                return false;
            }
        } else {
            CloseHandle(overlapped.hEvent);
            std::ostringstream oss;
            oss << "DeviceIoControl failed: error " << err;
            lastError_ = oss.str();
            L2CAP_DBG("DeviceIoControl failed: %d", err);
            return false;
        }
    }

    CloseHandle(overlapped.hEvent);
    L2CAP_DBG("Connected successfully");
    return true;
}

bool L2capTransport::connect() {
    if (connected_) {
        return true;
    }

    // Step 1: Open the driver device
    if (!openDriverDevice()) {
        return false;
    }

    // Step 2: Resolve device address
    uint64_t bluetoothAddress = 0;
    if (!resolveDeviceAddress(bluetoothAddress)) {
        CloseHandle(deviceHandle_);
        deviceHandle_ = INVALID_HANDLE_VALUE;
        return false;
    }

    // Step 3: Connect via the driver
    if (!connectToDevice(bluetoothAddress)) {
        CloseHandle(deviceHandle_);
        deviceHandle_ = INVALID_HANDLE_VALUE;
        return false;
    }

    connected_ = true;
    return true;
}

void L2capTransport::disconnect() {
    if (deviceHandle_ != INVALID_HANDLE_VALUE) {
        if (connected_) {
            // Send disconnect IOCTL
            DWORD bytesReturned = 0;
            DeviceIoControl(
                deviceHandle_,
                IOCTL_ECUCONNECT_DISCONNECT,
                nullptr,
                0,
                nullptr,
                0,
                &bytesReturned,
                nullptr
            );
            L2CAP_DBG("Disconnected");
        }

        CloseHandle(deviceHandle_);
        deviceHandle_ = INVALID_HANDLE_VALUE;
    }
    connected_ = false;
}

bool L2capTransport::isConnected() const {
    return connected_;
}

int L2capTransport::send(const std::vector<uint8_t>& data) {
    if (!connected_ || deviceHandle_ == INVALID_HANDLE_VALUE) {
        lastError_ = "Not connected";
        return -1;
    }

    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) {
        lastError_ = "Failed to create event";
        return -1;
    }

    DWORD bytesWritten = 0;
    BOOL result = WriteFile(
        deviceHandle_,
        data.data(),
        static_cast<DWORD>(data.size()),
        &bytesWritten,
        &overlapped
    );

    if (!result) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            // Wait for completion
            if (!GetOverlappedResult(deviceHandle_, &overlapped, &bytesWritten, TRUE)) {
                err = GetLastError();
                CloseHandle(overlapped.hEvent);
                std::ostringstream oss;
                oss << "Write failed: error " << err;
                lastError_ = oss.str();
                L2CAP_DBG("Write failed: %d", err);
                return -1;
            }
        } else {
            CloseHandle(overlapped.hEvent);
            std::ostringstream oss;
            oss << "WriteFile failed: error " << err;
            lastError_ = oss.str();
            L2CAP_DBG("WriteFile failed: %d", err);
            return -1;
        }
    }

    CloseHandle(overlapped.hEvent);
    return static_cast<int>(bytesWritten);
}

std::vector<uint8_t> L2capTransport::receive(uint32_t timeout_ms) {
    if (!connected_ || deviceHandle_ == INVALID_HANDLE_VALUE) {
        lastError_ = "Not connected";
        return {};
    }

    std::vector<uint8_t> buffer(ECUCONNECT_L2CAP_MTU);

    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) {
        lastError_ = "Failed to create event";
        return {};
    }

    DWORD bytesRead = 0;
    BOOL result = ReadFile(
        deviceHandle_,
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        &bytesRead,
        &overlapped
    );

    if (!result) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            // Wait with timeout
            DWORD waitResult = WaitForSingleObject(overlapped.hEvent, timeout_ms);
            if (waitResult == WAIT_TIMEOUT) {
                CancelIoEx(deviceHandle_, &overlapped);
                CloseHandle(overlapped.hEvent);
                return {};  // Timeout, no data
            }

            if (!GetOverlappedResult(deviceHandle_, &overlapped, &bytesRead, FALSE)) {
                err = GetLastError();
                CloseHandle(overlapped.hEvent);
                if (err == ERROR_OPERATION_ABORTED) {
                    return {};  // Cancelled, no data
                }
                std::ostringstream oss;
                oss << "Read failed: error " << err;
                lastError_ = oss.str();
                L2CAP_DBG("Read failed: %d", err);
                return {};
            }
        } else {
            CloseHandle(overlapped.hEvent);
            std::ostringstream oss;
            oss << "ReadFile failed: error " << err;
            lastError_ = oss.str();
            L2CAP_DBG("ReadFile failed: %d", err);
            return {};
        }
    }

    CloseHandle(overlapped.hEvent);

    if (bytesRead > 0) {
        buffer.resize(bytesRead);
        return buffer;
    }

    return {};
}

std::string L2capTransport::getLastError() const {
    return lastError_;
}

/**
 * Factory function to create L2CAP transport from BleConfig
 * (maintains compatibility with existing transport interface)
 */
std::unique_ptr<ITransport> createL2capTransport(const BleConfig& config) {
    L2capConfig l2capConfig;
    l2capConfig.deviceNameOrAddress = config.deviceNameOrAddress;
    l2capConfig.psm = config.psm;
    l2capConfig.connect_timeout_ms = config.connect_timeout_ms;
    l2capConfig.read_timeout_ms = config.receive_timeout_ms;

    return std::make_unique<L2capTransport>(l2capConfig);
}

} // namespace ecuconnect

#else // !_WIN32

namespace ecuconnect {

std::unique_ptr<ITransport> createL2capTransport(const BleConfig&) {
    // L2CAP driver transport only available on Windows
    return nullptr;
}

} // namespace ecuconnect

#endif // _WIN32
