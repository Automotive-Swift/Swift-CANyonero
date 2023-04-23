///
/// CANyonero. (C) 2022 - 2023 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#include "Protocol.hpp"

#include <sstream>

#include "lz4.h"

/// Protocol Implementation
namespace CANyonero {

//MARK: - Info
Info Info::from_vector(const Bytes& data) {
    Info info;
    std::stringstream ss;
    ss.str(std::string(data.begin(), data.end()));

    std::getline(ss, info.vendor, '\n');
    std::getline(ss, info.model, '\n');
    std::getline(ss, info.hardware, '\n');
    std::getline(ss, info.serial, '\n');
    std::getline(ss, info.firmware, '\n');

    return info;
}

//MARK: - Arbitration
void Arbitration::to_vector(Bytes& payload) const {
    vector_append_uint32(payload, request);
    payload.push_back(requestExtension);
    vector_append_uint32(payload, replyPattern);
    vector_append_uint32(payload, replyMask);
    payload.push_back(replyExtension);
}

static Arbitration from_vector(Bytes::const_iterator& it) {
    Arbitration a;
    a.request = vector_read_uint32(it);
    a.requestExtension = *it++;
    a.replyPattern = vector_read_uint32(it);
    a.replyMask = vector_read_uint32(it);
    a.replyExtension = *it++;
    return a;
}

const Info PDU::information() const {
    assert(_type == PDUType::info);
    return Info::from_vector(_payload);
}

const Arbitration PDU::arbitration() const {
    assert(_type == PDUType::setArbitration || _type == PDUType::startPeriodicMessage);

    auto it = _payload.begin() + 1; // skip channel info (setArbitration) or interval info (startPeriodic)
    return from_vector(it);
}

//MARK: - PDU
ChannelHandle PDU::channel() const {
    assert(_type == PDUType::openChannel ||
           _type == PDUType::closeChannel ||
           _type == PDUType::send ||
           _type == PDUType::sendCompressed ||
           _type == PDUType::received ||
           _type == PDUType::receivedCompressed ||
           _type == PDUType::setArbitration);

    return _payload[0];
}

PeriodicMessageHandle PDU::periodicMessage() const {
    assert(_type == PDUType::endPeriodicMessage);

    return _payload[0];
}

ChannelProtocol PDU::protocol() const {
    assert(_type == PDUType::openChannel);

    return static_cast<ChannelProtocol>(_payload[0]);
}

uint32_t PDU::bitrate() const {
    assert(_type == PDUType::openChannel);
    auto it = _payload.begin() + 1;
    return vector_read_uint32(it);
}

Bytes PDU::data() const {
    switch (_type) {
        case PDUType::received:
            return Bytes(_payload.begin() + 6, _payload.end());
        case PDUType::send:
            return Bytes(_payload.begin() + 1, _payload.end());
        case PDUType::sendUpdateData:
            return Bytes(_payload.begin(), _payload.end());
        default:
            assert(false);
    }
}

Bytes PDU::uncompressedData() const {
    switch (_type) {
        case PDUType::receivedCompressed: {
            auto length = uncompressedLength();
            auto uncompressedData = std::make_unique<char[]>(length);
            // offset computes from channel (1), id (4), extension (1), uncompressed length (2) = 8
            LZ4_decompress_safe(reinterpret_cast<const char*>(_payload.data() + 8), uncompressedData.get(), _length - 8, length);
            return Bytes(uncompressedData.get(), uncompressedData.get() + length);
        }

        case PDUType::sendCompressed: {
            auto length = uncompressedLength();
            auto uncompressedData = std::make_unique<char[]>(length);
            // offset computes from channel (1), uncompressed length (2) = 3
            LZ4_decompress_safe(reinterpret_cast<const char*>(_payload.data() + 3), uncompressedData.get(), _length - 3, length);
            return Bytes(uncompressedData.get(), uncompressedData.get() + length);
        }
        default:
            assert(false);
    }
}

uint16_t PDU::uncompressedLength() const {
    switch (_type) {
        case PDUType::receivedCompressed: {
            // offset computes from channel (1), id (4), extension (1) = 6
            auto it = _payload.begin() + 6;
            return vector_read_uint16(it);
        }
        case PDUType::sendCompressed: {
            // offset computes from channel (1)
            auto it = _payload.begin() + 1;
            return vector_read_uint16(it);
        }
        default:
            assert(false);
    }
}

const Bytes& PDU::payload() const {
    return _payload;
}

PDU::PDU(const Bytes& frame) {
    assert(frame.size() >= PDU::HEADER_SIZE);
    _length = frame[2] << 8 | frame[3];
    assert(frame.size() == PDU::HEADER_SIZE + _length);

    _type = static_cast<PDUType>(frame[1]);
    _payload = Bytes(frame.begin() + 4, frame.end());
}

const Bytes PDU::frame() const {

    std::vector<uint8_t> frame = {
        PDU::ATT,
        static_cast<uint8_t>(_type),
        static_cast<uint8_t>(_payload.size() >> 8),
        static_cast<uint8_t>(_payload.size() & 0xff),
    };
    frame.insert(frame.end(), _payload.begin(), _payload.end());
    return frame;
}

int PDU::containsPDU(const Bytes& bytes) {
    if (bytes.size() < PDU::HEADER_SIZE) { return -1; }
    uint16_t payloadLength = bytes[2] << 8 | bytes[3];
    if (bytes.size() < PDU::HEADER_SIZE + payloadLength) { return -1; }
    return PDU::HEADER_SIZE + payloadLength;
}

//MARK: - Tester -> Adapter PDU Construction
PDU PDU::ping(std::vector<uint8_t> payload) {
    return PDU(PDUType::ping, payload);
}

PDU PDU::requestInfo() {
    return PDU(PDUType::requestInfo);
}

PDU PDU::readVoltage() {
    return PDU(PDUType::readVoltage);
}

PDU PDU::reset() {
    return PDU(PDUType::reset);
}

PDU PDU::openChannel(const ChannelProtocol protocol, uint32_t bitrate) {
    auto payload = Bytes(static_cast<uint8_t>(protocol));
    vector_append_uint32(payload, bitrate);
    return PDU(PDUType::openChannel, payload);
}

PDU PDU::closeChannel(const ChannelHandle handle) {
    auto payload = Bytes(1, handle);
    return PDU(PDUType::openChannel, payload);
}

PDU PDU::send(const ChannelHandle handle, const Bytes& data) {
    auto payload = Bytes(1, handle);
    payload.insert(payload.end(), data.begin(), data.end());
    return PDU(PDUType::send, payload);
}

PDU PDU::sendCompressed(const ChannelHandle handle, const Bytes& uncompressedData) {
    const uint16_t uncompressedLength = uncompressedData.size();
    auto bound = LZ4_compressBound(uncompressedLength);
    auto buffer = new char[bound];
    auto compressedLength = LZ4_compress_default(reinterpret_cast<const char*>(uncompressedData.data()), buffer, uncompressedLength, bound);

    auto payload = Bytes(1, handle);
    vector_append_uint16(payload, uncompressedLength);
    payload.insert(payload.end(), buffer, buffer + compressedLength);
    delete[] buffer;
    return PDU(PDUType::sendCompressed, payload);
}

PDU PDU::setArbitration(const ChannelHandle handle, const Arbitration arbitration) {
    auto payload = Bytes(1, handle);
    arbitration.to_vector(payload);
    return PDU(PDUType::setArbitration, payload);
}

PDU PDU::startPeriodicMessage(const uint8_t interval, const Arbitration arbitration, const Bytes data) {
    auto payload = Bytes(1, interval);
    arbitration.to_vector(payload);
    payload.insert(payload.end(), data.begin(), data.end());
    return PDU(PDUType::startPeriodicMessage, payload);
}

PDU PDU::endPeriodicMessage(const PeriodicMessageHandle handle) {
    auto payload = Bytes(1, handle);
    return PDU(PDUType::endPeriodicMessage, payload);
}

PDU PDU::prepareForUpdate() {
    return PDU(PDUType::prepareForUpdate);
}

PDU PDU::sendUpdateData(const Bytes data) {
    return PDU(PDUType::sendUpdateData, data);
}

PDU PDU::commitUpdate() {
    return PDU(PDUType::commitUpdate);
}

//MARK: - Adapter -> Tester PDU Construction
PDU PDU::ok() {
    return PDU(PDUType::ok);
}

PDU PDU::pong(const Bytes payload) {
    return PDU(PDUType::pong, payload);
}

PDU PDU::info(const std::string vendor, const std::string model, const std::string hardware, const std::string serial, const std::string firmware) {
    std::vector<uint8_t> payload;
    std::vector<uint8_t> vendor_bytes(vendor.begin(), vendor.end());
    std::vector<uint8_t> model_bytes(model.begin(), model.end());
    std::vector<uint8_t> hardware_bytes(hardware.begin(), hardware.end());
    std::vector<uint8_t> serial_bytes(serial.begin(), serial.end());
    std::vector<uint8_t> firmware_bytes(firmware.begin(), firmware.end());

    payload.insert(payload.end(), vendor_bytes.begin(), vendor_bytes.end());
    payload.push_back('\n');
    payload.insert(payload.end(), model_bytes.begin(), model_bytes.end());
    payload.push_back('\n');
    payload.insert(payload.end(), hardware_bytes.begin(), hardware_bytes.end());
    payload.push_back('\n');
    payload.insert(payload.end(), serial_bytes.begin(), serial_bytes.end());
    payload.push_back('\n');
    payload.insert(payload.end(), firmware_bytes.begin(), firmware_bytes.end());

    return PDU(PDUType::info, payload);
}

PDU PDU::voltage(const uint16_t millivolts) {
    auto payload = Bytes();
    vector_append_uint16(payload, millivolts);
    return PDU(PDUType::voltage, payload);
}

PDU PDU::channelOpened(const ChannelHandle handle) {
    auto payload = Bytes(1, handle);
    return PDU(PDUType::channelOpened, payload);
}

PDU PDU::channelClosed(const ChannelHandle handle) {
    auto payload = Bytes(1, handle);
    return PDU(PDUType::channelClosed, payload);
}

PDU PDU::received(const ChannelHandle handle, const uint32_t id, const uint8_t extension, const Bytes& data) {
    auto payload = Bytes(1, handle);
    vector_append_uint32(payload, id);
    payload.push_back(extension);
    payload.insert(payload.end(), data.begin(), data.end());
    return PDU(PDUType::received, payload);
}

PDU PDU::receivedCompressed(const ChannelHandle handle, const uint32_t id, const uint8_t extension, const Bytes& uncompressedData) {
    const uint16_t uncompressedLength = uncompressedData.size();
    auto bound = LZ4_compressBound(uncompressedLength);
    auto buffer = new char[bound];
    auto compressedLength = LZ4_compress_default(reinterpret_cast<const char*>(uncompressedData.data()), buffer, uncompressedLength, bound);

    auto payload = Bytes(1, handle);
    vector_append_uint32(payload, id);
    payload.push_back(extension);
    vector_append_uint16(payload, uncompressedLength);
    payload.insert(payload.end(), buffer, buffer + compressedLength);
    delete[] buffer;
    return PDU(PDUType::receivedCompressed, payload);
}

PDU PDU::periodicMessageStarted(const PeriodicMessageHandle handle) {
    auto payload = Bytes(1, handle);
    return PDU(PDUType::periodicMessageStarted, payload);
}

PDU PDU::periodicMessageEnded(const PeriodicMessageHandle handle) {
    auto payload = Bytes(1, handle);
    return PDU(PDUType::periodicMessageEnded, payload);
}

PDU PDU::updateStartedSendData() {
    return PDU(PDUType::updateStartedSendData);
}

PDU PDU::updateDataReceived() {
    return PDU(PDUType::updateDataReceived);
}

PDU PDU::updateCompleted() {
    return PDU(PDUType::updateCompleted);
}

PDU PDU::errorUnspecified() {
    return PDU(PDUType::errorUnspecified);
}

PDU PDU::errorHardware() {
    return PDU(PDUType::errorHardware);
}

PDU PDU::errorInvalidChannel() {
    return PDU(PDUType::errorInvalidChannel);
}

PDU PDU::errorInvalidPeriodic() {
    return PDU(PDUType::errorInvalidPeriodic);
}

PDU PDU::errorNoResponse() {
    return PDU(PDUType::errorNoResponse);
}

PDU PDU::errorInvalidCommand() {
    return PDU(PDUType::errorInvalidCommand);
}

};
