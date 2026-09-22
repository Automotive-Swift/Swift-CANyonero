import XCTest
@testable import ecuconnect_tool
import Swift_Automotive_Client

/// Replays `Tests/Fixtures/annotations.json` — the same fixtures the Python
/// implementation is tested against, which is what keeps the two in step.
final class AnnotationTests: XCTestCase {

    private struct Fixture: Decodable {
        let name: String
        let framing: String
        let request: String?
        let response: String?
        let expectedRequest: [String]?
        let expectedResponse: [String]?
    }

    private struct Document: Decodable {
        let cases: [Fixture]
    }

    private static var fixtures: [Fixture] = {
        // #filePath is Tests/ecuconnect-tool-Tests/AnnotationTests.swift
        let url = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .appendingPathComponent("Fixtures/annotations.json")
        guard let data = try? Data(contentsOf: url),
              let document = try? JSONDecoder().decode(Document.self, from: data) else {
            return []
        }
        return document.cases
    }()

    private func bytes(from hex: String) -> [UInt8] {
        stride(from: 0, to: hex.count, by: 2).compactMap { offset in
            let start = hex.index(hex.startIndex, offsetBy: offset)
            let end = hex.index(start, offsetBy: 2)
            return UInt8(hex[start..<end], radix: 16)
        }
    }

    func test_fixtures_areLoaded() {
        XCTAssertFalse(Self.fixtures.isEmpty, "Tests/Fixtures/annotations.json could not be read")
    }

    func test_annotations_matchTheSharedFixtures() {
        // Chalk only emits escapes on a TTY, so rendering under XCTest is plain text.
        for fixture in Self.fixtures {
            let framing: MessageAnnotator.Framing = fixture.framing == "raw" ? .rawFrames : .pdu
            let annotator = MessageAnnotator(framing: framing)

            if let request = fixture.request, let expected = fixture.expectedRequest {
                let annotation = annotator.annotate(request: self.bytes(from: request))
                XCTAssertEqual(annotation?.terminalLines ?? [], expected, "request of \(fixture.name)")
            }

            if let response = fixture.response, let expected = fixture.expectedResponse {
                let message = Automotive.Message(
                    addressing: .unicast(id: 0x7E0, reply: 0x7E8),
                    payloadProtocol: .uds,
                    bytes: self.bytes(from: response)
                )
                let annotation = annotator.annotate(response: message)
                XCTAssertEqual(annotation?.terminalLines ?? [], expected, "response of \(fixture.name)")
            }
        }
    }
}
