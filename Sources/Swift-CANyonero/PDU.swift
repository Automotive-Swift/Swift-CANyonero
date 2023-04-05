@_exported import libCANyonero

extension CANyonero.PDU {

    public static func ping(payload: [UInt8] = []) -> CANyonero.PDU { .ping(payload.stdVector) }
}
