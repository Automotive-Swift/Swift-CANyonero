/**
 * CANyonero Protocol Implementation
 */
#include "canyonero_protocol.h"
#include <cstring>
#include <sstream>
#include <chrono>

namespace ecuconnect {

// Helper: append uint16 big-endian
static void appendUint16BE(std::vector<uint8_t>& vec, uint16_t value) {
    vec.push_back(static_cast<uint8_t>(value >> 8));
    vec.push_back(static_cast<uint8_t>(value & 0xFF));
}

// Helper: append uint32 big-endian
static void appendUint32BE(std::vector<uint8_t>& vec, uint32_t value) {
    vec.push_back(static_cast<uint8_t>(value >> 24));
    vec.push_back(static_cast<uint8_t>(value >> 16));
    vec.push_back(static_cast<uint8_t>(value >> 8));
    vec.push_back(static_cast<uint8_t>(value & 0xFF));
}

// Helper: read uint16 big-endian
static uint16_t readUint16BE(const uint8_t* data) {
    return (static_cast<uint16_t>(data[0]) << 8) |
           static_cast<uint16_t>(data[1]);
}

// Helper: read uint32 big-endian
static uint32_t readUint32BE(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

// ============================================================================
// Arbitration
// ============================================================================

std::vector<uint8_t> Arbitration::serialize() const {
    std::vector<uint8_t> result;
    result.reserve(SIZE);
    appendUint32BE(result, request);
    result.push_back(requestExtension);  // Extension interleaved after request
    appendUint32BE(result, replyPattern);
    appendUint32BE(result, replyMask);
    result.push_back(replyExtension);    // Extension at end
    return result;
}

Arbitration Arbitration::deserialize(const uint8_t* data) {
    Arbitration arb;
    arb.request = readUint32BE(data);
    arb.requestExtension = data[4];       // Extension after request
    arb.replyPattern = readUint32BE(data + 5);
    arb.replyMask = readUint32BE(data + 9);
    arb.replyExtension = data[13];
    return arb;
}

// ============================================================================
// PDU
// ============================================================================

PDU::PDU(PDUType type) : type_(type) {}

PDU::PDU(PDUType type, const std::vector<uint8_t>& payload)
    : type_(type), payload_(payload) {}

std::vector<uint8_t> PDU::serialize() const {
    std::vector<uint8_t> frame;
    frame.reserve(PDU_HEADER_SIZE + payload_.size());

    frame.push_back(PDU_ATT);
    frame.push_back(static_cast<uint8_t>(type_));
    appendUint16BE(frame, static_cast<uint16_t>(payload_.size()));
    frame.insert(frame.end(), payload_.begin(), payload_.end());

    return frame;
}

int PDU::parse(const std::vector<uint8_t>& buffer, PDU& out) {
    if (buffer.size() < PDU_HEADER_SIZE) {
        return 0;  // Need more data
    }

    // Check for valid ATT byte
    if (buffer[0] != PDU_ATT) {
        return -1;  // Garbage, need to resync
    }

    uint16_t length = readUint16BE(&buffer[2]);
    size_t totalSize = PDU_HEADER_SIZE + length;

    if (buffer.size() < totalSize) {
        return 0;  // Need more data
    }

    out.type_ = static_cast<PDUType>(buffer[1]);
    out.payload_.assign(buffer.begin() + PDU_HEADER_SIZE,
                        buffer.begin() + totalSize);

    return static_cast<int>(totalSize);
}

// Factory methods
PDU PDU::ping(const std::vector<uint8_t>& data) {
    return PDU(PDUType::Ping, data);
}

PDU PDU::requestInfo() {
    return PDU(PDUType::RequestInfo);
}

PDU PDU::readVoltage() {
    return PDU(PDUType::ReadVoltage);
}

PDU PDU::openChannel(ChannelProtocol protocol, uint32_t bitrate,
                      uint8_t rxSeparationTime, uint8_t txSeparationTime) {
    std::vector<uint8_t> payload;
    payload.reserve(6);

    payload.push_back(static_cast<uint8_t>(protocol));
    appendUint32BE(payload, bitrate);
    // Separation time: high nibble = RX, low nibble = TX
    payload.push_back((rxSeparationTime << 4) | (txSeparationTime & 0x0F));

    return PDU(PDUType::OpenChannel, payload);
}

PDU PDU::closeChannel(uint8_t handle) {
    return PDU(PDUType::CloseChannel, {handle});
}

PDU PDU::send(uint8_t handle, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> payload;
    payload.reserve(1 + data.size());
    payload.push_back(handle);
    payload.insert(payload.end(), data.begin(), data.end());
    return PDU(PDUType::Send, payload);
}

PDU PDU::setArbitration(uint8_t handle, const Arbitration& arb) {
    std::vector<uint8_t> payload;
    payload.reserve(1 + Arbitration::SIZE);
    payload.push_back(handle);
    auto arbData = arb.serialize();
    payload.insert(payload.end(), arbData.begin(), arbData.end());
    return PDU(PDUType::SetArbitration, payload);
}

PDU PDU::startPeriodicMessage(uint8_t timeout, const Arbitration& arb,
                               const std::vector<uint8_t>& data) {
    std::vector<uint8_t> payload;
    payload.reserve(1 + Arbitration::SIZE + data.size());
    payload.push_back(timeout);
    auto arbData = arb.serialize();
    payload.insert(payload.end(), arbData.begin(), arbData.end());
    payload.insert(payload.end(), data.begin(), data.end());
    return PDU(PDUType::StartPeriodicMessage, payload);
}

PDU PDU::endPeriodicMessage(uint8_t handle) {
    return PDU(PDUType::EndPeriodicMessage, {handle});
}

// Response parsing
uint8_t PDU::channelHandle() const {
    if (payload_.empty()) return 0;
    return payload_[0];
}

uint16_t PDU::voltageMillivolts() const {
    if (payload_.size() < 2) return 0;
    return readUint16BE(payload_.data());
}

DeviceInfo PDU::deviceInfo() const {
    DeviceInfo info;
    if (type_ != PDUType::Info || payload_.empty()) {
        return info;
    }

    // Parse newline-separated strings
    std::string data(payload_.begin(), payload_.end());
    std::istringstream iss(data);
    std::getline(iss, info.vendor);
    std::getline(iss, info.model);
    std::getline(iss, info.hardware);
    std::getline(iss, info.serial);
    std::getline(iss, info.firmware);

    return info;
}

CANFrame PDU::receivedFrame() const {
    CANFrame frame{};

    if (type_ != PDUType::Received || payload_.size() < 6) {
        return frame;
    }

    frame.channel = payload_[0];
    frame.id = readUint32BE(&payload_[1]);
    frame.extension = payload_[5];
    frame.data.assign(payload_.begin() + 6, payload_.end());
    frame.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();

    return frame;
}

bool PDU::isError() const {
    uint8_t t = static_cast<uint8_t>(type_);
    return t >= 0xE0 && t <= 0xEF;
}

std::string PDU::errorMessage() const {
    switch (type_) {
        case PDUType::ErrorUnspecified:     return "Unspecified error";
        case PDUType::ErrorHardware:        return "Hardware error";
        case PDUType::ErrorInvalidChannel:  return "Invalid channel";
        case PDUType::ErrorInvalidPeriodic: return "Invalid periodic message";
        case PDUType::ErrorNoResponse:      return "No response";
        case PDUType::ErrorInvalidRPC:      return "Invalid RPC";
        case PDUType::ErrorInvalidCommand:  return "Invalid command";
        default:                            return "Unknown error";
    }
}

// ============================================================================
// Protocol
// ============================================================================

Protocol::Protocol(std::unique_ptr<ITransport> transport)
    : transport_(std::move(transport)) {}

Protocol::~Protocol() {
    disconnect();
}

bool Protocol::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!transport_) {
        lastError_ = "No transport configured";
        return false;
    }
    return transport_->connect();
}

void Protocol::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (transport_) {
        transport_->disconnect();
    }
    receiveBuffer_.clear();
    while (!frameQueue_.empty()) frameQueue_.pop();
}

bool Protocol::isConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return transport_ && transport_->isConnected();
}

std::string Protocol::getLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

std::optional<PDU> Protocol::sendAndReceive(const PDU& pdu, uint32_t timeout_ms) {
    if (!transport_ || !transport_->isConnected()) {
        lastError_ = "Not connected";
        return std::nullopt;
    }

    // Send the PDU
    auto frame = pdu.serialize();
    if (transport_->send(frame) < 0) {
        lastError_ = transport_->getLastError();
        return std::nullopt;
    }

    // Wait for response
    auto startTime = std::chrono::steady_clock::now();

    while (true) {
        // Check timeout
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime
        ).count();

        if (elapsed >= timeout_ms) {
            lastError_ = "Response timeout";
            return std::nullopt;
        }

        // Try to receive data
        uint32_t remainingTimeout = static_cast<uint32_t>(timeout_ms - elapsed);
        auto data = transport_->receive(remainingTimeout);

        if (!data.empty()) {
            receiveBuffer_.insert(receiveBuffer_.end(), data.begin(), data.end());
        }

        // Try to parse PDUs from buffer
        while (!receiveBuffer_.empty()) {
            PDU response;
            int consumed = PDU::parse(receiveBuffer_, response);

            if (consumed < 0) {
                // Garbage at start, skip one byte and retry
                receiveBuffer_.erase(receiveBuffer_.begin());
                continue;
            }

            if (consumed == 0) {
                // Need more data
                break;
            }

            // Remove consumed bytes
            receiveBuffer_.erase(receiveBuffer_.begin(),
                                receiveBuffer_.begin() + consumed);

            // Check if this is a received frame (async data)
            if (response.type() == PDUType::Received ||
                response.type() == PDUType::ReceivedCompressed) {
                // Queue for later retrieval
                frameQueue_.push(response.receivedFrame());
                continue;
            }

            // This should be our response
            return response;
        }
    }
}

void Protocol::processReceivedData(const std::vector<uint8_t>& data) {
    receiveBuffer_.insert(receiveBuffer_.end(), data.begin(), data.end());

    while (!receiveBuffer_.empty()) {
        PDU pdu;
        int consumed = PDU::parse(receiveBuffer_, pdu);

        if (consumed <= 0) {
            if (consumed < 0) {
                receiveBuffer_.erase(receiveBuffer_.begin());
            }
            break;
        }

        receiveBuffer_.erase(receiveBuffer_.begin(),
                            receiveBuffer_.begin() + consumed);

        if (pdu.type() == PDUType::Received) {
            frameQueue_.push(pdu.receivedFrame());
        }
    }
}

// Device operations
std::optional<DeviceInfo> Protocol::getDeviceInfo(uint32_t timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto response = sendAndReceive(PDU::requestInfo(), timeout_ms);
    if (!response || response->type() != PDUType::Info) {
        if (response && response->isError()) {
            lastError_ = response->errorMessage();
        }
        return std::nullopt;
    }

    return response->deviceInfo();
}

std::optional<uint16_t> Protocol::readVoltage(uint32_t timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto response = sendAndReceive(PDU::readVoltage(), timeout_ms);
    if (!response || response->type() != PDUType::Voltage) {
        if (response && response->isError()) {
            lastError_ = response->errorMessage();
        }
        return std::nullopt;
    }

    return response->voltageMillivolts();
}

bool Protocol::ping(uint32_t timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto response = sendAndReceive(PDU::ping(), timeout_ms);
    return response && response->type() == PDUType::Pong;
}

// Channel operations
std::optional<uint8_t> Protocol::openChannel(ChannelProtocol protocol,
                                              uint32_t bitrate,
                                              uint32_t timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto response = sendAndReceive(
        PDU::openChannel(protocol, bitrate), timeout_ms);

    if (!response || response->type() != PDUType::ChannelOpened) {
        if (response && response->isError()) {
            lastError_ = response->errorMessage();
        }
        return std::nullopt;
    }

    return response->channelHandle();
}

bool Protocol::closeChannel(uint8_t handle, uint32_t timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto response = sendAndReceive(PDU::closeChannel(handle), timeout_ms);
    if (!response) return false;

    if (response->isError()) {
        lastError_ = response->errorMessage();
        return false;
    }

    return response->type() == PDUType::ChannelClosed;
}

bool Protocol::setArbitration(uint8_t handle, const Arbitration& arb,
                               uint32_t timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto response = sendAndReceive(
        PDU::setArbitration(handle, arb), timeout_ms);

    if (!response) return false;

    if (response->isError()) {
        lastError_ = response->errorMessage();
        return false;
    }

    return response->type() == PDUType::Ok;
}

// Message operations
bool Protocol::sendMessage(uint8_t handle, const std::vector<uint8_t>& data,
                            uint32_t timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto response = sendAndReceive(PDU::send(handle, data), timeout_ms);
    if (!response) return false;

    if (response->isError()) {
        lastError_ = response->errorMessage();
        return false;
    }

    return response->type() == PDUType::Ok;
}

std::vector<CANFrame> Protocol::receiveMessages(uint32_t timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<CANFrame> frames;

    // First, return any queued frames
    while (!frameQueue_.empty()) {
        frames.push_back(std::move(frameQueue_.front()));
        frameQueue_.pop();
    }

    if (!frames.empty()) {
        return frames;
    }

    // Try to receive more data
    if (transport_ && transport_->isConnected()) {
        auto data = transport_->receive(timeout_ms);
        if (!data.empty()) {
            receiveBuffer_.insert(receiveBuffer_.end(), data.begin(), data.end());

            // Parse received PDUs
            while (!receiveBuffer_.empty()) {
                PDU pdu;
                int consumed = PDU::parse(receiveBuffer_, pdu);

                if (consumed <= 0) {
                    if (consumed < 0) {
                        receiveBuffer_.erase(receiveBuffer_.begin());
                    }
                    break;
                }

                receiveBuffer_.erase(receiveBuffer_.begin(),
                                    receiveBuffer_.begin() + consumed);

                if (pdu.type() == PDUType::Received) {
                    frames.push_back(pdu.receivedFrame());
                }
            }
        }
    }

    return frames;
}

// Periodic messages
std::optional<uint8_t> Protocol::startPeriodicMessage(uint8_t timeout,
                                                        const Arbitration& arb,
                                                        const std::vector<uint8_t>& data,
                                                        uint32_t timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto response = sendAndReceive(
        PDU::startPeriodicMessage(timeout, arb, data), timeout_ms);

    if (!response || response->type() != PDUType::PeriodicMessageStarted) {
        if (response && response->isError()) {
            lastError_ = response->errorMessage();
        }
        return std::nullopt;
    }

    return response->channelHandle();
}

bool Protocol::endPeriodicMessage(uint8_t handle, uint32_t timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto response = sendAndReceive(PDU::endPeriodicMessage(handle), timeout_ms);
    if (!response) return false;

    if (response->isError()) {
        lastError_ = response->errorMessage();
        return false;
    }

    return response->type() == PDUType::PeriodicMessageEnded
        || response->type() == PDUType::Ok;
}

} // namespace ecuconnect
