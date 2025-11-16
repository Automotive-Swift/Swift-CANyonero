import ArgumentParser
import Chalk
import CornucopiaCore
import Foundation
import LineNoise
import Swift_Automotive_Client

fileprivate class Delegate: Automotive.AdapterDelegate {

    func adapter(_ adapter: Swift_Automotive_Core.Automotive.Adapter, didUpdateState state: Swift_Automotive_Core.Automotive.AdapterState) {
        if state == .gone {
            print("Disconnected from ECUconnect.")
            Foundation.exit(-1)
        }
    }
}

fileprivate class REPL {

    enum Error: Swift.Error {
        case eof
        case invalidSyntax(String)
    }

    private var lineNoise: LineNoise = .init()
    private var lastAddressing: Automotive.Addressing?
    private var payloadProtocol: Automotive.PayloadProtocol

    static let allowedCharacterSet = CharacterSet(charactersIn: "0123456789ABCDEFabcdef:,/").inverted

    init(defaultAddressing: Automotive.Addressing? = nil, channelProtocol: ECUconnect.ChannelProtocol) {
        self.lastAddressing = defaultAddressing
        self.payloadProtocol = channelProtocol == .isotp ? .uds : .raw
        if let addressing = defaultAddressing {
            print("Default addressing: \(Self.describe(addressing))")
        }
    }

    func read() throws -> Command {
        var command: Command? = nil

        repeat {
            var input = ""
            do {
                let prompt = lastAddressing != nil ? "> " : "> (set addressing first with :7df,7e8) "
                input = try self.lineNoise.getLine(prompt: prompt)
            } catch LinenoiseError.CTRL_C, LinenoiseError.EOF {
                print("^D")
                throw Error.eof
            }

            let trimmed = input.CC_trimmed()

            if !trimmed.isEmpty {
                guard !trimmed.hasPrefix("quit") else {
                    print("")
                    throw Error.eof
                }

                guard trimmed.rangeOfCharacter(from: Self.allowedCharacterSet) == nil else {
                    print("SyntaxError: Invalid characters (allowed: 0-9, A-F, a-f, :, ,, /)")
                    continue
                }

                if trimmed.hasPrefix(":") {
                    if let addressing = parseAddressing(String(trimmed.dropFirst())) {
                        command = .setAddressing(addressing)
                        self.lineNoise.addHistory(trimmed)
                        print("")
                    } else {
                        print("SyntaxError: Invalid addressing format. Use :7df,7e8 or :18DA33F1/10,18DAF110/20 (send[/ea],reply[/rea])")
                    }
                } else {
                    guard let addressing = lastAddressing else {
                        print("Error: Set addressing first with :7df,7e8")
                        continue
                    }

                    if let message = parseMessage(trimmed, addressing: addressing) {
                        command = .sendMessage(message)
                        self.lineNoise.addHistory(trimmed)
                        print("")
                    } else {
                        print("SyntaxError: Invalid message format. Use hex bytes like: 0902")
                    }
                }
            } else {
                print("")
            }
        } while command == nil

        return command!
    }

    func write(_ message: Automotive.Message) {
        let header = "\(message.addressing.id, radix: .hex, toWidth: 3)"
        let msg = message.bytes.map { String(format: "%02X ", $0) }.joined()
        let ascii = "\(message.bytes, printable: .ASCII128)"
        let firstLine = "\(header, color: .blue)   \(msg, color: .yellow)"
        let secondLine = " \("->", color: .red)  \(ascii, color: .green)"
        print(firstLine)
        print(secondLine)

        if let interpretation = interpretResponse(message.bytes) {
            print(" \("ℹ︎", color: .cyan)  \(interpretation)")
        }
    }

    func interpretResponse(_ bytes: [UInt8]) -> String? {
        guard !bytes.isEmpty else { return nil }

        let sid = bytes[0]

        // Negative response (0x7F)
        if sid == UDS.NegativeResponse {
            guard bytes.count >= 3 else { return "Negative Response (incomplete)" }
            let requestedService = bytes[1]
            let nrc = UDS.NegativeResponseCode(rawValue: bytes[2]) ?? .undefined
            let serviceName = serviceIdName(requestedService)
            let nrcDescription = negativeResponseCodeDescription(nrc)
            return "❌ Negative Response to \(serviceName): \(nrcDescription)"
        }

        // Positive OBD2 responses (0x41-0x4A = service 0x01-0x0A + 0x40)
        if sid >= 0x41 && sid <= 0x4A {
            let service = sid - 0x40
            let serviceName = obd2ServiceName(service)

            if service == 0x01 || service == 0x02 {
                // Mode 01 (current data) or Mode 02 (freeze frame)
                if bytes.count >= 2 {
                    let pid = bytes[1]
                    let pidName = obd2PidName(pid)
                    return "✓ \(serviceName) - \(pidName)"
                }
                return "✓ \(serviceName)"
            } else if service == 0x09 {
                // Mode 09 (vehicle information)
                if bytes.count >= 2 {
                    let infotype = bytes[1]
                    let infoName = obd2Mode09Name(infotype)
                    return "✓ \(serviceName) - \(infoName)"
                }
                return "✓ \(serviceName)"
            } else {
                return "✓ \(serviceName)"
            }
        }

        // Positive UDS responses (service ID + 0x40)
        if sid >= 0x50 {
            let requestedService = sid - 0x40
            let serviceName = serviceIdName(requestedService)
            return "✓ Positive Response to \(serviceName)"
        }

        return nil
    }

    func serviceIdName(_ sid: UInt8) -> String {
        switch sid {
            // OBD2 Services
            case 0x01: return "Show Current Data"
            case 0x02: return "Show Freeze Frame Data"
            case 0x03: return "Show Stored DTCs"
            case 0x04: return "Clear DTCs"
            case 0x05: return "O2 Sensor Monitoring"
            case 0x06: return "On-Board Monitoring"
            case 0x07: return "Show Pending DTCs"
            case 0x08: return "Control On-Board System"
            case 0x09: return "Request Vehicle Information"
            case 0x0A: return "Permanent DTCs"

            // UDS Services
            case 0x10: return "Diagnostic Session Control"
            case 0x11: return "ECU Reset"
            case 0x14: return "Clear Diagnostic Information"
            case 0x19: return "Read DTC Information"
            case 0x22: return "Read Data By Identifier"
            case 0x23: return "Read Memory By Address"
            case 0x24: return "Read Scaling Data By Identifier"
            case 0x27: return "Security Access"
            case 0x28: return "Communication Control"
            case 0x2E: return "Write Data By Identifier"
            case 0x2F: return "Input/Output Control By Identifier"
            case 0x31: return "Routine Control"
            case 0x34: return "Request Download"
            case 0x35: return "Request Upload"
            case 0x36: return "Transfer Data"
            case 0x37: return "Request Transfer Exit"
            case 0x3E: return "Tester Present"
            case 0x85: return "Control DTC Setting"

            default: return "Service 0x\(String(format: "%02X", sid))"
        }
    }

    func obd2ServiceName(_ service: UInt8) -> String {
        switch service {
            case 0x01: return "Mode 01: Current Data"
            case 0x02: return "Mode 02: Freeze Frame Data"
            case 0x03: return "Mode 03: Stored DTCs"
            case 0x04: return "Mode 04: Clear DTCs"
            case 0x09: return "Mode 09: Vehicle Information"
            default: return "Mode 0x\(String(format: "%02X", service))"
        }
    }

    func obd2PidName(_ pid: UInt8) -> String {
        switch pid {
            case 0x00: return "Supported PIDs [01-20]"
            case 0x01: return "Monitor Status"
            case 0x02: return "Freeze DTC"
            case 0x03: return "Fuel System Status"
            case 0x04: return "Calculated Engine Load"
            case 0x05: return "Engine Coolant Temperature"
            case 0x06: return "Short Term Fuel Trim Bank 1"
            case 0x0C: return "Engine RPM"
            case 0x0D: return "Vehicle Speed"
            case 0x0F: return "Intake Air Temperature"
            case 0x10: return "MAF Air Flow Rate"
            case 0x11: return "Throttle Position"
            case 0x1C: return "OBD Standards Compliance"
            case 0x1F: return "Engine Run Time"
            case 0x20: return "Supported PIDs [21-40]"
            case 0x21: return "Distance with MIL On"
            case 0x2F: return "Fuel Tank Level"
            case 0x33: return "Barometric Pressure"
            case 0x40: return "Supported PIDs [41-60]"
            case 0x42: return "Control Module Voltage"
            case 0x46: return "Ambient Air Temperature"
            case 0x51: return "Fuel Type"
            case 0x60: return "Supported PIDs [61-80]"
            default: return "PID 0x\(String(format: "%02X", pid))"
        }
    }

    func obd2Mode09Name(_ infotype: UInt8) -> String {
        switch infotype {
            case 0x00: return "Supported Info Types"
            case 0x01: return "VIN Message Count"
            case 0x02: return "Vehicle Identification Number (VIN)"
            case 0x03: return "Calibration ID Message Count"
            case 0x04: return "Calibration IDs"
            case 0x05: return "Calibration Verification Numbers Message Count"
            case 0x06: return "Calibration Verification Numbers (CVN)"
            case 0x08: return "In-use Performance Tracking Message Count"
            case 0x09: return "ECU Name Message Count"
            case 0x0A: return "ECU Name"
            default: return "Info Type 0x\(String(format: "%02X", infotype))"
        }
    }

    func negativeResponseCodeDescription(_ nrc: UDS.NegativeResponseCode) -> String {
        switch nrc {
            case .generalReject: return "General Reject"
            case .serviceNotSupported: return "Service Not Supported"
            case .subFunctionNotSupported: return "Sub-Function Not Supported"
            case .incorrectMessageLengthOrInvalidFormat: return "Incorrect Message Length or Invalid Format"
            case .responseTooLong: return "Response Too Long"
            case .busyRepeatRequest: return "Busy - Repeat Request"
            case .conditionsNotCorrect: return "Conditions Not Correct"
            case .requestSequenceError: return "Request Sequence Error"
            case .requestOutOfRange: return "Request Out of Range"
            case .securityAccessDenied: return "Security Access Denied"
            case .invalidKey: return "Invalid Key"
            case .exceedNumberOfAttempts: return "Exceeded Number of Attempts"
            case .requiredTimeDelayNotExpired: return "Required Time Delay Not Expired"
            case .requestCorrectlyReceivedResponsePending: return "Request Correctly Received - Response Pending"
            case .subFunctionNotSupportedInActiveSession: return "Sub-Function Not Supported in Active Session"
            case .serviceNotSupportedInActiveSession: return "Service Not Supported in Active Session"
            case .rpmTooHigh: return "RPM Too High"
            case .rpmTooLow: return "RPM Too Low"
            case .engineIsRunning: return "Engine Is Running"
            case .engineIsNotRunning: return "Engine Is Not Running"
            case .voltageTooHigh: return "Voltage Too High"
            case .voltageTooLow: return "Voltage Too Low"
            default: return "NRC 0x\(String(format: "%02X", nrc.rawValue))"
        }
    }

    func parseAddressing(_ input: String) -> Automotive.Addressing? {
        let components = input.components(separatedBy: ",")
        guard components.count == 2 else { return nil }

        guard let send = parseAddressComponent(components[0]),
              let reply = parseAddressComponent(components[1]) else {
            return nil
        }

        let addressing = Automotive.Addressing.unicast(id: send.id, ea: send.ext, reply: reply.id, rea: reply.ext)
        lastAddressing = addressing
        return addressing
    }

    private func parseAddressComponent(_ component: String) -> (id: Automotive.Header, ext: Automotive.HeaderExtension)? {
        let trimmed = component.CC_trimmed()
        guard !trimmed.isEmpty else { return nil }

        let parts = trimmed.split(separator: "/")
        guard parts.count <= 2 else { return nil }

        guard let id = Self.parseHex(String(parts[0]), as: Automotive.Header.self) else { return nil }
        var ext: Automotive.HeaderExtension = 0
        if parts.count == 2 {
            guard let parsedExt = Self.parseHex(String(parts[1]), as: Automotive.HeaderExtension.self) else { return nil }
            ext = parsedExt
        }
        return (id, ext)
    }

    private static func parseHex<T: FixedWidthInteger & UnsignedInteger>(_ text: String, as type: T.Type) -> T? {
        guard !text.isEmpty else { return nil }
        _ = type
        var normalized = text
        if normalized.count % 2 == 1 {
            normalized = "0" + normalized
        }
        return T(normalized, radix: 16)
    }

    func parseMessage(_ input: String, addressing: Automotive.Addressing) -> Automotive.Message? {
        var hexString = input

        if hexString.count % 2 == 1 {
            hexString = "0" + hexString
        }

        let bytes = hexString.CC_hexDecodedUInt8Array
        guard !bytes.isEmpty else { return nil }

        return Automotive.Message(addressing: addressing, payloadProtocol: self.payloadProtocol, bytes: bytes)
    }

    private static func formatted(header: Automotive.Header, ext: Automotive.HeaderExtension) -> String {
        let base = "\(header, radix: .hex)"
        guard ext != 0 else { return base }
        return "\(base)/\(String(format: "%02X", Int(ext)))"
    }

    static func describe(_ addressing: Automotive.Addressing) -> String {
        switch addressing {
            case let .unicast(id, ea, reply, rea):
                return "\(formatted(header: id, ext: ea)) -> \(formatted(header: reply, ext: rea))"
            case let .multicast(id, ea, pattern, _):
                return "\(formatted(header: id, ext: ea)) -> \(pattern, radix: .hex)"
            case let .broadcast(id, ea, reply, rea):
                return "\(formatted(header: id, ext: ea)) -> \(formatted(header: reply, ext: rea))"
            case let .oneshot(id, ea, reply, rea):
                return "\(formatted(header: id, ext: ea)) -> \(formatted(header: reply, ext: rea))"
            default:
                return "\(addressing.id, radix: .hex) -> \(addressing.reply, radix: .hex)"
        }
    }

    enum Command {
        case setAddressing(Automotive.Addressing)
        case sendMessage(Automotive.Message)
    }
}

struct Term: ParsableCommand {

    static var _commandName: String = "term"
    static var configuration = CommandConfiguration(abstract: "Interactive CAN terminal.")

    @OptionGroup() var parentOptions: ECUconnectCommand.Options

    @Argument(help: "Bitrate")
    var bitrate: Int = 500000

    @Option(name: .shortAndLong, help: "Channel protocol (passthrough or isotp)")
    var proto: String = "passthrough"

    mutating func run() throws {

        let url: URL = URL(string: parentOptions.url)!
        let bps = bitrate

        let channelProto: ECUconnect.ChannelProtocol
        switch proto.lowercased() {
            case "passthrough", "raw":
                channelProto = .passthrough
            case "isotp":
                channelProto = .isotp
            default:
                throw ValidationError("Invalid protocol '\(proto)'. Use 'passthrough' or 'isotp'.")
        }

        Task {
            do {
                let delegate = Delegate()
                guard let adapter = try await Automotive.BaseAdapter.create(for: url, delegate: delegate) as? ECUconnect.Adapter else { throw ValidationError("Not an ECUconnect adapter") }
                let info = try await adapter.identify()
                let voltage = try await adapter.readSystemVoltage()
                print("Connected to ECUconnect: \(info).")
                print("Reported system voltage is \(voltage)V.")

                try await adapter.openChannel(proto: channelProto, bitrate: bps)
                print("\(channelProto == .passthrough ? "Passthrough" : "ISOTP") channel opened at \(bps) bps.")
                print("Commands:")
                print("  :7df,7e8       - Set addressing (send to 7df, expect reply from 7e8)")
                print("  :6F1/12,612/F1 - Include CAN extended addressing bytes (EA/REA)")
                print("  0902           - Send hex data with current addressing")
                print("  quit           - Exit")
                print("")

                let repl = REPL(defaultAddressing: .unicast(id: 0x7DF, reply: 0x7E8), channelProtocol: channelProto)

                while true {
                    let command = try repl.read()

                    switch command {
                        case .setAddressing(let addressing):
                            print("Addressing set to: \(REPL.describe(addressing))")

                        case .sendMessage(let message):
                            do {
                                let response = try await adapter.sendMessageReceiveSingle(message)
                                repl.write(response)
                            } catch {
                                print("Error: \(error)")
                            }
                    }
                }

            } catch REPL.Error.eof {
                print("Goodbye!")
                Foundation.exit(0)
            } catch {
                print("Error: \(error)")
                Foundation.exit(-1)
            }
        }

        signal(SIGINT, SIG_IGN)
        let sigintSrc = DispatchSource.makeSignalSource(signal: SIGINT, queue: .main)
        sigintSrc.setEventHandler {
            Foundation.exit(0)
        }
        sigintSrc.resume()
        RunLoop.current.run()
    }
}

