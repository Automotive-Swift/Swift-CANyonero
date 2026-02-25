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

struct Raw: ParsableCommand {

    static var _commandName: String = "raw"
    static var configuration = CommandConfiguration(abstract: "Send raw packages.")

    @OptionGroup() var parentOptions: ECUconnectCommand.Options

#if false
    @Argument(help: "Filename")
    var filename: String

    mutating func validate() throws {
        guard FileManager.default.fileExists(atPath: self.filename) else { throw ValidationError("Can't find file '\(filename)'.") }
    }
#endif

    mutating func run() throws {

        let url = try parseECUconnectURL(parentOptions.url)

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
                let _ = try await Cornucopia.Core.Spinner.run("Opening channel") {
                    try await adapter.openChannel(proto: .raw, bitrate: 500000)
                }
                print("Channel opened.")
#if true
                while true {

                    let message = Automotive.Message(addressing: .oneshot(id: 0x777), bytes: [
                        0x08, 0x02, 0x3E, 0x80, 0xAA, 0xAA, 0xAA, 0xAA,
                        0x03, 0xFF, 0xFF, 0xFF, 0x08, 0x02, 0x3E, 0x00,
                        0xAB, 0xAB, 0xAB, 0xAB, /* implicit padding */
                    ])
                    try await adapter.sendMessageReceiveNothing(message)

                    try await Task.sleep(for: .seconds(0.5))
                }
#endif
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
