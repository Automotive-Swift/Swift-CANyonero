import ArgumentParser
import Foundation

func parseECUconnectURL(_ endpoint: String) throws -> URL {
    let trimmed = endpoint.trimmingCharacters(in: .whitespacesAndNewlines)
    guard !trimmed.isEmpty else {
        throw ValidationError("Endpoint URL cannot be empty.")
    }

    let invalidEndpointError = "Invalid endpoint '\(endpoint)'. Use host:port or a URL like ecuconnect-tcp://192.168.42.42:129."

    if trimmed.contains("://") {
        guard var components = URLComponents(string: trimmed),
              let scheme = components.scheme?.lowercased() else {
            throw ValidationError(invalidEndpointError)
        }

        switch scheme {
            case "ecuconnect", "ecuconnect-wifi", "tcp":
                components.scheme = "ecuconnect-tcp"
            default:
                break
        }

        guard let url = components.url else {
            throw ValidationError(invalidEndpointError)
        }
        return url
    }

    guard let components = URLComponents(string: "ecuconnect-tcp://\(trimmed)"),
          let url = components.url,
          components.host?.isEmpty == false else {
        throw ValidationError(invalidEndpointError)
    }
    if let port = components.port, (1...65535).contains(port) {
        return url
    }
    throw ValidationError(invalidEndpointError)
}

@main
struct ECUconnectCommand: ParsableCommand {

    /// Derived from argv[0] so that usage text matches however the binary was
    /// installed — it commonly sits next to the Python variant of this tool.
    static var _commandName: String = ProcessInfo.processInfo.arguments.first
        .map { URL(fileURLWithPath: $0).lastPathComponent } ?? "ecuconnect-tool"

    static var configuration = CommandConfiguration(
        abstract: "ECUconnect tool (Swift) for CANyonero adapters",
        version: "1.0.0 (Swift)",
        subcommands: [
            Benchmark.self,
            Config.self,
            Info.self,
            Login.self,
            Monitor.self,
            Ping.self,
            Raw.self,
            Term.self,
            Test.self,
            Update.self,
        ]
    )

    @OptionGroup() var options: Options

    struct Options: ParsableArguments {

        @Option(name: [.customShort("u"), .long], help: "Adapter endpoint (host:port or URL)")
        var url: String = "ecuconnect-l2cap://FFF1:129"
    }
}
