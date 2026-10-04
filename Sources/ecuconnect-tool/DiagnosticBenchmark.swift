import Foundation

/// Wire format shared with testing/ble_diagnostics in ECUconnect firmware.
enum DiagnosticBenchmarkWire {
    static func frame(_ type: UInt8, _ payload: [UInt8] = []) -> [UInt8] {
        [0x1f, type, UInt8(payload.count >> 8), UInt8(payload.count & 255)] + payload
    }

    static func request(size: Int, response: Int, token: UInt32) -> [UInt8] {
        frame(0x13, [1, 0, UInt8(response >> 8), UInt8(response & 255)] +
              (0..<4).map { UInt8(truncatingIfNeeded: token >> (24 - 8 * $0)) } +
              Array(repeating: 0, count: size - 8))
    }

    static func validate(_ reply: [UInt8], size: Int, token: UInt32) throws -> (UInt32, UInt32) {
        if reply.count >= 2 && reply[1] == 0xef {
            throw Failure("Firmware needs CONFIG_ECOS_DIAGNOSTIC_BENCHMARK (S31 debug build).")
        }
        guard reply.count == size + 4, reply.prefix(2) == [0x1f, 0x93],
              Int(reply[2]) * 256 + Int(reply[3]) == size else { throw Failure("Invalid response frame") }
        func word(_ offset: Int) -> UInt32 {
            reply[offset..<offset+4].reduce(UInt32(0)) { ($0 << 8) | UInt32($1) }
        }
        guard word(4) == token,
              (12..<size).allSatisfy({ reply[$0+4] == UInt8(truncatingIfNeeded: token &+ UInt32($0)) }) else {
            throw Failure("Response token or payload mismatch")
        }
        return (word(8), word(12))
    }

    struct Failure: Error, CustomStringConvertible {
        let description: String
        init(_ description: String) { self.description = description }
    }

    static func summary(_ values: [Double]) -> [String: Double] {
        let sorted = values.sorted()
        let n = sorted.count
        return ["mean": values.reduce(0, +) / Double(n),
                "median": n % 2 == 0 ? (sorted[n/2-1] + sorted[n/2])/2 : sorted[n/2],
                "p95": sorted[Int(ceil(Double(n)*0.95))-1], "minimum": sorted[0], "maximum": sorted[n-1]]
    }
}

#if canImport(CoreBluetooth)
import CoreBluetooth

/// A dedicated CoC session exposes first stream delivery as well as full-frame timing.
/// All delegate callbacks and the run loop execute on the command's main thread.
private final class DiagnosticBLESession: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate, StreamDelegate {
    private var central: CBCentralManager!
    private var peer: CBPeripheral?
    private var channel: CBL2CAPChannel?
    private let service: CBUUID
    private let psm: CBL2CAPPSM
    private var failure: Error?
    private var received: [UInt8] = []
    private var firstRX: UInt64?
    private var lastRX: UInt64 = 0
    var peerIdentifier: String { peer?.identifier.uuidString ?? "" }

    init(service: String, psm: UInt16) {
        self.service = CBUUID(string: service)
        self.psm = CBL2CAPPSM(psm)
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
    }

    private func wait(seconds: Double, until ready: () -> Bool) throws {
        let deadline = ProcessInfo.processInfo.systemUptime + seconds
        while !ready() {
            if let failure { throw failure }
            guard ProcessInfo.processInfo.systemUptime < deadline else {
                throw DiagnosticBenchmarkWire.Failure("BLE operation timed out")
            }
            RunLoop.current.run(until: Date(timeIntervalSinceNow: 0.001))
        }
        if let failure { throw failure }
    }

    func connect() throws {
        try wait(seconds: 20) { channel != nil }
        guard let channel else { throw DiagnosticBenchmarkWire.Failure("No L2CAP channel") }
        for stream: Stream in [channel.inputStream, channel.outputStream] {
            stream.delegate = self
            stream.schedule(in: .main, forMode: .default)
            stream.open()
        }
        try wait(seconds: 5) { channel.outputStream.hasSpaceAvailable }
    }

    func close() {
        central.stopScan()
        if let channel {
            for stream: Stream in [channel.inputStream, channel.outputStream] {
                stream.close()
                stream.remove(from: .main, forMode: .default)
                stream.delegate = nil
            }
        }
        if let peer { central.cancelPeripheralConnection(peer) }
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn {
            central.scanForPeripherals(withServices: [service])
        } else if central.state != .unknown && central.state != .resetting {
            failure = DiagnosticBenchmarkWire.Failure("Bluetooth unavailable: \(central.state.rawValue)")
        }
    }
    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        guard peer == nil else { return }
        peer = peripheral // First matching service, as with the existing benchmark.
        central.stopScan()
        peripheral.delegate = self
        central.connect(peripheral)
    }
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        peripheral.openL2CAPChannel(psm)
    }
    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        failure = error ?? DiagnosticBenchmarkWire.Failure("BLE connection failed")
    }
    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        failure = error ?? DiagnosticBenchmarkWire.Failure("BLE disconnected")
    }
    func peripheral(_ peripheral: CBPeripheral, didOpen channel: CBL2CAPChannel?, error: Error?) {
        if let channel { self.channel = channel }
        else { failure = error ?? DiagnosticBenchmarkWire.Failure("L2CAP channel failed") }
    }
    func stream(_ aStream: Stream, handle eventCode: Stream.Event) {
        if eventCode.contains(.errorOccurred) || eventCode.contains(.endEncountered) {
            failure = aStream.streamError ?? DiagnosticBenchmarkWire.Failure("BLE stream closed")
        }
        guard eventCode.contains(.hasBytesAvailable), let input = aStream as? InputStream else { return }
        var buffer = [UInt8](repeating: 0, count: 65539)
        while input.hasBytesAvailable {
            let count = input.read(&buffer, maxLength: buffer.count)
            guard count > 0 else {
                if count < 0 { failure = input.streamError ?? DiagnosticBenchmarkWire.Failure("BLE read failed") }
                break
            }
            let now = DispatchTime.now().uptimeNanoseconds
            if firstRX == nil { firstRX = now }
            lastRX = now
            received.append(contentsOf: buffer.prefix(count))
            if received.count > 5000 { failure = DiagnosticBenchmarkWire.Failure("Oversized response"); break }
        }
    }

    func exchange(_ bytes: [UInt8]) throws -> ([UInt8], Double, Double) {
        guard let output = channel?.outputStream else { throw DiagnosticBenchmarkWire.Failure("Not connected") }
        guard received.isEmpty else { throw DiagnosticBenchmarkWire.Failure("Unsolicited response") }
        firstRX = nil
        let start = DispatchTime.now().uptimeNanoseconds
        var offset = 0
        try wait(seconds: 5) {
            if output.hasSpaceAvailable && offset < bytes.count {
                let count = bytes.withUnsafeBufferPointer { output.write($0.baseAddress! + offset, maxLength: bytes.count-offset) }
                if count < 0 { failure = output.streamError ?? DiagnosticBenchmarkWire.Failure("BLE write failed") }
                else { offset += count }
            }
            return offset == bytes.count
        }
        try wait(seconds: 5) {
            if received.count >= 4 {
                let length = Int(received[2])*256 + Int(received[3]) + 4
                if received[0] != 0x1f || length > 5000 || received.count > length {
                    failure = DiagnosticBenchmarkWire.Failure("Invalid response framing")
                    return true
                }
                return received.count == length
            }
            return false
        }
        let reply = received
        received.removeAll(keepingCapacity: true)
        return (reply, Double(firstRX! - start)/1e6, Double(lastRX - start)/1e6)
    }
}
#endif

func runDiagnosticBenchmark(url: URL, serial: String, count: Int, warmup: Double, output: String, responseSizes: [Int]) throws {
#if canImport(CoreBluetooth)
    guard ["ecuconnect-l2cap", "ble"].contains(url.scheme?.lowercased() ?? ""),
          let service = url.host, let psm = UInt16(exactly: url.port ?? 129), psm > 0,
          url.path.isEmpty || url.path == "/" else {
        throw DiagnosticBenchmarkWire.Failure("Use ecuconnect-l2cap://FFF1:129 (first matching adapter).")
    }
    let session = DiagnosticBLESession(service: service, psm: psm)
    defer { session.close() }
    let started = DispatchTime.now().uptimeNanoseconds
    try session.connect()
    let (identity, _, _) = try session.exchange(DiagnosticBenchmarkWire.frame(0x11))
    guard identity.count >= 4, identity[1] == 0x91,
          let text = String(bytes: identity.dropFirst(4), encoding: .utf8) else {
        throw DiagnosticBenchmarkWire.Failure("Invalid adapter identity")
    }
    let fields = text.components(separatedBy: "\n")
    guard fields.count == 5, fields[3].caseInsensitiveCompare(serial) == .orderedSame else {
        throw DiagnosticBenchmarkWire.Failure("Wrong adapter: \(text). Expected serial \(serial).")
    }
    let info = Dictionary(uniqueKeysWithValues: zip(["vendor","model","hardware","serial","firmware"], fields))
    var results: [String: Any] = ["identity": info, "endpoint": url.absoluteString,
        "peer_uuid": session.peerIdentifier, "platform": ProcessInfo.processInfo.operatingSystemVersionString,
        "count": count, "warmup_seconds": warmup, "response_sizes": responseSizes,
        "connect_and_identify_ms": Double(DispatchTime.now().uptimeNanoseconds-started)/1e6,
        "complete": false]
    var samples: [[String: Any]] = []
    var groups: [[String: Any]] = []
    defer {
        results["samples"] = samples
        results["groups"] = groups
        do {
            let data = try JSONSerialization.data(withJSONObject: results, options: [.prettyPrinted, .sortedKeys])
            try data.write(to: URL(fileURLWithPath: output), options: .atomic)
            print("Saved: \(output)")
        } catch { fputs("Cannot save benchmark: \(error)\n", stderr) }
    }
    print("Adapter: \(info); peer \(session.peerIdentifier)")
    print("First RX = first stream read delivery. PHY/interval are controlled by macOS; verify firmware BLELink logs.")
    var token: UInt32 = 0
    let until = ProcessInfo.processInfo.systemUptime + warmup
    repeat {
        token &+= 1
        let (reply, _, _) = try session.exchange(DiagnosticBenchmarkWire.request(size: 8, response: 32, token: token))
        _ = try DiagnosticBenchmarkWire.validate(reply, size: 32, token: token)
        if ProcessInfo.processInfo.systemUptime >= until { break }
        RunLoop.current.run(until: Date(timeIntervalSinceNow: 0.05))
    } while true
    print("REQ RESP FIRST median/p95 ms COMPLETE median/p95 ms RESPONSE KiB/s")
    for q in [8, 32] {
        for r in responseSizes {
            var firsts: [Double] = [], completes: [Double] = []
            for _ in 0..<count {
                token &+= 1
                let (reply, first, complete) = try session.exchange(DiagnosticBenchmarkWire.request(size: q, response: r, token: token))
                let (dispatch, prepare) = try DiagnosticBenchmarkWire.validate(reply, size: r, token: token)
                firsts.append(first); completes.append(complete)
                samples.append(["request_bytes": q, "response_bytes": r, "token": token,
                    "first_ms": first, "complete_ms": complete,
                    "firmware_dispatch_us": dispatch, "firmware_prepare_us": prepare])
            }
            let first = DiagnosticBenchmarkWire.summary(firsts), full = DiagnosticBenchmarkWire.summary(completes)
            let rate = Double(r)/(full["mean"]!/1000)/1024
            groups.append(["request_bytes": q, "response_bytes": r, "first_ms": first, "complete_ms": full, "response_kib_s": rate])
            print(String(format: "%3d %4d %8.2f/%8.2f %8.2f/%8.2f %8.2f", q, r,
                         first["median"]!, first["p95"]!, full["median"]!, full["p95"]!, rate))
        }
    }
    results["complete"] = true
#else
    throw DiagnosticBenchmarkWire.Failure("The Swift diagnostic BLE benchmark requires macOS/CoreBluetooth. Use the Python runner on Linux.")
#endif
}
