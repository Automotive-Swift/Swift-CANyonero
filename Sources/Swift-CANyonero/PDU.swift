@_exported import libCANyonero
import CornucopiaCore

extension CANyonero.PDU {

    public static func ping(payload: [UInt8] = []) -> CANyonero.PDU { .ping(payload.stdVector) }

    public func dump() {

        var collection: [UInt8] = .init()
        for data in self.frame() {
            collection.append(data)
        }
        print("Dumping frame of PDU w/ type \(collection[1], radix: .hex, prefix: true, toWidth: 2):")
        print(collection.CC_hexdump(frameDecoration: true))
    }
}
