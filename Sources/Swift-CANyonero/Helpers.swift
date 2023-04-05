import libCANyonero

extension Array<UInt8> {

    var stdVector: CANyonero.StdVectorOfUInt8 {
        self.withUnsafeBufferPointer { ptr in
            createVector8FromArray(ptr.baseAddress, self.count)
        }
    }
}

