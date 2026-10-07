import ArgumentParser
import CornucopiaCore
import Foundation
import Swift_Automotive_Client

struct Health: ParsableCommand {
    static var configuration = CommandConfiguration(abstract: "Read memory and radio health without a diagnostic cable.")
    @OptionGroup var parentOptions: ECUconnectCommand.Options
    @Flag(help: "Output JSON (one object per sample when watching).") var json = false
    @Flag(help: "Keep the connection open and sample repeatedly; Ctrl-C stops.") var watch = false
    @Option(help: "Seconds between watch samples (at least 0.2).") var interval: Double = 1
    @Option(help: "Stop watching after this many samples.") var count: Int?

    func validate() throws {
        guard interval.isFinite && interval >= 0.2 && interval <= 86400 else {
            throw ValidationError("--interval must be between 0.2 and 86400 seconds.")
        }
        if let count, !watch || count < 1 { throw ValidationError("--count requires --watch and a positive number.") }
    }

    func run() throws {
        let url = try parseECUconnectURL(parentOptions.url)
        try runHealthSession(url: url) { adapter in
            var samples = 0
            repeat {
                let result = try await healthRPC(adapter, "system.health")
                let data = try JSONEncoder().encode(result)
                let snapshot = try HealthSnapshot.decode(data)
                if json { try DiagnosticOutput.line(String(decoding: data, as: UTF8.self)) }
                else { try DiagnosticOutput.line(snapshot.summary) }
                samples += 1
                if !watch || samples == count { break }
                try await Task.sleep(nanoseconds: UInt64(interval * 1_000_000_000))
            } while true
        }
    }
}

struct HealthSnapshot: Decodable {
    struct Memory: Decodable {
        let free: Int
        let minimum: Int
        let largest: Int
    }
    struct Sample: Decodable {
        let `internal`: Memory
        let psram: Memory
    }
    struct Stop: Decodable {
        let before: Sample
        let after: Sample
        let internal_reclaimed: Int
    }
    struct Radios: Decodable {
        let ble: Bool
        let network: Bool
        let owner: String
        let restore_in_ms: Int
    }
    let schema: Int
    let serial: String
    let firmware: String
    let elf_sha256: String
    let uptime_ms: Int
    let memory: Sample
    let radios: Radios
    let last_stops: [String: Stop?]
    struct Failures: Decodable { let count: Int; let last_bytes: Int; let last_caps: Int }
    let idf: String?
    let boot_count: Int?
    let reset_reason: Int?
    let voltage: Double?
    let task_count: Int?
    let coredump_count: Int?
    let allocation_failures: Failures?

    static func decode(_ data: Data) throws -> Self {
        struct Schema: Decodable { let schema: Int }
        let decoder = JSONDecoder()
        guard try decoder.decode(Schema.self, from: data).schema == 1 else {
            throw ValidationError("Unsupported system.health schema; expected schema 1.")
        }
        return try decoder.decode(Self.self, from: data)
    }

    static func bytes(_ value: Int) -> String { "\(value) B (\(String(format: "%.1f", Double(value) / 1024)) KiB)" }

    var summary: String {
        var lines = [
            "ECUconnect \(serial) · \(firmware) · uptime \(uptime_ms / 1000) s",
            "Internal heap: \(Self.bytes(memory.internal.free)) free; minimum \(Self.bytes(memory.internal.minimum)); largest \(Self.bytes(memory.internal.largest))",
            "PSRAM: \(Self.bytes(memory.psram.free)) free; minimum \(Self.bytes(memory.psram.minimum)); largest \(Self.bytes(memory.psram.largest))",
            "Radios: BLE \(radios.ble ? "on" : "off"), network \(radios.network ? "on" : "off"); owner \(radios.owner); restore in \(radios.restore_in_ms) ms",
        ]
        lines.append("ELF SHA-256: \(elf_sha256)")
        if let idf { lines.append("ESP-IDF: \(idf)") }
        if let voltage { lines.append("Voltage: \(voltage) V") }
        if let task_count { lines.append("Tasks: \(task_count)") }
        if let boot_count { lines.append("Boot count: \(boot_count)") }
        if let reset_reason { lines.append("Reset reason: \(reset_reason)") }
        if let coredump_count { lines.append("Core dumps: \(coredump_count)") }
        if let allocation_failures {
            lines.append("Allocation failures: \(allocation_failures.count); last \(allocation_failures.last_bytes) bytes, caps \(allocation_failures.last_caps)")
        }
        for name in ["network", "ble"] {
            if let value = last_stops[name], let stop = value {
                lines.append("Last \(name) stop: \(Self.bytes(stop.before.internal.free)) → \(Self.bytes(stop.after.internal.free)); reclaimed \(Self.bytes(stop.internal_reclaimed))")
                lines.append("  Largest block: \(Self.bytes(stop.before.internal.largest)) → \(Self.bytes(stop.after.internal.largest))")
            }
        }
        return lines.joined(separator: "\n")
    }
}
