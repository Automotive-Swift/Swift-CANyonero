#include "j2534.h"
#include "ecuconnect.h"
#include <cstring>
#include <exception>

using namespace ecuconnect_v0500;

#define J2534_TRY_CATCH(expr) \
    try { \
        return (expr); \
    } catch (const std::exception& e) { \
        DeviceManager::instance().setLastError(std::string("Exception: ") + e.what()); \
        return ERR_FAILED; \
    } catch (...) { \
        DeviceManager::instance().setLastError("Unknown exception"); \
        return ERR_FAILED; \
    }

extern "C" {

J2534_EXPORT int32_t J2534_API PassThruScanForDevices(void) {
    J2534_TRY_CATCH(DeviceManager::instance().scanForDevices());
}

J2534_EXPORT int32_t J2534_API PassThruGetNextDevice(SDEVICE* pDevice) {
    J2534_TRY_CATCH(DeviceManager::instance().getNextDevice(pDevice));
}

J2534_EXPORT int32_t J2534_API PassThruOpen(const void* pName, uint32_t* pDeviceID) {
    J2534_TRY_CATCH(DeviceManager::instance().openDevice(static_cast<const char*>(pName), pDeviceID));
}

J2534_EXPORT int32_t J2534_API PassThruClose(uint32_t DeviceID) {
    J2534_TRY_CATCH(DeviceManager::instance().closeDevice(DeviceID));
}

J2534_EXPORT int32_t J2534_API PassThruConnect(
    uint32_t DeviceID,
    uint32_t ProtocolID,
    uint32_t Flags,
    uint32_t Baudrate,
    const RESOURCE_STRUCT* pResourceStruct,
    uint32_t* pChannelID
) {
    J2534_TRY_CATCH(DeviceManager::instance().connect(
        DeviceID, ProtocolID, Flags, Baudrate, pResourceStruct, pChannelID));
}

J2534_EXPORT int32_t J2534_API PassThruDisconnect(uint32_t ChannelID) {
    J2534_TRY_CATCH(DeviceManager::instance().disconnect(ChannelID));
}

J2534_EXPORT int32_t J2534_API PassThruLogicalConnect(
    uint32_t PhysicalChannelID,
    uint32_t ProtocolID,
    uint32_t Flags,
    void* pChannelDescriptor,
    uint32_t* pChannelID
) {
    J2534_TRY_CATCH(DeviceManager::instance().logicalConnect(
        PhysicalChannelID, ProtocolID, Flags, pChannelDescriptor, pChannelID));
}

J2534_EXPORT int32_t J2534_API PassThruLogicalDisconnect(uint32_t ChannelID) {
    J2534_TRY_CATCH(DeviceManager::instance().logicalDisconnect(ChannelID));
}

J2534_EXPORT int32_t J2534_API PassThruSelect(
    uint32_t DeviceID,
    uint32_t SelectType,
    const SCHANNELSET* pChannelSet,
    uint32_t Timeout
) {
    J2534_TRY_CATCH(DeviceManager::instance().select(DeviceID, SelectType, pChannelSet, Timeout));
}

J2534_EXPORT int32_t J2534_API PassThruReadMsgs(
    uint32_t ChannelID,
    PASSTHRU_MSG* pMsg,
    uint32_t* pNumMsgs,
    uint32_t Timeout
) {
    J2534_TRY_CATCH(DeviceManager::instance().readMsgs(ChannelID, pMsg, pNumMsgs, Timeout));
}

J2534_EXPORT int32_t J2534_API PassThruQueueMsgs(
    uint32_t ChannelID,
    const PASSTHRU_MSG* pMsg,
    uint32_t* pNumMsgs,
    uint32_t Timeout
) {
    J2534_TRY_CATCH(DeviceManager::instance().queueMsgs(ChannelID, pMsg, pNumMsgs, Timeout));
}

J2534_EXPORT int32_t J2534_API PassThruStartPeriodicMsg(
    uint32_t ChannelID,
    const PASSTHRU_MSG* pMsg,
    uint32_t* pMsgID,
    uint32_t TimeInterval
) {
    J2534_TRY_CATCH(DeviceManager::instance().startPeriodicMsg(ChannelID, pMsg, pMsgID, TimeInterval));
}

J2534_EXPORT int32_t J2534_API PassThruStopPeriodicMsg(uint32_t ChannelID, uint32_t MsgID) {
    J2534_TRY_CATCH(DeviceManager::instance().stopPeriodicMsg(ChannelID, MsgID));
}

J2534_EXPORT int32_t J2534_API PassThruStartMsgFilter(
    uint32_t ChannelID,
    uint32_t FilterType,
    const PASSTHRU_MSG* pMaskMsg,
    const PASSTHRU_MSG* pPatternMsg,
    const PASSTHRU_MSG* pFlowControlMsg,
    uint32_t* pFilterID
) {
    J2534_TRY_CATCH(DeviceManager::instance().startMsgFilter(
        ChannelID, FilterType, pMaskMsg, pPatternMsg, pFlowControlMsg, pFilterID));
}

J2534_EXPORT int32_t J2534_API PassThruStopMsgFilter(uint32_t ChannelID, uint32_t FilterID) {
    J2534_TRY_CATCH(DeviceManager::instance().stopMsgFilter(ChannelID, FilterID));
}

J2534_EXPORT int32_t J2534_API PassThruSetProgrammingVoltage(
    uint32_t DeviceID,
    uint32_t PinNumber,
    uint32_t Voltage
) {
    (void)DeviceID;
    (void)PinNumber;
    (void)Voltage;
    DeviceManager::instance().setLastError("Programming voltage not supported");
    return ERR_NOT_SUPPORTED;
}

J2534_EXPORT int32_t J2534_API PassThruReadVersion(
    uint32_t DeviceID,
    char* pFirmwareVersion,
    char* pDllVersion,
    char* pApiVersion
) {
    J2534_TRY_CATCH(DeviceManager::instance().readVersion(
        DeviceID, pFirmwareVersion, pDllVersion, pApiVersion));
}

J2534_EXPORT int32_t J2534_API PassThruGetLastError(char* pErrorDescription) {
    if (!pErrorDescription) {
        return ERR_NULL_PARAMETER;
    }
    std::strncpy(pErrorDescription, DeviceManager::instance().getLastError(), 80);
    pErrorDescription[79] = '\0';
    return STATUS_NOERROR;
}

J2534_EXPORT int32_t J2534_API PassThruIoctl(
    uint32_t HandleID,
    uint32_t IoctlID,
    const void* pInput,
    void* pOutput
) {
    J2534_TRY_CATCH(DeviceManager::instance().ioctl(HandleID, IoctlID, pInput, pOutput));
}

}
