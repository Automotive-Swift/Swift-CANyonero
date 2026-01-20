/**
 * ECUconnect Device Manager
 * Manages devices and channels for J2534 API
 */
#ifndef ECUCONNECT_H
#define ECUCONNECT_H

#include "canyonero_protocol.h"
#include "j2534.h"
#include <memory>
#include <unordered_map>
#include <mutex>
#include <queue>
#include <atomic>

namespace ecuconnect {

/**
 * Filter configuration
 */
struct Filter {
    unsigned long type;
    uint32_t mask;
    uint32_t pattern;
    uint32_t flowControlId;
    bool active = false;
};

/**
 * J2534 Channel state
 */
struct Channel {
    unsigned long deviceId;
    unsigned long protocolId;
    unsigned long flags;
    unsigned long baudrate;
    uint8_t ecuHandle;          // Handle from ECUconnect device

    // Filters
    std::unordered_map<unsigned long, Filter> filters;
    unsigned long nextFilterId = 1;

    // Periodic messages
    std::unordered_map<unsigned long, uint8_t> periodicMessages;  // J2534 ID -> ECU handle
    unsigned long nextPeriodicId = 1;

    // Receive queue
    std::queue<PASSTHRU_MSG> rxQueue;
    std::mutex rxMutex;

    // Configuration
    bool loopback = false;
    uint32_t dataRate = 500000;
};

/**
 * Device state
 */
struct Device {
    std::unique_ptr<Protocol> protocol;
    std::unordered_map<unsigned long, std::unique_ptr<Channel>> channels;
    unsigned long nextChannelId = 1;

    DeviceInfo info;
    std::string connectionString;
};

/**
 * ECUconnect Device Manager
 * Singleton managing all J2534 devices and channels
 */
class DeviceManager {
public:
    static DeviceManager& instance();

    // Device management
    long openDevice(const char* name, unsigned long* deviceId);
    long closeDevice(unsigned long deviceId);

    // Channel management
    long connect(unsigned long deviceId, unsigned long protocolId,
                 unsigned long flags, unsigned long baudrate,
                 unsigned long* channelId);
    long disconnect(unsigned long channelId);

    // Message operations
    long readMsgs(unsigned long channelId, PASSTHRU_MSG* msgs,
                  unsigned long* numMsgs, unsigned long timeout);
    long writeMsgs(unsigned long channelId, const PASSTHRU_MSG* msgs,
                   unsigned long* numMsgs, unsigned long timeout);

    // Periodic messages
    long startPeriodicMsg(unsigned long channelId, const PASSTHRU_MSG* msg,
                          unsigned long* msgId, unsigned long timeInterval);
    long stopPeriodicMsg(unsigned long channelId, unsigned long msgId);

    // Filters
    long startMsgFilter(unsigned long channelId, unsigned long filterType,
                        const PASSTHRU_MSG* maskMsg, const PASSTHRU_MSG* patternMsg,
                        const PASSTHRU_MSG* flowControlMsg, unsigned long* filterId);
    long stopMsgFilter(unsigned long channelId, unsigned long filterId);

    // IOCTL
    long ioctl(unsigned long channelId, unsigned long ioctlId,
               const void* input, void* output);

    // Version info
    long readVersion(unsigned long deviceId, char* firmwareVersion,
                     char* dllVersion, char* apiVersion);

    // Error handling
    const char* getLastError() const;
    void setLastError(const std::string& error);

private:
    DeviceManager() = default;
    ~DeviceManager() = default;

    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    Device* getDevice(unsigned long deviceId);
    Channel* getChannel(unsigned long channelId);

    std::unordered_map<unsigned long, std::unique_ptr<Device>> devices_;
    std::unordered_map<unsigned long, unsigned long> channelToDevice_;  // channelId -> deviceId
    unsigned long nextDeviceId_ = 1;
    mutable std::mutex mutex_;
    std::string lastError_;

    // Poll for received messages
    void pollMessages(Channel* channel, Device* device, uint32_t timeout_ms);
};

} // namespace ecuconnect

#endif // ECUCONNECT_H
