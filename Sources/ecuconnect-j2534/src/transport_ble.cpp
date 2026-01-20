/**
 * BLE L2CAP Transport Implementation
 *
 * Uses Windows Runtime APIs for Bluetooth LE L2CAP Connection-Oriented Channels (CoC).
 * Requires Windows 10 version 2004 (build 19041) or later for L2CAP CoC.
 *
 * Note: Full BLE L2CAP implementation requires Windows SDK 10.0.19041+ with
 * C++/WinRT projections that include BluetoothLEDevice.RequestL2CapChannelAsync().
 * This file provides a stub implementation that gracefully returns errors when
 * BLE is not available, allowing the TCP transport to continue working.
 */
#include "transport.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// Debug logging macro
#define BLE_DBG(fmt, ...) fprintf(stderr, "[ble-transport] " fmt "\n", ##__VA_ARGS__)

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

// Check if Windows version supports L2CAP CoC (Windows 10 2004+, build 19041)
bool checkWindowsVersionForL2Cap() {
    OSVERSIONINFOEXW osvi = { sizeof(osvi) };

    // Use RtlGetVersion to get accurate version info (GetVersionEx is deprecated and lies)
    typedef LONG(WINAPI* RtlGetVersionFn)(OSVERSIONINFOEXW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto RtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
            GetProcAddress(ntdll, "RtlGetVersion"));
        if (RtlGetVersion && RtlGetVersion(&osvi) == 0) {
            // L2CAP CoC requires Windows 10 2004 (build 19041) or later
            if (osvi.dwMajorVersion > 10) return true;
            if (osvi.dwMajorVersion == 10 && osvi.dwBuildNumber >= 19041) return true;
        }
    }
    return false;
}

} // namespace

namespace ecuconnect {

struct BleTransport::Impl {
    BleConfig config;
    std::string lastError;
    bool connected = false;

    explicit Impl(const BleConfig& cfg) : config(cfg) {
        // Check Windows version at construction time
        if (!checkWindowsVersionForL2Cap()) {
            lastError = "BLE L2CAP requires Windows 10 version 2004 (build 19041) or later. "
                        "Use TCP transport (WiFi) instead.";
        } else {
            lastError = "BLE L2CAP transport not yet implemented in this build. "
                        "This feature requires building with Windows SDK 10.0.19041+ "
                        "and C++/WinRT. Use TCP transport (WiFi) instead.";
        }
    }

    ~Impl() {
        disconnect();
    }

    void disconnect() {
        connected = false;
    }

    bool connect() {
        BLE_DBG("Attempting to connect to BLE device: %s", config.deviceNameOrAddress.c_str());

        if (!checkWindowsVersionForL2Cap()) {
            lastError = "BLE L2CAP requires Windows 10 version 2004 (build 19041) or later";
            BLE_DBG("Error: %s", lastError.c_str());
            return false;
        }

        // TODO: Full BLE L2CAP implementation
        // Requires building with Windows SDK that includes:
        //   - winrt/Windows.Devices.Bluetooth.h with IBluetoothLEDeviceX interface
        //   - BluetoothLEL2CapChannel and BluetoothLEL2CapChannelResult types
        //
        // Implementation steps:
        // 1. Discover device by name using BluetoothLEAdvertisementWatcher
        // 2. Connect to device using BluetoothLEDevice::FromBluetoothAddressAsync
        // 3. Request L2CAP channel using device.RequestL2CapChannelAsync(psm)
        // 4. Get StreamSocket from the L2CAP channel result
        // 5. Use DataReader/DataWriter for send/receive

        lastError = "BLE L2CAP transport not yet implemented in this build. "
                    "Use TCP transport (WiFi) with connection string like '192.168.42.42'";
        BLE_DBG("Error: %s", lastError.c_str());
        return false;
    }

    int send(const std::vector<uint8_t>& data) {
        (void)data;
        if (!connected) {
            lastError = "Not connected";
            return -1;
        }
        lastError = "BLE send not implemented";
        return -1;
    }

    std::vector<uint8_t> receive(uint32_t timeout_ms) {
        (void)timeout_ms;
        if (!connected) {
            lastError = "Not connected";
        }
        return {};
    }
};

BleTransport::BleTransport(const BleConfig& config)
    : pImpl(std::make_unique<Impl>(config)) {
    BLE_DBG("BLE transport created for device: %s", config.deviceNameOrAddress.c_str());
}

BleTransport::~BleTransport() = default;

bool BleTransport::connect() {
    return pImpl->connect();
}

void BleTransport::disconnect() {
    pImpl->disconnect();
}

bool BleTransport::isConnected() const {
    return pImpl->connected;
}

int BleTransport::send(const std::vector<uint8_t>& data) {
    return pImpl->send(data);
}

std::vector<uint8_t> BleTransport::receive(uint32_t timeout_ms) {
    return pImpl->receive(timeout_ms);
}

std::string BleTransport::getLastError() const {
    return pImpl->lastError;
}

} // namespace ecuconnect

#else // !_WIN32

// Non-Windows stub
namespace ecuconnect {

struct BleTransport::Impl {
    BleConfig config;
    std::string lastError = "BLE transport is only supported on Windows";
    explicit Impl(const BleConfig& cfg) : config(cfg) {}
};

BleTransport::BleTransport(const BleConfig& config)
    : pImpl(std::make_unique<Impl>(config)) {
    BLE_DBG("BLE transport not available on this platform");
}

BleTransport::~BleTransport() = default;

bool BleTransport::connect() {
    pImpl->lastError = "BLE transport is only supported on Windows";
    BLE_DBG("connect() failed: %s", pImpl->lastError.c_str());
    return false;
}

void BleTransport::disconnect() {}

bool BleTransport::isConnected() const {
    return false;
}

int BleTransport::send(const std::vector<uint8_t>&) {
    pImpl->lastError = "Not connected";
    return -1;
}

std::vector<uint8_t> BleTransport::receive(uint32_t) {
    return {};
}

std::string BleTransport::getLastError() const {
    return pImpl->lastError;
}

} // namespace ecuconnect

#endif // _WIN32
