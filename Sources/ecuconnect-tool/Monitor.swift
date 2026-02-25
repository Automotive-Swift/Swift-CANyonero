import ArgumentParser
import Chalk
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

struct Monitor: ParsableCommand {

    static var _commandName: String = "monitor"
    static var configuration = CommandConfiguration(abstract: "Monitor the CAN Bus.")

    @OptionGroup() var parentOptions: ECUconnectCommand.Options

    @Argument(help: "Bitrate")
    var bitrate: Int = 500000

    mutating func run() throws {

        let url = try parseECUconnectURL(parentOptions.url)
        let bps = bitrate

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
                let monitorStream = try await Cornucopia.Core.Spinner.run("Starting monitor") {
                    try await adapter.monitor(bitrate: bps)
                }
                var lastTime = CFAbsoluteTimeGetCurrent()
                for try await (id, _, data) in monitorStream {
                    let now = CFAbsoluteTimeGetCurrent()
                    let timeDiff = CFAbsoluteTimeGetCurrent() - lastTime
                    let timestamp = String(format: "%010.06f", timeDiff)
                    let width = id < 0x800 ? 3 : 8
                    let sid = "\(id, radix: .hex, toWidth: width)"
                    var sdata = "\(data, radix: .hex, toWidth: 2, separator: " ")"
                    while sdata.count < (3 * 8) { sdata += " " }
                    var ascii = "'"
                    for byte in data {
                        ascii += (byte > 0x1F && byte < 0x7F) ? String(UnicodeScalar(byte)) : "."
                    }
                    ascii += "'"

                    print("(\(timestamp))  \(sid, color: .blue)  [\(data.count)]  \(sdata, color: .magenta)  \(ascii, color: .green)")
                    lastTime = now
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
