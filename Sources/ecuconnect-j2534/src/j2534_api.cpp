/**
 * J2534 API Implementation
 * Exports the standard J2534-1 (04.04) API functions
 */
#include "j2534.h"
#include "ecuconnect.h"
#include <exception>

using namespace ecuconnect;

// Exception-safe wrapper macro for all API functions
// Catches any C++ exceptions and converts to ERR_FAILED
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

// ============================================================================
// Device Functions
// ============================================================================

J2534_EXPORT long J2534_API PassThruOpen(const void* pName, unsigned long* pDeviceID) {
    J2534_TRY_CATCH(DeviceManager::instance().openDevice(
        static_cast<const char*>(pName), pDeviceID));
}

J2534_EXPORT long J2534_API PassThruClose(unsigned long DeviceID) {
    J2534_TRY_CATCH(DeviceManager::instance().closeDevice(DeviceID));
}

// ============================================================================
// Channel Functions
// ============================================================================

J2534_EXPORT long J2534_API PassThruConnect(unsigned long DeviceID,
                                             unsigned long ProtocolID,
                                             unsigned long Flags,
                                             unsigned long Baudrate,
                                             unsigned long* pChannelID) {
    J2534_TRY_CATCH(DeviceManager::instance().connect(
        DeviceID, ProtocolID, Flags, Baudrate, pChannelID));
}

J2534_EXPORT long J2534_API PassThruDisconnect(unsigned long ChannelID) {
    J2534_TRY_CATCH(DeviceManager::instance().disconnect(ChannelID));
}

// ============================================================================
// Message Functions
// ============================================================================

J2534_EXPORT long J2534_API PassThruReadMsgs(unsigned long ChannelID,
                                              PASSTHRU_MSG* pMsg,
                                              unsigned long* pNumMsgs,
                                              unsigned long Timeout) {
    J2534_TRY_CATCH(DeviceManager::instance().readMsgs(
        ChannelID, pMsg, pNumMsgs, Timeout));
}

J2534_EXPORT long J2534_API PassThruWriteMsgs(unsigned long ChannelID,
                                               const PASSTHRU_MSG* pMsg,
                                               unsigned long* pNumMsgs,
                                               unsigned long Timeout) {
    J2534_TRY_CATCH(DeviceManager::instance().writeMsgs(
        ChannelID, pMsg, pNumMsgs, Timeout));
}

// ============================================================================
// Periodic Message Functions
// ============================================================================

J2534_EXPORT long J2534_API PassThruStartPeriodicMsg(unsigned long ChannelID,
                                                      const PASSTHRU_MSG* pMsg,
                                                      unsigned long* pMsgID,
                                                      unsigned long TimeInterval) {
    J2534_TRY_CATCH(DeviceManager::instance().startPeriodicMsg(
        ChannelID, pMsg, pMsgID, TimeInterval));
}

J2534_EXPORT long J2534_API PassThruStopPeriodicMsg(unsigned long ChannelID,
                                                     unsigned long MsgID) {
    J2534_TRY_CATCH(DeviceManager::instance().stopPeriodicMsg(ChannelID, MsgID));
}

// ============================================================================
// Filter Functions
// ============================================================================

J2534_EXPORT long J2534_API PassThruStartMsgFilter(unsigned long ChannelID,
                                                    unsigned long FilterType,
                                                    const PASSTHRU_MSG* pMaskMsg,
                                                    const PASSTHRU_MSG* pPatternMsg,
                                                    const PASSTHRU_MSG* pFlowControlMsg,
                                                    unsigned long* pFilterID) {
    J2534_TRY_CATCH(DeviceManager::instance().startMsgFilter(
        ChannelID, FilterType, pMaskMsg, pPatternMsg, pFlowControlMsg, pFilterID));
}

J2534_EXPORT long J2534_API PassThruStopMsgFilter(unsigned long ChannelID,
                                                   unsigned long FilterID) {
    J2534_TRY_CATCH(DeviceManager::instance().stopMsgFilter(ChannelID, FilterID));
}

// ============================================================================
// Voltage Functions
// ============================================================================

J2534_EXPORT long J2534_API PassThruSetProgrammingVoltage(unsigned long DeviceID,
                                                           unsigned long PinNumber,
                                                           unsigned long Voltage) {
    // Not supported on ECUconnect
    try {
        DeviceManager::instance().setLastError("Programming voltage not supported");
    } catch (...) {}
    return ERR_NOT_SUPPORTED;
}

// ============================================================================
// Version Functions
// ============================================================================

J2534_EXPORT long J2534_API PassThruReadVersion(unsigned long DeviceID,
                                                 char* pFirmwareVersion,
                                                 char* pDllVersion,
                                                 char* pApiVersion) {
    J2534_TRY_CATCH(DeviceManager::instance().readVersion(
        DeviceID, pFirmwareVersion, pDllVersion, pApiVersion));
}

// ============================================================================
// Error Functions
// ============================================================================

J2534_EXPORT long J2534_API PassThruGetLastError(char* pErrorDescription) {
    try {
        if (!pErrorDescription) {
            return ERR_NULL_PARAMETER;
        }

        const char* error = DeviceManager::instance().getLastError();
        std::strncpy(pErrorDescription, error, 80);
        pErrorDescription[79] = '\0';

        return STATUS_NOERROR;
    } catch (...) {
        if (pErrorDescription) {
            std::strncpy(pErrorDescription, "Exception in GetLastError", 80);
            pErrorDescription[79] = '\0';
        }
        return STATUS_NOERROR;
    }
}

// ============================================================================
// IOCTL Functions
// ============================================================================

J2534_EXPORT long J2534_API PassThruIoctl(unsigned long ChannelID,
                                           unsigned long IoctlID,
                                           const void* pInput,
                                           void* pOutput) {
    J2534_TRY_CATCH(DeviceManager::instance().ioctl(
        ChannelID, IoctlID, pInput, pOutput));
}
