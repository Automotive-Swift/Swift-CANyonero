/**
 * BLE Transport Implementation
 *
 * IMPORTANT: Windows Limitation
 * Windows does NOT expose BLE L2CAP Connection-Oriented Channels (CoC) at the
 * application level. The L2CAP APIs are only available through kernel-mode
 * Bluetooth profile drivers using BRB_L2CA_OPEN_CHANNEL.
 *
 * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/bluetooth/creating-a-l2cap-client-connection-to-a-remote-device
 *
 * This implementation uses GATT (Generic Attribute Profile) as an alternative:
 * - Discovers ECUconnect device by name via BLE advertisements
 * - Connects to device and discovers GATT services
 * - Uses custom service UUID (FFF1) with TX/RX characteristics
 * - TX via Write Without Response characteristic
 * - RX via Notify characteristic
 *
 * Requires Windows 10 Anniversary Update (1607) or later.
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

// Check for MSVC with C++/WinRT support (VS 2019+)
#if defined(_MSC_VER) && _MSC_VER >= 1920
#define HAVE_WINRT 1
#endif

#ifdef HAVE_WINRT

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Storage.Streams.h>

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <chrono>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Storage::Streams;

namespace ecuconnect {

namespace {

// ECUconnect GATT Service and Characteristic UUIDs
// Service: FFF1 (custom ECUconnect service)
// TX Characteristic: FFF2 (write without response)
// RX Characteristic: FFF3 (notify)
constexpr guid SERVICE_UUID{ 0x0000FFF1, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB } };
constexpr guid TX_CHAR_UUID{ 0x0000FFF2, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB } };
constexpr guid RX_CHAR_UUID{ 0x0000FFF3, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB } };

// Convert MAC address string "XX:XX:XX:XX:XX:XX" to uint64_t
std::optional<uint64_t> parseMacAddress(const std::string& mac) {
    if (mac.length() != 17) return std::nullopt;

    uint64_t addr = 0;
    int parts[6];
    if (sscanf(mac.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
               &parts[0], &parts[1], &parts[2], &parts[3], &parts[4], &parts[5]) != 6) {
        return std::nullopt;
    }

    for (int i = 0; i < 6; i++) {
        addr = (addr << 8) | static_cast<uint8_t>(parts[i]);
    }
    return addr;
}

// Check if string looks like a MAC address
bool looksLikeMacAddress(const std::string& s) {
    if (s.length() != 17) return false;
    for (size_t i = 0; i < s.length(); i++) {
        if ((i + 1) % 3 == 0) {
            if (s[i] != ':') return false;
        } else {
            if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
        }
    }
    return true;
}

// Convert wide string to narrow string
std::string wstringToString(const std::wstring& ws) {
    if (ws.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
}

// Convert hstring to std::string
std::string hstringToString(const hstring& hs) {
    return wstringToString(std::wstring(hs));
}

} // namespace

struct BleTransport::Impl {
    BleConfig config;
    std::string lastError;

    // WinRT objects
    BluetoothLEDevice device{nullptr};
    GattDeviceService service{nullptr};
    GattCharacteristic txCharacteristic{nullptr};
    GattCharacteristic rxCharacteristic{nullptr};

    // Connection state
    std::atomic<bool> connected{false};
    std::mutex mutex;

    // Receive queue for notifications
    std::queue<std::vector<uint8_t>> rxQueue;
    std::mutex rxMutex;
    std::condition_variable rxCV;

    // Event token for notifications
    event_token notifyToken{};

    explicit Impl(const BleConfig& cfg) : config(cfg) {}

    ~Impl() {
        disconnect();
    }

    void disconnect() {
        connected = false;

        // Unsubscribe from notifications
        if (rxCharacteristic && notifyToken) {
            try {
                rxCharacteristic.ValueChanged(notifyToken);
                notifyToken = {};
            } catch (...) {}
        }

        // Close GATT objects
        if (service) {
            try { service.Close(); } catch (...) {}
            service = nullptr;
        }

        txCharacteristic = nullptr;
        rxCharacteristic = nullptr;

        // Close device
        if (device) {
            try { device.Close(); } catch (...) {}
            device = nullptr;
        }

        // Clear RX queue
        {
            std::lock_guard<std::mutex> lock(rxMutex);
            while (!rxQueue.empty()) rxQueue.pop();
        }
    }

    bool connect() {
        BLE_DBG("Connecting to BLE device: %s", config.deviceNameOrAddress.c_str());

        try {
            uint64_t bluetoothAddress = 0;

            // Check if we have a MAC address or device name
            if (looksLikeMacAddress(config.deviceNameOrAddress)) {
                auto addr = parseMacAddress(config.deviceNameOrAddress);
                if (!addr) {
                    lastError = "Invalid MAC address format";
                    return false;
                }
                bluetoothAddress = *addr;
                BLE_DBG("Using MAC address: %012llX", static_cast<unsigned long long>(bluetoothAddress));
            } else {
                // Need to discover device by name
                BLE_DBG("Discovering device by name: %s", config.deviceNameOrAddress.c_str());
                auto addr = discoverDeviceByName(config.deviceNameOrAddress);
                if (!addr) {
                    lastError = "Device not found: " + config.deviceNameOrAddress;
                    return false;
                }
                bluetoothAddress = *addr;
                BLE_DBG("Found device at address: %012llX", static_cast<unsigned long long>(bluetoothAddress));
            }

            // Connect to the device
            BLE_DBG("Opening BLE device...");
            auto deviceOp = BluetoothLEDevice::FromBluetoothAddressAsync(bluetoothAddress);
            device = deviceOp.get();

            if (!device) {
                lastError = "Failed to open BLE device";
                return false;
            }

            BLE_DBG("BLE device opened: %s", hstringToString(device.Name()).c_str());

            // Get GATT services
            BLE_DBG("Discovering GATT services...");
            auto servicesResult = device.GetGattServicesForUuidAsync(SERVICE_UUID).get();

            if (servicesResult.Status() != GattCommunicationStatus::Success) {
                std::ostringstream oss;
                oss << "Failed to get GATT services: status " << static_cast<int>(servicesResult.Status());
                lastError = oss.str();
                disconnect();
                return false;
            }

            auto services = servicesResult.Services();
            if (services.Size() == 0) {
                lastError = "ECUconnect GATT service (FFF1) not found";
                disconnect();
                return false;
            }

            service = services.GetAt(0);
            BLE_DBG("Found ECUconnect GATT service");

            // Get TX characteristic (write without response)
            BLE_DBG("Getting TX characteristic (FFF2)...");
            auto txResult = service.GetCharacteristicsForUuidAsync(TX_CHAR_UUID).get();
            if (txResult.Status() != GattCommunicationStatus::Success || txResult.Characteristics().Size() == 0) {
                lastError = "TX characteristic (FFF2) not found";
                disconnect();
                return false;
            }
            txCharacteristic = txResult.Characteristics().GetAt(0);
            BLE_DBG("Found TX characteristic");

            // Get RX characteristic (notify)
            BLE_DBG("Getting RX characteristic (FFF3)...");
            auto rxResult = service.GetCharacteristicsForUuidAsync(RX_CHAR_UUID).get();
            if (rxResult.Status() != GattCommunicationStatus::Success || rxResult.Characteristics().Size() == 0) {
                lastError = "RX characteristic (FFF3) not found";
                disconnect();
                return false;
            }
            rxCharacteristic = rxResult.Characteristics().GetAt(0);
            BLE_DBG("Found RX characteristic");

            // Subscribe to notifications
            BLE_DBG("Subscribing to notifications...");
            auto cccdResult = rxCharacteristic.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue::Notify).get();

            if (cccdResult != GattCommunicationStatus::Success) {
                std::ostringstream oss;
                oss << "Failed to enable notifications: status " << static_cast<int>(cccdResult);
                lastError = oss.str();
                disconnect();
                return false;
            }

            // Register notification handler
            notifyToken = rxCharacteristic.ValueChanged([this](const GattCharacteristic&,
                                                               const GattValueChangedEventArgs& args) {
                auto reader = DataReader::FromBuffer(args.CharacteristicValue());
                std::vector<uint8_t> data(reader.UnconsumedBufferLength());
                reader.ReadBytes(data);

                {
                    std::lock_guard<std::mutex> lock(rxMutex);
                    rxQueue.push(std::move(data));
                }
                rxCV.notify_one();
            });

            connected = true;
            BLE_DBG("BLE GATT connection established");
            return true;

        } catch (const hresult_error& e) {
            std::ostringstream oss;
            oss << "BLE error: " << hstringToString(e.message())
                << " (0x" << std::hex << static_cast<uint32_t>(e.code()) << ")";
            lastError = oss.str();
            BLE_DBG("BLE error: %s", lastError.c_str());
            disconnect();
            return false;
        } catch (const std::exception& e) {
            lastError = std::string("Exception: ") + e.what();
            BLE_DBG("Exception: %s", e.what());
            disconnect();
            return false;
        }
    }

    std::optional<uint64_t> discoverDeviceByName(const std::string& targetName) {
        BLE_DBG("Starting BLE advertisement watcher for device: %s", targetName.c_str());

        std::optional<uint64_t> foundAddress;
        std::mutex foundMutex;
        std::condition_variable foundCV;
        bool searchComplete = false;

        BluetoothLEAdvertisementWatcher watcher;
        watcher.ScanningMode(BluetoothLEScanningMode::Active);

        // Handle received advertisements
        auto receivedToken = watcher.Received([&](const BluetoothLEAdvertisementWatcher&,
                                                   const BluetoothLEAdvertisementReceivedEventArgs& args) {
            std::string localName = hstringToString(args.Advertisement().LocalName());

            if (!localName.empty()) {
                BLE_DBG("Found device: %s at %012llX", localName.c_str(),
                        static_cast<unsigned long long>(args.BluetoothAddress()));

                // Check if name contains our target (case-insensitive)
                std::string lowerLocalName = localName;
                std::string lowerTarget = targetName;
                std::transform(lowerLocalName.begin(), lowerLocalName.end(), lowerLocalName.begin(), ::tolower);
                std::transform(lowerTarget.begin(), lowerTarget.end(), lowerTarget.begin(), ::tolower);

                if (lowerLocalName.find(lowerTarget) != std::string::npos ||
                    lowerTarget.find(lowerLocalName) != std::string::npos) {
                    std::lock_guard<std::mutex> lock(foundMutex);
                    if (!foundAddress) {  // Only set once
                        foundAddress = args.BluetoothAddress();
                        searchComplete = true;
                        foundCV.notify_all();
                    }
                }
            }
        });

        // Handle watcher stopped
        auto stoppedToken = watcher.Stopped([&](const BluetoothLEAdvertisementWatcher&,
                                                 const BluetoothLEAdvertisementWatcherStoppedEventArgs&) {
            std::lock_guard<std::mutex> lock(foundMutex);
            searchComplete = true;
            foundCV.notify_all();
        });

        // Start scanning
        watcher.Start();

        // Wait for device or timeout
        {
            std::unique_lock<std::mutex> lock(foundMutex);
            foundCV.wait_for(lock, std::chrono::milliseconds(config.connect_timeout_ms),
                            [&]{ return searchComplete; });
        }

        watcher.Stop();
        watcher.Received(receivedToken);
        watcher.Stopped(stoppedToken);

        return foundAddress;
    }

    int send(const std::vector<uint8_t>& data) {
        if (!connected || !txCharacteristic) {
            lastError = "Not connected";
            return -1;
        }

        try {
            // Create buffer from data
            DataWriter writer;
            writer.WriteBytes(array_view<const uint8_t>(data));
            auto buffer = writer.DetachBuffer();

            // Write without response for lower latency
            auto writeResult = txCharacteristic.WriteValueAsync(
                buffer, GattWriteOption::WriteWithoutResponse).get();

            if (writeResult != GattCommunicationStatus::Success) {
                std::ostringstream oss;
                oss << "GATT write failed: status " << static_cast<int>(writeResult);
                lastError = oss.str();
                BLE_DBG("Send error: %s", lastError.c_str());
                return -1;
            }

            return static_cast<int>(data.size());

        } catch (const hresult_error& e) {
            std::ostringstream oss;
            oss << "Send error: " << hstringToString(e.message());
            lastError = oss.str();
            BLE_DBG("Send error: %s", lastError.c_str());
            return -1;
        }
    }

    std::vector<uint8_t> receive(uint32_t timeout_ms) {
        if (!connected) {
            lastError = "Not connected";
            return {};
        }

        std::unique_lock<std::mutex> lock(rxMutex);

        // Wait for data with timeout
        if (rxQueue.empty()) {
            rxCV.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                         [this]{ return !rxQueue.empty() || !connected; });
        }

        if (!rxQueue.empty()) {
            auto data = std::move(rxQueue.front());
            rxQueue.pop();
            return data;
        }

        return {};
    }
};

BleTransport::BleTransport(const BleConfig& config)
    : pImpl(std::make_unique<Impl>(config)) {
    BLE_DBG("BLE GATT transport created for device: %s", config.deviceNameOrAddress.c_str());
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

#else // !HAVE_WINRT

// Stub implementation when WinRT is not available (older compilers)
namespace ecuconnect {

struct BleTransport::Impl {
    BleConfig config;
    std::string lastError = "BLE transport requires Visual Studio 2019 or later with C++/WinRT";
    explicit Impl(const BleConfig& cfg) : config(cfg) {}
};

BleTransport::BleTransport(const BleConfig& config)
    : pImpl(std::make_unique<Impl>(config)) {
    BLE_DBG("BLE transport not available (compiler does not support C++/WinRT)");
}

BleTransport::~BleTransport() = default;

bool BleTransport::connect() {
    pImpl->lastError = "BLE transport requires Visual Studio 2019 or later";
    BLE_DBG("connect() failed: %s", pImpl->lastError.c_str());
    return false;
}

void BleTransport::disconnect() {}
bool BleTransport::isConnected() const { return false; }
int BleTransport::send(const std::vector<uint8_t>&) { return -1; }
std::vector<uint8_t> BleTransport::receive(uint32_t) { return {}; }
std::string BleTransport::getLastError() const { return pImpl->lastError; }

} // namespace ecuconnect

#endif // HAVE_WINRT

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
bool BleTransport::isConnected() const { return false; }
int BleTransport::send(const std::vector<uint8_t>&) { return -1; }
std::vector<uint8_t> BleTransport::receive(uint32_t) { return {}; }
std::string BleTransport::getLastError() const { return pImpl->lastError; }

} // namespace ecuconnect

#endif // _WIN32
