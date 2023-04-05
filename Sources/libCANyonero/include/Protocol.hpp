///
/// CANyonero. (C) 2022 - 2023 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#ifndef Command_hpp
#define Command_hpp

#include <string>
#include <vector>

// Helper method for swift
std::vector<uint8_t> createVector8FromArray(const uint8_t* array, const size_t length);

namespace CANyonero {

/// The CANyonero protocol.
///
/// This is the C++ header file for a protocol called "CANyonero".
/// This protocol is designed for communication over a Controller Area Network (CAN) bus,
/// a standard used in automotive and industrial applications.
/// The protocol specifies a structure for the messages that are sent over the bus,
/// including a header with a fixed length of four bytes, and optional payload data.
///
/// The basic protocol unit on the wire looks like that:
/// ```
/// [ ATT:UInt8 | TYP:UInt8 | LEN:UInt16 | < ... optional payload ... > ]
/// ```
///
/// The fixed header length is 4. `ATT` is hardcoded to be `0x1F`. Maximum payload length is `0xFFFF`, therefore the maximum PDU length is `0x10003`.
/// The header includes a "type" field (`TYP`) that specifies the type of message being sent,
/// such as a ping command or a request for information, and a "length" field (`LEN`) that indicates the length of the payload data.
///
/// The protocol includes several types and structures that are used to define the messages sent over the bus,
/// such as an ``Arbitration`` structure that specifies the addresses of the sending and receiving devices,
/// and a ``ChannelProtocol`` enumeration that defines the type of protocol being used on a logical channel.
///
/// Also included are several command codes that can be used to send messages over the bus,
/// such as a ping command to test the connection and measure latency, a send command to send data
/// over a logical channel, and a reset command to reset the connection.
///
/// A number of positive response codes can be used to indicate that a command was successful,
/// as well as several error codes that can be used to indicate that an error occurred.
///

typedef uint8_t ChannelHandle;
typedef uint8_t PeriodicMessageHandle;
typedef std::vector<uint8_t> Bytes;

using StdVectorOfUInt8 = std::vector<uint8_t>;

/// The Arbitration type.
struct Arbitration {

    /// Request (or Source).
    uint32_t request;
    /// Reply Pattern (or Destination)
    uint32_t replyPattern;
    /// Reply Mask (0xffff, if not used)
    uint32_t replyMask;
    /// Request extension (for CAN EA)
    uint8_t requestExtension;
    /// Reply extension (for CAN EA)
    uint8_t replyExtension;

    /// Serialize.
    void serialize(Bytes& payload) const;
};

/// The Channel protocol type.
enum class ChannelProtocol: uint8_t {
    /// Raw frame. Maximum length = 8 Byte.
    raw                   = 0x00,
    /// ISOTP (ISO 15765-2) frame. Maximum length = 4095.
    isotp                 = 0x01,
};

/// Periodic message payload type.
struct PeriodicMessage {

    /// Time interval in milliseconds.
    uint32_t timeInterval;
    /// Data frame.
    uint8_t data[8];
};

/// The PDU type.
enum class PDUType: uint8_t {

    /// Ping & Info Commands

    /// PING ­– Tests the command processor. CAN include an arbitrary amount of payload (up to the maximum of 65535 bytes) which will be echoed.
    ping                    = 0x00,
    /// REQUEST INFO­ – Requests device information.
    requestInfo             = 0x01,
    /// READ VOLTAGE ­– Requests battery voltage.
    readVoltage             = 0x02,

    /// *Automotive Communication Commands*

    /// OPEN ­– Open a logical channel. MUST include protocol specification (`UInt8`). See ``ChannelProtocol`` for available protocols.
    openChannel             = 0x03,
    /// CLOSE ­– Close a logical channel. MUST include the channel number (`UInt8`).
    closeChannel            = 0x04,
    /// SEND ­– Send a data frame over the logical channel. MUST include the channel number (`UInt8`) and the data (`[UInt8]`). Maximum data length is protocol specific.
    send                    = 0x05,
    /// SET ARBITRATION ­– Set the request and response (or source and target) addresses. MUST include the channel number (`UInt8`) and arbitration infos. See ``Arbitration``.
    setArbitration          = 0x06,
    /// START PERIODIC ­MESSAGE – Begin with sending a periodic message out-of-band. MUST include the ``PeriodicMessage`` structure.
    startPeriodicMessage    = 0x07,
    /// END PERIODIC MESSAGE ­– End with sending a periodic message out-of-band. MUST include a valid handle received from `startPeriodicMessage`.
    endPeriodicMessage      = 0x08,

    /// *Maintenance Commands*

    /// PREPARE FOR UPDATE ­– Begin firmware update.
    prepareForUpdate        = 0x0A,
    /// SEND UPDATE DATA ­– Send update data.
    sendUpdateData          = 0x0B,
    /// COMMIT UPDATE ­– Install new data and reset.
    commitUpdate            = 0x0C,
    /// REBOOT ­– Reset.
    reset                   = 0x0F,

    /// *Positive Replies*

    /// Response to PING. MAY include payload.
    pong                    = 0xF0,
    /// Info ­response ­– MUST include the following UTF8-strings separated by `\n` (`0x0A`):
    /// 1. Vendor name
    /// 2. Model name
    /// 3. Chipset + Hardware revision
    /// 4. Serial number
    /// 5. Firmware version
    info                    = 0xF1,
    /// Battery Voltage. MUST include the battery voltage in millivolts (UInt16).
    voltage                 = 0xF2,
    /// Channel successfully opened ­– MUST include the (new) logical channel number (UInt8).
    channelOpen             = 0xF3,
    /// Channel successfully closed ­– MUST include the logical channel number (UInt8).
    channelClosed           = 0xF4,
    /// Data sent ­– MUST include the logical channel number (UInt8).
    sent                    = 0xF5,
    /// Arbitration set ­– MUST include the (new) logical channel number (UInt8).
    arbitrationSet          = 0xF6,
    /// Periodic message started to send ­– MUST include the (new) handle for the periodic message.
    periodicMessageStarted  = 0xF7,
    /// Periodic message ended ­– MUST include the (new) handle for the periodic message.
    periodicMessageEnded    = 0xF8,
    /// Update prepared.
    updateStartedSendData   = 0xFA,
    /// Update data received.
    updateDataReceived      = 0xFB,
    /// Update completed.
    updateCompleted         = 0xFC,
    /// Reset triggered. Connection will close.
    resetting               = 0xFF,

    /// *Negative Replies*

    /// An unspecified error has occured.
    errorUnspecified        = 0xE0,
    /// A hardware error has occured.
    errorHardware           = 0xE1,
    /// Invalid channel selected.
    errorInvalidChannel     = 0xE2,
    /// Invalid periodic message handle.
    errorInvalidPeriodic    = 0xE3,
    /// No response received.
    errorNoResponse         = 0xE4,
    /// Invalid command sent.
    errorInvalidCommand     = 0xEF,
};

/// Encapsulates a PDU on the wire.
class PDU {

    static const uint8_t ATT = 0x1F;

    PDUType _type;
    std::vector<uint8_t> _payload;

public:
    PDU(const PDUType type): _type(type) {};
    PDU(const PDUType type, const std::vector<uint8_t> payload): _type(type), _payload(payload) {};
    const std::vector<uint8_t> frame() const;
    const PDUType& type() const { return _type; }

    /// Creates a `ping` PDU.
    static PDU ping(const Bytes payload = {});
    /// Creates a `requestInfo` PDU.
    static PDU requestInfo();
    /// Creates a `readVoltage` PDU.
    static PDU readVoltage();
    /// Creates an `openChannel` PDU.
    static PDU openChannel(const ChannelProtocol protocol);
    /// Creates a `closeChannel` PDU.
    static PDU closeChannel(const ChannelHandle handle);
    /// Creates a `send` PDU.
    static PDU send(const ChannelHandle handle, const Bytes data);
    /// Creates a `setArbitration` PDU.
    static PDU setArbitration(const ChannelHandle handle, const Arbitration arbitration);
    /// Creates a `startPeriodicMessage` PDU.
    static PDU startPeriodicMessage(const Arbitration arbitration, const Bytes data);
    /// Creates a `endPeriodicMessage` PDU.
    static PDU endPeriodicMessage(const PeriodicMessageHandle handle);
    /// Creates a `prepareForUpdate` PDU.
    static PDU prepareForUpdate();
    /// Creates a `sendUpdateData` PDU.
    static PDU sendUpdateData(const Bytes data);
    /// Creates a `commitUpdate` PDU.
    static PDU commitUpdate();
    /// Creates a `reset` PDU.
    static PDU reset();

    

};

};

#endif
