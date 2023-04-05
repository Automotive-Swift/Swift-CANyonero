///
/// CANyonero. (C) 2022 - 2023 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#include "Protocol.hpp"

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
PDU PDU::pong(const Bytes payload) {
    return PDU(PDUType::pong, payload);
}

PDU PDU::info(std::string vendor, std::string model, std::string hardware, std::string serial, std::string firmware) {
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

PDU PDU::voltage(uint16_t millivolts) {
    auto payload = Bytes();
    vector_append_uint16(payload, millivolts);
    return PDU(PDUType::voltage, payload);
}

PDU PDU::channelOpened(ChannelHandle handle) {
    auto payload = Bytes(handle);
    return PDU(PDUType::channelOpened, payload);
}

PDU PDU::channelClosed(ChannelHandle handle) {
    auto payload = Bytes(handle);
    return PDU(PDUType::channelOpened, payload);
}

PDU PDU::sent(ChannelHandle handle, uint16_t numberOfBytes) {
    auto payload = Bytes(handle);
    vector_append_uint16(payload, numberOfBytes);
    return PDU(PDUType::sent, payload);
}

PDU PDU::arbitrationSet() {
    return PDU(PDUType::arbitrationSet);
}

PDU PDU::periodicMessageStarted(PeriodicMessageHandle handle) {
    auto payload = Bytes(handle);
    return PDU(PDUType::periodicMessageStarted, payload);
}

PDU PDU::periodicMessageEnded(PeriodicMessageHandle handle) {
    auto payload = Bytes(handle);
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

PDU PDU::resetting() {
    return PDU(PDUType::resetting);
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
