///
/// CANyonero. (C) 2022 - 2023 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#include "Protocol.hpp"

/// Helpers
void vector_append_uint32(std::vector<uint8_t>& vec, uint32_t value) {
    vec.push_back(static_cast<uint8_t>(value >> 24));
    vec.push_back(static_cast<uint8_t>(value >> 16));
    vec.push_back(static_cast<uint8_t>(value >> 8));
    vec.push_back(static_cast<uint8_t>(value & 0xFF));
}

/// Protocol Implementation
namespace CANyonero {

void Arbitration::serialize(Bytes& payload) const {
    vector_append_uint32(payload, request);
    payload.push_back(requestExtension);
    vector_append_uint32(payload, replyPattern);
    vector_append_uint32(payload, replyMask);
    payload.push_back(replyExtension);
}

const std::vector<uint8_t> PDU::frame() const {

    std::vector<uint8_t> frame = {
        PDU::ATT,
        static_cast<uint8_t>(_type),
        static_cast<uint8_t>(_payload.size() >> 8),
        static_cast<uint8_t>(_payload.size() & 0xff),
    };
    frame.insert(frame.end(), _payload.begin(), _payload.end());
    return frame;
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

PDU PDU::openChannel(const ChannelProtocol protocol) {
    auto payload = Bytes(static_cast<uint8_t>(protocol));
    return PDU(PDUType::openChannel, payload);
}

PDU PDU::closeChannel(const ChannelHandle handle) {
    auto payload = Bytes(handle);
    return PDU(PDUType::openChannel, payload);
}

PDU PDU::send(const ChannelHandle handle, const Bytes data) {
    auto payload = Bytes(handle);
    payload.insert(payload.end(), data.begin(), data.end());
    return PDU(PDUType::send, payload);
}

PDU PDU::setArbitration(const ChannelHandle handle, const Arbitration arbitration) {
    auto payload = Bytes(handle);
    arbitration.serialize(payload);
    return PDU(PDUType::setArbitration, payload);
}

PDU PDU::startPeriodicMessage(const Arbitration arbitration, const Bytes data) {
    auto payload = Bytes();
    arbitration.serialize(payload);
    payload.insert(payload.end(), data.begin(), data.end());
    return PDU(PDUType::startPeriodicMessage, payload);
}

PDU PDU::endPeriodicMessage(const PeriodicMessageHandle handle) {
    auto payload = Bytes(handle);
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

PDU PDU::reset() {
    return PDU(PDUType::reset);
}

//MARK: - Adapter -> Tester PDU Construction


};
