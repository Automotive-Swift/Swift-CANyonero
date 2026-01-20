/**
 * Transport Abstraction Layer
 * Supports TCP and future BLE/L2CAP transports
 */
#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <optional>

namespace ecuconnect {

/**
 * Transport interface - abstracts the underlying communication layer
 */
class ITransport {
public:
    virtual ~ITransport() = default;

    /**
     * Connect to the device
     * @return true on success
     */
    virtual bool connect() = 0;

    /**
     * Disconnect from the device
     */
    virtual void disconnect() = 0;

    /**
     * Check if connected
     * @return true if connected
     */
    virtual bool isConnected() const = 0;

    /**
     * Send data to the device
     * @param data Data to send
     * @return Number of bytes sent, or -1 on error
     */
    virtual int send(const std::vector<uint8_t>& data) = 0;

    /**
     * Receive data from the device
     * @param timeout Timeout in milliseconds (0 = non-blocking)
     * @return Received data, empty if no data or error
     */
    virtual std::vector<uint8_t> receive(uint32_t timeout_ms) = 0;

    /**
     * Get the last error message
     * @return Error string
     */
    virtual std::string getLastError() const = 0;
};

/**
 * TCP Transport configuration
 */
struct TcpConfig {
    std::string host = "192.168.42.42";
    uint16_t port = 129;
    uint32_t connect_timeout_ms = 5000;
    uint32_t receive_timeout_ms = 1000;
};

/**
 * BLE L2CAP Transport configuration
 */
struct BleConfig {
    std::string deviceNameOrAddress;      // "ECUconnect-XXXX" or "XX:XX:XX:XX:XX:XX"
    std::string serviceUUID = "FFF1";     // ECUconnect BLE service UUID
    uint16_t psm = 129;                   // L2CAP PSM for CANyonero protocol
    uint32_t connect_timeout_ms = 10000;  // BLE connections take longer
    uint32_t receive_timeout_ms = 1000;
};

/**
 * TCP Transport implementation
 */
class TcpTransport : public ITransport {
public:
    explicit TcpTransport(const TcpConfig& config = TcpConfig{});
    ~TcpTransport() override;

    // Non-copyable
    TcpTransport(const TcpTransport&) = delete;
    TcpTransport& operator=(const TcpTransport&) = delete;

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    int send(const std::vector<uint8_t>& data) override;
    std::vector<uint8_t> receive(uint32_t timeout_ms) override;
    std::string getLastError() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

/**
 * BLE L2CAP Transport implementation (Windows 10 1803+)
 */
class BleTransport : public ITransport {
public:
    explicit BleTransport(const BleConfig& config = BleConfig{});
    ~BleTransport() override;

    // Non-copyable
    BleTransport(const BleTransport&) = delete;
    BleTransport& operator=(const BleTransport&) = delete;

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    int send(const std::vector<uint8_t>& data) override;
    std::vector<uint8_t> receive(uint32_t timeout_ms) override;
    std::string getLastError() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

/**
 * Transport factory for creating transports by type
 */
enum class TransportType {
    TCP,
    BLE_L2CAP
};

/**
 * Create transport from type and connection string.
 *
 * Connection string formats:
 *   - "" or NULL              → TCP default (192.168.42.42:129)
 *   - "192.168.42.42"         → TCP explicit (IP address detected)
 *   - "192.168.42.42:129"     → TCP with port
 *   - "BLE:ECUconnect"        → BLE by device name
 *   - "BLE:XX:XX:XX:XX:XX:XX" → BLE by MAC address
 *   - "ECUconnect-XXXX"       → BLE auto-detect (no dots = BLE device name)
 *   - "TCP:192.168.42.42"     → TCP explicit prefix
 */
std::unique_ptr<ITransport> createTransport(TransportType type, const std::string& connection_string = "");

} // namespace ecuconnect

#endif // TRANSPORT_H
