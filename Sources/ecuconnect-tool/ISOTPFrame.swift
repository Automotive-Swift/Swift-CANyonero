import Foundation

/// The ISO 15765-2 protocol control information of a single CAN frame.
///
/// Only needed on raw CAN channels, where the adapter hands us individual
/// frames instead of a reassembled PDU. Extended (per-frame) addressing is not
/// recognized — with no channel configuration to tell us about it, guessing
/// would produce confident nonsense.
enum ISOTPFrame {

    enum FlowState: UInt8 {
        case clearToSend = 0
        case wait        = 1
        case overflow    = 2
    }

    case single(payload: [UInt8])
    case first(totalLength: Int, payload: [UInt8])
    case consecutive(sequence: UInt8, payload: [UInt8])
    case flowControl(state: FlowState, blockSize: UInt8, separationTime: UInt8)

    init?(frame bytes: [UInt8]) {

        guard let pci = bytes.first else { return nil }

        switch pci >> 4 {
            case 0x0:
                let length = Int(pci & 0x0F)
                guard (1...7).contains(length), bytes.count >= length + 1 else { return nil }
                self = .single(payload: Array(bytes.dropFirst().prefix(length)))

            case 0x1:
                guard bytes.count >= 3 else { return nil }
                let totalLength = Int(pci & 0x0F) << 8 | Int(bytes[1])
                self = .first(totalLength: totalLength, payload: Array(bytes.dropFirst(2)))

            case 0x2:
                guard bytes.count >= 2 else { return nil }
                self = .consecutive(sequence: pci & 0x0F, payload: Array(bytes.dropFirst()))

            case 0x3:
                guard bytes.count >= 3, let state = FlowState(rawValue: pci & 0x0F) else { return nil }
                self = .flowControl(state: state, blockSize: bytes[1], separationTime: bytes[2])

            default:
                return nil
        }
    }

    /// Human-readable separation time as encoded by ISO 15765-2.
    static func separationTimeDescription(_ value: UInt8) -> String {
        switch value {
            case 0x00...0x7F: "\(value) ms"
            case 0xF1...0xF9: "\(Int(value - 0xF0) * 100) µs"
            default:          "reserved (0x\(String(format: "%02X", value)))"
        }
    }
}
