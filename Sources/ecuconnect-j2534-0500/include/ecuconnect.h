/**
 * ECUconnect Device Manager for the 05.00 J2534 surface.
 */
#ifndef ECUCONNECT_J2534_V0500_MANAGER_H
#define ECUCONNECT_J2534_V0500_MANAGER_H

#include "../../ecuconnect-j2534/include/canyonero_protocol.h"
#include "j2534.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ecuconnect_v0500 {

struct QueuedMessage {
    uint32_t protocolId = 0;
    uint32_t msgHandle = 0;
    uint32_t rxStatus = 0;
    uint32_t txFlags = 0;
    uint32_t timestamp = 0;
    uint32_t extraDataIndex = 0;
    std::vector<uint8_t> data;
};

struct Filter {
    uint32_t type = 0;
    std::vector<uint8_t> mask;
    std::vector<uint8_t> pattern;
    bool active = false;
};

struct ScanEntry {
    std::string connectionString;
    std::string displayName;
    ecuconnect::DeviceInfo info;
    bool available = false;
};

struct Channel {
    uint32_t id = 0;
    uint32_t deviceId = 0;
    uint32_t protocolId = 0;
    uint32_t flags = 0;
    uint32_t baudrate = 0;
    uint32_t dataPhaseRate = 0;
    uint32_t fdTxDataLength = 64;
    uint8_t ecuHandle = 0;
    ecuconnect::ChannelProtocol ecuProtocol = ecuconnect::ChannelProtocol::Raw;
    bool loopback = false;
    bool physical = true;

    std::unordered_map<uint32_t, Filter> filters;
    uint32_t nextFilterId = 1;
    std::unordered_map<uint32_t, uint8_t> periodicMessages;
    uint32_t nextPeriodicId = 1;

    bool hasTxArb = false;
    ecuconnect::Arbitration lastTxArb{};

    std::queue<QueuedMessage> rxQueue;
    std::mutex rxMutex;
    std::condition_variable rxCv;
};

struct Device {
    std::unique_ptr<ecuconnect::Protocol> protocol;
    ecuconnect::DeviceInfo info;
    std::string connectionString;
    std::unordered_map<uint32_t, std::unique_ptr<Channel>> channels;
    std::thread pollingThread;
    std::atomic<bool> stopPolling{false};
};

class DeviceManager {
public:
    static DeviceManager& instance();

    int32_t scanForDevices();
    int32_t getNextDevice(SDEVICE* device);

    int32_t openDevice(const char* name, uint32_t* deviceId);
    int32_t closeDevice(uint32_t deviceId);
    int32_t connect(uint32_t deviceId, uint32_t protocolId, uint32_t flags,
                    uint32_t baudrate, const RESOURCE_STRUCT* resources, uint32_t* channelId);
    int32_t disconnect(uint32_t channelId);
    int32_t logicalConnect(uint32_t physicalChannelId, uint32_t protocolId, uint32_t flags,
                           void* descriptor, uint32_t* channelId);
    int32_t logicalDisconnect(uint32_t channelId);
    int32_t select(uint32_t deviceId, uint32_t selectType, const SCHANNELSET* channelSet, uint32_t timeout);

    int32_t readMsgs(uint32_t channelId, PASSTHRU_MSG* msgs, uint32_t* numMsgs, uint32_t timeout);
    int32_t queueMsgs(uint32_t channelId, const PASSTHRU_MSG* msgs, uint32_t* numMsgs, uint32_t timeout);
    int32_t startPeriodicMsg(uint32_t channelId, const PASSTHRU_MSG* msg, uint32_t* msgId, uint32_t interval);
    int32_t stopPeriodicMsg(uint32_t channelId, uint32_t msgId);
    int32_t startMsgFilter(uint32_t channelId, uint32_t filterType,
                           const PASSTHRU_MSG* maskMsg, const PASSTHRU_MSG* patternMsg,
                           const PASSTHRU_MSG* flowControlMsg, uint32_t* filterId);
    int32_t stopMsgFilter(uint32_t channelId, uint32_t filterId);
    int32_t ioctl(uint32_t handleId, uint32_t ioctlId, const void* input, void* output);
    int32_t readVersion(uint32_t deviceId, char* firmwareVersion, char* dllVersion, char* apiVersion);

    const char* getLastError() const;
    void setLastError(const std::string& error);

private:
    DeviceManager() = default;

    Device* getDevice(uint32_t deviceId);
    Channel* getChannel(uint32_t channelId);
    void pollingThreadFunc(uint32_t deviceId);

    std::vector<ScanEntry> scanEntries_;
    size_t nextScanIndex_ = 0;
    std::unordered_map<uint32_t, std::unique_ptr<Device>> devices_;
    std::unordered_map<uint32_t, uint32_t> channelToDevice_;
    uint32_t nextDeviceId_ = 1;
    uint32_t nextChannelId_ = 1;
    mutable std::mutex mutex_;
    mutable std::mutex errorMutex_;
    std::string lastError_;
};

} // namespace ecuconnect_v0500

#endif
