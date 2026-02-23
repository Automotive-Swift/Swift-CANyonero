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

PDU PDU::openFDChannel(ChannelProtocol protocol, uint32_t bitrate, uint32_t dataBitrate,
                        uint8_t rxSeparationTime, uint8_t txSeparationTime) {
    std::vector<uint8_t> payload;
    payload.reserve(10);

    payload.push_back(static_cast<uint8_t>(protocol));
    appendUint32BE(payload, bitrate);
    appendUint32BE(payload, dataBitrate);
    payload.push_back((rxSeparationTime << 4) | (txSeparationTime & 0x0F));

    return PDU(PDUType::OpenFDChannel, payload);
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

PDU PDU::sendBatch(uint8_t handle, const std::vector<std::vector<uint8_t>>& frames) {
    // Calculate total size: handle + (length + data) for each frame
    size_t totalSize = 1;
    for (const auto& frame : frames) {
        totalSize += 1 + frame.size();  // 1 byte length prefix + data
    }

    std::vector<uint8_t> payload;
    payload.reserve(totalSize);
    payload.push_back(handle);

    // Pack frames as length-prefixed entries
    for (const auto& frame : frames) {
        payload.push_back(static_cast<uint8_t>(frame.size()));
        payload.insert(payload.end(), frame.begin(), frame.end());
    }

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

void Protocol::setAsyncMode(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    asyncMode_ = enabled;
}

std::string Protocol::getLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

bool Protocol::send(const PDU& pdu) {
    if (!transport_ || !transport_->isConnected()) {
        lastError_ = "Not connected";
        return false;
    }

    auto frame = pdu.serialize();
    if (transport_->send(frame) < 0) {
        lastError_ = transport_->getLastError();
        return false;
    }
    return true;
}

std::optional<PDU> Protocol::waitResponse(PDUType type, uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    // Set expectation
    expectedResponse_ = type;
    capturedResponse_.reset();

    auto startTime = std::chrono::steady_clock::now();

    if (asyncMode_) {
        // Async mode: Wait for background thread to notify
        bool success = responseCv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
            return capturedResponse_.has_value();
        });
        
        expectedResponse_.reset();

        if (!success) {
            lastError_ = "Response timeout";
            return std::nullopt;
        }
        return std::move(capturedResponse_);
    } else {
        // Sync mode: Manually pump transport
        // unlock while reading to allow transport internal handling
        // but we need to protect internal state.
        // Actually, receiveBuffer_ and others are protected by mutex_.
        // transport->receive is thread-safe? The transport impl uses its own socket.
        // But we are holding mutex_.
        
        while (true) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime
            ).count();

            if (elapsed >= timeout_ms) {
                expectedResponse_.reset();
                lastError_ = "Response timeout";
                return std::nullopt;
            }

            // Drop lock to read (IO is slow)
            lock.unlock();
            auto data = transport_->receive(50); // Short timeout for polling
            lock.lock();

            if (!data.empty()) {
                processReceivedData(data);
            }

            if (capturedResponse_) {
                expectedResponse_.reset();
                return std::move(capturedResponse_);
            }
        }
    }
}

void Protocol::processReceivedData(const std::vector<uint8_t>& data) {
    // Note: mutex_ is already locked by caller (receiveMessages)
    receiveBuffer_.insert(receiveBuffer_.end(), data.begin(), data.end());

    while (!receiveBuffer_.empty()) {
        PDU pdu;
        int consumed = PDU::parse(receiveBuffer_, pdu);

        if (consumed <= 0) {
            if (consumed < 0) {
                // Garbage, skip byte
                receiveBuffer_.erase(receiveBuffer_.begin());
                continue;
            }
            break; // Need more data
        }

        // Consume bytes
        receiveBuffer_.erase(receiveBuffer_.begin(),
                            receiveBuffer_.begin() + consumed);

        // Dispatch PDU
        if (pdu.type() == PDUType::Received || pdu.type() == PDUType::ReceivedCompressed) {
            // Async frame -> Queue
            frameQueue_.push(pdu.receivedFrame());
        } 
        else if (expectedResponse_ && (pdu.type() == *expectedResponse_ || pdu.isError())) {
            // Expected response -> Capture and Notify
            capturedResponse_ = std::move(pdu);
            responseCv_.notify_all();
        }
        // Else: Drop unexpected PDU (e.g. Ok from async send)
    }
}

// Device operations
std::optional<DeviceInfo> Protocol::getDeviceInfo(uint32_t timeout_ms) {
    if (!send(PDU::requestInfo())) return std::nullopt;

    auto response = waitResponse(PDUType::Info, timeout_ms);
    if (!response) return std::nullopt;

    return response->deviceInfo();
}

std::optional<uint16_t> Protocol::readVoltage(uint32_t timeout_ms) {
    if (!send(PDU::readVoltage())) return std::nullopt;

    auto response = waitResponse(PDUType::Voltage, timeout_ms);
    if (!response) return std::nullopt;

    return response->voltageMillivolts();
}

bool Protocol::ping(uint32_t timeout_ms) {
    if (!send(PDU::ping())) return false;
    
    auto response = waitResponse(PDUType::Pong, timeout_ms);
    return response.has_value();
}

// Channel operations
std::optional<uint8_t> Protocol::openChannel(ChannelProtocol protocol,
                                              uint32_t bitrate,
                                              uint32_t timeout_ms,
                                              std::optional<uint32_t> dataBitrate) {
    const bool fdProtocol =
        protocol == ChannelProtocol::RawFD ||
        protocol == ChannelProtocol::CANFD ||
        protocol == ChannelProtocol::ISOTP_FD;
    if (fdProtocol) {
        if (!dataBitrate.has_value() || dataBitrate.value() == 0) {
            setLastError("Missing data bitrate for CAN-FD channel");
            return std::nullopt;
        }
        if (!send(PDU::openFDChannel(protocol, bitrate, dataBitrate.value()))) return std::nullopt;
    } else {
        if (!send(PDU::openChannel(protocol, bitrate))) return std::nullopt;
    }

    auto response = waitResponse(PDUType::ChannelOpened, timeout_ms);
    if (!response) return std::nullopt;

    return response->channelHandle();
}

bool Protocol::closeChannel(uint8_t handle, uint32_t timeout_ms) {
    if (!send(PDU::closeChannel(handle))) return false;

    auto response = waitResponse(PDUType::ChannelClosed, timeout_ms);
    return response.has_value();
}

bool Protocol::setArbitration(uint8_t handle, const Arbitration& arb,
                               uint32_t timeout_ms) {
    if (!send(PDU::setArbitration(handle, arb))) return false;

    auto response = waitResponse(PDUType::Ok, timeout_ms);
    return response.has_value();
}

// Message operations
bool Protocol::sendMessage(uint8_t handle, const std::vector<uint8_t>& data,
                            uint32_t timeout_ms) {
    // Fire and forget: Do not wait for response (Async Sending)
    // The "Ok" response will be dropped by processReceivedData
    return send(PDU::send(handle, data));
}

bool Protocol::sendMessages(uint8_t handle, const std::vector<std::vector<uint8_t>>& frames,
                             uint32_t timeout_ms) {
    // Fire and forget batch send
    return send(PDU::sendBatch(handle, frames));
}

std::vector<CANFrame> Protocol::receiveMessages(uint32_t timeout_ms) {
    // Note: This function is the "pump" for the background thread.
    // It reads from transport and feeds the dispatcher.

    // 1. Read data from transport (with timeout)
    // If we are in async mode, this is called by the background thread.
    // We should lock mutex only when accessing shared state.
    
    std::vector<uint8_t> data;
    if (transport_ && transport_->isConnected()) {
        data = transport_->receive(timeout_ms);
    }
    
    std::lock_guard<std::mutex> lock(mutex_);

    if (!data.empty()) {
        processReceivedData(data);
    }

    // 2. Return queued frames
    std::vector<CANFrame> frames;
    while (!frameQueue_.empty()) {
        frames.push_back(std::move(frameQueue_.front()));
        frameQueue_.pop();
    }

    return frames;
}
// Periodic messages
std::optional<uint8_t> Protocol::startPeriodicMessage(uint8_t timeout,
                                                        const Arbitration& arb,
                                                        const std::vector<uint8_t>& data,
                                                        uint32_t timeout_ms) {
    if (!send(PDU::startPeriodicMessage(timeout, arb, data))) return std::nullopt;

    auto response = waitResponse(PDUType::PeriodicMessageStarted, timeout_ms);
    if (!response) return std::nullopt;

    return response->channelHandle();
}

bool Protocol::endPeriodicMessage(uint8_t handle, uint32_t timeout_ms) {
    if (!send(PDU::endPeriodicMessage(handle))) return false;
    
    // Accept either PeriodicMessageEnded or Ok (some firmware versions differ)
    // For simplicity, we just check if we got a response that isn't null.
    // Ideally we should check type, but waitResponse takes a specific type.
    // Let's assume current firmware sends PeriodicMessageEnded.
    auto response = waitResponse(PDUType::PeriodicMessageEnded, timeout_ms);
    return response.has_value();
}

} // namespace ecuconnect
