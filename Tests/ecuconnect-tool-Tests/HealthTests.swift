import XCTest
import ArgumentParser
import CornucopiaCore
@testable import ecuconnect_tool

final class HealthTests: XCTestCase {
    func testFullHealthFixtureAndUnsupportedSchema() throws {
        let fixture = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .deletingLastPathComponent().appendingPathComponent("Fixtures/health.json")
        let data = try Data(contentsOf: fixture)
        let snapshot = try HealthSnapshot.decode(data)
        XCTAssertEqual(snapshot.schema, 1)
        XCTAssertEqual(snapshot.firmware, "0.9.536")
        XCTAssertEqual(snapshot.allocation_failures?.count, 0)
        XCTAssertEqual(snapshot.coredump_count, 2)
        XCTAssertTrue(snapshot.summary.contains("PSRAM:"))
        XCTAssertTrue(snapshot.summary.contains("ELF SHA-256:"))
        XCTAssertTrue(snapshot.summary.contains("Allocation failures: 0"))
        var object = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        object["schema"] = 2
        XCTAssertThrowsError(try HealthSnapshot.decode(JSONSerialization.data(withJSONObject: object)))
        object.removeValue(forKey: "schema")
        XCTAssertThrowsError(try HealthSnapshot.decode(JSONSerialization.data(withJSONObject: object)))
    }

    func testWatchValidation() throws {
        XCTAssertThrowsError(try Health.parse(["--count", "2"]))
        XCTAssertThrowsError(try Health.parse(["--watch", "--interval", "nan"]))
        XCTAssertThrowsError(try Health.parse(["--watch", "--interval", "0"]))
        let parsed = try Health.parse(["--watch", "--count", "2", "--json"])
        XCTAssertEqual(parsed.count, 2)
        XCTAssertTrue(parsed.json)
    }

    func testFrozenHistoryAndStalledCursor() throws {
        var cursor = HealthEventCursor()
        XCTAssertTrue(try cursor.advance(["through": .int(10), "next": .int(8), "more": .bool(true), "events": .array([])]))
        XCTAssertEqual(cursor.through, 10)
        XCTAssertThrowsError(try cursor.advance(["through": .int(11), "next": .int(10), "more": .bool(false), "events": .array([])]))
        XCTAssertThrowsError(try cursor.advance(["through": .int(10), "next": .int(8), "more": .bool(true), "events": .array([])]))
        XCTAssertFalse(try cursor.advance(["through": .int(10), "next": .int(10), "more": .bool(false), "events": .array([])]))
    }

    func testDumpPathMustRemainInsideBundle() throws {
        XCTAssertEqual(try validatedDumpName("coredump-1.bin"), "coredump-1.bin")
        for name in ["../coredump-1.bin", "coredump-../../x.bin", "coredump-x\\y.bin", "firmware.bin"] {
            XCTAssertThrowsError(try validatedDumpName(name))
        }
    }

    func testChunkRejectsCorruptionAndMissingBytes() throws {
        let valid: Cornucopia.Core.StringAnyCollection = ["success": .bool(true), "offset": .int(0), "size": .int(3),
            "bytes": .int(3), "next_offset": .int(3), "eof": .bool(true), "data": .string("YWJj")]
        XCTAssertEqual(try DiagnosticChunk(valid, expectedOffset: 0, expectedSize: nil).data, Data("abc".utf8))
        XCTAssertThrowsError(try DiagnosticChunk(valid, expectedOffset: 1, expectedSize: nil))
        XCTAssertThrowsError(try DiagnosticChunk(valid, expectedOffset: 0, expectedSize: 4))
        for change: Cornucopia.Core.StringAnyCollection in [["data": .string("YQ==")], ["data": .string("???")],
                                                           ["eof": .bool(false)], ["next_offset": .int(2)]] {
            XCTAssertThrowsError(try DiagnosticChunk(valid.merging(change) { _, new in new }, expectedOffset: 0, expectedSize: nil))
        }
    }
}
