//
//  Swift-CANyonero – (C) Dr. Lauer Information Technology
//
import CornucopiaCore
import Swift_CAN

public enum CANyonero {

    public typealias Channel = UInt8
    public typealias PeriodicMessage = UInt8
    public static let HeaderLength = 4
    public static let ATT: UInt8 = 0x1A

    /// Constants for the protocol 'on-the-wire'.
    private enum Wire {
        enum Command: UInt8 {
            case ping
            case bitrate
            case open
            case send
            case close
            case version
            case beginPeriodicMessage
            case endPeriodicMessage
            case arbitration
            case filter
        }
        enum Reply: UInt8 {
            case ping = 0xF0
            case bitrate
            case open
            case send
            case close
            case version
            case beginPeriodicMessage
            case endPeriodicMessage
            case arbitration
            case filter
        }
        enum Error: UInt8 {
            case configuration = 0xE0
            case invalidChannel
            case timeout
            case invalidCommand = 0xEF
        }
    }

    /// Available channel protocols.
    public enum ChannelProtocol: UInt8 {
        case passthrough
        case isotp
    }

    public enum Error: Swift.Error {
        case protocolViolation
        case timeout
    }

    /// Available replies.
    public enum Reply {
        case ok
        case pong
        case version(serialNumber: String, hardware: String, software: String)
        case channel(_ channel: Channel)
        case periodicMessage(_ message: PeriodicMessage)
        case received(data: [UInt8])
        case protocolViolation(details: String)

        public init(bytes: [UInt8]) {
            guard bytes.count >= HeaderLength else {
                self = .protocolViolation(details: "Message w/ \(bytes.count) bytes too short. Needs to be at least \(HeaderLength) bytes.")
                return
            }
            if bytes[0] != ATT {
                self = .protocolViolation(details: "Message preambel not ATT \(ATT, radix: .hex, prefix: true, toWidth: 2), received \(bytes[0], radix: .hex, prefix: true, toWidth: 2) instead.")
                return
            }
            guard let reply = Wire.Reply(rawValue: bytes[1]) else {
                self = .protocolViolation(details: "Raw value \(bytes[1], radix: .hex, toWidth: 2) is not a valid CANyonero protocol reply.")
                return
            }
            let plen = UInt16.CC_fromBytes(bytes[2...3])
            guard bytes.count == plen + UInt16(HeaderLength) else {
                self = .protocolViolation(details: "Message length \(bytes.count) not matching payload spec. Expected \(plen + UInt16(HeaderLength)) bytes.")
                return
            }
            switch reply {

                case .ping:
                    self = .pong
                default:
                    fatalError("not yet implemented")
            }
        }
    }

    /// Available commands.
    public enum Command {

        case ping
        case version
        case setBitrate(_ bitrate: UInt32)
        case setArbitration(_ channel: Channel, request: CAN.ArbitrationId, reply: CAN.ArbitrationId)
        case setFilter(_ channel: Channel, request: CAN.ArbitrationId, pattern: CAN.ArbitrationId, mask: CAN.ArbitrationId)
        case openChannel(proto: ChannelProtocol, request: CAN.ArbitrationId, reply: CAN.ArbitrationId)
        case closeChannel(_ channel: Channel)
        case send(onChannel: Channel, data: [UInt8])
        case beginPeriodicMessage(onChannel: Channel, data: [UInt8])
        case endPeriodicMessage(onChannel: Channel)

        public var command: UInt8 {
            switch self {
                case .ping: return Wire.Command.ping.rawValue
                case .version: return Wire.Command.version.rawValue
                case .setBitrate(_): return Wire.Command.bitrate.rawValue
                case .setArbitration(_, _, _): return Wire.Command.arbitration.rawValue
                case .setFilter(_, _, _, _): return Wire.Command.filter.rawValue
                case .openChannel(_, _, _): return Wire.Command.open.rawValue
                case .closeChannel(_): return Wire.Command.close.rawValue
                case .send(_, _): return Wire.Command.send.rawValue
                case .beginPeriodicMessage(_, _): return Wire.Command.beginPeriodicMessage.rawValue
                case .endPeriodicMessage(_): return Wire.Command.endPeriodicMessage.rawValue
            }
        }

        public var payload: [UInt8] {
            switch self {
                case .ping: return []
                case .version: return []
                case .setBitrate(let bitrate): return bitrate.CC_UInt8array
                case .setArbitration(let channel, let request, let reply): return [channel] + request.CC_UInt8array + reply.CC_UInt8array
                case .setFilter(let channel, let request, let pattern, let mask): return [channel] + request.CC_UInt8array + pattern.CC_UInt8array + mask.CC_UInt8array
                case .openChannel(let proto, let request, let reply): return [proto.rawValue] + request.CC_UInt8array + reply.CC_UInt8array
                case .closeChannel(let channel): return [channel]
                case .send(let onChannel, let data): return [onChannel] + data
                case .beginPeriodicMessage(let onChannel, let data): return [onChannel] + data
                case .endPeriodicMessage(let onChannel): return [onChannel]
            }
        }

        public var frame: [UInt8] {
            let payload = self.payload
            return [CANyonero.ATT, self.command] + UInt16(payload.count).CC_UInt8array + payload
        }
    }
}
