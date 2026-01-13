import Accelerate
import ArgumentParser
import Chalk
import CornucopiaCore
import Foundation
import Swift_Automotive_Client

#if os(Linux)
import Glibc
#else
import Darwin.C
#endif

fileprivate class Delegate: Automotive.AdapterDelegate {

    func adapter(_ adapter: Swift_Automotive_Core.Automotive.Adapter, didUpdateState state: Swift_Automotive_Core.Automotive.AdapterState) {
        if state == .gone {
            print("Disconnected from ECUconnect.")
            Foundation.exit(-1)
        }
    }
}

struct Ping: ParsableCommand {

    static var _commandName: String = "ping"
    static var configuration = CommandConfiguration(abstract: "Run a performance test.")

    @OptionGroup() var parentOptions: ECUconnectCommand.Options

    @Argument(help: "PayloadSize (0...65535)")
    var payloadSize: Int = 512

    @Option(name: .short, help: "Count")
    var numberOfPings: Int = Int.max

    mutating func run() throws {

        let url: URL = URL(string: parentOptions.url)!
        let ps = payloadSize
        let count = numberOfPings
        let protocolPayloadCap = Int(UInt16.max)
        guard ps >= 0 else { throw ValidationError("Payload size must be >= 0.") }
        guard ps <= protocolPayloadCap else {
            throw ValidationError("Payload size \(ps) exceeds protocol maximum of \(protocolPayloadCap) bytes.")
        }

        Task {
            var count = count
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

                print("Starting speed test w/ payload size \(ps)...")

                var minimumPing: Double = 999999.0
                var maximumPing: Double = 0.0
                var allPings: [Double] = []
                var median: Double = 0.0

                print("PING w/ payload size \(ps)\n[  MINIMUM  <--  MEDIAN   <--  MAXIMUM  ]")

                let characters = ["⋯", "⋰", "⋮", "⋱"]
                var charactersIndex = 0

                while count > 0 {
                    let duration = try await adapter.ping(payloadSize: ps)
                    allPings.append(duration)
                    minimumPing = min(duration, minimumPing)
                    maximumPing = max(duration, maximumPing)

                    //let avg = vDSP.mean(allPings)

                    let sortedArray = allPings.sorted()
                    if sortedArray.count % 2 == 0 {
                        // If the count is even, the median is the average of the two middle numbers
                        median = (sortedArray[sortedArray.count / 2] + sortedArray[sortedArray.count / 2 - 1]) / 2
                    } else {
                        // If the count is odd, the median is the middle number
                        median = sortedArray[sortedArray.count / 2]
                    }
                    let character = characters[charactersIndex]
                    charactersIndex = (charactersIndex + 1) % characters.count
                    let line = String(format: "   %04.0f ms  <--  %04.0f ms  <--  %04.0f ms   ", minimumPing * 1000, median * 1000, maximumPing * 1000, character )
                    count -= 1
                    if (count > 0) {
                        print(line + character + "\r", terminator: "")
                    } else {
                        print(line + character)
                    }
                    fflush(stdout)
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
