import XCTest
@testable import ecuconnect_tool

final class DiagnosticBenchmarkTests: XCTestCase {
    func testRequestFixture() {
        XCTAssertEqual(DiagnosticBenchmarkWire.request(size: 8, response: 4096, token: 0x12345678),
                       [0x1f, 0x13, 0, 8, 1, 0, 0x10, 0, 0x12, 0x34, 0x56, 0x78])
    }

    func testResponseValidation() throws {
        let payload: [UInt8] = [0,0,0,42, 0,0,0,10, 0,0,0,20] + (12..<32).map { UInt8(42 + $0) }
        let reply = DiagnosticBenchmarkWire.frame(0x93, payload)
        let (dispatch, prepare) = try DiagnosticBenchmarkWire.validate(reply, size: 32, token: 42)
        XCTAssertEqual(dispatch, 10)
        XCTAssertEqual(prepare, 20)
        XCTAssertThrowsError(try DiagnosticBenchmarkWire.validate(reply, size: 32, token: 43))
        XCTAssertThrowsError(try DiagnosticBenchmarkWire.validate(Array(reply.dropLast()), size: 32, token: 42))
        XCTAssertThrowsError(try DiagnosticBenchmarkWire.validate([0x1f, 0xef, 0, 0], size: 32, token: 42))
    }

    func testStatistics() {
        let result = DiagnosticBenchmarkWire.summary((1...20).map(Double.init))
        XCTAssertEqual(result["median"], 10.5)
        XCTAssertEqual(result["p95"], 19)
    }
}
