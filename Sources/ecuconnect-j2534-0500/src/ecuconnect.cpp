#include "ecuconnect.h"
#include "../../ecuconnect-j2534/include/transport.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#define DBG(fmt, ...) fprintf(stderr, "[ecuconnect-0500] " fmt "\n", ##__VA_ARGS__)

namespace ecuconnect_v0500 {

namespace {

using ecuconnect::Arbitration;
using ecuconnect::ChannelProtocol;
using ecuconnect::DeviceInfo;
using ecuconnect::Protocol;
using ecuconnect::TransportType;
using ecuconnect::createTransport;

uint32_t read_u32_be(const unsigned char* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

void write_u32_be(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

bool filter_matches(const Filter& filter, const std::vector<uint8_t>& data) {
    const size_t size = std::min(filter.mask.size(), filter.pattern.size());
    if (size == 0 || data.size() < size) {
        return size == 0;
    }
    for (size_t i = 0; i < size; ++i) {
        if ((data[i] & filter.mask[i]) != (filter.pattern[i] & filter.mask[i])) {
            return false;
        }
    }
    return true;
}

bool message_passes_filters(const Channel& channel, const std::vector<uint8_t>& data) {
    bool hasPass = false;
    bool passMatch = false;
    for (const auto& [id, filter] : channel.filters) {
        (void)id;
        if (!filter.active) {
            continue;
        }
        if (filter.type == PASS_FILTER) {
            hasPass = true;
            if (filter_matches(filter, data)) {
                passMatch = true;
            }
        } else if (filter.type == BLOCK_FILTER && filter_matches(filter, data)) {
            return false;
        }
    }
    return hasPass ? passMatch : true;
}

bool arbitration_equals(const Arbitration& lhs, const Arbitration& rhs) {
    return lhs.request == rhs.request
        && lhs.replyPattern == rhs.replyPattern
        && lhs.replyMask == rhs.replyMask
        && lhs.requestExtension == rhs.requestExtension
        && lhs.replyExtension == rhs.replyExtension;
}

bool protocol_uses_can_header(uint32_t protocolId) {
    return protocolId == CAN || protocolId == ISO15765 || protocolId == CAN_FD_PS || protocolId == ISO15765_FD_PS;
}

size_t max_payload_size_for_protocol(uint32_t protocolId) {
    switch (protocolId) {
        case CAN: return 12;
        case CAN_FD_PS: return 68;
        case ISO15765: return 4099;
        case ISO15765_FD_PS: return 4099;
        case ISO9141:
        case ISO14230:
            return 4128;
        default:
            return 0;
    }
}

std::optional<ChannelProtocol> map_protocol(uint32_t protocolId) {
    switch (protocolId) {
        case CAN: return ChannelProtocol::Raw;
        case CAN_FD_PS: return ChannelProtocol::RawFD;
        case ISO9141:
        case ISO14230:
            return ChannelProtocol::KLine;
        default:
            return std::nullopt;
    }
}

uint32_t default_data_rate(uint32_t nominal) {
    if (nominal >= 1000000) {
        return nominal * 2;
    }
    return 2000000;
}

std::string make_display_name(const DeviceInfo& info, const std::string& connectionString) {
    std::string name = info.vendor.empty() ? "ECUconnect" : info.vendor;
    if (!info.model.empty()) {
        name += " " + info.model;
    }
    if (!info.serial.empty()) {
        name += " " + info.serial;
    } else if (!connectionString.empty()) {
        name += " " + connectionString;
    }
    if (name.size() >= 80) {
        name.resize(79);
    }
    return name;
}

} // namespace

DeviceManager& DeviceManager::instance() {
    static DeviceManager instance;
    return instance;
}

Device* DeviceManager::getDevice(uint32_t deviceId) {
    auto it = devices_.find(deviceId);
    return it == devices_.end() ? nullptr : it->second.get();
}

Channel* DeviceManager::getChannel(uint32_t channelId) {
    auto devIt = channelToDevice_.find(channelId);
    if (devIt == channelToDevice_.end()) {
        return nullptr;
    }
    auto* device = getDevice(devIt->second);
    if (!device) {
        return nullptr;
    }
    auto chIt = device->channels.find(channelId);
    return chIt == device->channels.end() ? nullptr : chIt->second.get();
}

const char* DeviceManager::getLastError() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return lastError_.c_str();
}

void DeviceManager::setLastError(const std::string& error) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    lastError_ = error;
}

int32_t DeviceManager::scanForDevices() {
    std::lock_guard<std::mutex> lock(mutex_);
    scanEntries_.clear();
    nextScanIndex_ = 0;

    std::vector<std::string> candidates = {"", "192.168.42.42:129"};
    for (const auto& candidate : candidates) {
        auto transport = createTransport(TransportType::TCP, candidate);
        if (!transport) {
            continue;
        }
        auto protocol = std::make_unique<Protocol>(std::move(transport));
        ScanEntry entry{};
        entry.connectionString = candidate;
        if (protocol->connect()) {
            auto info = protocol->getDeviceInfo(1000);
            if (info) {
                entry.info = *info;
                entry.available = true;
                entry.displayName = make_display_name(*info, candidate);
            }
            protocol->disconnect();
        }
        if (entry.displayName.empty()) {
            entry.displayName = candidate.empty() ? "ECUconnect Default TCP" : "ECUconnect " + candidate;
        }
        if (scanEntries_.empty() || scanEntries_.back().displayName != entry.displayName) {
            scanEntries_.push_back(std::move(entry));
        }
    }

    if (scanEntries_.empty()) {
        ScanEntry fallback{};
        fallback.connectionString = "";
        fallback.displayName = "ECUconnect Default TCP";
        scanEntries_.push_back(std::move(fallback));
    }

    return STATUS_NOERROR;
}

int32_t DeviceManager::getNextDevice(SDEVICE* device) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!device) {
        setLastError("Null device parameter");
        return ERR_NULL_PARAMETER;
    }
    if (nextScanIndex_ >= scanEntries_.size()) {
        return ERR_BUFFER_EMPTY;
    }

    const auto& entry = scanEntries_[nextScanIndex_++];
    std::memset(device, 0, sizeof(*device));
    std::strncpy(device->DeviceName, entry.displayName.c_str(), sizeof(device->DeviceName) - 1);
    device->DeviceAvailable = entry.available ? 1U : 0U;
    device->DeviceDLLFWStatus = entry.available ? 1U : 0U;
    device->DeviceConnectMedia = 1U;
    device->DeviceConnectSpeed = 100000000U;
    device->DeviceSignalQuality = entry.available ? 100U : 0U;
    device->DeviceSignalStrength = entry.available ? 100U : 0U;
    return STATUS_NOERROR;
}

int32_t DeviceManager::openDevice(const char* name, uint32_t* deviceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!deviceId) {
        setLastError("Null device ID pointer");
        return ERR_NULL_PARAMETER;
    }

    const std::string connectionString = name ? name : "";
    auto transport = createTransport(TransportType::TCP, connectionString);
    if (!transport) {
        setLastError("Failed to create transport");
        return ERR_FAILED;
    }

    auto protocol = std::make_unique<Protocol>(std::move(transport));
    if (!protocol->connect()) {
        setLastError("Failed to connect: " + protocol->getLastError());
        return ERR_DEVICE_NOT_CONNECTED;
    }

    auto info = protocol->getDeviceInfo(2000);
    if (!info) {
        setLastError("Failed to get device info: " + protocol->getLastError());
        return ERR_DEVICE_NOT_CONNECTED;
    }

    auto device = std::make_unique<Device>();
    device->protocol = std::move(protocol);
    device->info = *info;
    device->connectionString = connectionString;

    *deviceId = nextDeviceId_++;
    devices_[*deviceId] = std::move(device);
    return STATUS_NOERROR;
}

int32_t DeviceManager::closeDevice(uint32_t deviceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = devices_.find(deviceId);
    if (it == devices_.end()) {
        setLastError("Invalid device ID");
        return ERR_INVALID_DEVICE_ID;
    }

    auto& device = it->second;
    device->stopPolling = true;
    if (device->pollingThread.joinable()) {
        device->pollingThread.join();
    }

    for (auto& [channelId, channel] : device->channels) {
        if (device->protocol && device->protocol->isConnected() && channel->physical) {
            device->protocol->closeChannel(channel->ecuHandle, 1000);
        }
        channelToDevice_.erase(channelId);
    }
    if (device->protocol) {
        device->protocol->disconnect();
    }
    devices_.erase(it);
    return STATUS_NOERROR;
}

int32_t DeviceManager::connect(uint32_t deviceId, uint32_t protocolId, uint32_t flags,
                               uint32_t baudrate, const RESOURCE_STRUCT* resources, uint32_t* channelId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!channelId) {
        setLastError("Null channel ID pointer");
        return ERR_NULL_PARAMETER;
    }
    auto* device = getDevice(deviceId);
    if (!device) {
        setLastError("Invalid device ID");
        return ERR_INVALID_DEVICE_ID;
    }
    if (!device->channels.empty()) {
        setLastError("ECUconnect supports only one active physical channel");
        return ERR_CHANNEL_IN_USE;
    }
    auto ecuProtocol = map_protocol(protocolId);
    if (!ecuProtocol) {
        setLastError("Protocol not supported as physical channel");
        return ERR_INVALID_PROTOCOL_ID;
    }
    if (baudrate == 0) {
        setLastError("Invalid baudrate");
        return ERR_INVALID_BAUDRATE;
    }

    std::optional<uint32_t> dataBitrate = std::nullopt;
    if (protocolId == CAN_FD_PS) {
        dataBitrate = default_data_rate(baudrate);
    }

    auto handle = device->protocol->openChannel(*ecuProtocol, baudrate, 1000, dataBitrate);
    if (!handle) {
        setLastError("Failed to open channel: " + device->protocol->getLastError());
        return ERR_FAILED;
    }

    auto channel = std::make_unique<Channel>();
    channel->id = nextChannelId_++;
    channel->deviceId = deviceId;
    channel->protocolId = protocolId;
    channel->flags = flags;
    channel->baudrate = baudrate;
    channel->dataPhaseRate = dataBitrate.value_or(0);
    channel->ecuHandle = *handle;
    channel->ecuProtocol = *ecuProtocol;
    channel->physical = true;

    if (resources && resources->NumOfResources > 0 && resources->ResourceListPtr) {
        (void)resources;
    }

    *channelId = channel->id;
    channelToDevice_[*channelId] = deviceId;
    device->channels[*channelId] = std::move(channel);

    device->stopPolling = false;
    device->protocol->setAsyncMode(true);
    device->pollingThread = std::thread(&DeviceManager::pollingThreadFunc, this, deviceId);
    return STATUS_NOERROR;
}

int32_t DeviceManager::disconnect(uint32_t channelId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto devIt = channelToDevice_.find(channelId);
    if (devIt == channelToDevice_.end()) {
        setLastError("Invalid channel ID");
        return ERR_INVALID_CHANNEL_ID;
    }
    auto* device = getDevice(devIt->second);
    if (!device) {
        setLastError("Invalid device ID");
        return ERR_INVALID_DEVICE_ID;
    }
    auto chIt = device->channels.find(channelId);
    if (chIt == device->channels.end()) {
        setLastError("Invalid channel ID");
        return ERR_INVALID_CHANNEL_ID;
    }

    const bool physical = chIt->second->physical;
    if (physical) {
        device->stopPolling = true;
        if (device->pollingThread.joinable()) {
            device->pollingThread.join();
        }
        if (device->protocol && device->protocol->isConnected()) {
            device->protocol->closeChannel(chIt->second->ecuHandle, 1000);
        }
    }

    device->channels.erase(chIt);
    channelToDevice_.erase(devIt);
    return STATUS_NOERROR;
}

int32_t DeviceManager::logicalConnect(uint32_t physicalChannelId, uint32_t protocolId, uint32_t flags,
                                      void* descriptor, uint32_t* channelId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!descriptor || !channelId) {
        setLastError("Null logical connect parameter");
        return ERR_NULL_PARAMETER;
    }
    Channel* physical = getChannel(physicalChannelId);
    if (!physical || !physical->physical) {
        setLastError("Invalid physical channel ID");
        return ERR_INVALID_CHANNEL_ID;
    }
    if (protocolId != ISO15765 && protocolId != ISO15765_FD_PS) {
        setLastError("Only ISO15765 logical channels are supported");
        return ERR_INVALID_PROTOCOL_ID;
    }
    if (!(physical->protocolId == CAN || physical->protocolId == CAN_FD_PS)) {
        setLastError("ISO15765 logical channels require CAN or CAN-FD physical transport");
        return ERR_INVALID_PROTOCOL_ID;
    }

    auto* device = getDevice(physical->deviceId);
    auto logical = std::make_unique<Channel>();
    logical->id = nextChannelId_++;
    logical->deviceId = physical->deviceId;
    logical->protocolId = protocolId;
    logical->flags = flags;
    logical->baudrate = physical->baudrate;
    logical->dataPhaseRate = physical->dataPhaseRate;
    logical->fdTxDataLength = physical->fdTxDataLength;
    logical->ecuHandle = physical->ecuHandle;
    logical->ecuProtocol = physical->ecuProtocol;
    logical->loopback = physical->loopback;
    logical->physical = false;

    const auto* isoDesc = static_cast<ISO15765_CHANNEL_DESCRIPTOR*>(descriptor);
    Arbitration arb{};
    arb.request = read_u32_be(isoDesc->RemoteAddress + 1);
    arb.replyPattern = read_u32_be(isoDesc->LocalAddress + 1);
    arb.replyMask = 0xFFFFFFFF;
    arb.requestExtension = (isoDesc->RemoteTxFlags & ISO15765_ADDR_TYPE) ? isoDesc->RemoteAddress[0] : 0;
    arb.replyExtension = (isoDesc->LocalTxFlags & ISO15765_ADDR_TYPE) ? isoDesc->LocalAddress[0] : 0;
    logical->lastTxArb = arb;
    logical->hasTxArb = true;

    *channelId = logical->id;
    channelToDevice_[*channelId] = logical->deviceId;
    device->channels[*channelId] = std::move(logical);
    return STATUS_NOERROR;
}

int32_t DeviceManager::logicalDisconnect(uint32_t channelId) {
    return disconnect(channelId);
}

int32_t DeviceManager::select(uint32_t deviceId, uint32_t selectType, const SCHANNELSET* channelSet, uint32_t timeout) {
    (void)timeout;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!getDevice(deviceId)) {
        setLastError("Invalid device ID");
        return ERR_INVALID_DEVICE_ID;
    }
    if (selectType != READABLE_TYPE || !channelSet || !channelSet->ChannelList) {
        setLastError("Invalid select parameters");
        return ERR_INVALID_IOCTL_VALUE;
    }
    uint32_t readable = 0;
    for (uint32_t i = 0; i < channelSet->ChannelCount; ++i) {
        auto* channel = getChannel(channelSet->ChannelList[i]);
        if (!channel) {
            continue;
        }
        std::lock_guard<std::mutex> rxLock(channel->rxMutex);
        if (!channel->rxQueue.empty()) {
            ++readable;
        }
    }
    return readable >= channelSet->ChannelThreshold ? STATUS_NOERROR : ERR_TIMEOUT;
}

void DeviceManager::pollingThreadFunc(uint32_t deviceId) {
    while (true) {
        Device* device = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            device = getDevice(deviceId);
            if (!device || device->stopPolling) {
                return;
            }
        }
        if (!device->protocol || !device->protocol->isConnected()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        auto frames = device->protocol->receiveMessages(100);
        if (frames.empty()) {
            continue;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& frame : frames) {
            for (auto& [id, channelPtr] : device->channels) {
                (void)id;
                auto& channel = *channelPtr;
                if (channel.physical && channel.ecuHandle != frame.channel) {
                    continue;
                }

                QueuedMessage msg{};
                msg.protocolId = channel.protocolId;
                msg.timestamp = static_cast<uint32_t>(frame.timestamp & 0xFFFFFFFF);
                if (protocol_uses_can_header(channel.protocolId)) {
                    write_u32_be(msg.data, frame.id);
                    if (frame.extension != 0) {
                        msg.rxStatus |= ISO15765_ADDR_TYPE_STATUS;
                    }
                    if (frame.id > 0x7FF) {
                        msg.rxStatus |= CAN_29BIT_ID_STATUS;
                    }
                }
                msg.data.insert(msg.data.end(), frame.data.begin(), frame.data.end());
                msg.extraDataIndex = static_cast<uint32_t>(msg.data.size());

                if (!message_passes_filters(channel, msg.data)) {
                    continue;
                }

                std::lock_guard<std::mutex> rxLock(channel.rxMutex);
                channel.rxQueue.push(std::move(msg));
                channel.rxCv.notify_one();
            }
        }
    }
}

int32_t DeviceManager::readMsgs(uint32_t channelId, PASSTHRU_MSG* msgs, uint32_t* numMsgs, uint32_t timeout) {
    if (!msgs || !numMsgs) {
        setLastError("Null parameter");
        return ERR_NULL_PARAMETER;
    }

    Channel* channel = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        channel = getChannel(channelId);
    }
    if (!channel) {
        setLastError("Invalid channel ID");
        return ERR_INVALID_CHANNEL_ID;
    }

    const uint32_t requested = *numMsgs;
    *numMsgs = 0;

    std::unique_lock<std::mutex> rxLock(channel->rxMutex);
    if (channel->rxQueue.empty() && timeout > 0) {
        channel->rxCv.wait_for(rxLock, std::chrono::milliseconds(timeout), [&] {
            return !channel->rxQueue.empty();
        });
    }

    int32_t status = STATUS_NOERROR;
    while (*numMsgs < requested && !channel->rxQueue.empty()) {
        auto queued = std::move(channel->rxQueue.front());
        channel->rxQueue.pop();
        auto& target = msgs[*numMsgs];
        target.ProtocolID = queued.protocolId;
        target.MsgHandle = queued.msgHandle;
        target.RxStatus = queued.rxStatus;
        target.TxFlags = queued.txFlags;
        target.Timestamp = queued.timestamp;
        target.DataLength = static_cast<uint32_t>(queued.data.size());
        target.ExtraDataIndex = queued.extraDataIndex;

        if (!target.DataBuffer) {
            setLastError("Null DataBuffer");
            return ERR_NULL_PARAMETER;
        }

        const uint32_t copyLen = std::min(target.DataBufferSize, target.DataLength);
        if (copyLen > 0) {
            std::memcpy(target.DataBuffer, queued.data.data(), copyLen);
        }
        if (copyLen < target.DataLength) {
            status = ERR_BUFFER_TOO_SMALL;
        }

        ++(*numMsgs);
    }

    if (*numMsgs == 0) {
        return timeout > 0 ? ERR_TIMEOUT : ERR_BUFFER_EMPTY;
    }
    return status;
}

int32_t DeviceManager::queueMsgs(uint32_t channelId, const PASSTHRU_MSG* msgs, uint32_t* numMsgs, uint32_t timeout) {
    if (!msgs || !numMsgs) {
        setLastError("Null parameter");
        return ERR_NULL_PARAMETER;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto devIt = channelToDevice_.find(channelId);
    if (devIt == channelToDevice_.end()) {
        setLastError("Invalid channel ID");
        return ERR_INVALID_CHANNEL_ID;
    }
    auto* device = getDevice(devIt->second);
    auto* channel = getChannel(channelId);
    if (!device || !channel) {
        setLastError("Invalid channel");
        return ERR_INVALID_CHANNEL_ID;
    }

    const uint32_t requested = *numMsgs;
    *numMsgs = 0;

    for (uint32_t i = 0; i < requested; ++i) {
        const auto& msg = msgs[i];
        if (msg.ProtocolID != channel->protocolId) {
            setLastError("Message protocol mismatch");
            return ERR_MSG_PROTOCOL_ID;
        }
        if (!msg.DataBuffer || msg.DataLength > msg.DataBufferSize) {
            setLastError("Invalid message buffer");
            return ERR_INVALID_MSG;
        }
        if (msg.DataLength > max_payload_size_for_protocol(channel->protocolId)) {
            setLastError("Message too large for protocol");
            return ERR_INVALID_MSG;
        }

        std::vector<uint8_t> payload;
        Arbitration arb{};

        if (protocol_uses_can_header(channel->protocolId)) {
            if (msg.DataLength < 4) {
                setLastError("CAN-family message missing identifier");
                return ERR_INVALID_MSG;
            }
            const uint32_t canId = read_u32_be(msg.DataBuffer);
            arb.request = canId;
            arb.replyPattern = channel->hasTxArb ? channel->lastTxArb.replyPattern : 0;
            arb.replyMask = channel->hasTxArb ? channel->lastTxArb.replyMask : 0;
            arb.requestExtension = (msg.TxFlags & ISO15765_ADDR_TYPE) ? msg.DataBuffer[0] : 0;
            arb.replyExtension = channel->hasTxArb ? channel->lastTxArb.replyExtension : 0;
            payload.assign(msg.DataBuffer + 4, msg.DataBuffer + msg.DataLength);

            if (!channel->hasTxArb || !arbitration_equals(channel->lastTxArb, arb)) {
                if (!device->protocol->setArbitration(channel->ecuHandle, arb, timeout)) {
                    setLastError("Failed to set arbitration: " + device->protocol->getLastError());
                    return ERR_FAILED;
                }
                channel->hasTxArb = true;
                channel->lastTxArb = arb;
            }
        } else {
            payload.assign(msg.DataBuffer, msg.DataBuffer + msg.DataLength);
        }

        if (!device->protocol->sendMessage(channel->ecuHandle, payload, timeout)) {
            setLastError("Failed to queue message: " + device->protocol->getLastError());
            return ERR_FAILED;
        }

        ++(*numMsgs);

        if (msg.MsgHandle != 0) {
            QueuedMessage done{};
            done.protocolId = msg.ProtocolID;
            done.msgHandle = msg.MsgHandle;
            done.rxStatus = TX_SUCCESS;
            done.txFlags = msg.TxFlags;
            done.timestamp = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()
                ).count() & 0xFFFFFFFF);
            {
                std::lock_guard<std::mutex> notifyLock(channel->rxMutex);
                channel->rxQueue.push(std::move(done));
            }
            channel->rxCv.notify_one();
        }
    }

    return STATUS_NOERROR;
}

int32_t DeviceManager::startPeriodicMsg(uint32_t channelId, const PASSTHRU_MSG* msg, uint32_t* msgId, uint32_t interval) {
    if (!msg || !msgId) {
        setLastError("Null parameter");
        return ERR_NULL_PARAMETER;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto* channel = getChannel(channelId);
    auto* device = channel ? getDevice(channel->deviceId) : nullptr;
    if (!device || !channel) {
        setLastError("Invalid channel");
        return ERR_INVALID_CHANNEL_ID;
    }
    if (!protocol_uses_can_header(channel->protocolId) || msg->DataLength < 5 || !msg->DataBuffer) {
        setLastError("Periodic messaging is only implemented for CAN-family channels");
        return ERR_NOT_SUPPORTED;
    }

    Arbitration arb{};
    arb.request = read_u32_be(msg->DataBuffer);
    arb.replyPattern = 0;
    arb.replyMask = 0;

    std::vector<uint8_t> data(msg->DataBuffer + 4, msg->DataBuffer + msg->DataLength);
    const uint8_t timeoutByte = static_cast<uint8_t>(std::min(interval / 10U, 255U));
    auto handle = device->protocol->startPeriodicMessage(timeoutByte, arb, data, 1000);
    if (!handle) {
        setLastError("Failed to start periodic message: " + device->protocol->getLastError());
        return ERR_FAILED;
    }

    *msgId = channel->nextPeriodicId++;
    channel->periodicMessages[*msgId] = *handle;
    return STATUS_NOERROR;
}

int32_t DeviceManager::stopPeriodicMsg(uint32_t channelId, uint32_t msgId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* channel = getChannel(channelId);
    auto* device = channel ? getDevice(channel->deviceId) : nullptr;
    if (!device || !channel) {
        setLastError("Invalid channel");
        return ERR_INVALID_CHANNEL_ID;
    }
    auto it = channel->periodicMessages.find(msgId);
    if (it == channel->periodicMessages.end()) {
        setLastError("Invalid message ID");
        return ERR_INVALID_MSG_ID;
    }
    if (!device->protocol->endPeriodicMessage(it->second, 1000)) {
        setLastError("Failed to stop periodic message: " + device->protocol->getLastError());
        return ERR_FAILED;
    }
    channel->periodicMessages.erase(it);
    return STATUS_NOERROR;
}

int32_t DeviceManager::startMsgFilter(uint32_t channelId, uint32_t filterType,
                                      const PASSTHRU_MSG* maskMsg, const PASSTHRU_MSG* patternMsg,
                                      const PASSTHRU_MSG* flowControlMsg, uint32_t* filterId) {
    (void)flowControlMsg;
    if (!maskMsg || !patternMsg || !filterId) {
        setLastError("Null parameter");
        return ERR_NULL_PARAMETER;
    }
    if (filterType != PASS_FILTER && filterType != BLOCK_FILTER) {
        setLastError("Unsupported filter type");
        return ERR_NOT_SUPPORTED;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto* channel = getChannel(channelId);
    if (!channel) {
        setLastError("Invalid channel ID");
        return ERR_INVALID_CHANNEL_ID;
    }
    if (!maskMsg->DataBuffer || !patternMsg->DataBuffer ||
        maskMsg->DataLength > maskMsg->DataBufferSize || patternMsg->DataLength > patternMsg->DataBufferSize) {
        setLastError("Invalid filter message");
        return ERR_INVALID_MSG;
    }

    Filter filter{};
    filter.type = filterType;
    filter.mask.assign(maskMsg->DataBuffer, maskMsg->DataBuffer + maskMsg->DataLength);
    filter.pattern.assign(patternMsg->DataBuffer, patternMsg->DataBuffer + patternMsg->DataLength);
    filter.active = true;

    *filterId = channel->nextFilterId++;
    channel->filters[*filterId] = std::move(filter);
    return STATUS_NOERROR;
}

int32_t DeviceManager::stopMsgFilter(uint32_t channelId, uint32_t filterId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* channel = getChannel(channelId);
    if (!channel) {
        setLastError("Invalid channel ID");
        return ERR_INVALID_CHANNEL_ID;
    }
    auto it = channel->filters.find(filterId);
    if (it == channel->filters.end()) {
        setLastError("Invalid filter ID");
        return ERR_INVALID_FILTER_ID;
    }
    channel->filters.erase(it);
    return STATUS_NOERROR;
}

int32_t DeviceManager::ioctl(uint32_t handleId, uint32_t ioctlId, const void* input, void* output) {
    std::lock_guard<std::mutex> lock(mutex_);

    Device* device = getDevice(handleId);
    Channel* channel = getChannel(handleId);
    if (!device && channel) {
        device = getDevice(channel->deviceId);
    }

    switch (ioctlId) {
        case READ_PIN_VOLTAGE:
        case READ_PROG_VOLTAGE: {
            if (!device || !output) {
                setLastError("Invalid voltage IOCTL parameters");
                return !output ? ERR_NULL_PARAMETER : ERR_INVALID_DEVICE_ID;
            }
            auto voltage = device->protocol->readVoltage(1000);
            if (!voltage) {
                setLastError("Failed to read voltage");
                return ERR_FAILED;
            }
            *static_cast<uint32_t*>(output) = *voltage;
            return STATUS_NOERROR;
        }

        case GET_CONFIG: {
            if (!channel || !input) {
                setLastError("Invalid GET_CONFIG parameters");
                return !input ? ERR_NULL_PARAMETER : ERR_INVALID_CHANNEL_ID;
            }
            auto* list = static_cast<SCONFIG_LIST*>(const_cast<void*>(input));
            for (uint32_t i = 0; i < list->NumOfParams; ++i) {
                auto& item = list->ConfigPtr[i];
                switch (item.Parameter) {
                    case DATA_RATE: item.Value = channel->baudrate; break;
                    case LOOPBACK: item.Value = channel->loopback ? 1U : 0U; break;
                    case FD_CAN_DATA_PHASE_RATE: item.Value = channel->dataPhaseRate; break;
                    case FD_ISO15765_TX_DATA_LEN: item.Value = channel->fdTxDataLength; break;
                    default: break;
                }
            }
            return STATUS_NOERROR;
        }

        case SET_CONFIG: {
            if (!channel || !input) {
                setLastError("Invalid SET_CONFIG parameters");
                return !input ? ERR_NULL_PARAMETER : ERR_INVALID_CHANNEL_ID;
            }
            const auto* list = static_cast<const SCONFIG_LIST*>(input);
            if (!list->ConfigPtr) {
                setLastError("Null config pointer");
                return ERR_NULL_PARAMETER;
            }
            for (uint32_t i = 0; i < list->NumOfParams; ++i) {
                const auto& item = list->ConfigPtr[i];
                switch (item.Parameter) {
                    case LOOPBACK: channel->loopback = item.Value != 0; break;
                    case FD_CAN_DATA_PHASE_RATE: channel->dataPhaseRate = item.Value; break;
                    case FD_ISO15765_TX_DATA_LEN: channel->fdTxDataLength = item.Value; break;
                    default: break;
                }
            }
            return STATUS_NOERROR;
        }

        case CLEAR_TX_QUEUE:
            return STATUS_NOERROR;

        case CLEAR_RX_QUEUE: {
            if (!channel) {
                setLastError("Invalid channel ID");
                return ERR_INVALID_CHANNEL_ID;
            }
            std::lock_guard<std::mutex> rxLock(channel->rxMutex);
            channel->rxQueue = {};
            return STATUS_NOERROR;
        }

        case CLEAR_PERIODIC_MSGS: {
            if (!channel || !device) {
                setLastError("Invalid channel ID");
                return ERR_INVALID_CHANNEL_ID;
            }
            for (const auto& [id, periodicHandle] : channel->periodicMessages) {
                (void)id;
                device->protocol->endPeriodicMessage(periodicHandle, 1000);
            }
            channel->periodicMessages.clear();
            return STATUS_NOERROR;
        }

        case CLEAR_MSG_FILTERS:
            if (!channel) {
                setLastError("Invalid channel ID");
                return ERR_INVALID_CHANNEL_ID;
            }
            channel->filters.clear();
            return STATUS_NOERROR;

        case BUS_ON:
            return channel ? STATUS_NOERROR : ERR_INVALID_CHANNEL_ID;

        case GET_DEVICE_INFO: {
            if (!device || !output) {
                setLastError("Invalid GET_DEVICE_INFO parameters");
                return !output ? ERR_NULL_PARAMETER : ERR_INVALID_DEVICE_ID;
            }
            auto* list = static_cast<SDEVICE_INFO_LIST*>(output);
            if (!list->ItemPtr) {
                setLastError("Null device info buffer");
                return ERR_NULL_PARAMETER;
            }
            for (uint32_t i = 0; i < list->NumOfItems; ++i) {
                auto& item = list->ItemPtr[i];
                item.Supported = 1;
                switch (item.Id) {
                    case 1:
                    case 2:
                    case 3:
                    case 4:
                    case 5:
                        item.Value = 1;
                        break;
                    default:
                        item.Supported = 0;
                        item.Value = 0;
                        break;
                }
            }
            return STATUS_NOERROR;
        }

        case GET_PROTOCOL_INFO: {
            if (!output) {
                setLastError("Null protocol info buffer");
                return ERR_NULL_PARAMETER;
            }
            auto* list = static_cast<SPROTOCOL_INFO_LIST*>(output);
            if (!list->ProtocolPtr) {
                setLastError("Null protocol list");
                return ERR_NULL_PARAMETER;
            }
            const SPROTOCOL_INFO supported[] = {
                {CAN, 1, CAN_29BIT_ID, 12, 500000, 0},
                {CAN_FD_PS, 1, CAN_29BIT_ID, 68, 500000, 2000000},
                {ISO9141, 1, 0, 4128, 10400, 0},
                {ISO14230, 1, CHECKSUM_DISABLED, 4128, 10400, 0},
                {ISO15765, 0, CAN_29BIT_ID | ISO15765_FRAME_PAD | ISO15765_ADDR_TYPE, 4099, 500000, 0},
                {ISO15765_FD_PS, 0, CAN_29BIT_ID | ISO15765_FRAME_PAD | ISO15765_ADDR_TYPE, 4099, 500000, 2000000},
            };
            const uint32_t supportedCount = static_cast<uint32_t>(sizeof(supported) / sizeof(supported[0]));
            const uint32_t count = std::min<uint32_t>(list->NumOfProtocols, supportedCount);
            for (uint32_t i = 0; i < count; ++i) {
                list->ProtocolPtr[i] = supported[i];
            }
            list->NumOfProtocols = count;
            return STATUS_NOERROR;
        }

        case GET_RESOURCE_INFO: {
            if (!output) {
                setLastError("Null resource info buffer");
                return ERR_NULL_PARAMETER;
            }
            auto* list = static_cast<SRESOURCE_INFO_LIST*>(output);
            if (!list->ResourcePtr) {
                setLastError("Null resource list");
                return ERR_NULL_PARAMETER;
            }
            const SRESOURCE_INFO supported[] = {
                {6, 6, 1},
                {14, 14, 1},
                {7, 7, 1},
                {15, 15, 1},
            };
            const uint32_t supportedCount = static_cast<uint32_t>(sizeof(supported) / sizeof(supported[0]));
            const uint32_t count = std::min<uint32_t>(list->NumOfResources, supportedCount);
            for (uint32_t i = 0; i < count; ++i) {
                list->ResourcePtr[i] = supported[i];
            }
            list->NumOfResources = count;
            return STATUS_NOERROR;
        }

        default:
            setLastError("IOCTL not supported");
            return ERR_INVALID_IOCTL_ID;
    }
}

int32_t DeviceManager::readVersion(uint32_t deviceId, char* firmwareVersion, char* dllVersion, char* apiVersion) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!firmwareVersion || !dllVersion || !apiVersion) {
        setLastError("Null parameter");
        return ERR_NULL_PARAMETER;
    }
    auto* device = getDevice(deviceId);
    if (!device) {
        setLastError("Invalid device ID");
        return ERR_INVALID_DEVICE_ID;
    }
    auto info = device->protocol->getDeviceInfo(1000);
    if (info) {
        device->info = *info;
    }
    std::strncpy(firmwareVersion, device->info.firmware.c_str(), 79);
    firmwareVersion[79] = '\0';
    std::strncpy(dllVersion, "2.0.0", 79);
    dllVersion[79] = '\0';
    std::strncpy(apiVersion, "05.00", 79);
    apiVersion[79] = '\0';
    return STATUS_NOERROR;
}

} // namespace ecuconnect_v0500
