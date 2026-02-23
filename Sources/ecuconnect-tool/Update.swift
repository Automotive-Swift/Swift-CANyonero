import ArgumentParser
import Chalk
import CornucopiaCore
import Foundation
import Progress
import Swift_Automotive_Client

fileprivate class Delegate: Automotive.AdapterDelegate {

    func adapter(_ adapter: Swift_Automotive_Core.Automotive.Adapter, didUpdateState state: Swift_Automotive_Core.Automotive.AdapterState) {
        if state == .gone {
            print("Disconnected from ECUconnect.")
            Foundation.exit(-1)
        }
    }
}

extension Array {
    func chunked(into size: Int) -> [[Element]] {
        return stride(from: 0, to: count, by: size).map {
            Array(self[$0 ..< Swift.min($0 + size, count)])
        }
    }
}

struct Update: ParsableCommand {

    static var _commandName: String = "update"
    static var configuration = CommandConfiguration(abstract: "Update firmware.")

    @OptionGroup() var parentOptions: ECUconnectCommand.Options

    @Argument(help: "Filename")
    var filename: String

    mutating func validate() throws {
        guard FileManager.default.fileExists(atPath: self.filename) else { throw ValidationError("Can't find file '\(filename)'.") }
    }

    mutating func run() throws {

        let url: URL = URL(string: parentOptions.url)!
        let fileUrl = URL(fileURLWithPath: self.filename)
        let data = try Data(contentsOf: fileUrl)

        Task {
            do {
                let delegate = Delegate()
                let adapter = try await Cornucopia.Core.Spinner.run("Connecting to adapter") {
                    guard let adapter = try await Automotive.BaseAdapter.create(for: url, delegate: delegate) as? ECUconnect.Adapter else { throw ValidationError("Not an ECUconnect adapter") }
                    return adapter
                }
                let info = try await adapter.identify()
                let voltage = try await adapter.readSystemVoltage()
                print("Connected to ECUconnect: \(info).")
                print("Reported system voltage is \(voltage)V.")

                try await Cornucopia.Core.Spinner.run("Preparing firmware update") {
                    try await adapter.prepareUpdate()
                }
                let bytes = Array(data)
                for chunk in Progress(bytes.chunked(into: 4000)) {
                    try await adapter.sendUpdateData(chunk)
                }
                try await adapter.commitUpdate()
                print("Uploading complete, resetting hardware.")
                try await adapter.reset()
                adapter.shutdown()

                let nextInfo = try await Cornucopia.Core.Spinner.run("Waiting to reconnect") {
                    try await Task.sleep(for: .seconds(3))
                    return try await adapter.identify()
                }
                print("Connected to ECUconnect: \(nextInfo).")
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
