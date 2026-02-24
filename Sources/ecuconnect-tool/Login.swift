import ArgumentParser
import Foundation
import LineNoise
import SocketSwift

fileprivate final class LoginRemote {

    enum Error: Swift.Error {
        case writeFailed
    }

    enum DrainResult {
        case open
        case closed
    }

    private let socket: Socket

    init(socket: Socket) {
        self.socket = socket
    }

    func drainOutput(firstByteTimeout: TimeInterval = 2.0, idleTimeout: TimeInterval = 0.20) throws -> DrainResult {
        var byte: [UInt8] = [0]
        var output: [UInt8] = []
        var waitTimeout = firstByteTimeout
        while try socket.wait(for: [.read, .hup, .err], timeout: waitTimeout) {
            let readCount = try socket.read(&byte, size: 1)
            if readCount == 0 {
                if !output.isEmpty {
                    FileHandle.standardOutput.write(Data(output))
                }
                return .closed
            }
            output.append(byte[0])
            waitTimeout = idleTimeout
        }
        if !output.isEmpty {
            FileHandle.standardOutput.write(Data(output))
        }
        return .open
    }

    func send(_ bytes: [UInt8]) throws {
        var pending = bytes
        while !pending.isEmpty {
            let written = try socket.write(&pending, size: pending.count)
            if written <= 0 {
                throw Error.writeFailed
            }
            pending.removeFirst(written)
        }
    }
}

fileprivate func defaultLoginHost(from endpoint: String) -> String {
    if endpoint.contains("://"), let url = URL(string: endpoint), let host = url.host {
        let scheme = (url.scheme ?? "").lowercased()
        if !host.isEmpty && (scheme.contains("tcp") || scheme == "ecuconnect" || scheme == "ecuconnect-wifi") {
            return host
        }
    }
    if !endpoint.contains("://"), let split = endpoint.lastIndex(of: ":"), split > endpoint.startIndex {
        let host = String(endpoint[..<split])
        if !host.isEmpty {
            return host
        }
    }
    return "192.168.42.42"
}

fileprivate func runLoginSession(host: String, port: Int, responseTimeout: TimeInterval, idleTimeout: TimeInterval, localPrompt: String) throws -> Bool {
    let socket = try Socket(.inet, type: .stream, protocol: .tcp)
    guard let address = try socket.addresses(for: host, port: Port(port)).first else {
        throw ValidationError("Could not resolve \(host):\(port).")
    }
    try socket.connect(address: address)
    print("ECUconnect connected and ready.")

    let remote = LoginRemote(socket: socket)
    let lineNoise = LineNoise()
    if try remote.drainOutput(firstByteTimeout: 0.50, idleTimeout: idleTimeout) == .closed {
        print("Connection closed by remote host.")
        return true
    }

    while true {
        let input: String
        do {
            input = try lineNoise.getLine(prompt: localPrompt)
        } catch LinenoiseError.CTRL_C, LinenoiseError.EOF {
            print("^D")
            return false
        }

        let trimmed = input.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.lowercased().hasPrefix("quit") || trimmed.lowercased().hasPrefix("exit") {
            print("")
            return false
        }

        let outbound = input + "\r\n"
        try remote.send([UInt8](outbound.utf8))

        if try remote.drainOutput(firstByteTimeout: responseTimeout, idleTimeout: idleTimeout) == .closed {
            print("Connection closed by remote host.")
            return true
        }
    }
}

struct Login: ParsableCommand {

    static var _commandName: String = "login"
    static var configuration = CommandConfiguration(abstract: "Open an interactive ECOS login shell (TCP).")

    @OptionGroup() var parentOptions: ECUconnectCommand.Options

    @Option(name: .long, help: "Login shell host (defaults to endpoint host or 192.168.42.42).")
    var host: String?

    @Option(name: .long, help: "Login shell TCP port.")
    var port: Int = 4242

    @Flag(name: .long, help: "Reconnect automatically when the shell disconnects.")
    var reconnect: Bool = false

    @Option(name: .long, help: "Seconds to wait before reconnecting.")
    var reconnectDelay: Double = 1.0

    @Option(name: .long, help: "Seconds to wait for first response byte after each command.")
    var responseTimeout: Double = 2.0

    @Option(name: .long, help: "Seconds of RX inactivity that mark end of current output chunk.")
    var idleTimeout: Double = 0.20

    @Option(name: .long, help: "Local input prompt. Empty string disables client-side prompt.")
    var localPrompt: String = ""

    mutating func run() throws {
        guard (1...65535).contains(port) else {
            throw ValidationError("Port must be between 1 and 65535.")
        }
        guard reconnectDelay >= 0 else {
            throw ValidationError("Reconnect delay must be >= 0.")
        }
        guard responseTimeout >= 0.01 else {
            throw ValidationError("Response timeout must be >= 0.01 seconds.")
        }
        guard idleTimeout >= 0.01 else {
            throw ValidationError("Idle timeout must be >= 0.01 seconds.")
        }

        let targetHost = host ?? defaultLoginHost(from: parentOptions.url)

        while true {
            do {
                let disconnected = try runLoginSession(
                    host: targetHost,
                    port: port,
                    responseTimeout: responseTimeout,
                    idleTimeout: idleTimeout,
                    localPrompt: localPrompt
                )
                if !reconnect || !disconnected {
                    return
                }
                print("Disconnected, reconnecting in \(String(format: "%.1f", reconnectDelay))s...")
            } catch {
                if !reconnect {
                    throw error
                }
                print("Connection error: \(error)")
                print("Retrying in \(String(format: "%.1f", reconnectDelay))s...")
            }
            Thread.sleep(forTimeInterval: reconnectDelay)
        }
    }
}
