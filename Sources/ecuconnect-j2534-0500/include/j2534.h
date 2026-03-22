/**
 * J2534 API Header - SAE J2534-1 (05.00)
 *
 * This target keeps the 05.00 surface separate from the legacy 04.04 DLL.
 * The 05.00 API switches to fixed-width integer types and a pointer-based
 * PASSTHRU_MSG payload model.
 */
#ifndef ECUCONNECT_J2534_V0500_H
#define ECUCONNECT_J2534_V0500_H

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

enum J2534Error : uint32_t {
    STATUS_NOERROR              = 0x00000000,
    ERR_NOT_SUPPORTED           = 0x00000001,
    ERR_INVALID_CHANNEL_ID      = 0x00000002,
    ERR_INVALID_PROTOCOL_ID     = 0x00000003,
    ERR_NULL_PARAMETER          = 0x00000004,
    ERR_INVALID_IOCTL_VALUE     = 0x00000005,
    ERR_INVALID_FLAGS           = 0x00000006,
    ERR_FAILED                  = 0x00000007,
    ERR_DEVICE_NOT_CONNECTED    = 0x00000008,
    ERR_TIMEOUT                 = 0x00000009,
    ERR_INVALID_MSG             = 0x0000000A,
    ERR_INVALID_TIME_INTERVAL   = 0x0000000B,
    ERR_EXCEEDED_LIMIT          = 0x0000000C,
    ERR_INVALID_MSG_ID          = 0x0000000D,
    ERR_DEVICE_IN_USE           = 0x0000000E,
    ERR_INVALID_IOCTL_ID        = 0x0000000F,
    ERR_BUFFER_EMPTY            = 0x00000010,
    ERR_BUFFER_FULL             = 0x00000011,
    ERR_BUFFER_OVERFLOW         = 0x00000012,
    ERR_PIN_INVALID             = 0x00000013,
    ERR_CHANNEL_IN_USE          = 0x00000014,
    ERR_MSG_PROTOCOL_ID         = 0x00000015,
    ERR_INVALID_FILTER_ID       = 0x00000016,
    ERR_NO_FLOW_CONTROL         = 0x00000017,
    ERR_NOT_UNIQUE              = 0x00000018,
    ERR_INVALID_BAUDRATE        = 0x00000019,
    ERR_INVALID_DEVICE_ID       = 0x0000001A,
    ERR_DEVICE_NOT_OPEN         = 0x0000001B,
    ERR_CONCURRENT_API_CALL     = 0x0000001C,
    ERR_BUFFER_TOO_SMALL        = 0x0000001D,
    ERR_INIT_FAILED             = 0x0000001E,
};

enum J2534Protocol : uint32_t {
    J1850VPW                    = 0x00000001,
    J1850PWM                    = 0x00000002,
    ISO9141                     = 0x00000004,
    ISO14230                    = 0x00000008,
    CAN                         = 0x00000010,
    ISO15765                    = 0x00000200,
    J2610                       = 0x00000020,

    // Optional feature identifiers used by this DLL for CAN-FD support.
    CAN_FD_PS                   = 0x00008000,
    ISO15765_FD_PS              = 0x00008001,
};

enum J2534Ioctl : uint32_t {
    GET_CONFIG                  = 0x00000001,
    SET_CONFIG                  = 0x00000002,
    READ_PIN_VOLTAGE            = 0x00000003,
    FIVE_BAUD_INIT              = 0x00000004,
    FAST_INIT                   = 0x00000005,
    CLEAR_TX_QUEUE              = 0x00000007,
    CLEAR_RX_QUEUE              = 0x00000008,
    CLEAR_PERIODIC_MSGS         = 0x00000009,
    CLEAR_MSG_FILTERS           = 0x0000000A,
    CLEAR_FUNCT_MSG_LOOKUP_TABLE = 0x0000000B,
    ADD_TO_FUNCT_MSG_LOOKUP_TABLE = 0x0000000C,
    DELETE_FROM_FUNCT_MSG_LOOKUP_TABLE = 0x0000000D,
    READ_PROG_VOLTAGE           = 0x0000000E,
    BUS_ON                      = 0x0000000F,

    // Optional discovery/introspection helpers implemented by this driver.
    GET_DEVICE_INFO             = 0x00008000,
    GET_PROTOCOL_INFO           = 0x00008001,
    GET_RESOURCE_INFO           = 0x00008002,
};

enum J2534ConfigParam : uint32_t {
    DATA_RATE                   = 0x00000001,
    LOOPBACK                    = 0x00000003,
    NODE_ADDRESS                = 0x00000004,
    NETWORK_LINE                = 0x00000005,
    P1_MIN                      = 0x00000006,
    P1_MAX                      = 0x00000007,
    P2_MIN                      = 0x00000008,
    P2_MAX                      = 0x00000009,
    P3_MIN                      = 0x0000000A,
    P3_MAX                      = 0x0000000B,
    P4_MIN                      = 0x0000000C,
    P4_MAX                      = 0x0000000D,
    W1                          = 0x0000000E,
    W2                          = 0x0000000F,
    W3                          = 0x00000010,
    W4                          = 0x00000011,
    W5                          = 0x00000012,
    TIDLE                       = 0x00000013,
    TINIL                       = 0x00000014,
    TWUP                        = 0x00000015,
    PARITY                      = 0x00000016,
    BIT_SAMPLE_POINT            = 0x00000017,
    SYNC_JUMP_WIDTH             = 0x00000018,
    W0                          = 0x00000019,
    T1_MAX                      = 0x0000001A,
    T2_MAX                      = 0x0000001B,
    T4_MAX                      = 0x0000001C,
    T5_MAX                      = 0x0000001D,
    ISO15765_BS                 = 0x0000001E,
    ISO15765_STMIN              = 0x0000001F,
    DATA_BITS                   = 0x00000020,
    FIVE_BAUD_MOD               = 0x00000021,
    BS_TX                       = 0x00000022,
    STMIN_TX                    = 0x00000023,
    T3_MAX                      = 0x00000024,
    ISO15765_WFT_MAX            = 0x00000025,
    J1962_PINS                  = 0x00008001,
    FD_CAN_DATA_PHASE_RATE      = 0x00008030,
    FD_ISO15765_TX_DATA_LEN     = 0x00008031,
};

enum J2534FilterType : uint32_t {
    PASS_FILTER                 = 0x00000001,
    BLOCK_FILTER                = 0x00000002,
};

enum J2534ConnectFlags : uint32_t {
    FULL_DUPLEX                 = 0x00000001,
    CAN_29BIT_ID                = 0x00000100,
    ISO15765_FRAME_PAD          = 0x00000040,
    ISO15765_ADDR_TYPE          = 0x00000080,
    CHECKSUM_DISABLED           = 0x00000400,
};

enum J2534RxStatus : uint32_t {
    TX_MSG_TYPE                 = 0x00000001,
    START_OF_MESSAGE            = 0x00000002,
    RX_BREAK                    = 0x00000004,
    TX_SUCCESS                  = 0x00000008,
    TX_FAILED                   = 0x00000010,
    ISO15765_ADDR_TYPE_STATUS   = 0x00000080,
    CAN_29BIT_ID_STATUS         = 0x00000100,
};

enum J2534SelectType : uint32_t {
    READABLE_TYPE               = 0x00000001,
};

typedef struct {
    char DeviceName[80];
    uint32_t DeviceAvailable;
    uint32_t DeviceDLLFWStatus;
    uint32_t DeviceConnectMedia;
    uint32_t DeviceConnectSpeed;
    uint32_t DeviceSignalQuality;
    uint32_t DeviceSignalStrength;
} SDEVICE;

typedef struct {
    uint32_t Connector;
    uint32_t NumOfResources;
    uint32_t* ResourceListPtr;
} RESOURCE_STRUCT;

typedef struct {
    uint32_t LocalTxFlags;
    uint32_t RemoteTxFlags;
    unsigned char LocalAddress[5];
    unsigned char RemoteAddress[5];
} ISO15765_CHANNEL_DESCRIPTOR;

typedef struct {
    uint32_t ChannelCount;
    uint32_t ChannelThreshold;
    uint32_t* ChannelList;
} SCHANNELSET;

typedef struct {
    uint32_t Parameter;
    uint32_t Value;
} SCONFIG;

typedef struct {
    uint32_t NumOfParams;
    SCONFIG* ConfigPtr;
} SCONFIG_LIST;

typedef struct {
    uint32_t NumOfBytes;
    unsigned char* BytePtr;
} SBYTE_ARRAY;

typedef struct {
    uint32_t Id;
    uint32_t Supported;
    uint32_t Value;
} SDEVICE_INFO_ITEM;

typedef struct {
    uint32_t NumOfItems;
    SDEVICE_INFO_ITEM* ItemPtr;
} SDEVICE_INFO_LIST;

typedef struct {
    uint32_t ProtocolID;
    uint32_t Supported;
    uint32_t Flags;
    uint32_t MaxPayloadSize;
    uint32_t NominalBaudRate;
    uint32_t DataBaudRate;
} SPROTOCOL_INFO;

typedef struct {
    uint32_t NumOfProtocols;
    SPROTOCOL_INFO* ProtocolPtr;
} SPROTOCOL_INFO_LIST;

typedef struct {
    uint32_t Connector;
    uint32_t ResourceId;
    uint32_t Supported;
} SRESOURCE_INFO;

typedef struct {
    uint32_t NumOfResources;
    SRESOURCE_INFO* ResourcePtr;
} SRESOURCE_INFO_LIST;

typedef struct {
    uint32_t ProtocolID;
    uint32_t MsgHandle;
    uint32_t RxStatus;
    uint32_t TxFlags;
    uint32_t Timestamp;
    uint32_t DataLength;
    uint32_t ExtraDataIndex;
    unsigned char* DataBuffer;
    uint32_t DataBufferSize;
} PASSTHRU_MSG;

J2534_EXPORT int32_t J2534_API PassThruScanForDevices(void);
J2534_EXPORT int32_t J2534_API PassThruGetNextDevice(SDEVICE* pDevice);

J2534_EXPORT int32_t J2534_API PassThruOpen(const void* pName, uint32_t* pDeviceID);
J2534_EXPORT int32_t J2534_API PassThruClose(uint32_t DeviceID);
J2534_EXPORT int32_t J2534_API PassThruConnect(
    uint32_t DeviceID,
    uint32_t ProtocolID,
    uint32_t Flags,
    uint32_t Baudrate,
    const RESOURCE_STRUCT* pResourceStruct,
    uint32_t* pChannelID
);
J2534_EXPORT int32_t J2534_API PassThruDisconnect(uint32_t ChannelID);
J2534_EXPORT int32_t J2534_API PassThruLogicalConnect(
    uint32_t PhysicalChannelID,
    uint32_t ProtocolID,
    uint32_t Flags,
    void* pChannelDescriptor,
    uint32_t* pChannelID
);
J2534_EXPORT int32_t J2534_API PassThruLogicalDisconnect(uint32_t ChannelID);
J2534_EXPORT int32_t J2534_API PassThruSelect(
    uint32_t DeviceID,
    uint32_t SelectType,
    const SCHANNELSET* pChannelSet,
    uint32_t Timeout
);
J2534_EXPORT int32_t J2534_API PassThruReadMsgs(
    uint32_t ChannelID,
    PASSTHRU_MSG* pMsg,
    uint32_t* pNumMsgs,
    uint32_t Timeout
);
J2534_EXPORT int32_t J2534_API PassThruQueueMsgs(
    uint32_t ChannelID,
    const PASSTHRU_MSG* pMsg,
    uint32_t* pNumMsgs,
    uint32_t Timeout
);
J2534_EXPORT int32_t J2534_API PassThruStartPeriodicMsg(
    uint32_t ChannelID,
    const PASSTHRU_MSG* pMsg,
    uint32_t* pMsgID,
    uint32_t TimeInterval
);
J2534_EXPORT int32_t J2534_API PassThruStopPeriodicMsg(uint32_t ChannelID, uint32_t MsgID);
J2534_EXPORT int32_t J2534_API PassThruStartMsgFilter(
    uint32_t ChannelID,
    uint32_t FilterType,
    const PASSTHRU_MSG* pMaskMsg,
    const PASSTHRU_MSG* pPatternMsg,
    const PASSTHRU_MSG* pFlowControlMsg,
    uint32_t* pFilterID
);
J2534_EXPORT int32_t J2534_API PassThruStopMsgFilter(uint32_t ChannelID, uint32_t FilterID);
J2534_EXPORT int32_t J2534_API PassThruSetProgrammingVoltage(
    uint32_t DeviceID,
    uint32_t PinNumber,
    uint32_t Voltage
);
J2534_EXPORT int32_t J2534_API PassThruReadVersion(
    uint32_t DeviceID,
    char* pFirmwareVersion,
    char* pDllVersion,
    char* pApiVersion
);
J2534_EXPORT int32_t J2534_API PassThruGetLastError(char* pErrorDescription);
J2534_EXPORT int32_t J2534_API PassThruIoctl(
    uint32_t HandleID,
    uint32_t IoctlID,
    const void* pInput,
    void* pOutput
);

#ifdef __cplusplus
}
#endif

#endif
