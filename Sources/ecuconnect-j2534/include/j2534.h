/**
 * J2534 API Header - SAE J2534-1 (04.04)
 * Standard Pass-Thru Vehicle Programming Interface
 */
#ifndef J2534_H
#define J2534_H

#ifdef _WIN32
#include <windows.h>
#define J2534_API __stdcall
#ifdef J2534_EXPORTS
#define J2534_EXPORT __declspec(dllexport)
#else
#define J2534_EXPORT __declspec(dllimport)
#endif
#else
#define J2534_API
#define J2534_EXPORT
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Error Codes
// ============================================================================
enum J2534Error : unsigned long {
    STATUS_NOERROR              = 0x00,
    ERR_NOT_SUPPORTED           = 0x01,
    ERR_INVALID_CHANNEL_ID      = 0x02,
    ERR_INVALID_PROTOCOL_ID     = 0x03,
    ERR_NULL_PARAMETER          = 0x04,
    ERR_INVALID_IOCTL_VALUE     = 0x05,
    ERR_INVALID_FLAGS           = 0x06,
    ERR_FAILED                  = 0x07,
    ERR_DEVICE_NOT_CONNECTED    = 0x08,
    ERR_TIMEOUT                 = 0x09,
    ERR_INVALID_MSG             = 0x0A,
    ERR_INVALID_TIME_INTERVAL   = 0x0B,
    ERR_EXCEEDED_LIMIT          = 0x0C,
    ERR_INVALID_MSG_ID          = 0x0D,
    ERR_DEVICE_IN_USE           = 0x0E,
    ERR_INVALID_IOCTL_ID        = 0x0F,
    ERR_BUFFER_EMPTY            = 0x10,
    ERR_BUFFER_FULL             = 0x11,
    ERR_BUFFER_OVERFLOW         = 0x12,
    ERR_PIN_INVALID             = 0x13,
    ERR_CHANNEL_IN_USE          = 0x14,
    ERR_MSG_PROTOCOL_ID         = 0x15,
    ERR_INVALID_FILTER_ID       = 0x16,
    ERR_NO_FLOW_CONTROL         = 0x17,
    ERR_NOT_UNIQUE              = 0x18,
    ERR_INVALID_BAUDRATE        = 0x19,
    ERR_INVALID_DEVICE_ID       = 0x1A,
};

// ============================================================================
// Protocol IDs
// ============================================================================
enum J2534Protocol : unsigned long {
    J1850VPW                    = 0x01,
    J1850PWM                    = 0x02,
    ISO9141                     = 0x03,
    ISO14230                    = 0x04,
    CAN                         = 0x05,
    ISO15765                    = 0x06,
    SCI_A_ENGINE                = 0x07,
    SCI_A_TRANS                 = 0x08,
    SCI_B_ENGINE                = 0x09,
    SCI_B_TRANS                 = 0x0A,
};

// ============================================================================
// IOCTL IDs
// ============================================================================
enum J2534Ioctl : unsigned long {
    GET_CONFIG                  = 0x01,
    SET_CONFIG                  = 0x02,
    READ_VBATT                  = 0x03,
    FIVE_BAUD_INIT              = 0x04,
    FAST_INIT                   = 0x05,
    CLEAR_TX_BUFFER             = 0x07,
    CLEAR_RX_BUFFER             = 0x08,
    CLEAR_PERIODIC_MSGS         = 0x09,
    CLEAR_MSG_FILTERS           = 0x0A,
    CLEAR_FUNCT_MSG_LOOKUP_TABLE = 0x0B,
    ADD_TO_FUNCT_MSG_LOOKUP_TABLE = 0x0C,
    DELETE_FROM_FUNCT_MSG_LOOKUP_TABLE = 0x0D,
    READ_PROG_VOLTAGE           = 0x0E,
};

// ============================================================================
// Configuration Parameter IDs
// ============================================================================
enum J2534ConfigParam : unsigned long {
    DATA_RATE                   = 0x01,
    LOOPBACK                    = 0x03,
    NODE_ADDRESS                = 0x04,
    NETWORK_LINE                = 0x05,
    P1_MIN                      = 0x06,
    P1_MAX                      = 0x07,
    P2_MIN                      = 0x08,
    P2_MAX                      = 0x09,
    P3_MIN                      = 0x0A,
    P3_MAX                      = 0x0B,
    P4_MIN                      = 0x0C,
    P4_MAX                      = 0x0D,
    W1                          = 0x0E,
    W2                          = 0x0F,
    W3                          = 0x10,
    W4                          = 0x11,
    W5                          = 0x12,
    TIDLE                       = 0x13,
    TINIL                       = 0x14,
    TWUP                        = 0x15,
    PARITY                      = 0x16,
    BIT_SAMPLE_POINT            = 0x17,
    SYNC_JUMP_WIDTH             = 0x18,
    W0                          = 0x19,
    T1_MAX                      = 0x1A,
    T2_MAX                      = 0x1B,
    T4_MAX                      = 0x1C,
    T5_MAX                      = 0x1D,
    ISO15765_BS                 = 0x1E,
    ISO15765_STMIN              = 0x1F,
    DATA_BITS                   = 0x20,
    FIVE_BAUD_MOD               = 0x21,
    BS_TX                       = 0x22,
    STMIN_TX                    = 0x23,
    T3_MAX                      = 0x24,
    ISO15765_WFT_MAX            = 0x25,
    CAN_MIXED_FORMAT            = 0x8000,
    J1962_PINS                  = 0x8001,
    SW_CAN_HS_DATA_RATE         = 0x8010,
    SW_CAN_SPEEDCHANGE_ENABLE   = 0x8011,
    SW_CAN_RES_SWITCH           = 0x8012,
    ACTIVE_CHANNELS             = 0x8020,
    SAMPLE_RATE                 = 0x8021,
    SAMPLES_PER_READING         = 0x8022,
    READINGS_PER_MSG            = 0x8023,
    AVERAGING_METHOD            = 0x8024,
    SAMPLE_RESOLUTION           = 0x8025,
    INPUT_RANGE_LOW             = 0x8026,
    INPUT_RANGE_HIGH            = 0x8027,
};

// ============================================================================
// Filter Types
// ============================================================================
enum J2534FilterType : unsigned long {
    PASS_FILTER                 = 0x01,
    BLOCK_FILTER                = 0x02,
    FLOW_CONTROL_FILTER         = 0x03,
};

// ============================================================================
// Message Flags
// ============================================================================
enum J2534MsgFlags : unsigned long {
    TX_MSG_TYPE                 = 0x0001,
    ISO15765_FRAME_PAD          = 0x0040,
    ISO15765_ADDR_TYPE          = 0x0080,
    CAN_29BIT_ID                = 0x0100,
    WAIT_P3_MIN_ONLY            = 0x0200,
    SW_CAN_HV_TX                = 0x0400,
    SCI_MODE                    = 0x400000,
    SCI_TX_VOLTAGE              = 0x800000,
};

// ============================================================================
// Structures
// ============================================================================

// SCONFIG and SCONFIG_LIST use natural alignment (no packing)
typedef struct {
    unsigned long Parameter;
    unsigned long Value;
} SCONFIG;

typedef struct {
    unsigned long NumOfParams;
    SCONFIG* ConfigPtr;
} SCONFIG_LIST;

typedef struct {
    unsigned long NumOfBytes;
    unsigned char* BytePtr;
} SBYTE_ARRAY;

// PASSTHRU_MSG requires 1-byte packing per J2534 spec
#pragma pack(push, 1)

typedef struct {
    unsigned long ProtocolID;
    unsigned long RxStatus;
    unsigned long TxFlags;
    unsigned long Timestamp;
    unsigned long DataSize;
    unsigned long ExtraDataIndex;
    unsigned char Data[4128];
} PASSTHRU_MSG;

#pragma pack(pop)

// ============================================================================
// J2534 API Functions
// ============================================================================

J2534_EXPORT long J2534_API PassThruOpen(
    const void* pName,
    unsigned long* pDeviceID
);

J2534_EXPORT long J2534_API PassThruClose(
    unsigned long DeviceID
);

J2534_EXPORT long J2534_API PassThruConnect(
    unsigned long DeviceID,
    unsigned long ProtocolID,
    unsigned long Flags,
    unsigned long Baudrate,
    unsigned long* pChannelID
);

J2534_EXPORT long J2534_API PassThruDisconnect(
    unsigned long ChannelID
);

J2534_EXPORT long J2534_API PassThruReadMsgs(
    unsigned long ChannelID,
    PASSTHRU_MSG* pMsg,
    unsigned long* pNumMsgs,
    unsigned long Timeout
);

J2534_EXPORT long J2534_API PassThruWriteMsgs(
    unsigned long ChannelID,
    const PASSTHRU_MSG* pMsg,
    unsigned long* pNumMsgs,
    unsigned long Timeout
);

J2534_EXPORT long J2534_API PassThruStartPeriodicMsg(
    unsigned long ChannelID,
    const PASSTHRU_MSG* pMsg,
    unsigned long* pMsgID,
    unsigned long TimeInterval
);

J2534_EXPORT long J2534_API PassThruStopPeriodicMsg(
    unsigned long ChannelID,
    unsigned long MsgID
);

J2534_EXPORT long J2534_API PassThruStartMsgFilter(
    unsigned long ChannelID,
    unsigned long FilterType,
    const PASSTHRU_MSG* pMaskMsg,
    const PASSTHRU_MSG* pPatternMsg,
    const PASSTHRU_MSG* pFlowControlMsg,
    unsigned long* pFilterID
);

J2534_EXPORT long J2534_API PassThruStopMsgFilter(
    unsigned long ChannelID,
    unsigned long FilterID
);

J2534_EXPORT long J2534_API PassThruSetProgrammingVoltage(
    unsigned long DeviceID,
    unsigned long PinNumber,
    unsigned long Voltage
);

J2534_EXPORT long J2534_API PassThruReadVersion(
    unsigned long DeviceID,
    char* pFirmwareVersion,
    char* pDllVersion,
    char* pApiVersion
);

J2534_EXPORT long J2534_API PassThruGetLastError(
    char* pErrorDescription
);

J2534_EXPORT long J2534_API PassThruIoctl(
    unsigned long ChannelID,
    unsigned long IoctlID,
    const void* pInput,
    void* pOutput
);

#ifdef __cplusplus
}
#endif

#endif // J2534_H
