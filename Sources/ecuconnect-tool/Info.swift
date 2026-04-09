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

struct Info: ParsableCommand {

    static var _commandName: String = "info"
    static var configuration = CommandConfiguration(abstract: "Query adapter info, voltage, and configuration.")

    @OptionGroup() var parentOptions: ECUconnectCommand.Options

    mutating func run() throws {
        let url = try parseECUconnectURL(parentOptions.url)

        Task {
            do {
                let delegate = Delegate()
                let (adapter, info, voltage) = try await Cornucopia.Core.Spinner.run("Connecting to adapter") {
                    guard let adapter = try await Automotive.BaseAdapter.create(for: url, delegate: delegate) as? ECUconnect.Adapter else {
                        throw ValidationError("Not an ECUconnect adapter")
                    }
                    let info = try await adapter.identify()
                    let voltage = try await adapter.readSystemVoltage()
                    return (adapter, info, voltage)
                }

                print("Vendor:   \(info.vendor)")
                print("Model:    \(info.model)")
                print("Hardware: \(info.ic)")
                print("Serial:   \(info.deviceParameter)")
                print("Firmware: \(info.firmwareVersion)")
                print("Voltage:  \(String(format: "%.2f", voltage)) V")

                if let config = try? await adapter.rpcCall(method: "app.config") {
                    let mode = (config["mode"]?.anyValue as? Int) ??
                               Int(config["mode"]?.anyValue as? Double ?? -1)
                    if mode >= 0 {
                        print("Mode:     \(ConfigAppMode.name(for: mode))")
                    }
                    if let appState = config["appstate"]?.anyValue as? String {
                        print("State:    \(appState)")
                    } else if let appState = config["state"]?.anyValue as? String {
                        print("State:    \(appState)")
                    }
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
