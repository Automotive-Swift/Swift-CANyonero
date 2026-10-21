import ArgumentParser
import CoreBluetooth
import Darwin
import Foundation

private final class BLEScanner: NSObject, CBCentralManagerDelegate {
    private let service: CBUUID
    private var manager: CBCentralManager?
    private var startedAt: Date?
    private var failure: String?
    private var devices: [UUID: String] = [:]

    init(service: String) {
        self.service = CBUUID(string: service)
        super.init()
    }

    func scan() throws -> [(UUID, String)] {
        manager = CBCentralManager(delegate: self, queue: .main)
        let readyDeadline = Date().addingTimeInterval(5)
        while startedAt == nil && failure == nil && Date() < readyDeadline {
            RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.05))
        }
        if let failure { throw ValidationError(failure) }
        guard let startedAt else { throw ValidationError("Bluetooth did not become ready for scanning.") }
        while Date().timeIntervalSince(startedAt) < 1 {
            RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.05))
        }
        manager?.stopScan()
        manager?.delegate = nil
        return devices.map { ($0.key, $0.value) }.sorted { $0.0.uuidString < $1.0.uuidString }
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
            case .poweredOn:
                guard startedAt == nil else { return }
                central.scanForPeripherals(withServices: [service])
                startedAt = Date()
            case .poweredOff, .unauthorized, .unsupported:
                failure = "Bluetooth is unavailable (state \(central.state.rawValue))."
            default:
                break
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? peripheral.name ?? "Unnamed adapter"
        devices[peripheral.identifier] = name
    }
}

func selectBLEAdapter(for url: URL) throws -> URL {
    guard url.scheme?.lowercased() == "ecuconnect-l2cap" else { return url }
    if !url.path.isEmpty && url.path != "/" {
        guard url.path.first == "/", UUID(uuidString: String(url.path.dropFirst())) != nil else {
            throw ValidationError("BLE endpoint needs a valid peripheral UUID in its path.")
        }
        return url
    }
    guard let service = url.host, !service.isEmpty else { throw ValidationError("BLE endpoint needs a service UUID.") }
    fputs("Scanning for BLE adapters advertising \(service) (1 second)...\n", stderr)
    let devices = try BLEScanner(service: service).scan()
    guard !devices.isEmpty else {
        throw ValidationError("No BLE adapter advertising \(service) was found. Check power and range.")
    }

    var selected = devices[0].0
    if devices.count > 1 {
        for (index, device) in devices.enumerated() {
            fputs("  \(index + 1). \(device.1) (\(device.0.uuidString))\n", stderr)
        }
        guard isatty(STDIN_FILENO) != 0 else {
            throw ValidationError("Multiple BLE adapters found. Pass --url with /<peripheral-uuid> to select one without a terminal.")
        }
        while true {
            fputs("Select adapter [1-\(devices.count)]: ", stderr)
            guard let answer = readLine() else { throw ValidationError("Adapter selection cancelled.") }
            if let index = Int(answer), (1...devices.count).contains(index) {
                selected = devices[index - 1].0
                break
            }
            fputs("Enter a number from the list.\n", stderr)
        }
    }
    var components = URLComponents(url: url, resolvingAgainstBaseURL: false)!
    components.path = "/\(selected.uuidString)"
    guard let resolved = components.url else { throw ValidationError("Invalid BLE endpoint URL.") }
    return resolved
}
