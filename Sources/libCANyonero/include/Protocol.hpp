///
/// CANyonero. (C) 2022 - 2023 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#ifndef Command_hpp
#define Command_hpp

#include <string>
#include <utility>
#include <vector>

#include "Helpers.hpp"

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

/// The separation time type.
typedef uint8_t SeparationTimeCode;

/// The microseconds.
typedef uint16_t Microseconds;

/// The Info type
struct Info {

    std::string vendor;
    std::string model;
    std::string hardware;
    std::string serial;
    std::string firmware;

    static Info from_vector(const Bytes& data);
};

/// The Arbitration type.
struct Arbitration {

    static constexpr const size_t size = 4+4+4+1+1;

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

    void to_vector(Bytes& payload) const;
    static Arbitration from_vector(const Bytes& payload);
};

/// The Channel protocol type.
enum class ChannelProtocol: uint8_t {
    /// Raw CAN frames. Maximum length = 8 Byte.
    raw                   = 0x00,
    /// ISOTP (ISO 15765-2) frames. Maximum length = 4095 Bytes.
    isotp                 = 0x01,
    /// KLINE (ISO 9141)
    kline                 = 0x02,
    /// CAN FD frames. Maximum length = 64 Bytes.
    can_fd                = 0x03,
    /// ISOTP w/ CANFD. Maximum Length = 4 GBytes.
    isotp_fd              = 0x04,
    /// Raw CAN frames with automatic Flow Control for ISOTP First Frames. Maximum length = 8 Byte.
    raw_with_fc           = 0x05,
    /// ENET frames. Maximum length = 4095 Bytes.
    enet                  = 0x06,
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

    /// *Ping & Info Commands*

    /// PING ­– Tests the command processor. CAN include an arbitrary amount of payload (up to the maximum of 65535 bytes) which will be echoed.
    ping                    = 0x10,
    /// REQUEST INFO­ – Requests sending the device information.
    requestInfo             = 0x11,
    /// READ VOLTAGE ­– Requests sending the battery voltage.
    readVoltage             = 0x12,

    /// *Automotive Communication Commands*

    /// OPEN ­– Requests opening a logical channel. MUST include protocol specification (`UInt8`), bitrate (`UInt32`) in bps, and STmin specification (`UInt16`) in µs.
    /// The separation time is encoded for RX in the high nibble, TX in the low nibble, according to the following table:
    /// Value | Separation Time (µs)
    /// ----- | ---------------------
    /// 0x00  | 0
    /// 0x01  | 1000
    /// 0x02  | 2000
    /// 0x03  | 3000
    /// 0x04  | 4000
    /// 0x05  | 5000
    /// 0x06  | 6000
    /// 0x07  | 100
    /// 0x08  | 200
    /// 0x09  | 300
    /// 0x0A  | 400
    /// 0x0B  | 500
    /// 0x0C  | 600
    /// 0x0D  | 700
    /// 0x0E  | 800
    /// 0x0F  | 900
    /// ----- | ---------------------
    /// See ``ChannelProtocol`` for available protocols.
    openChannel             = 0x30,
    /// CLOSE ­– Requests closing a logical channel. MUST include the channel number (`UInt8`).
    closeChannel            = 0x31,
    /// SEND ­– Requests sending a data frame of vehicle protocol data over the logical channel. MUST include the channel number (`UInt8`) and the data (`[UInt8]`). The maximum data length specific to the channel protocol.
    send                    = 0x33,
    /// SET ARBITRATION ­– Set the request and response (or source and target) addresses. MUST include the channel number (`UInt8`) and arbitration infos. See ``Arbitration``.
    setArbitration          = 0x34,
    /// START PERIODIC ­MESSAGE – Begin with sending a periodic message out-of-band. MUST include the ``PeriodicMessage`` structure.
    startPeriodicMessage    = 0x35,
    /// END PERIODIC MESSAGE ­– End with sending a periodic message out-of-band. MUST include a valid handle received from `startPeriodicMessage or `0` (for all).
    endPeriodicMessage      = 0x36,
    /// SEND COMPRESSED ­– Requests sending a data frame of vehicle protocol data over the logical channel. MUST include the channel number (`UInt8`), the uncompressed size (`UInt16`), and the LZ4-compressed payload (`[UInt8]`). The maximum data length is specific to the channel protocol.
    sendCompressed          = 0x37,

    /// *Maintenance Commands*

    /// PREPARE FOR UPDATE ­– Begin firmware update.
    prepareForUpdate        = 0x40,
    /// SEND UPDATE DATA ­– Send update data.
    sendUpdateData          = 0x41,
    /// COMMIT UPDATE ­– Install new data and reset.
    commitUpdate            = 0x42,
    /// REBOOT ­– Reset.
    reset                   = 0x43,

    /// *RPC commands*

    /// RPC CALL ­– Call a "method". MUST include the stringified JSON payload.
    rpcCall                 = 0x50,
    /// RPC SEND BINARY – Request the sending of a binary payload. MUST include the filename.
    rpcSendBinary           = 0x51,

    /// *Positive Replies*

    /// Generic OK.
    ok                      = 0x80,
    /// Response to PING. MAY include payload.
    pong                    = 0x90,
    /// Info ­response ­– MUST include the following UTF8-strings separated by `\n` (`0x0A`):
    /// 1. Vendor name
    /// 2. Model name
    /// 3. Chipset + Hardware revision
    /// 4. Serial number
    /// 5. Firmware version
    info                    = 0x91,
    /// Battery Voltage. MUST include the battery voltage in millivolts (`UInt16`).
    voltage                 = 0x92,

    /// Channel successfully opened ­– MUST include the (new) logical channel number (`UInt8`).
    channelOpened           = 0xB0,
    /// Channel successfully closed ­– MUST include the logical channel number (`UInt8`).
    channelClosed           = 0xB1,
    /// Data for channel received ­– MUST include the logical channel number (`UInt8`) and the received data.
    received                = 0xB2,
    /// Data for channel received ­– MUST include the logical channel number (`UInt8`), the length of the uncompressed data (`UInt16`) and the received data compressed via LZ4.
    receivedCompressed      = 0xB3,

    /// Periodic message started to send ­– MUST include the (new) handle for the periodic message.
    periodicMessageStarted  = 0xB5,
    /// Periodic message ended ­– MUST include the handle for the stopped periodic message.
    periodicMessageEnded    = 0xB6,
    /// Update prepared.
    updateStartedSendData   = 0xC0,
    /// Update data received.
    updateDataReceived      = 0xC1,
    /// Update completed.
    updateCompleted         = 0xC2,

    /// RPC call response ­– MUST include the stringified JSON payload.
    rpcResponse             = 0xD0,
    /// RPC binary response ­– MUST include the binary payload.
    rpcBinaryResponse       = 0xD1,

    /// *Negative Replies*

    /// An unspecified error has occured, e.g., a protocol violation.
    errorUnspecified        = 0xE0,
    /// A hardware error has occured, e.g., a bitrate could not be set.
    errorHardware           = 0xE1,
    /// Invalid channel selected.
    errorInvalidChannel     = 0xE2,
    /// Invalid periodic message handle.
    errorInvalidPeriodic    = 0xE3,
    /// No response received.
    errorNoResponse         = 0xE4,
    /// Invalid RPC call sent.
    errorInvalidRPC         = 0xE5,
    /// Invalid command sent.
    errorInvalidCommand     = 0xEF,
};

/// Encapsulates a PDU on the wire.
class PDU {

    static const size_t HEADER_SIZE = 4;
    PDUType _type;
    uint16_t _length;
    std::vector<uint8_t> _payload;
    static const uint8_t ATT = 0x1F;

public:

    PDU(const PDUType type): _type(type), _length(0) {};
    PDU(const PDUType type, const std::vector<uint8_t>& payload): _type(type), _length(static_cast<uint16_t>(payload.size())), _payload(payload) {
        //printf("Creating packet with type %02X and payload length %d\n", uint8_t(_type), _payload.size());
    };
    PDU(const Bytes& frame);
    const Bytes frame() const;

    /// Returns the PDU type.
    PDUType type() const { return _type; }
    /// Returns the information value of this PDU, iff the PDU is `information`.
    const Info information() const;
    /// Returns the arbitration value of this PDU, iff the PDU is `setArbitration`.
    const Arbitration arbitration() const;
    /// Returns the channel handle value of this PDU, iff the PDU contains one.
    ChannelHandle channel() const;
    /// Returns the periodic message value of this PDU, iff the PDU contains one.
    PeriodicMessageHandle periodicMessage() const;
    /// Returns the channel protocol value of this PDU, iff the PDU is `openChannel`.
    ChannelProtocol protocol() const;
    /// Returns the channel bitrate value of this PDU, iff the PDU is `openChannel`.
    uint32_t bitrate() const;
    /// Returns the separation times (in µs) for TX and RX, iff the PDU is `openChannel`.
    std::pair<Microseconds, Microseconds> separationTimes() const;
    /// Returns the interval value of this PDU, iff the PDU is `startPeriodicMessage`.
    uint16_t milliseconds() const;
    /// Returns the hardware data value of this PDU, iff the PDU is `send` or `received`.
    Bytes data() const;
    /// Returns the hardware data value of this PDU, iff the PDU is `sendCompressed` or `receivedCompressed`.
    Bytes uncompressedData() const;
    /// Returns the uncompressed length value of this PDU, iff the PDU is `sendCompressed` or `receivedCompressed`.
    uint16_t uncompressedLength() const;
    /// Returns the complete payload of this PDU, i.e. the complete PDU minus the fixed header.
    const Bytes& payload() const;
    /// Returns the filename of the PDU, iff the PDU is `rpcSendBinary`.
    std::string filename() const;

    /// Returns a negative value, if we need to more data to form a valid PDU.
    /// Returns the number of consumed data, if there is enough data to create the PDU.
    static int containsPDU(const Bytes& bytes);

    //
    // Tester -> Adapter
    //

    /// Creates a `ping` PDU.
    static PDU ping(const Bytes payload = {});
    /// Creates a `requestInfo` PDU.
    static PDU requestInfo();
    /// Creates a `readVoltage` PDU.
    static PDU readVoltage();
    /// Creates an `openChannel` PDU.
    static PDU openChannel(const ChannelProtocol protocol, const uint32_t bitrate, const SeparationTimeCode rxSeparationTime, const SeparationTimeCode txSeparationTime);
    /// Creates a `closeChannel` PDU.
    static PDU closeChannel(const ChannelHandle handle);
    /// Creates a `send` PDU.
    static PDU send(const ChannelHandle handle, const Bytes& data);
    /// Creates a `sendCompressed` PDU. The payload will be compressed.
    static PDU sendCompressed(const ChannelHandle handle, const Bytes& uncompressedData);
    /// Creates a `setArbitration` PDU.
    static PDU setArbitration(const ChannelHandle handle, const Arbitration arbitration);
    /// Creates a `startPeriodicMessage` PDU.
    static PDU startPeriodicMessage(const uint8_t timeout, const Arbitration arbitration, const Bytes& data);
    /// Creates a `endPeriodicMessage` PDU.
    static PDU endPeriodicMessage(const PeriodicMessageHandle handle);
    /// Creates a `prepareForUpdate` PDU.
    static PDU prepareForUpdate();
    /// Creates a `sendUpdateData` PDU.
    static PDU sendUpdateData(const Bytes& data);
    /// Creates a `commitUpdate` PDU.
    static PDU commitUpdate();
    /// Creates a `reset` PDU.
    static PDU reset();
    /// Creates a `rpcCall` PDU.
    static PDU rpcCall(const std::string& string);
    /// Creates a `rpcSendBinary` PDU.
    static PDU rpcSendBinary(const std::string& filename);

    //
    // Adapter -> Tester
    //

    /// Creates an `OK` PDU.
    static PDU ok();
    /// Creates a `pong` PDU.
    static PDU pong(const Bytes payload = {});
    /// Creates an `info` PDU.
    static PDU info(const std::string vendor, const std::string model, const std::string hardware, const std::string serial, const std::string firmware);
    /// Creates a `voltage` PDU.
    static PDU voltage(const uint16_t millivolts);
    /// Creates a `channelOpened` PDU.
    static PDU channelOpened(const ChannelHandle handle);
    /// Creates a `channelClosed` PDU.
    static PDU channelClosed(const ChannelHandle handle);
    /// Creates a `received` PDU.
    static PDU received(const ChannelHandle handle, const uint32_t id, const uint8_t extension, const Bytes& data);
    /// Creates a `receivedCompressed` PDU. Payload will be compressed using LZ4.
    static PDU receivedCompressed(const ChannelHandle handle, const uint32_t id, const uint8_t extension, const Bytes& uncompressedData);
    /// Creates a `periodicMessageStarted` PDU.
    static PDU periodicMessageStarted(const PeriodicMessageHandle handle);
    /// Creates a `periodicMessageEnded` PDU.
    static PDU periodicMessageEnded(const PeriodicMessageHandle handle);
    /// Creates an `updateStartedSendData` PDU.
    static PDU updateStartedSendData();
    /// Creates an `updateDataReceived` PDU.
    static PDU updateDataReceived();
    /// Creates an `updateCompleted` PDU.
    static PDU updateCompleted();
    /// Creates a `rpcResponse` PDU.
    static PDU rpcResponse(const std::string& string);
    /// Creates a `rpcBinaryResponse` PDU.
    static PDU rpcBinaryResponse(const Bytes& binary);

    /// Creates an `errorUnspecified` PDU.
    static PDU errorUnspecified();
    /// Creates an `errorHardware` PDU.
    static PDU errorHardware();
    /// Creates an `errorInvalidChannel` PDU.
    static PDU errorInvalidChannel();
    /// Creates an `errorInvalidPeriodic` PDU.
    static PDU errorInvalidPeriodic();
    /// Creates an `errorNoResponse` PDU.
    static PDU errorNoResponse();
    /// Creates an `errorInvalidRPC` PDU.
    static PDU errorInvalidRPC();
    /// Creates an `errorInvalidCommand` PDU.
    static PDU errorInvalidCommand();

    //
    // Helpers
    //
    static SeparationTimeCode separationTimeCodeFromMicroseconds(const Microseconds microseconds);
    static Microseconds microsecondsFromSeparationTimeCode(const SeparationTimeCode separationTimeCode);
};

};

#endif
