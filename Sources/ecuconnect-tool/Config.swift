import ArgumentParser
import CornucopiaCore
import Foundation
import Swift_Automotive_Client

fileprivate class Delegate: Automotive.AdapterDelegate {

    func adapter(_ adapter: Swift_Automotive_Core.Automotive.Adapter, didUpdateState state: Swift_Automotive_Core.Automotive.AdapterState) {
        if state == .gone {
            print("Disconnected from ECUconnect.")
            Foundation.exit(-1)
        }
    }
}

fileprivate func connectAdapter(url: URL) async throws -> ECUconnect.Adapter {
    let delegate = Delegate()
    let adapter = try await Cornucopia.Core.Spinner.run("Connecting to adapter") {
        guard let adapter = try await Automotive.BaseAdapter.create(for: url, delegate: delegate) as? ECUconnect.Adapter else {
            throw ValidationError("Not an ECUconnect adapter")
        }
        return adapter
    }
    let info = try await adapter.identify()
    let voltage = try await adapter.readSystemVoltage()
    print("Connected to ECUconnect: \(info).")
    print("Reported system voltage is \(voltage)V.")
    return adapter
}

enum ConfigAppMode: String, CaseIterable, ExpressibleByArgument {
    case elm327
    case logger
    case canvoy
    case ecos

    var id: Int {
        switch self {
            case .elm327: return 0
            case .logger: return 1
            case .canvoy: return 2
            case .ecos: return 3
        }
    }

    static func name(for id: Int) -> String {
        switch id {
            case 0: return ConfigAppMode.elm327.rawValue
            case 1: return ConfigAppMode.logger.rawValue
            case 2: return ConfigAppMode.canvoy.rawValue
            case 3: return ConfigAppMode.ecos.rawValue
            default: return "unknown(\(id))"
        }
    }

    init?(argument: String) {
        let normalized = argument.lowercased()
        switch normalized {
            case "0", "elm", "elm327": self = .elm327
            case "1", "logger": self = .logger
            case "2", "canvoy": self = .canvoy
            case "3", "ecos": self = .ecos
            default: return nil
        }
    }
}

enum CANvoyRole: String, CaseIterable, ExpressibleByArgument {
    case unconfigured
    case vehicle
    case tester

    var id: Int {
        switch self {
            case .unconfigured: return 0
            case .vehicle: return 1
            case .tester: return 2
        }
    }

    init?(argument: String) {
        let normalized = argument.lowercased()
        switch normalized {
            case "0", "unconfigured", "none": self = .unconfigured
            case "1", "vehicle": self = .vehicle
            case "2", "tester": self = .tester
            default: return nil
        }
    }
}

struct Config: ParsableCommand {

    static var _commandName: String = "config"
    static var configuration = CommandConfiguration(
        abstract: "Configure ECUconnect using JSON-RPC.",
        subcommands: [
            ConfigMode.self,
            ConfigShow.self,
            ConfigReboot.self,
            ConfigCANvoy.self,
        ]
    )
}

struct ConfigMode: ParsableCommand {

    static var _commandName: String = "mode"
    static var configuration = CommandConfiguration(
        abstract: "Set the ECUconnect operating mode (JSON-RPC app.set_mode)."
    )

    @OptionGroup() var parentOptions: ECUconnectCommand.Options

    @Argument(help: "Mode: ecos, elm327, logger, canvoy (or 0-3).")
    var mode: ConfigAppMode

    mutating func run() throws {
        let url: URL = URL(string: parentOptions.url)!

        Task {
            do {
                let adapter = try await connectAdapter(url: url)
                let params: Cornucopia.Core.StringAnyCollection = ["mode": .int(mode.id)]
                let result = try await adapter.rpcCall(method: "app.set_mode", params: params)
                let success = (result["success"]?.anyValue as? Bool) ?? false
                if success {
                    print("Mode change requested: \(mode.rawValue). Adapter will reboot shortly.")
                    Foundation.exit(0)
                }
                let message = (result["error"]?.anyValue as? String) ?? "Failed to change mode."
                print("Error: \(message)")
                Foundation.exit(-1)
            } catch {
                print("Error: \(error)")
                Foundation.exit(-1)
            }
        }

        while true {
            RunLoop.current.run(until: Date() + 0.5)
        }
    }
}

struct ConfigReboot: ParsableCommand {

    static var _commandName: String = "reboot"
    static var configuration = CommandConfiguration(
        abstract: "Reboot the ECUconnect adapter (tries JSON-RPC system.reboot, falls back to reset)."
    )

    @OptionGroup() var parentOptions: ECUconnectCommand.Options

    mutating func run() throws {
        let url: URL = URL(string: parentOptions.url)!

        Task {
            do {
                let adapter = try await connectAdapter(url: url)
                do {
                    _ = try await adapter.rpcCall(method: "system.reboot")
                    print("Reboot requested via JSON-RPC.")
                    Foundation.exit(0)
                } catch {
                    print("JSON-RPC reboot not available (\(error.localizedDescription)). Falling back to reset command.")
                    try await adapter.reset()
                    print("Reboot requested via reset command.")
                    Foundation.exit(0)
                }
            } catch {
                print("Error: \(error)")
                Foundation.exit(-1)
            }
        }

        while true {
            RunLoop.current.run(until: Date() + 0.5)
        }
    }
}

struct ConfigShow: ParsableCommand {

    static var _commandName: String = "show"
    static var configuration = CommandConfiguration(
        abstract: "Show current configuration (best-effort via JSON-RPC)."
    )

    @OptionGroup() var parentOptions: ECUconnectCommand.Options

    mutating func run() throws {
        let url: URL = URL(string: parentOptions.url)!

        Task {
            do {
                let adapter = try await connectAdapter(url: url)

                if let result = try? await adapter.rpcCall(method: "app.config") {
                    let mode = (result["mode"]?.anyValue as? Int) ??
                               Int(result["mode"]?.anyValue as? Double ?? -1)
                    if mode >= 0 {
                        let modeName = ConfigAppMode.name(for: mode)
                        print("App mode: \(modeName)")
                    } else {
                        print("App mode: unavailable (RPC not supported)")
                    }
                    if let appState = result["appstate"]?.anyValue as? String {
                        print("App state: \(appState)")
                    } else if let appState = result["state"]?.anyValue as? String {
                        print("App state: \(appState)")
                    }
                } else {
                    print("App mode: unavailable (RPC not supported)")
                }

                if let canvoyResult = try? await adapter.rpcCall(method: "canvoy.role") {
                    let roleValue = (canvoyResult["role"]?.anyValue as? Int) ??
                                    Int(canvoyResult["role"]?.anyValue as? Double ?? 0)
                    let roleName = CANvoyRole(argument: String(roleValue))?.rawValue ?? "unknown(\(roleValue))"
                    let bitrate = (canvoyResult["bitrate"]?.anyValue as? Int) ??
                                  Int(canvoyResult["bitrate"]?.anyValue as? Double ?? 500_000)
                    let termination: Bool
                    if let boolValue = canvoyResult["termination"]?.anyValue as? Bool {
                        termination = boolValue
                    } else if let intValue = canvoyResult["termination"]?.anyValue as? Int {
                        termination = intValue != 0
                    } else {
                        termination = false
                    }
                    print("CANvoy role: \(roleName)")
                    print("CANvoy bitrate: \(bitrate)")
                    print("CANvoy termination: \(termination ? "enabled" : "disabled")")
                }

                Foundation.exit(0)
            } catch {
                print("Error: \(error)")
                Foundation.exit(-1)
            }
        }

        while true {
            RunLoop.current.run(until: Date() + 0.5)
        }
    }
}

struct ConfigCANvoy: ParsableCommand {

    static var _commandName: String = "canvoy"
    static var configuration = CommandConfiguration(
        abstract: "Configure CANvoy role and bus settings.",
        subcommands: [
            ConfigCANvoyGet.self,
            ConfigCANvoySet.self,
        ]
    )
}

struct ConfigCANvoyGet: ParsableCommand {

    static var _commandName: String = "get"
    static var configuration = CommandConfiguration(
        abstract: "Fetch CANvoy configuration (JSON-RPC canvoy.role)."
    )

    @OptionGroup() var parentOptions: ECUconnectCommand.Options

    mutating func run() throws {
        let url: URL = URL(string: parentOptions.url)!

        Task {
            do {
                let adapter = try await connectAdapter(url: url)
                let result = try await adapter.rpcCall(method: "canvoy.role")

                let roleValue = (result["role"]?.anyValue as? Int) ??
                                Int(result["role"]?.anyValue as? Double ?? 0)
                let role = CANvoyRole(argument: String(roleValue)) ?? .unconfigured
                let bitrate = (result["bitrate"]?.anyValue as? Int) ??
                              Int(result["bitrate"]?.anyValue as? Double ?? 500_000)
                let termination: Bool
                if let boolValue = result["termination"]?.anyValue as? Bool {
                    termination = boolValue
                } else if let intValue = result["termination"]?.anyValue as? Int {
                    termination = intValue != 0
                } else {
                    termination = false
                }

                print("CANvoy role: \(role.rawValue)")
                print("Bitrate: \(bitrate)")
                print("Termination: \(termination ? "enabled" : "disabled")")
                Foundation.exit(0)
            } catch {
                print("Error: \(error)")
                Foundation.exit(-1)
            }
        }

        while true {
            RunLoop.current.run(until: Date() + 0.5)
        }
    }
}

struct ConfigCANvoySet: ParsableCommand {

    static var _commandName: String = "set"
    static var configuration = CommandConfiguration(
        abstract: "Set CANvoy configuration (JSON-RPC canvoy.set_role)."
    )

    @OptionGroup() var parentOptions: ECUconnectCommand.Options

    @Argument(help: "Role: vehicle, tester, unconfigured (or 0-2).")
    var role: CANvoyRole

    @Option(help: "CAN bitrate (e.g. 500000).")
    var bitrate: Int = 500_000

    @Flag(inversion: .prefixedNo, help: "Enable termination resistor.")
    var termination: Bool = false

    mutating func run() throws {
        let url: URL = URL(string: parentOptions.url)!

        Task {
            do {
                let adapter = try await connectAdapter(url: url)
                let params: Cornucopia.Core.StringAnyCollection = [
                    "role": .int(role.id),
                    "bitrate": .int(bitrate),
                    "termination": .bool(termination),
                ]
                let result = try await adapter.rpcCall(method: "canvoy.set_role", params: params)
                let success = (result["success"]?.anyValue as? Bool) ?? false
                if success {
                    let appliedBitrate = (result["bitrate"]?.anyValue as? Int) ??
                                         Int(result["bitrate"]?.anyValue as? Double ?? bitrate)
                    let appliedTermination: Bool
                    if let boolValue = result["termination"]?.anyValue as? Bool {
                        appliedTermination = boolValue
                    } else if let intValue = result["termination"]?.anyValue as? Int {
                        appliedTermination = intValue != 0
                    } else {
                        appliedTermination = termination
                    }
                    print("CANvoy role set to \(role.rawValue).")
                    print("Bitrate: \(appliedBitrate)")
                    print("Termination: \(appliedTermination ? "enabled" : "disabled")")
                    Foundation.exit(0)
                }
                let message = (result["error"]?.anyValue as? String) ?? "Unable to set CANvoy role."
                print("Error: \(message)")
                Foundation.exit(-1)
            } catch {
                print("Error: \(error)")
                Foundation.exit(-1)
            }
        }

        while true {
            RunLoop.current.run(until: Date() + 0.5)
        }
    }
}
