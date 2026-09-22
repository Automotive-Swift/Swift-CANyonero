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
    private var historyPath: String?
    private var lastAddressing: Automotive.Addressing?
    private var payloadProtocol: Automotive.PayloadProtocol
    private let annotator: MessageAnnotator
    var annotationsEnabled: Bool

    static let allowedCharacterSet = CharacterSet(charactersIn: "0123456789ABCDEFabcdefxX:,/").inverted

    init(
        defaultAddressing: Automotive.Addressing? = nil,
        channelProtocol: ECUconnect.ChannelProtocol,
        payloadProtocol overridePayloadProtocol: Automotive.PayloadProtocol? = nil,
        annotationsEnabled: Bool = true
    ) {
        self.lastAddressing = defaultAddressing
        self.annotationsEnabled = annotationsEnabled
        // Raw channels hand us single CAN frames, so any PDU there is still
        // wrapped in ISO-TP framing and has to be unwrapped before decoding.
        let isRaw = channelProtocol == .raw || channelProtocol == .rawFD
        self.annotator = .init(framing: isRaw ? .rawFrames : .pdu)
        if let overridePayloadProtocol {
            self.payloadProtocol = overridePayloadProtocol
        } else {
            switch channelProtocol {
                case .isotp:
                    self.payloadProtocol = .uds
                case .isotpFD:
                    self.payloadProtocol = .uds
                case .kline:
                    self.payloadProtocol = .kwp
                default:
                    self.payloadProtocol = .raw
            }
        }
        if let addressing = defaultAddressing {
            print("Default addressing: \(Self.describe(addressing))")
        }
        self.historyPath = InteractiveHistory.configure(lineNoise, scope: "term")
    }

    private func addHistory(_ line: String) {
        lineNoise.addHistory(line)
        InteractiveHistory.persist(lineNoise, historyPath: historyPath)
    }

    func read() throws -> Command {
        var command: Command? = nil

        repeat {
            var input = ""
            do {
                let prompt = lastAddressing != nil ? "> " : "> (set addressing first with :7df or :7df,7e8) "
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

                if trimmed.hasPrefix(":annotate") {
                    command = parseAnnotateCommand(String(trimmed.dropFirst(":annotate".count)))
                    self.addHistory(trimmed)
                    print("")
                } else if trimmed.hasPrefix(":filter") {
                    command = parseFilterCommand(String(trimmed.dropFirst("filter".count + 1)))
                    self.addHistory(trimmed)
                    print("")
                } else if trimmed.hasPrefix(":") {
                    guard trimmed.dropFirst().rangeOfCharacter(from: Self.allowedCharacterSet) == nil else {
                        print("SyntaxError: Invalid addressing format. Use :7df or :7df,7e8 (or :18DA33F1/10,18DAF110/20 for extended addressing)")
                        continue
                    }
                    if let addressing = parseAddressing(String(trimmed.dropFirst())) {
                        command = .setAddressing(addressing)
                        self.addHistory(trimmed)
                        print("")
                    } else {
                        print("SyntaxError: Invalid addressing format. Use :7df or :7df,7e8 (or :18DA33F1/10,18DAF110/20 for extended addressing)")
                    }
                } else {
                    guard trimmed.rangeOfCharacter(from: Self.allowedCharacterSet) == nil else {
                        print("SyntaxError: Invalid characters (allowed: 0-9, A-F, a-f, :, ,, /)")
                        continue
                    }
                    guard let addressing = lastAddressing else {
                        print("Error: Set addressing first with :7df,7e8")
                        continue
                    }

                    if let message = parseMessage(trimmed, addressing: addressing) {
                        command = .sendMessage(message)
                        self.addHistory(trimmed)
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

        guard self.annotationsEnabled else { return }
        self.annotator.annotate(response: message)?.print()
    }

    func writeRequest(_ message: Automotive.Message) {
        guard self.annotationsEnabled else { return }
        self.annotator.annotate(request: message.bytes)?.print()
    }

    func parseAnnotateCommand(_ input: String) -> Command? {
        switch input.CC_trimmed().lowercased() {
            case "", "on":
                return .setAnnotations(true)
            case "off":
                return .setAnnotations(false)
            default:
                print("Usage: :annotate      – enable protocol annotations")
                print("       :annotate off  – disable them")
                return nil
        }
    }

    func parseFilterCommand(_ input: String) -> Command? {
        let arg = input.CC_trimmed()
        if arg.isEmpty || arg.lowercased() == "auto" {
            return .setFilter(.auto)
        }
        if arg.lowercased() == "off" {
            return .setFilter(.off)
        }
        let hexComponents = arg.components(separatedBy: ",").compactMap { UInt8($0.CC_trimmed(), radix: 16) }
        guard !hexComponents.isEmpty else {
            print("Usage: :filter          – auto-derive from request SID")
            print("       :filter 62       – accept only 0x62 as valid positive response")
            print("       :filter 62,6A    – accept 0x62 or 0x6A")
            print("       :filter off      – disable filtering")
            return nil
        }
        return .setFilter(.explicit(hexComponents))
    }

    func parseAddressing(_ input: String) -> Automotive.Addressing? {
        let components = input.components(separatedBy: ",")
        guard components.count == 1 || components.count == 2 else { return nil }

        guard let send = parseAddressComponent(components[0]) else { return nil }

        let replyComponent: String
        if components.count == 1 || components[1].CC_trimmed().isEmpty {
            replyComponent = Self.defaultReplyWildcard(for: send.id)
        } else {
            replyComponent = components[1]
        }

        // Check for wildcards in reply ID to determine if this is a multicast/broadcast
        let replyString = replyComponent.CC_trimmed()
        let replyParts = replyString.split(separator: "/")
        
        if replyParts.count > 0 && (replyParts[0].contains("x") || replyParts[0].contains("X")) {
            // Multicast/Wildcard parsing
            let idString = String(replyParts[0])
            let patternString = idString.replacingOccurrences(of: "x", with: "0").replacingOccurrences(of: "X", with: "0")
            let maskString = idString.map { ($0 == "x" || $0 == "X") ? "0" : "F" }.joined()
            
            guard let pattern = Self.parseHex(patternString, as: Automotive.Header.self),
                  var mask = Self.parseHex(maskString, as: Automotive.Header.self) else {
                return nil
            }
            if pattern <= 0x7FF {
                mask &= 0x7FF
            }
            
            var ext: Automotive.HeaderExtension = 0
            if replyParts.count == 2 {
                 // Note: Wildcards not supported in extension yet, strict parsing
                guard let parsedExt = Self.parseHex(String(replyParts[1]), as: Automotive.HeaderExtension.self) else { return nil }
                ext = parsedExt
            } else {
                 // Implicit extension '0' is usually what we want if omitted, 
                 // but if we are doing 6xx/F1 broadcast, the user MUST supply /F1.
            }
            
            // Use reply extension when provided; wildcards are still not supported there.
            let addressing = Automotive.Addressing.multicast(id: send.id, ea: send.ext, pattern: pattern, mask: mask, rea: ext)
            lastAddressing = addressing
            return addressing

        } else {
            // Standard Unicast
            guard let reply = parseAddressComponent(replyComponent) else { return nil }
            let addressing = Automotive.Addressing.unicast(id: send.id, ea: send.ext, reply: reply.id, rea: reply.ext)
            lastAddressing = addressing
            return addressing
        }
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

    private static func defaultReplyWildcard(for requestId: Automotive.Header) -> String {
        let width = requestId > 0x7FF ? 8 : 3
        return String(repeating: "x", count: width)
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

    private static func formattedMask(_ mask: Automotive.Header) -> String {
        let value = UInt32(mask)
        let hex = value <= 0x7FF ? String(format: "%03X", value) : String(format: "%08X", value)
        return "0x\(hex)"
    }

    static func describe(_ addressing: Automotive.Addressing) -> String {
        switch addressing {
            case let .unicast(id, ea, reply, rea):
                return "unicast \(formatted(header: id, ext: ea)) -> \(formatted(header: reply, ext: rea))"
            case let .multicast(id, ea, pattern, mask, rea):
                return "multicast \(formatted(header: id, ext: ea)) -> \(formatted(header: pattern, ext: rea)) mask=\(formattedMask(mask))"
            case let .broadcast(id, ea, reply, rea):
                return "broadcast \(formatted(header: id, ext: ea)) -> \(formatted(header: reply, ext: rea))"
            case let .oneshot(id, ea, reply, rea):
                return "oneshot \(formatted(header: id, ext: ea)) -> \(formatted(header: reply, ext: rea))"
            default:
                return "\(addressing.id, radix: .hex) -> \(addressing.reply, radix: .hex)"
        }
    }

    enum FilterMode: CustomStringConvertible {
        case off
        case auto
        case explicit([UInt8])

        var description: String {
            switch self {
                case .off: return "off"
                case .auto: return "auto (SID + 0x40)"
                case .explicit(let validResponseFirstBytes):
                    let bytes = validResponseFirstBytes.map { String(format: "%02X", $0) }.joined(separator: ", ")
                    return "[\(bytes)]"
            }
        }

        func resolve(for message: Automotive.Message) -> Automotive.ResponseClassifier? {
            switch self {
                case .off: return nil
                case .auto:
                    return message.payloadProtocol.defaultResponseClassifier(for: message.bytes, mode: .full)
                case .explicit(let validResponseFirstBytes):
                    let uniqueBytes = validResponseFirstBytes.reduce(into: [UInt8]()) { bytes, value in
                        if !bytes.contains(value) {
                            bytes.append(value)
                        }
                    }
                    return Automotive.ResponseClassifier { responseBytes, requestBytes in
                        guard let first = responseBytes.first else { return .ignore }

                        if first == UDS.NegativeResponse {
                            guard responseBytes.count >= 3 else { return .ignore }
                            if let requestSid = requestBytes.first, responseBytes[1] != requestSid {
                                return .ignore
                            }
                            if responseBytes[2] == UDS.NegativeResponseCode.requestCorrectlyReceivedResponsePending.rawValue {
                                return .pending
                            }
                            return .final
                        }

                        return uniqueBytes.contains(first) ? .final : .ignore
                    }
            }
        }
    }

    var filterMode: FilterMode = .off

    enum Command {
        case setAddressing(Automotive.Addressing)
        case setFilter(FilterMode)
        case setAnnotations(Bool)
        case sendMessage(Automotive.Message)
    }
}

struct Term: ParsableCommand {

    static var _commandName: String = "term"
    static var configuration = CommandConfiguration(abstract: "Interactive CAN terminal.")

    @OptionGroup() var parentOptions: ECUconnectCommand.Options

    @Argument(help: "Bitrate")
    var bitrate: Int = 500000

    @Option(name: .shortAndLong, help: "Channel protocol (raw, isotp, kline, raw_fd, isotp_fd, or tp20)")
    var proto: String = "raw"

    @Option(name: .long, help: "CAN-FD data bitrate (used for raw_fd/isotp_fd)")
    var dataBitrate: Int = 2_000_000

    @Option(name: .long, help: "TP2.0 target address (hex or decimal), e.g. 0x01. Defaults to 0x01 for term unless --no-tp20-setup is used.")
    var tp20Target: String?

    @Option(name: .long, help: "TP2.0 tester active reply CAN ID used during setup (hex or decimal)")
    var tp20ReplyId: String = "0x300"

    @Option(name: .long, help: "TP2.0 application type byte (hex or decimal)")
    var tp20ApplicationType: String = "0x01"

    @Option(name: .long, help: "TP2.0 setup timeout in milliseconds")
    var tp20SetupTimeoutMs: Int = 750

    @Option(name: .long, help: "TP2.0 setup retries")
    var tp20SetupRetries: Int = 5

    @Flag(name: .long, help: "Skip TP2.0 fixed-ID setup and open only the dynamic TP2.0 channel.")
    var noTP20Setup: Bool = false

    @Flag(name: .long, help: "Do not annotate requests and responses.")
    var noAnnotate: Bool = false

    private static func parseUnsignedInteger(_ rawValue: String, optionName: String, max: UInt64) throws -> UInt64 {
        let trimmed = rawValue.trimmingCharacters(in: .whitespacesAndNewlines)
        let value: UInt64?
        if trimmed.lowercased().hasPrefix("0x") {
            value = UInt64(trimmed.dropFirst(2), radix: 16)
        } else {
            value = UInt64(trimmed, radix: 10) ?? UInt64(trimmed, radix: 16)
        }
        guard let parsed = value, parsed <= max else {
            throw ValidationError("Invalid \(optionName) '\(rawValue)'.")
        }
        return parsed
    }

    mutating func run() throws {

        let url = try parseECUconnectURL(parentOptions.url)
        let bps = bitrate

        let channelProto: ECUconnect.ChannelProtocol
        switch proto.lowercased() {
            case "raw":
                channelProto = .raw
            case "isotp":
                channelProto = .isotp
            case "kline":
                channelProto = .kline
            case "raw_fd":
                channelProto = .rawFD
            case "isotp_fd":
                channelProto = .isotpFD
            case "tp20":
                channelProto = .tp20
            default:
                throw ValidationError("Invalid protocol '\(proto)'. Use 'raw', 'isotp', 'kline', 'raw_fd', 'isotp_fd', or 'tp20'.")
        }
        let selectedDataBitrate: Int? = (channelProto == .rawFD || channelProto == .isotpFD) ? dataBitrate : nil

        if channelProto != .tp20, tp20Target != nil {
            throw ValidationError("--tp20-target requires --proto tp20.")
        }
        if channelProto != .tp20, noTP20Setup {
            throw ValidationError("--no-tp20-setup requires --proto tp20.")
        }
        if tp20SetupTimeoutMs <= 0 {
            throw ValidationError("--tp20-setup-timeout-ms must be greater than 0.")
        }
        if tp20SetupRetries <= 0 {
            throw ValidationError("--tp20-setup-retries must be greater than 0.")
        }

        let tp20OpenParameters: (target: UInt8, replyId: Automotive.Header, applicationType: TP20.ApplicationType)?
        if channelProto == .tp20, !noTP20Setup {
            let targetValue = tp20Target ?? "0x01"
            let target = try UInt8(Self.parseUnsignedInteger(targetValue, optionName: "TP2.0 target address", max: 0xFF))
            let replyId = Automotive.Header(try Self.parseUnsignedInteger(tp20ReplyId, optionName: "TP2.0 tester active reply CAN ID", max: 0x1FFFFFFF))
            let applicationType = TP20.ApplicationType(rawValue: try UInt8(Self.parseUnsignedInteger(tp20ApplicationType, optionName: "TP2.0 application type", max: 0xFF)))
            tp20OpenParameters = (target: target, replyId: replyId, applicationType: applicationType)
        } else {
            tp20OpenParameters = nil
        }

        var defaultAddressing: Automotive.Addressing?
        switch channelProto {
            case .kline:
                defaultAddressing = .broadcast(id: 0x33, reply: 0xF1)
            case .tp20:
                defaultAddressing = nil
            default:
                defaultAddressing = .unicast(id: 0x7DF, reply: 0x7E8)
        }

        let tp20SetupTimeoutMs = self.tp20SetupTimeoutMs
        let tp20SetupRetries = self.tp20SetupRetries
        let annotationsDisabled = self.noAnnotate

        Task {
            do {
                let delegate = Delegate()
                let (adapter, info, voltage) = try await Cornucopia.Core.Spinner.run("Connecting to adapter") {
                    guard let adapter = try await Automotive.BaseAdapter.create(for: url, delegate: delegate) as? ECUconnect.Adapter else { throw ValidationError("Not an ECUconnect adapter") }
                    let info = try await adapter.identify()
                    let voltage = try await adapter.readSystemVoltage()
                    return (adapter, info, voltage)
                }
                print("Connected to ECUconnect: \(info).")
                print("Reported system voltage is \(voltage)V.")

                var negotiatedTP20Channel: TP20.NegotiatedChannel?
                try await Cornucopia.Core.Spinner.run("Opening channel") {
                    if let tp20OpenParameters {
                        negotiatedTP20Channel = try await adapter.openTP20Channel(
                            targetAddress: tp20OpenParameters.target,
                            bitrate: bps,
                            applicationType: tp20OpenParameters.applicationType,
                            replyId: tp20OpenParameters.replyId,
                            setupTimeout: .milliseconds(tp20SetupTimeoutMs),
                            setupRetries: tp20SetupRetries
                        )
                    } else {
                        try await adapter.openChannel(proto: channelProto, bitrate: bps, dataBitrate: selectedDataBitrate ?? 0)
                    }
                }
                let channelDescription: String
                switch channelProto {
                    case .raw: channelDescription = "Raw"
                    case .isotp: channelDescription = "ISOTP"
                    case .kline: channelDescription = "KLine"
                    case .rawFD: channelDescription = "Raw CAN-FD"
                    case .isotpFD: channelDescription = "ISOTP-FD"
                    case .tp20: channelDescription = "VW TP2.0"
                    default: channelDescription = "\(channelProto)"
                }
                if let selectedDataBitrate {
                    print("\(channelDescription) channel opened at \(bps)/\(selectedDataBitrate) bps.")
                } else {
                    print("\(channelDescription) channel opened at \(bps) bps.")
                }
                if let negotiatedTP20Channel {
                    defaultAddressing = .unicast(id: negotiatedTP20Channel.dynamicRequestId, reply: negotiatedTP20Channel.dynamicReplyId)
                    print(
                        "TP2.0 setup complete for target 0x\(String(format: "%02X", negotiatedTP20Channel.targetAddress)): " +
                        "TX=0x\(String(negotiatedTP20Channel.dynamicRequestId, radix: 16, uppercase: true)) " +
                        "RX=0x\(String(negotiatedTP20Channel.dynamicReplyId, radix: 16, uppercase: true))"
                    )
                }
                print("Commands:")
                print("  :REQ,REPLY     - Set addressing (e.g. :7df,7e8 or :33,F1)")
                print("  :6F1/12,612/F1 - Include CAN extended addressing bytes (EA/REA)")
                print("  :filter        - Enable auto response filtering (SID + 0x40)")
                print("  :filter 62,6A  - Filter for specific response bytes")
                print("  :filter off    - Disable response filtering")
                print("  :annotate off  - Turn protocol annotations off (:annotate turns them back on)")
                print("  0902           - Send hex data with current addressing")
                print("  quit           - Exit")
                if channelProto == .tp20, negotiatedTP20Channel == nil {
                    print("  Note: TP2.0 requires negotiated dynamic CAN IDs. Set them manually, e.g. :740,300")
                }
                if channelProto == .raw || channelProto == .rawFD {
                    print("  Note: on raw channels, annotations decode the ISO-TP framing of each CAN frame.")
                }
                print("")

                let replPayloadProtocol: Automotive.PayloadProtocol?
                if channelProto == .tp20, tp20OpenParameters?.applicationType == .diagnostics {
                    replPayloadProtocol = .kwp
                } else {
                    replPayloadProtocol = nil
                }
                let repl = REPL(
                    defaultAddressing: defaultAddressing,
                    channelProtocol: channelProto,
                    payloadProtocol: replPayloadProtocol,
                    annotationsEnabled: !annotationsDisabled
                )

                while true {
                    let command = try repl.read()

                    switch command {
                        case .setAddressing(let addressing):
                            print("Addressing set to: \(REPL.describe(addressing))")

                        case .setFilter(let mode):
                            repl.filterMode = mode
                            print("Response filter: \(mode)")

                        case .setAnnotations(let enabled):
                            repl.annotationsEnabled = enabled
                            print("Annotations: \(enabled ? "on" : "off")")

                        case .sendMessage(let message):
                            do {
                                repl.writeRequest(message)
                                let responseClassifier = repl.filterMode.resolve(for: message)
                                switch message.addressing {
                                    case .oneshot(_, _, _, _):
                                        try await adapter.sendMessageReceiveNothing(message)
                                    case .multicast(_, _, _, _, _), .broadcast(_, _, _, _):
                                        let responses = try await adapter.sendMessageReceiveMultiple(
                                            message,
                                            expectedResponseCount: nil,
                                            responseClassifier: responseClassifier
                                        )
                                        for response in responses {
                                            repl.write(response)
                                        }
                                    default:
                                        let response = try await adapter.sendMessageReceiveSingle(
                                            message,
                                            responseClassifier: responseClassifier
                                        )
                                        repl.write(response)
                                }
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
