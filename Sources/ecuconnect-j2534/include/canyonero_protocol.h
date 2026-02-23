/**
 * CANyonero Protocol Implementation
 * Wire format: [ATT:0x1F | TYP:UInt8 | LEN:UInt16 | payload...]
 * All multi-byte values are big-endian
 */
#ifndef CANYONERO_PROTOCOL_H
#define CANYONERO_PROTOCOL_H

#include "transport.h"
#include <cstdint>
#include <vector>
#include <optional>
#include <functional>
#include <mutex>
#include <queue>
#include <string>

namespace ecuconnect {

// Protocol constants
constexpr uint8_t PDU_ATT = 0x1F;
constexpr size_t PDU_HEADER_SIZE = 4;
constexpr size_t MAX_PDU_PAYLOAD = 0xFFFF;
constexpr size_t MAX_BATCH_SIZE = 16384;  // Max bytes per batched send (TCP packet limit)

/**
 * PDU Types (commands and responses)
 */
enum class PDUType : uint8_t {
    // Commands (Tester -> Adapter)
    Ping                    = 0x10,
    RequestInfo             = 0x11,
    ReadVoltage             = 0x12,
    OpenChannel             = 0x30,
    CloseChannel            = 0x31,
    OpenFDChannel           = 0x32,
    Send                    = 0x33,
    SetArbitration          = 0x34,
    StartPeriodicMessage    = 0x35,
    EndPeriodicMessage      = 0x36,
    SendCompressed          = 0x37,
    Reset                   = 0x43,

    // Positive Responses (Adapter -> Tester)
    Ok                      = 0x80,
    Pong                    = 0x90,
    Info                    = 0x91,
    Voltage                 = 0x92,
    ChannelOpened           = 0xB0,
    ChannelClosed           = 0xB1,
    Received                = 0xB2,
    ReceivedCompressed      = 0xB3,
    PeriodicMessageStarted  = 0xB5,
    PeriodicMessageEnded    = 0xB6,

    // Negative Responses
    ErrorUnspecified        = 0xE0,
    ErrorHardware           = 0xE1,
    ErrorInvalidChannel     = 0xE2,
    ErrorInvalidPeriodic    = 0xE3,
    ErrorNoResponse         = 0xE4,
    ErrorInvalidRPC         = 0xE5,
    ErrorInvalidCommand     = 0xEF,
};

/**
 * Channel Protocol Types
 */
enum class ChannelProtocol : uint8_t {
    Raw         = 0x00,     // Raw CAN frames (max 8 bytes)
    ISOTP       = 0x01,     // ISO 15765-2 (max 4095 bytes)
    KLine       = 0x02,     // ISO 9141
    RawFD       = 0x03,     // Raw CAN FD (max 64 bytes)
    CANFD       = RawFD,    // Backwards-compatible alias
    ISOTP_FD    = 0x04,     // ISOTP with CAN FD
    RawWithFC   = 0x05,     // Raw CAN with auto Flow Control
    ENET        = 0x06,     // Ethernet frames
};

/**
 * Arbitration configuration for a channel
 */
struct Arbitration {
    uint32_t request = 0;           // Request/Source ID
    uint32_t replyPattern = 0;      // Reply pattern/Destination
    uint32_t replyMask = 0xFFFFFFFF;// Reply mask
    uint8_t requestExtension = 0;   // CAN EA extension
    uint8_t replyExtension = 0;     // CAN EA extension

    static constexpr size_t SIZE = 14;  // 4+4+4+1+1

    std::vector<uint8_t> serialize() const;
    static Arbitration deserialize(const uint8_t* data);
};

/**
 * Device information
 */
struct DeviceInfo {
    std::string vendor;
    std::string model;
    std::string hardware;
    std::string serial;
    std::string firmware;
};

/**
 * Received CAN frame
 */
struct CANFrame {
    uint8_t channel;
    uint32_t id;
    uint8_t extension;
    std::vector<uint8_t> data;
    uint64_t timestamp;     // Local timestamp when received
};

/**
 * Protocol Data Unit
 */
class PDU {
public:
    PDU() = default;
    PDU(PDUType type);
    PDU(PDUType type, const std::vector<uint8_t>& payload);

    // Serialize to wire format
    std::vector<uint8_t> serialize() const;

    // Parse from buffer, returns bytes consumed or 0 if incomplete, -1 if garbage
    static int parse(const std::vector<uint8_t>& buffer, PDU& out);

    PDUType type() const { return type_; }
    const std::vector<uint8_t>& payload() const { return payload_; }

    // Factory methods for commands
    static PDU ping(const std::vector<uint8_t>& data = {});
    static PDU requestInfo();
    static PDU readVoltage();
    static PDU openChannel(ChannelProtocol protocol, uint32_t bitrate,
                           uint8_t rxSeparationTime = 0, uint8_t txSeparationTime = 0);
    static PDU openFDChannel(ChannelProtocol protocol, uint32_t bitrate, uint32_t dataBitrate,
                             uint8_t rxSeparationTime = 0, uint8_t txSeparationTime = 0);
    static PDU closeChannel(uint8_t handle);
    static PDU send(uint8_t handle, const std::vector<uint8_t>& data);
    static PDU sendBatch(uint8_t handle, const std::vector<std::vector<uint8_t>>& frames);
    static PDU setArbitration(uint8_t handle, const Arbitration& arb);
    static PDU startPeriodicMessage(uint8_t timeout, const Arbitration& arb,
                                     const std::vector<uint8_t>& data);
    static PDU endPeriodicMessage(uint8_t handle);

    // Response parsing helpers
    uint8_t channelHandle() const;
    uint16_t voltageMillivolts() const;
    DeviceInfo deviceInfo() const;
    CANFrame receivedFrame() const;

    // Check if this is an error response
    bool isError() const;
    std::string errorMessage() const;

private:
    PDUType type_ = PDUType::Ok;
    std::vector<uint8_t> payload_;
};

/**
 * CANyonero Protocol Handler
 * Manages communication with ECUconnect device
 */
class Protocol {
public:
    explicit Protocol(std::unique_ptr<ITransport> transport);
    ~Protocol();

    // Non-copyable
    Protocol(const Protocol&) = delete;
    Protocol& operator=(const Protocol&) = delete;

    // Connection management
    bool connect();
    void disconnect();
    bool isConnected() const;

    // Async mode (default false). Set to true when a background thread 
    // is responsible for pumping receiveMessages().
    void setAsyncMode(bool enabled);

    // Device operations
    std::optional<DeviceInfo> getDeviceInfo(uint32_t timeout_ms = 1000);
    std::optional<uint16_t> readVoltage(uint32_t timeout_ms = 1000);
    bool ping(uint32_t timeout_ms = 1000);

    // Channel operations
    std::optional<uint8_t> openChannel(ChannelProtocol protocol, uint32_t bitrate,
                                       uint32_t timeout_ms = 1000,
                                       std::optional<uint32_t> dataBitrate = std::nullopt);
    bool closeChannel(uint8_t handle, uint32_t timeout_ms = 1000);
    bool setArbitration(uint8_t handle, const Arbitration& arb, uint32_t timeout_ms = 1000);

    // Message operations
    bool sendMessage(uint8_t handle, const std::vector<uint8_t>& data, uint32_t timeout_ms = 1000);
    bool sendMessages(uint8_t handle, const std::vector<std::vector<uint8_t>>& frames, uint32_t timeout_ms = 1000);
    std::vector<CANFrame> receiveMessages(uint32_t timeout_ms);

    // Periodic messages
    std::optional<uint8_t> startPeriodicMessage(uint8_t timeout, const Arbitration& arb,
                                                  const std::vector<uint8_t>& data,
                                                  uint32_t timeout_ms = 1000);
    bool endPeriodicMessage(uint8_t handle, uint32_t timeout_ms = 1000);

    // Error handling
    std::string getLastError() const;

private:
    std::unique_ptr<ITransport> transport_;
    std::vector<uint8_t> receiveBuffer_;
    std::queue<CANFrame> frameQueue_;
    
    mutable std::mutex mutex_;
    std::condition_variable responseCv_;
    bool asyncMode_ = false;
    std::optional<PDUType> expectedResponse_;
    std::optional<PDU> capturedResponse_;
    
    std::string lastError_;

    // Internal helpers
    bool send(const PDU& pdu);
    std::optional<PDU> waitResponse(PDUType type, uint32_t timeout_ms);

    // Process received data, queue frames, notify waiters
    void processReceivedData(const std::vector<uint8_t>& data);
};

} // namespace ecuconnect

#endif // CANYONERO_PROTOCOL_H
