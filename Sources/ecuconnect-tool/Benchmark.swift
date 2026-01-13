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

fileprivate struct BenchmarkResult {
    let payloadSize: Int
    let average: Double
    let minimum: Double
    let maximum: Double
    let bandwidthBytesPerSecond: Double
}

fileprivate func isBLELink(_ url: URL) -> Bool {
    let scheme = url.scheme?.lowercased() ?? ""
    if scheme.contains("ble") { return true }
    return url.absoluteString.lowercased().contains("ecuconnect-ble")
}

fileprivate func isTCPLink(_ url: URL) -> Bool {
    let scheme = url.scheme?.lowercased() ?? ""
    return scheme.contains("tcp")
}

fileprivate func formatRate(_ bytesPerSecond: Double) -> String {
    let kib = 1024.0
    let mib = kib * 1024.0
    if bytesPerSecond >= mib {
        return String(format: "%.2f MiB/s", bytesPerSecond / mib)
    }
    if bytesPerSecond >= kib {
        return String(format: "%.2f KiB/s", bytesPerSecond / kib)
    }
    return String(format: "%.0f B/s", bytesPerSecond)
}

fileprivate func defaultPayloadSizes(maxPayloadSize: Int) -> [Int] {
    if maxPayloadSize <= 0 { return [] }
    let base = [8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768]
    var sizes = base.filter { $0 <= maxPayloadSize }
    if sizes.isEmpty {
        sizes = [maxPayloadSize]
    } else if let last = sizes.last, last < maxPayloadSize {
        sizes.append(maxPayloadSize)
    }
    return sizes
}

fileprivate func recommendPayloadSize(_ results: [BenchmarkResult]) -> BenchmarkResult? {
    guard let maxBandwidth = results.map(\.bandwidthBytesPerSecond).max(), maxBandwidth > 0 else {
        return results.min(by: { $0.average < $1.average })
    }
    let threshold = maxBandwidth * 0.98
    let candidates = results.filter { $0.bandwidthBytesPerSecond >= threshold }
    let latencyEpsilon = 0.0005
    return candidates.min {
        if abs($0.average - $1.average) <= latencyEpsilon {
            return $0.payloadSize < $1.payloadSize
        }
        return $0.average < $1.average
    }
}

struct Benchmark: ParsableCommand {

    static var _commandName: String = "benchmark"
    static var configuration = CommandConfiguration(abstract: "Benchmark latency and bandwidth using PING.")

    @OptionGroup() var parentOptions: ECUconnectCommand.Options

    @Option(name: .short, help: "Pings per payload size")
    var numberOfPings: Int = 32

    @Option(name: .customLong("sizes"), parsing: .upToNextOption, help: "Payload sizes in bytes (space-separated). If omitted, a default range is used (BLE cap 5000 bytes, TCP cap 16384 bytes). Protocol max is 65535 bytes.")
    var payloadSizes: [Int] = []

    @Flag(name: .long, help: "Allow payload sizes exceeding transport caps (may cause failures).")
    var force: Bool = false

    mutating func run() throws {

        let url: URL = URL(string: parentOptions.url)!

        let blePayloadCap = 5000
        let isBLE = isBLELink(url)
        let tcpPayloadCap = 16384
        let isTCP = isTCPLink(url)
        let protocolPayloadCap = Int(UInt16.max)
        let defaultMaxPayload = isBLE ? blePayloadCap : (isTCP ? tcpPayloadCap : 4096)
        var sizes = payloadSizes.isEmpty ? defaultPayloadSizes(maxPayloadSize: defaultMaxPayload) : payloadSizes
        sizes = Array(Set(sizes)).sorted()
        let pingCount = numberOfPings

        guard pingCount > 0 else { throw ValidationError("Count must be greater than 0.") }
        guard !sizes.isEmpty else { throw ValidationError("Payload sizes are empty.") }
        if sizes.contains(where: { $0 < 0 }) { throw ValidationError("Payload sizes must be >= 0.") }
        if let firstTooLarge = sizes.first(where: { $0 > protocolPayloadCap }) {
            throw ValidationError("Payload size \(firstTooLarge) exceeds protocol maximum of \(protocolPayloadCap) bytes.")
        }
        if isBLE {
            if let firstTooLarge = sizes.first(where: { $0 > blePayloadCap }) {
                if force {
                    print("\("Warning:", color: .yellow) Payload size \(firstTooLarge) exceeds BLE cap of \(blePayloadCap) bytes.")
                } else {
                    throw ValidationError("Payload size \(firstTooLarge) exceeds BLE cap of \(blePayloadCap) bytes. Use --force to override.")
                }
            }
        }
        if isTCP {
            if let firstTooLarge = sizes.first(where: { $0 > tcpPayloadCap }) {
                if force {
                    print("\("Warning:", color: .yellow) Payload size \(firstTooLarge) exceeds TCP cap of \(tcpPayloadCap) bytes.")
                } else {
                    throw ValidationError("Payload size \(firstTooLarge) exceeds TCP cap of \(tcpPayloadCap) bytes. Use --force to override.")
                }
            }
        }

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

                print("Benchmarking PING with \(pingCount) pings per size.")
                print("Payload sizes: \(sizes.map { String($0) }.joined(separator: ", ")) bytes")
                if isBLE { print("BLE payload cap: \(blePayloadCap) bytes") }
                if isTCP { print("TCP payload cap: \(tcpPayloadCap) bytes") }
                let headerSize = "SIZE".padding(toLength: 10, withPad: " ", startingAt: 0)
                let headerAvg = "AVG".padding(toLength: 13, withPad: " ", startingAt: 0)
                let headerMin = "MIN".padding(toLength: 13, withPad: " ", startingAt: 0)
                let headerMax = "MAX".padding(toLength: 13, withPad: " ", startingAt: 0)
                let headerBw = "AVG BW"
                let header = "\(headerSize, color: .cyan)\(headerAvg, color: .yellow)\(headerMin, color: .green)\(headerMax, color: .red)\(headerBw, color: .magenta)"
                print(header)

                var results: [BenchmarkResult] = []
                var totalDuration: Double = 0.0
                var totalCount: Int = 0

                let spinnerFrames = ["⋯", "⋰", "⋮", "⋱"]
                var spinnerIndex = 0

                for payloadSize in sizes {
                    var minimumPing = Double.greatestFiniteMagnitude
                    var maximumPing: Double = 0.0
                    var sumPing: Double = 0.0
                    var progressLineLength = 0

                    for index in 0..<pingCount {
                        let spinner = spinnerFrames[spinnerIndex]
                        spinnerIndex = (spinnerIndex + 1) % spinnerFrames.count
                        let progressPlain = "\(spinner) \(payloadSize) bytes  ping \(index + 1)/\(pingCount)"
                        progressLineLength = max(progressLineLength, progressPlain.count)
                        let progressLine = "\(spinner, color: .blue) \(String(payloadSize), color: .cyan) bytes  ping \(index + 1)/\(pingCount)"
                        let padCount = max(0, progressLineLength - progressPlain.count)
                        let paddedLine = progressLine + String(repeating: " ", count: padCount)
                        print(paddedLine + "\r", terminator: "")
                        fflush(stdout)
                        let duration = try await adapter.ping(payloadSize: payloadSize)
                        sumPing += duration
                        minimumPing = min(minimumPing, duration)
                        maximumPing = max(maximumPing, duration)
                    }

                    if progressLineLength > 0 {
                        let clearLine = String(repeating: " ", count: progressLineLength)
                        print(clearLine + "\r", terminator: "")
                    }

                    let averagePing = sumPing / Double(pingCount)
                    // ECUconnect ping echoes the full payload; count both directions for round-trip throughput.
                    let roundTripBandwidth = payloadSize > 0 ? (Double(payloadSize) * 2.0 / averagePing) : 0.0
                    let result = BenchmarkResult(
                        payloadSize: payloadSize,
                        average: averagePing,
                        minimum: minimumPing,
                        maximum: maximumPing,
                        bandwidthBytesPerSecond: roundTripBandwidth
                    )
                    results.append(result)
                    totalDuration += sumPing
                    totalCount += pingCount

                    let sizeStr = String(format: "%6d bytes", payloadSize)
                    let avgStr = String(format: "avg %7.2f ms", averagePing * 1000)
                    let minStr = String(format: "min %7.2f ms", minimumPing * 1000)
                    let maxStr = String(format: "max %7.2f ms", maximumPing * 1000)
                    let bwStr = "avg rt bw \(formatRate(roundTripBandwidth))"
                    print("\(sizeStr, color: .cyan)  \(avgStr, color: .yellow)  \(minStr, color: .green)  \(maxStr, color: .red)  \(bwStr, color: .magenta)")
                    fflush(stdout)
                }

                let overallAverageLatency = totalDuration / Double(totalCount)
                if let best = results.max(by: { $0.bandwidthBytesPerSecond < $1.bandwidthBytesPerSecond }) {
                    print("Max avg rt bandwidth: \(formatRate(best.bandwidthBytesPerSecond), color: .magenta) @ \(String(best.payloadSize), color: .cyan) bytes")
                }
                if let recommended = recommendPayloadSize(results) {
                    let recLine = String(
                        format: "Recommended payload size: %d bytes (avg rt bw %@, avg latency %.2f ms)",
                        recommended.payloadSize,
                        formatRate(recommended.bandwidthBytesPerSecond),
                        recommended.average * 1000
                    )
                    print("\(recLine, color: .blue)")
                }
                let avgLatencyLine = String(format: "Average latency: %.2f ms", overallAverageLatency * 1000)
                print("\(avgLatencyLine, color: .yellow)")

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
