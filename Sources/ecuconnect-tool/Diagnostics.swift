import ArgumentParser
import CornucopiaCore
import Foundation
import Swift_Automotive_Client

struct Diagnostics: ParsableCommand {
    static var configuration = CommandConfiguration(abstract: "Export health history and optional crash dumps.", subcommands: [DiagnosticsExport.self])
}

struct DiagnosticsExport: ParsableCommand {
    static var configuration = CommandConfiguration(commandName: "export", abstract: "Save a diagnostic bundle; crash dumps remain on the device.")
    @OptionGroup var parentOptions: ECUconnectCommand.Options
    @Option(help: "New output directory; defaults to ecuconnect-diagnostics-<timestamp>.") var output: String?
    @Flag(help: "Also download stored crash dumps in bounded chunks.") var includeCoredumps = false

    func run() throws {
        let endpoint = try parseECUconnectURL(parentOptions.url)
        let name = output ?? "ecuconnect-diagnostics-\(Int(Date().timeIntervalSince1970))"
        let directory = URL(fileURLWithPath: name, isDirectory: true).standardizedFileURL
        guard !FileManager.default.fileExists(atPath: directory.path) else {
            throw ValidationError("Output already exists: \(directory.path). Choose a new --output directory.")
        }
        try runHealthSession(url: endpoint) { adapter in
            let snapshot = try await healthRPC(adapter, "system.health")
            // Decode before creating a bundle, so old firmware produces a clear failure.
            _ = try HealthSnapshot.decode(JSONEncoder().encode(snapshot))
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: false)
            try writeDiagnosticJSON(snapshot, to: directory.appendingPathComponent("health.json"))
            var pages: [Cornucopia.Core.StringAnyCollection] = []
            var cursor = HealthEventCursor()
            repeat {
                var params: Cornucopia.Core.StringAnyCollection = ["after": .int(cursor.after)]
                if let through = cursor.through { params["through"] = .int(through) }
                let page = try await healthRPC(adapter, "system.health.events", params: params)
                pages.append(page)
                if try !cursor.advance(page) { break }
            } while true
            try writeDiagnosticJSON(pages, to: directory.appendingPathComponent("events.json"))
            if includeCoredumps { try await exportCoreDumps(adapter, to: directory) }
            let manifest: [String: String] = ["schema": "1", "captured_at": ISO8601DateFormatter().string(from: Date()),
                                             "endpoint": endpoint.absoluteString, "status": "complete"]
            try writeDiagnosticJSON(manifest, to: directory.appendingPathComponent("manifest.json"))
            try DiagnosticOutput.line(directory.path)
        }
    }
}

func writeDiagnosticJSON<T: Encodable>(_ value: T, to url: URL) throws {
    let encoder = JSONEncoder()
    encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
    try encoder.encode(value).write(to: url, options: [.atomic])
}

struct HealthEventCursor {
    var after = 0
    var through: Int?

    mutating func advance(_ page: Cornucopia.Core.StringAnyCollection) throws -> Bool {
        guard case let .int(end) = page["through"], case let .int(next) = page["next"],
              case let .bool(more) = page["more"], case .array = page["events"], end >= 0,
              through == nil || through == end else { throw ValidationError("Invalid health history page.") }
        if more && (next <= after || next >= end) { throw ValidationError("Health history cursor made no progress.") }
        through = end
        after = next
        return more
    }
}

func validatedDumpName(_ name: String) throws -> String {
    guard name.hasPrefix("coredump-"), name.hasSuffix(".bin"), !name.contains("/"), !name.contains("\\"), !name.contains("..") else {
        throw ValidationError("Invalid crash dump filename.")
    }
    return name
}

private func exportCoreDumps(_ adapter: ECUconnect.Adapter, to directory: URL) async throws {
    let listing = try await healthRPC(adapter, "system.coredumps.list")
    try writeDiagnosticJSON(listing, to: directory.appendingPathComponent("coredumps.json"))
    guard case let .array(files) = listing["coredumps"] else { throw ValidationError("Invalid crash dump listing.") }
    for file in files {
        guard case let .string(rawName) = file else { throw ValidationError("Invalid crash dump entry.") }
        let name = try validatedDumpName(rawName)
        let url = directory.appendingPathComponent(name + ".partial")
        guard FileManager.default.createFile(atPath: url.path, contents: nil) else { throw ValidationError("Cannot create \(url.path).") }
        let handle = try FileHandle(forWritingTo: url)
        defer { try? handle.close() }
        var offset = 0
        var total: Int?
        repeat {
            let reply = try await healthRPC(adapter, "system.coredumps.chunk", params: ["filename": .string(name), "offset": .int(offset), "length": .int(2048)])
            let chunk = try DiagnosticChunk(reply, expectedOffset: offset, expectedSize: total)
            try handle.write(contentsOf: chunk.data)
            offset += chunk.data.count
            total = chunk.size
            if chunk.eof { break }
        } while true
        try handle.close()
        try FileManager.default.moveItem(at: url, to: directory.appendingPathComponent(name))
    }
}

struct DiagnosticChunk {
    let data: Data
    let size: Int
    let eof: Bool

    init(_ reply: Cornucopia.Core.StringAnyCollection, expectedOffset: Int, expectedSize: Int?) throws {
        guard case .bool(true) = reply["success"], case let .int(offset) = reply["offset"], offset == expectedOffset,
              case let .int(size) = reply["size"], size > 0, size <= 16 * 1024 * 1024,
              expectedSize == nil || expectedSize == size,
              case let .int(bytes) = reply["bytes"], bytes > 0, bytes <= 2048,
              case let .int(next) = reply["next_offset"], next == offset + bytes, next <= size,
              case let .bool(eof) = reply["eof"], eof == (next == size),
              case let .string(encoded) = reply["data"], let data = Data(base64Encoded: encoded), data.count == bytes else {
            throw ValidationError("Invalid or incomplete crash dump chunk at offset \(expectedOffset).")
        }
        self.data = data
        self.size = size
        self.eof = eof
    }
}
