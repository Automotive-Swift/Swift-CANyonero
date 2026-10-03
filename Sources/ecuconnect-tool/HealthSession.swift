import ArgumentParser
import CornucopiaCore
import Foundation
import Swift_Automotive_Client

private final class HealthDelegate: Automotive.AdapterDelegate {
    func adapter(_ adapter: Automotive.Adapter, didUpdateState state: Automotive.AdapterState) {}
}

enum DiagnosticOutput {
    private static var stream = FileHandle.standardOutput

    static func prepare() throws {
        fflush(stdout)
        let descriptor = dup(STDOUT_FILENO)
        guard descriptor >= 0 else { throw ValidationError("Cannot open diagnostic output.") }
        stream = FileHandle(fileDescriptor: descriptor, closeOnDealloc: true)
        // The transport dependency prints lifecycle messages directly to stdout.
        // Preserve the original destination for data and send library output to stderr.
        guard dup2(STDERR_FILENO, STDOUT_FILENO) >= 0 else { throw ValidationError("Cannot separate diagnostic output.") }
    }

    static func line(_ value: String) throws { try stream.write(contentsOf: Data((value + "\n").utf8)) }
}

func healthRPC(_ adapter: ECUconnect.Adapter, _ method: String,
               params: Cornucopia.Core.StringAnyCollection = [:]) async throws -> Cornucopia.Core.StringAnyCollection {
    let result = try await adapter.rpcCall(method: method, params: params)
    if case let .string(message) = result["error"] { throw ValidationError("\(method): \(message)") }
    return result
}

func runHealthSession(url: URL, operation: @escaping (ECUconnect.Adapter) async throws -> Void) throws -> Never {
    try DiagnosticOutput.prepare()
    // CoreBluetooth needs the main run loop even for a command-line client.
    var activeAdapter: ECUconnect.Adapter?
    let interrupt = DispatchSource.makeSignalSource(signal: SIGINT, queue: .main)
    let terminate = DispatchSource.makeSignalSource(signal: SIGTERM, queue: .main)
    signal(SIGINT, SIG_IGN)
    signal(SIGTERM, SIG_IGN)
    interrupt.setEventHandler { activeAdapter?.shutdown(); Foundation.exit(130) }
    terminate.setEventHandler { activeAdapter?.shutdown(); Foundation.exit(143) }
    interrupt.resume()
    terminate.resume()
    Task { @MainActor in
        do {
            let delegate = HealthDelegate()
            guard let adapter = try await Automotive.BaseAdapter.create(for: url, delegate: delegate) as? ECUconnect.Adapter else {
                throw ValidationError("The endpoint is not an ECUconnect adapter.")
            }
            activeAdapter = adapter
            _ = try await adapter.identify()
            try await operation(adapter)
            adapter.shutdown()
            withExtendedLifetime(delegate) {}
            Foundation.exit(0)
        } catch {
            activeAdapter?.shutdown()
            FileHandle.standardError.write(Data("Error: \(error)\n".utf8))
            Foundation.exit(1)
        }
    }
    while true { RunLoop.current.run(until: Date() + 0.1) }
}
