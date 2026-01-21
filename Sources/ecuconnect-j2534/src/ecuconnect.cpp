/**
 * ECUconnect Device Manager Implementation
 */
#include "ecuconnect.h"
#include <cstring>
#include <chrono>
#include <cstdio>

// Debug logging macro - outputs to stderr which bridge passes through
#define DBG(fmt, ...) fprintf(stderr, "[ecuconnect] " fmt "\n", ##__VA_ARGS__)

namespace ecuconnect {

namespace {
uint8_t id_byte(uint32_t can_id, size_t index) {
    switch (index) {
        case 0: return static_cast<uint8_t>((can_id >> 24) & 0xFF);
        case 1: return static_cast<uint8_t>((can_id >> 16) & 0xFF);
        case 2: return static_cast<uint8_t>((can_id >> 8) & 0xFF);
        default: return static_cast<uint8_t>(can_id & 0xFF);
    }
}

bool filter_matches_bytes(const Filter& filter, uint32_t can_id, const std::vector<uint8_t>& data) {
    if (!filter.maskBytes.empty() && filter.maskBytes.size() == filter.patternBytes.size()) {
        const size_t len = filter.maskBytes.size();
        for (size_t i = 0; i < len; i++) {
            uint8_t value = 0;
            if (i < 4) {
                value = id_byte(can_id, i);
            } else {
                size_t dataIndex = i - 4;
                if (dataIndex >= data.size()) {
                    return false;
                }
                value = data[dataIndex];
            }

            uint8_t mask = filter.maskBytes[i];
            uint8_t pattern = filter.patternBytes[i];
            if ((value & mask) != (pattern & mask)) {
                return false;
            }
        }
        return true;
    }

    return (can_id & filter.mask) == (filter.pattern & filter.mask);
}

bool arbitration_equals(const Arbitration& lhs, const Arbitration& rhs) {
    return lhs.request == rhs.request
        && lhs.replyPattern == rhs.replyPattern
        && lhs.replyMask == rhs.replyMask
        && lhs.requestExtension == rhs.requestExtension
        && lhs.replyExtension == rhs.replyExtension;
}

bool message_passes_filters(const Channel& channel, uint32_t can_id, const std::vector<uint8_t>& data) {
    bool has_pass_filter = false;
    bool pass_match = false;

    for (const auto& [id, filter] : channel.filters) {
        if (!filter.active) {
            continue;
        }

        if (filter.type == PASS_FILTER) {
            has_pass_filter = true;
            if (filter_matches_bytes(filter, can_id, data)) {
                pass_match = true;
            }
        } else if (filter.type == BLOCK_FILTER) {
            if (filter_matches_bytes(filter, can_id, data)) {
                return false;
            }
        }
    }

    return has_pass_filter ? pass_match : true;
}
} // namespace

DeviceManager& DeviceManager::instance() {
    static DeviceManager instance;
    return instance;
}

Device* DeviceManager::getDevice(unsigned long deviceId) {
    auto it = devices_.find(deviceId);
    return (it != devices_.end()) ? it->second.get() : nullptr;
}

Channel* DeviceManager::getChannel(unsigned long channelId) {
    auto devIt = channelToDevice_.find(channelId);
    if (devIt == channelToDevice_.end()) {
        return nullptr;
    }

    auto device = getDevice(devIt->second);
    if (!device) {
        return nullptr;
    }

    auto chIt = device->channels.find(channelId);
    return (chIt != device->channels.end()) ? chIt->second.get() : nullptr;
}

const char* DeviceManager::getLastError() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return lastError_.c_str();
}

void DeviceManager::setLastError(const std::string& error) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    lastError_ = error;
}

// ============================================================================
// Device Management
// ============================================================================

long DeviceManager::openDevice(const char* name, unsigned long* deviceId) {
    DBG("openDevice called, name=%s", name ? name : "(null)");
    std::lock_guard<std::mutex> lock(mutex_);

    if (!deviceId) {
        DBG("openDevice: null deviceId pointer");
        setLastError("Null device ID pointer");
        return ERR_NULL_PARAMETER;
    }

    // Parse connection string or use default
    std::string connectionString = name ? name : "";
    DBG("openDevice: connectionString=%s", connectionString.c_str());

    // Create transport and protocol
    DBG("openDevice: creating transport...");
    auto transport = createTransport(TransportType::TCP, connectionString);
    if (!transport) {
        DBG("openDevice: failed to create transport");
        setLastError("Failed to create transport");
        return ERR_FAILED;
    }

    auto protocol = std::make_unique<Protocol>(std::move(transport));

    // Connect to device
    DBG("openDevice: connecting...");
    if (!protocol->connect()) {
        DBG("openDevice: connect failed: %s", protocol->getLastError().c_str());
        setLastError("Failed to connect: " + protocol->getLastError());
        return ERR_DEVICE_NOT_CONNECTED;
    }
    DBG("openDevice: connected");

    // Get device info (also verifies connection is working)
    DBG("openDevice: getting device info...");
    auto info = protocol->getDeviceInfo(2000);
    if (!info) {
        DBG("openDevice: getDeviceInfo failed: %s", protocol->getLastError().c_str());
        setLastError("Failed to get device info: " + protocol->getLastError());
        return ERR_DEVICE_NOT_CONNECTED;
    }
    DBG("openDevice: got device info: %s %s %s", info->vendor.c_str(), info->model.c_str(), info->firmware.c_str());

    // Create device
    auto device = std::make_unique<Device>();
    device->protocol = std::move(protocol);
    device->connectionString = connectionString;
    device->info = *info;

    *deviceId = nextDeviceId_++;
    devices_[*deviceId] = std::move(device);

    DBG("openDevice: success, deviceId=%lu", *deviceId);
    return STATUS_NOERROR;
}

long DeviceManager::closeDevice(unsigned long deviceId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = devices_.find(deviceId);
    if (it == devices_.end()) {
        setLastError("Invalid device ID");
        return ERR_INVALID_DEVICE_ID;
    }

    auto& device = it->second;

    // Close all channels
    for (auto& [channelId, channel] : device->channels) {
        if (device->protocol && device->protocol->isConnected()) {
            device->protocol->closeChannel(channel->ecuHandle, 1000);
        }
        channelToDevice_.erase(channelId);
    }

    // Disconnect
    if (device->protocol) {
        device->protocol->disconnect();
    }

    devices_.erase(it);
    return STATUS_NOERROR;
}

// ============================================================================
// Channel Management
// ============================================================================

long DeviceManager::connect(unsigned long deviceId, unsigned long protocolId,
                             unsigned long flags, unsigned long baudrate,
                             unsigned long* channelId) {
    DBG("connect called: deviceId=%lu, protocolId=%lu, flags=0x%lx, baudrate=%lu",
        deviceId, protocolId, flags, baudrate);
    std::lock_guard<std::mutex> lock(mutex_);

    if (!channelId) {
        DBG("connect: null channelId pointer");
        setLastError("Null channel ID pointer");
        return ERR_NULL_PARAMETER;
    }

    auto device = getDevice(deviceId);
    if (!device) {
        DBG("connect: invalid device ID");
        setLastError("Invalid device ID");
        return ERR_INVALID_DEVICE_ID;
    }

    // ECUconnect only supports one active channel at a time
    if (!device->channels.empty()) {
        DBG("connect: channel already active (ECUconnect supports only one channel)");
        setLastError("ECUconnect supports only one active channel at a time");
        return ERR_CHANNEL_IN_USE;
    }

    // Only support CAN for now
    if (protocolId != CAN) {
        DBG("connect: unsupported protocol %lu", protocolId);
        setLastError("Protocol not supported (only CAN supported)");
        return ERR_INVALID_PROTOCOL_ID;
    }

    // Validate baudrate
    if (baudrate == 0) {
        DBG("connect: invalid baudrate");
        setLastError("Invalid baudrate");
        return ERR_INVALID_BAUDRATE;
    }

    // Open channel on ECUconnect device
    DBG("connect: opening channel (Raw, %lu bps)...", baudrate);
    auto handle = device->protocol->openChannel(ChannelProtocol::Raw, baudrate, 1000);
    if (!handle) {
        DBG("connect: openChannel failed: %s", device->protocol->getLastError().c_str());
        setLastError("Failed to open channel: " + device->protocol->getLastError());
        return ERR_FAILED;
    }
    DBG("connect: channel opened, handle=%u", *handle);

    // Create J2534 channel
    auto channel = std::make_unique<Channel>();
    channel->deviceId = deviceId;
    channel->protocolId = protocolId;
    channel->flags = flags;
    channel->baudrate = baudrate;
    channel->ecuHandle = *handle;
    channel->dataRate = baudrate;

    *channelId = nextDeviceId_++;  // Use shared ID counter for simplicity
    device->channels[*channelId] = std::move(channel);
    channelToDevice_[*channelId] = deviceId;

    // Start background polling thread
    device->stopPolling = false;
    device->pollingThread = std::thread(&DeviceManager::pollingThreadFunc, this, deviceId);

    DBG("connect: success, channelId=%lu", *channelId);
    return STATUS_NOERROR;
}

long DeviceManager::disconnect(unsigned long channelId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto devIt = channelToDevice_.find(channelId);
    if (devIt == channelToDevice_.end()) {
        setLastError("Invalid channel ID");
        return ERR_INVALID_CHANNEL_ID;
    }

    auto device = getDevice(devIt->second);
    if (!device) {
        setLastError("Invalid device");
        return ERR_INVALID_DEVICE_ID;
    }

    auto chIt = device->channels.find(channelId);
    if (chIt == device->channels.end()) {
        setLastError("Invalid channel ID");
        return ERR_INVALID_CHANNEL_ID;
    }

    // Stop polling thread
    if (device->pollingThread.joinable()) {
        device->stopPolling = true;
        device->pollingThread.join();
    }

    // Close channel on device
    auto& channel = chIt->second;
    if (device->protocol && device->protocol->isConnected()) {
        device->protocol->closeChannel(channel->ecuHandle, 1000);
    }

    // Stop all periodic messages
    for (auto& [id, ecuHandle] : channel->periodicMessages) {
        device->protocol->endPeriodicMessage(ecuHandle, 1000);
    }

    device->channels.erase(chIt);
    channelToDevice_.erase(devIt);

    return STATUS_NOERROR;
}

// ============================================================================
// Message Operations
// ============================================================================

void DeviceManager::pollingThreadFunc(unsigned long deviceId) {
    DBG("Polling thread started for deviceId=%lu", deviceId);

    while (true) {
        // Use a local lock to get device and check stop flag
        Device* device = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            device = getDevice(deviceId);
            if (!device || device->stopPolling) {
                break; // Exit if device is gone or stop is requested
            }
        }

        if (!device->protocol || !device->protocol->isConnected()) {
            // Wait a bit before retrying if not connected
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Poll for messages with a short timeout to remain responsive
        auto frames = device->protocol->receiveMessages(100);
        if (frames.empty()) {
            continue;
        }

        // This lock is to access the channels map
        std::lock_guard<std::mutex> lock(mutex_);
        if (device->channels.empty()) {
            continue; // No channels to deliver to
        }
        // Assuming one channel per device for now
        auto& channel = device->channels.begin()->second;

        for (const auto& frame : frames) {
            if (!message_passes_filters(*channel, frame.id, frame.data)) {
                continue;
            }

            // Create J2534 message
            PASSTHRU_MSG msg{};
            msg.ProtocolID = channel->protocolId;
            msg.RxStatus = 0;
            msg.Timestamp = static_cast<unsigned long>(frame.timestamp & 0xFFFFFFFF);
            msg.DataSize = static_cast<unsigned long>(4 + frame.data.size());
            msg.ExtraDataIndex = msg.DataSize;

            msg.Data[0] = static_cast<uint8_t>((frame.id >> 24) & 0xFF);
            msg.Data[1] = static_cast<uint8_t>((frame.id >> 16) & 0xFF);
            msg.Data[2] = static_cast<uint8_t>((frame.id >> 8) & 0xFF);
            msg.Data[3] = static_cast<uint8_t>(frame.id & 0xFF);
            std::memcpy(&msg.Data[4], frame.data.data(), frame.data.size());

            if (frame.id > 0x7FF) {
                msg.RxStatus |= CAN_29BIT_ID;
            }

            // Queue message and notify reader
            {
                std::lock_guard<std::mutex> rxLock(channel->rxMutex);
                channel->rxQueue.push(msg);
            }
            channel->rxCv.notify_one();
        }
    }
    DBG("Polling thread stopped for deviceId=%lu", deviceId);
}

long DeviceManager::readMsgs(unsigned long channelId, PASSTHRU_MSG* msgs,
                              unsigned long* numMsgs, unsigned long timeout) {
    if (!msgs || !numMsgs) {
        setLastError("Null parameter");
        return ERR_NULL_PARAMETER;
    }

    unsigned long requested = *numMsgs;
    *numMsgs = 0;

    Channel* channel = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        channel = getChannel(channelId);
    }
    
    if (!channel) {
        setLastError("Invalid channel ID");
        return ERR_INVALID_CHANNEL_ID;
    }

    std::unique_lock<std::mutex> rxLock(channel->rxMutex);

    // Wait for messages if the queue is empty
    if (channel->rxQueue.empty() && timeout > 0) {
        channel->rxCv.wait_for(rxLock, std::chrono::milliseconds(timeout),
                               [&] { return !channel->rxQueue.empty(); });
    }

    // Return queued messages
    while (*numMsgs < requested && !channel->rxQueue.empty()) {
        msgs[*numMsgs] = channel->rxQueue.front();
        channel->rxQueue.pop();
        (*numMsgs)++;
    }

    if (*numMsgs == 0) {
        return (timeout > 0) ? ERR_TIMEOUT : ERR_BUFFER_EMPTY;
    }

    return STATUS_NOERROR;
}

long DeviceManager::writeMsgs(unsigned long channelId, const PASSTHRU_MSG* msgs,
                               unsigned long* numMsgs, unsigned long timeout) {
    if (!msgs || !numMsgs) {
        setLastError("Null parameter");
        return ERR_NULL_PARAMETER;
    }

    unsigned long requested = *numMsgs;
    *numMsgs = 0;

    std::lock_guard<std::mutex> lock(mutex_);

    auto devIt = channelToDevice_.find(channelId);
    if (devIt == channelToDevice_.end()) {
        setLastError("Invalid channel ID");
        return ERR_INVALID_CHANNEL_ID;
    }

    auto device = getDevice(devIt->second);
    auto chIt = device->channels.find(channelId);
    auto& channel = chIt->second;

    // Group messages by CAN ID for batched sending
    // Each batch contains frames with the same arbitration (CAN ID + extension)
    struct BatchKey {
        uint32_t canId;
        uint8_t extension;
        bool operator==(const BatchKey& other) const {
            return canId == other.canId && extension == other.extension;
        }
    };

    unsigned long i = 0;
    while (i < requested) {
        const auto& firstMsg = msgs[i];

        // Validate protocol
        if (firstMsg.ProtocolID != channel->protocolId) {
            setLastError("Message protocol mismatch");
            return ERR_MSG_PROTOCOL_ID;
        }

        if (firstMsg.DataSize < 4) {
            setLastError("Invalid message size");
            return ERR_INVALID_MSG;
        }

        // Extract CAN ID for this batch
        uint32_t batchCanId = (static_cast<uint32_t>(firstMsg.Data[0]) << 24) |
                              (static_cast<uint32_t>(firstMsg.Data[1]) << 16) |
                              (static_cast<uint32_t>(firstMsg.Data[2]) << 8) |
                              static_cast<uint32_t>(firstMsg.Data[3]);
        uint8_t batchExtension = (firstMsg.TxFlags & CAN_29BIT_ID) ? 1 : 0;

        // Collect all consecutive frames with the same CAN ID into a batch
        std::vector<std::vector<uint8_t>> batch;
        std::vector<unsigned long> batchIndices;  // Track which messages are in this batch
        size_t batchBytes = 1;  // Start with handle byte

        while (i < requested) {
            const auto& msg = msgs[i];

            if (msg.ProtocolID != channel->protocolId) {
                break;  // Protocol mismatch, start new batch
            }

            if (msg.DataSize < 4) {
                break;  // Invalid message, will be caught next iteration
            }

            uint32_t canId = (static_cast<uint32_t>(msg.Data[0]) << 24) |
                             (static_cast<uint32_t>(msg.Data[1]) << 16) |
                             (static_cast<uint32_t>(msg.Data[2]) << 8) |
                             static_cast<uint32_t>(msg.Data[3]);
            uint8_t extension = (msg.TxFlags & CAN_29BIT_ID) ? 1 : 0;

            // Stop batch if CAN ID changes
            if (canId != batchCanId || extension != batchExtension) {
                break;
            }

            // Check if adding this frame would exceed batch size limit
            size_t frameSize = 1 + (msg.DataSize - 4);  // length byte + payload
            if (batchBytes + frameSize > MAX_BATCH_SIZE && !batch.empty()) {
                break;  // Batch is full, send what we have
            }

            // Add frame to batch
            std::vector<uint8_t> data(msg.Data + 4, msg.Data + msg.DataSize);
            batch.push_back(std::move(data));
            batchIndices.push_back(i);
            batchBytes += frameSize;
            i++;
        }

        if (batch.empty()) {
            // Should not happen, but handle gracefully
            i++;
            continue;
        }

        // Set arbitration for this batch (only if changed)
        Arbitration arb;
        arb.request = batchCanId;
        arb.requestExtension = batchExtension;
        arb.replyPattern = 0;
        arb.replyMask = 0;  // Pass all incoming messages
        arb.replyExtension = 0;

        if (!channel->hasTxArb || !arbitration_equals(channel->lastTxArb, arb)) {
            if (!device->protocol->setArbitration(channel->ecuHandle, arb, timeout)) {
                setLastError("Failed to set arbitration: " + device->protocol->getLastError());
                return ERR_FAILED;
            }
            channel->lastTxArb = arb;
            channel->hasTxArb = true;
        }

        // Send the batch
        if (!device->protocol->sendMessages(channel->ecuHandle, batch, timeout)) {
            const auto last_error = device->protocol->getLastError();
            setLastError("Failed to send messages: " + last_error);
            const bool is_timeout = last_error.find("timeout") != std::string::npos
                || last_error.find("Timeout") != std::string::npos;
            if (timeout > 0 && is_timeout) {
                return ERR_TIMEOUT;
            }
            return ERR_FAILED;
        }

        // Update sent count and handle loopback for all messages in batch
        for (size_t j = 0; j < batchIndices.size(); j++) {
            unsigned long idx = batchIndices[j];
            const auto& msg = msgs[idx];
            (*numMsgs)++;

            if (channel->loopback && message_passes_filters(*channel, batchCanId, batch[j])) {
                PASSTHRU_MSG loopbackMsg = msg;
                loopbackMsg.RxStatus = TX_MSG_TYPE;
                if (msg.TxFlags & CAN_29BIT_ID) {
                    loopbackMsg.RxStatus |= CAN_29BIT_ID;
                }
                loopbackMsg.Timestamp = static_cast<unsigned long>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()
                    ).count() & 0xFFFFFFFF
                );

                std::lock_guard<std::mutex> rxLock(channel->rxMutex);
                channel->rxQueue.push(loopbackMsg);
            }
        }
    }

    return STATUS_NOERROR;
}

// ============================================================================
// Periodic Messages
// ============================================================================

long DeviceManager::startPeriodicMsg(unsigned long channelId, const PASSTHRU_MSG* msg,
                                      unsigned long* msgId, unsigned long timeInterval) {
    if (!msg || !msgId) {
        setLastError("Null parameter");
        return ERR_NULL_PARAMETER;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto devIt = channelToDevice_.find(channelId);
    if (devIt == channelToDevice_.end()) {
        setLastError("Invalid channel ID");
        return ERR_INVALID_CHANNEL_ID;
    }

    auto device = getDevice(devIt->second);
    auto chIt = device->channels.find(channelId);
    auto& channel = chIt->second;

    // Extract CAN ID
    if (msg->DataSize < 4) {
        setLastError("Invalid message size");
        return ERR_INVALID_MSG;
    }

    uint32_t canId = (static_cast<uint32_t>(msg->Data[0]) << 24) |
                     (static_cast<uint32_t>(msg->Data[1]) << 16) |
                     (static_cast<uint32_t>(msg->Data[2]) << 8) |
                     static_cast<uint32_t>(msg->Data[3]);

    Arbitration arb;
    arb.request = canId;
    arb.replyPattern = 0;
    arb.replyMask = 0xFFFFFFFF;

    std::vector<uint8_t> data(msg->Data + 4, msg->Data + msg->DataSize);

    // Convert interval to timeout byte (ms -> protocol format)
    uint8_t timeout = static_cast<uint8_t>(std::min(timeInterval / 10, 255UL));

    auto ecuHandle = device->protocol->startPeriodicMessage(timeout, arb, data, 1000);
    if (!ecuHandle) {
        setLastError("Failed to start periodic message: " + device->protocol->getLastError());
        return ERR_FAILED;
    }

    *msgId = channel->nextPeriodicId++;
    channel->periodicMessages[*msgId] = *ecuHandle;

    return STATUS_NOERROR;
}

long DeviceManager::stopPeriodicMsg(unsigned long channelId, unsigned long msgId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto devIt = channelToDevice_.find(channelId);
    if (devIt == channelToDevice_.end()) {
        setLastError("Invalid channel ID");
        return ERR_INVALID_CHANNEL_ID;
    }

    auto device = getDevice(devIt->second);
    auto chIt = device->channels.find(channelId);
    auto& channel = chIt->second;

    auto it = channel->periodicMessages.find(msgId);
    if (it == channel->periodicMessages.end()) {
        setLastError("Invalid message ID");
        return ERR_INVALID_MSG_ID;
    }

    if (!device->protocol->endPeriodicMessage(it->second, 1000)) {
        const auto last_error = device->protocol->getLastError();
        if (!device->protocol->endPeriodicMessage(0, 1000)) {
            setLastError("Failed to stop periodic message: " + last_error);
            return ERR_FAILED;
        }
        channel->periodicMessages.clear();
        return STATUS_NOERROR;
    }

    channel->periodicMessages.erase(it);
    return STATUS_NOERROR;
}

// ============================================================================
// Filters
// ============================================================================

long DeviceManager::startMsgFilter(unsigned long channelId, unsigned long filterType,
                                    const PASSTHRU_MSG* maskMsg, const PASSTHRU_MSG* patternMsg,
                                    const PASSTHRU_MSG* flowControlMsg, unsigned long* filterId) {
    DBG("startMsgFilter called: channelId=%lu, filterType=%lu", channelId, filterType);
    if (!maskMsg || !patternMsg || !filterId) {
        DBG("startMsgFilter: null parameter");
        setLastError("Null parameter");
        return ERR_NULL_PARAMETER;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto devIt = channelToDevice_.find(channelId);
    if (devIt == channelToDevice_.end()) {
        DBG("startMsgFilter: invalid channel ID");
        setLastError("Invalid channel ID");
        return ERR_INVALID_CHANNEL_ID;
    }

    auto device = getDevice(devIt->second);
    auto chIt = device->channels.find(channelId);
    auto& channel = chIt->second;

    if (filterType != PASS_FILTER && filterType != BLOCK_FILTER && filterType != FLOW_CONTROL_FILTER) {
        setLastError("Invalid filter type");
        return ERR_INVALID_MSG;
    }

    if (filterType == FLOW_CONTROL_FILTER && channel->protocolId != ISO15765) {
        setLastError("Flow control filters only supported for ISO15765");
        return ERR_NOT_SUPPORTED;
    }

    const unsigned long maskSize = maskMsg->DataSize;
    const unsigned long patternSize = patternMsg->DataSize;
    if (maskSize == 0 || patternSize == 0 || maskSize > 12 || patternSize > 12 || maskSize != patternSize) {
        DBG("startMsgFilter: invalid message size (mask=%lu, pattern=%lu)",
            maskMsg->DataSize, patternMsg->DataSize);
        setLastError("Invalid filter message size");
        return ERR_INVALID_MSG;
    }

    Filter filter;
    filter.type = filterType;
    filter.mask = (static_cast<uint32_t>(maskMsg->Data[0]) << 24) |
                  (static_cast<uint32_t>(maskMsg->Data[1]) << 16) |
                  (static_cast<uint32_t>(maskMsg->Data[2]) << 8) |
                  static_cast<uint32_t>(maskMsg->Data[3]);
    filter.pattern = (static_cast<uint32_t>(patternMsg->Data[0]) << 24) |
                     (static_cast<uint32_t>(patternMsg->Data[1]) << 16) |
                     (static_cast<uint32_t>(patternMsg->Data[2]) << 8) |
                     static_cast<uint32_t>(patternMsg->Data[3]);
    filter.maskBytes.assign(maskMsg->Data, maskMsg->Data + maskSize);
    filter.patternBytes.assign(patternMsg->Data, patternMsg->Data + patternSize);

    DBG("startMsgFilter: mask=0x%08X, pattern=0x%08X", filter.mask, filter.pattern);

    if (flowControlMsg && flowControlMsg->DataSize >= 4) {
        filter.flowControlId = (static_cast<uint32_t>(flowControlMsg->Data[0]) << 24) |
                               (static_cast<uint32_t>(flowControlMsg->Data[1]) << 16) |
                               (static_cast<uint32_t>(flowControlMsg->Data[2]) << 8) |
                               static_cast<uint32_t>(flowControlMsg->Data[3]);
    }
    filter.active = true;

    *filterId = channel->nextFilterId++;
    channel->filters[*filterId] = filter;

    // For Raw CAN, configure the device to pass all frames.
    // Software filtering is done by the driver based on J2534 filters.
    // We set replyMask=0 which means "don't care about any bits" = pass all.
    DBG("startMsgFilter: setting device to pass-all mode...");
    Arbitration arb;
    arb.request = 0;
    arb.requestExtension = 0;
    arb.replyPattern = 0;
    arb.replyMask = 0;  // mask=0 means pass all CAN IDs
    arb.replyExtension = 0;
    bool arbResult = device->protocol->setArbitration(channel->ecuHandle, arb, 1000);
    DBG("startMsgFilter: setArbitration(pass-all) returned %s", arbResult ? "true" : "false");

    DBG("startMsgFilter: success, filterId=%lu", *filterId);
    return STATUS_NOERROR;
}

long DeviceManager::stopMsgFilter(unsigned long channelId, unsigned long filterId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto devIt = channelToDevice_.find(channelId);
    if (devIt == channelToDevice_.end()) {
        setLastError("Invalid channel ID");
        return ERR_INVALID_CHANNEL_ID;
    }

    auto device = getDevice(devIt->second);
    auto chIt = device->channels.find(channelId);
    auto& channel = chIt->second;

    auto it = channel->filters.find(filterId);
    if (it == channel->filters.end()) {
        setLastError("Invalid filter ID");
        return ERR_INVALID_FILTER_ID;
    }

    channel->filters.erase(it);
    return STATUS_NOERROR;
}

// ============================================================================
// IOCTL
// ============================================================================

long DeviceManager::ioctl(unsigned long channelId, unsigned long ioctlId,
                           const void* input, void* output) {
    DBG("ioctl called: channelId=%lu, ioctlId=0x%lx, input=%p, output=%p",
        channelId, ioctlId, input, output);
    std::lock_guard<std::mutex> lock(mutex_);

    // Handle device-level IOCTLs (channelId = 0 means device ID)
    if (ioctlId == READ_VBATT || ioctlId == READ_PROG_VOLTAGE) {
        auto device = getDevice(channelId);  // Some callers pass deviceId, others pass channelId.
        if (!device) {
            auto devIt = channelToDevice_.find(channelId);
            if (devIt != channelToDevice_.end()) {
                device = getDevice(devIt->second);
            }
        }
        if (!device) {
            setLastError("Invalid device ID");
            return ERR_INVALID_DEVICE_ID;
        }

        if (!output) {
            setLastError("Null output parameter");
            return ERR_NULL_PARAMETER;
        }

        auto voltage = device->protocol->readVoltage(1000);
        if (!voltage) {
            setLastError("Failed to read voltage");
            return ERR_FAILED;
        }

        *static_cast<unsigned long*>(output) = *voltage;
        return STATUS_NOERROR;
    }

    // Channel-level IOCTLs
    auto devIt = channelToDevice_.find(channelId);
    if (devIt == channelToDevice_.end()) {
        setLastError("Invalid channel ID");
        return ERR_INVALID_CHANNEL_ID;
    }

    auto device = getDevice(devIt->second);
    auto chIt = device->channels.find(channelId);
    auto& channel = chIt->second;

    switch (ioctlId) {
        case GET_CONFIG: {
            // J2534 spec: pInput is SCONFIG_LIST, values are written back to same structure
            if (!input) {
                setLastError("Null input parameter");
                return ERR_NULL_PARAMETER;
            }
            auto configList = static_cast<SCONFIG_LIST*>(const_cast<void*>(input));
            for (unsigned long i = 0; i < configList->NumOfParams; i++) {
                switch (configList->ConfigPtr[i].Parameter) {
                    case DATA_RATE:
                        configList->ConfigPtr[i].Value = channel->dataRate;
                        break;
                    case LOOPBACK:
                        configList->ConfigPtr[i].Value = channel->loopback ? 1 : 0;
                        break;
                    default:
                        // Unknown parameter, leave value unchanged
                        break;
                }
            }
            return STATUS_NOERROR;
        }

        case SET_CONFIG: {
            DBG("ioctl SET_CONFIG");
            DBG("ioctl SET_CONFIG: sizeof(SCONFIG_LIST)=%zu, sizeof(SCONFIG)=%zu, sizeof(unsigned long)=%zu, sizeof(void*)=%zu",
                sizeof(SCONFIG_LIST), sizeof(SCONFIG), sizeof(unsigned long), sizeof(void*));
            if (!input) {
                DBG("ioctl SET_CONFIG: null input");
                setLastError("Null input parameter");
                return ERR_NULL_PARAMETER;
            }
            // Debug: dump raw bytes of the structure
            auto rawBytes = static_cast<const uint8_t*>(input);
            DBG("ioctl SET_CONFIG: raw bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                rawBytes[0], rawBytes[1], rawBytes[2], rawBytes[3],
                rawBytes[4], rawBytes[5], rawBytes[6], rawBytes[7],
                rawBytes[8], rawBytes[9], rawBytes[10], rawBytes[11],
                rawBytes[12], rawBytes[13], rawBytes[14], rawBytes[15]);
            auto configList = static_cast<const SCONFIG_LIST*>(input);
            DBG("ioctl SET_CONFIG: NumOfParams=%lu, ConfigPtr=%p",
                configList->NumOfParams, (void*)configList->ConfigPtr);
            if (!configList->ConfigPtr) {
                DBG("ioctl SET_CONFIG: null ConfigPtr");
                setLastError("Null config pointer");
                return ERR_NULL_PARAMETER;
            }
            for (unsigned long i = 0; i < configList->NumOfParams; i++) {
                DBG("ioctl SET_CONFIG: param[%lu] = 0x%lx, value = %lu",
                    i, configList->ConfigPtr[i].Parameter, configList->ConfigPtr[i].Value);
                switch (configList->ConfigPtr[i].Parameter) {
                    case DATA_RATE:
                        channel->dataRate = configList->ConfigPtr[i].Value;
                        break;
                    case LOOPBACK:
                        channel->loopback = configList->ConfigPtr[i].Value != 0;
                        break;
                    default:
                        // Ignore unknown parameters
                        break;
                }
            }
            DBG("ioctl SET_CONFIG: returning STATUS_NOERROR");
            return STATUS_NOERROR;
        }

        case CLEAR_TX_BUFFER:
            return STATUS_NOERROR;

        case CLEAR_RX_BUFFER: {
            std::lock_guard<std::mutex> rxLock(channel->rxMutex);
            while (!channel->rxQueue.empty()) {
                channel->rxQueue.pop();
            }
            return STATUS_NOERROR;
        }

        case CLEAR_PERIODIC_MSGS:
            for (auto& [id, ecuHandle] : channel->periodicMessages) {
                device->protocol->endPeriodicMessage(ecuHandle, 1000);
            }
            channel->periodicMessages.clear();
            return STATUS_NOERROR;

        case CLEAR_MSG_FILTERS:
            channel->filters.clear();
            return STATUS_NOERROR;

        default:
            setLastError("IOCTL not supported");
            return ERR_INVALID_IOCTL_ID;
    }
}

// ============================================================================
// Version Info
// ============================================================================

long DeviceManager::readVersion(unsigned long deviceId, char* firmwareVersion,
                                 char* dllVersion, char* apiVersion) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!firmwareVersion || !dllVersion || !apiVersion) {
        setLastError("Null parameter");
        return ERR_NULL_PARAMETER;
    }

    auto device = getDevice(deviceId);
    if (!device) {
        setLastError("Invalid device ID");
        return ERR_INVALID_DEVICE_ID;
    }

    // Get fresh device info
    auto info = device->protocol->getDeviceInfo(1000);
    if (info) {
        device->info = *info;
    }

    std::strncpy(firmwareVersion, device->info.firmware.c_str(), 80);
    firmwareVersion[79] = '\0';

    std::strncpy(dllVersion, "1.0.0", 80);
    dllVersion[79] = '\0';

    std::strncpy(apiVersion, "04.04", 80);
    apiVersion[79] = '\0';

    return STATUS_NOERROR;
}

} // namespace ecuconnect
