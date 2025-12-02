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

struct Test: ParsableCommand {

    static var _commandName: String = "test"
    static var configuration = CommandConfiguration(abstract: "Test various functions.")

    @OptionGroup() var parentOptions: ECUconnectCommand.Options

#if false
    @Argument(help: "Filename")
    var filename: String

    mutating func validate() throws {
        guard FileManager.default.fileExists(atPath: self.filename) else { throw ValidationError("Can't find file '\(filename)'.") }
    }
#endif

    mutating func run() throws {

        let url: URL = URL(string: parentOptions.url)!

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

                let channel = try await Cornucopia.Core.Spinner.run("Opening channel") {
                    try await adapter.openChannel(proto: .isotp, bitrate: 500000)
                }
                print("Channel #\(channel) opened.")

                while true {

                    var message = Automotive.Message(addressing: .oneshot(id: 0x777), bytes: [0x3E, 0x80])
                    print("Started periodic message #1")
                    try await adapter.startSendingPeriodically(message, interval: 1000, repeatCount: 0)

                    try await Task.CC_sleep(seconds: 0.5)

                    message = Automotive.Message(addressing: .oneshot(id: 0x123), bytes: [0x3E, 0x80])
                    print("Started periodic message #2")
                    try await adapter.startSendingPeriodically(message, interval: 1000, repeatCount: 0)

                    for waiting in stride(from: 5, to: 1, by: -1) {
                        print("Cancelling in \(waiting) seconds...")
                        try await Task.CC_sleep(seconds: 1)
                    }

                    try await adapter.cancelSendingPeriodically()
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
