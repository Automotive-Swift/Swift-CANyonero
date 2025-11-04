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

    static let allowedCharacterSet = CharacterSet(charactersIn: "0123456789ABCDEFabcdef:,").inverted

    init(defaultAddressing: Automotive.Addressing? = nil, channelProtocol: ECUconnect.ChannelProtocol) {
        self.lastAddressing = defaultAddressing
        self.payloadProtocol = channelProtocol == .isotp ? .uds : .raw
        if let addressing = defaultAddressing {
            print("Default addressing: \(addressing.id, radix: .hex) -> \(addressing.reply, radix: .hex)")
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
                    print("SyntaxError: Invalid characters (allowed: 0-9, A-F, a-f, :, ,)")
                    continue
                }

                if trimmed.hasPrefix(":") {
                    if let addressing = parseAddressing(String(trimmed.dropFirst())) {
                        command = .setAddressing(addressing)
                        self.lineNoise.addHistory(trimmed)
                        print("")
                    } else {
                        print("SyntaxError: Invalid addressing format. Use :7df,7e8 (send,reply)")
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
    }

    func parseAddressing(_ input: String) -> Automotive.Addressing? {
        let components = input.components(separatedBy: ",")
        guard components.count == 2 else { return nil }

        var sendId = components[0].CC_trimmed()
        var replyId = components[1].CC_trimmed()

        if sendId.count % 2 == 1 {
            sendId = "0" + sendId
        }
        if replyId.count % 2 == 1 {
            replyId = "0" + replyId
        }

        guard let send = UInt32(sendId, radix: 16),
              let reply = UInt32(replyId, radix: 16) else {
            return nil
        }

        let addressing = Automotive.Addressing.unicast(id: send, reply: reply)
        lastAddressing = addressing
        return addressing
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
                print("  :7df,7e8    - Set addressing (send to 7df, expect reply from 7e8)")
                print("  0902        - Send hex data with current addressing")
                print("  quit        - Exit")
                print("")

                let repl = REPL(defaultAddressing: .unicast(id: 0x7DF, reply: 0x7E8), channelProtocol: channelProto)

                while true {
                    let command = try repl.read()

                    switch command {
                    case .setAddressing(let addressing):
                        print("Addressing set to: \(addressing.id, radix: .hex) -> \(addressing.reply, radix: .hex)")

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
