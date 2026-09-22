import Foundation
import Swift_Automotive_Client

/// Turns request and response PDUs into human-readable `Annotation`s.
///
/// All protocol knowledge that `Swift-Automotive` already carries — PID names,
/// measurement specs and converters, DTC decoding and explanations — is used
/// from there rather than re-implemented; only the naming tables it does not
/// have yet live in `DiagnosticNames`.
struct MessageAnnotator {

    // MARK: - Requests

    func annotate(request bytes: [UInt8]) -> Annotation? {

        guard let sid = bytes.first else { return nil }
        let parameters = Array(bytes.dropFirst())
        let details = DiagnosticNames.isOBD2Service(sid)
            ? self.obd2RequestDetails(sid: sid, parameters: parameters)
            : self.udsRequestDetails(sid: sid, parameters: parameters)

        return Annotation(
            severity: .request,
            headline: "\(Self.hex(sid)) \(DiagnosticNames.serviceName(sid))",
            details: details
        )
    }

    private func obd2RequestDetails(sid: UInt8, parameters: [UInt8]) -> [Annotation.Detail] {

        switch sid {
            case 0x01:
                // A single request may ask for up to six PIDs at once.
                return parameters.map { .field(label: "PID \(Self.hex($0))", value: Automotive.localizedPIDName(for: $0)) }

            case 0x02:
                guard let pid = parameters.first else { return [] }
                var details: [Annotation.Detail] = [.field(label: "PID \(Self.hex(pid))", value: Automotive.localizedPIDName(for: pid))]
                if parameters.count >= 2 {
                    details.append(.field(label: "Frame", value: Self.hex(parameters[1])))
                }
                return details

            case 0x06:
                return parameters.map { .field(label: "Test ID", value: Self.hex($0)) }

            case 0x09:
                return parameters.map { .field(label: "Info type \(Self.hex($0))", value: DiagnosticNames.vehicleInformationName($0)) }

            default:
                return []
        }
    }

    private func udsRequestDetails(sid: UInt8, parameters: [UInt8]) -> [Annotation.Detail] {

        var details: [Annotation.Detail] = []

        switch sid {
            case 0x22:
                // Read data by identifier accepts a list of two-byte DIDs.
                for index in stride(from: 0, to: parameters.count - 1, by: 2) {
                    let did = UInt16(parameters[index]) << 8 | UInt16(parameters[index + 1])
                    details.append(Self.dataIdentifierDetail(did))
                }

            case 0x2E:
                guard parameters.count >= 2 else { break }
                let did = UInt16(parameters[0]) << 8 | UInt16(parameters[1])
                details.append(Self.dataIdentifierDetail(did))
                let record = Array(parameters.dropFirst(2))
                if !record.isEmpty {
                    details.append(.field(label: "Value", value: Self.renderedRecord(record)))
                }

            case 0x27:
                guard let subFunction = parameters.first else { break }
                let level = (subFunction & 0x7F + 1) / 2
                let step = (subFunction & 0x7F).isMultiple(of: 2) ? "Send key" : "Request seed"
                details.append(.field(label: "Security access", value: "\(step), level \(level)"))

            case 0x31:
                guard let subFunction = parameters.first else { break }
                if let name = Self.subFunctionName(service: sid, value: subFunction) {
                    details.append(.field(label: "Sub-function", value: name))
                }
                if parameters.count >= 3 {
                    let routine = UInt16(parameters[1]) << 8 | UInt16(parameters[2])
                    details.append(.field(label: "Routine", value: "0x\(String(format: "%04X", routine))"))
                }

            case 0x34, 0x35:
                guard parameters.count >= 2 else { break }
                details.append(.field(label: "Data format", value: Self.hex(parameters[0])))
                details.append(.field(label: "Address/length format", value: Self.hex(parameters[1])))

            case 0x36:
                guard let counter = parameters.first else { break }
                details.append(.field(label: "Block sequence counter", value: "\(counter)"))

            default:
                guard let subFunction = parameters.first else { break }
                if let name = Self.subFunctionName(service: sid, value: subFunction) {
                    details.append(.field(label: "Sub-function", value: name))
                }
        }

        // The suppression bit lives in the sub-function byte of every service that has one.
        if let subFunction = parameters.first, subFunction & 0x80 == 0x80, Self.hasSubFunction(sid) {
            details.append(.note("positive response suppressed"))
        }

        return details
    }

    // MARK: - Responses

    func annotate(response message: Automotive.Message) -> Annotation? {

        let bytes = message.bytes
        guard let responseSid = bytes.first else { return nil }

        if responseSid == UDS.NegativeResponse {
            return self.negativeResponseAnnotation(bytes: bytes)
        }

        guard responseSid & 0x40 == 0x40 else {
            return Annotation(
                severity: .warning,
                headline: "Unrecognized payload",
                details: [.note("first byte \(Self.hex(responseSid)) is neither a positive nor a negative response")]
            )
        }

        let sid = responseSid & ~0x40
        let headline = "\(Self.hex(responseSid)) \(DiagnosticNames.serviceName(sid))"
        let details = DiagnosticNames.isOBD2Service(sid)
            ? self.obd2ResponseDetails(sid: sid, message: message)
            : self.udsResponseDetails(sid: sid, bytes: bytes)

        return Annotation(severity: .ok, headline: headline, details: details)
    }

    private func negativeResponseAnnotation(bytes: [UInt8]) -> Annotation {

        guard bytes.count >= 3 else {
            return Annotation(severity: .error, headline: "Negative response (truncated)")
        }

        let service = bytes[1]
        let nrc = bytes[2]
        let isPending = nrc == UDS.NegativeResponseCode.requestCorrectlyReceivedResponsePending.rawValue
        let details: [Annotation.Detail] = [
            .field(label: "Service", value: "\(Self.hex(service)) \(DiagnosticNames.serviceName(service))"),
            .field(label: "NRC \(Self.hex(nrc))", value: DiagnosticNames.negativeResponseName(nrc)),
        ]

        return Annotation(
            severity: isPending ? .pending : .error,
            headline: isPending ? "Response pending" : "Negative response",
            details: details
        )
    }

    private func obd2ResponseDetails(sid: UInt8, message: Automotive.Message) -> [Annotation.Detail] {

        switch sid {
            case 0x03, 0x07, 0x0A:
                let response = OBD2.DTCResponse(message: message)
                return Self.dtcDetails(response.dtc, emptyNote: "no trouble codes reported")

            case 0x04:
                return [.note("trouble codes and freeze frame data cleared")]

            case 0x01, 0x02, 0x06, 0x09:
                return self.measurementDetails(sid: sid, message: message)

            default:
                return []
        }
    }

    private func measurementDetails(sid: UInt8, message: Automotive.Message) -> [Annotation.Detail] {

        guard message.bytes.count >= 2 else { return [] }
        let pid = message.bytes[1]
        let label = switch sid {
            case 0x09: DiagnosticNames.vehicleInformationName(pid)
            case 0x06: "Test \(Self.hex(pid))"
            default:   Automotive.localizedPIDName(for: pid)
        }

        let response = OBD2.GenericResponse(message: message)

        switch response.valueType {
            case .measurement:
                guard let measurement = response.measurement else { break }
                return [.measurement(label: label, value: Self.rendered(measurement))]

            case .string:
                guard let string = response.string, !string.isEmpty else { break }
                return [.measurement(label: label, value: string)]

            case .multiString:
                guard let strings = response.multiString, !strings.isEmpty else { break }
                return strings.map { .measurement(label: label, value: $0) }

            case .pids:
                guard let pids = response.pids, !pids.isEmpty else { break }
                return [.field(label: label, value: pids.map { Self.hex($0) }.joined(separator: " "))]

            case .dtc:
                guard let dtc = response.dtc else { break }
                return Self.dtcDetails([dtc], emptyNote: nil)

            case .invalid, .unknown, .oxygenSensorPositions, .performanceCounters:
                break
        }

        return [.field(label: "Parameter \(Self.hex(pid))", value: label)]
    }

    private func udsResponseDetails(sid: UInt8, bytes: [UInt8]) -> [Annotation.Detail] {

        var details: [Annotation.Detail] = []

        switch sid {
            case 0x19:
                guard bytes.count >= 2 else { break }
                let reportType = bytes[1]
                if let name = Self.subFunctionName(service: sid, value: reportType) {
                    details.append(.field(label: "Report type", value: name))
                }
                details.append(contentsOf: Self.readDTCInformationDetails(reportType: reportType, bytes: bytes))

            case 0x22:
                guard bytes.count >= 3 else { break }
                let did = UInt16(bytes[1]) << 8 | UInt16(bytes[2])
                details.append(Self.dataIdentifierDetail(did))
                let record = Array(bytes.dropFirst(3))
                if !record.isEmpty {
                    details.append(.measurement(label: "Value", value: Self.renderedRecord(record)))
                }

            case 0x2E:
                guard bytes.count >= 3 else { break }
                let did = UInt16(bytes[1]) << 8 | UInt16(bytes[2])
                details.append(Self.dataIdentifierDetail(did))
                details.append(.note("written"))

            case 0x10:
                guard bytes.count >= 2 else { break }
                if let name = Self.subFunctionName(service: sid, value: bytes[1]) {
                    details.append(.field(label: "Session", value: name))
                }
                guard bytes.count >= 6 else { break }
                let p2 = UInt16(bytes[2]) << 8 | UInt16(bytes[3])
                let p2Star = UInt16(bytes[4]) << 8 | UInt16(bytes[5])
                details.append(.field(label: "Timings", value: "P2 = \(p2) ms, P2* = \(Int(p2Star) * 10) ms"))

            case 0x27:
                guard bytes.count >= 2 else { break }
                let subFunction = bytes[1]
                let level = (subFunction + 1) / 2
                if subFunction.isMultiple(of: 2) {
                    details.append(.field(label: "Security access", value: "key accepted, level \(level)"))
                } else {
                    details.append(.field(label: "Security access", value: "seed for level \(level)"))
                    let seed = Array(bytes.dropFirst(2))
                    if !seed.isEmpty {
                        details.append(.field(label: "Seed", value: Self.hexString(seed)))
                    }
                }

            case 0x31:
                guard bytes.count >= 4 else { break }
                if let name = Self.subFunctionName(service: sid, value: bytes[1]) {
                    details.append(.field(label: "Sub-function", value: name))
                }
                let routine = UInt16(bytes[2]) << 8 | UInt16(bytes[3])
                details.append(.field(label: "Routine", value: "0x\(String(format: "%04X", routine))"))
                let status = Array(bytes.dropFirst(4))
                if !status.isEmpty {
                    details.append(.field(label: "Status", value: Self.hexString(status)))
                }

            case 0x34, 0x35:
                guard bytes.count >= 3 else { break }
                let lengthFormat = Int(bytes[1] >> 4)
                guard lengthFormat > 0, bytes.count >= 2 + lengthFormat else { break }
                let blockLength = bytes[2..<(2 + lengthFormat)].reduce(UInt64(0)) { $0 << 8 | UInt64($1) }
                details.append(.field(label: "Max block length", value: "\(blockLength) bytes"))

            case 0x36:
                guard bytes.count >= 2 else { break }
                details.append(.field(label: "Block sequence counter", value: "\(bytes[1])"))

            default:
                guard bytes.count >= 2 else { break }
                if let name = Self.subFunctionName(service: sid, value: bytes[1]) {
                    details.append(.field(label: "Sub-function", value: name))
                }
        }

        return details
    }

    /// ISO 14229-1 service 0x19 — only the report types that carry DTC records
    /// in a self-describing layout are decoded; the rest stay raw.
    private static func readDTCInformationDetails(reportType: UInt8, bytes: [UInt8]) -> [Annotation.Detail] {

        switch reportType {
            case 0x01, 0x07, 0x11, 0x12:
                guard bytes.count >= 6 else { return [] }
                let count = Int(bytes[4]) << 8 | Int(bytes[5])
                return [.field(label: "Matching DTCs", value: "\(count)")]

            case 0x02, 0x0A, 0x15, 0x17:
                var message = Automotive.Message(addressing: .unicast(id: 0, reply: 0), bytes: bytes)
                message.bytes = bytes
                let response = UDS.DTCResponse(message: message)
                return Self.dtcDetails(response.dtc, emptyNote: "no trouble codes reported")

            default:
                return []
        }
    }

    // MARK: - Shared helpers

    private static func dtcDetails(_ dtcs: [Automotive.DTC], emptyNote: String?) -> [Annotation.Detail] {

        guard !dtcs.isEmpty else {
            guard let emptyNote else { return [] }
            return [.note(emptyNote)]
        }

        return dtcs.map { dtc in
            .dtc(
                code: dtc.obd2,
                explanation: DTCExplanations.explanation(for: dtc),
                status: DiagnosticNames.statusFlags(dtc.state)
            )
        }
    }

    private static func dataIdentifierDetail(_ did: UInt16) -> Annotation.Detail {
        let label = "DID 0x\(String(format: "%04X", did))"
        guard let name = DiagnosticNames.dataIdentifierName(did) else {
            return .field(label: label, value: "vendor-specific")
        }
        return .field(label: label, value: name)
    }

    /// Services carrying a sub-function byte, and therefore the suppress-positive-response bit.
    private static func hasSubFunction(_ sid: UInt8) -> Bool {
        switch sid {
            case 0x10, 0x11, 0x19, 0x27, 0x28, 0x29, 0x31, 0x3E, 0x85, 0x86, 0x87: true
            default: false
        }
    }

    private static func subFunctionName(service: UInt8, value: UInt8) -> String? {
        Self.definedSubFunctionName(service: service, value: value)
            ?? Self.definedSubFunctionName(service: service, value: value & 0x7F)
    }

    private static func definedSubFunctionName(service: UInt8, value: UInt8) -> String? {
        switch service {
            case 0x10: UDS.DiagnosticSessionType(rawValue: value).map { String(describing: $0) }
            case 0x11: UDS.EcuResetType(rawValue: value).map { String(describing: $0) }
            case 0x19: UDS.ReadDTCReportType(rawValue: value).map { String(describing: $0) }
            case 0x28: UDS.CommunicationControlType(rawValue: value).map { String(describing: $0) }
            case 0x31: UDS.RoutineControlType(rawValue: value).map { String(describing: $0) }
            case 0x3E: UDS.TesterPresentType(rawValue: value).map { String(describing: $0) }
            case 0x85: UDS.DTCSettingType(rawValue: value).map { String(describing: $0) }
            default:   nil
        }
    }

    /// Prefer a readable string over hex when the record is plausible ASCII.
    private static func renderedRecord(_ bytes: [UInt8]) -> String {
        let printable = bytes.allSatisfy { (0x20...0x7E).contains($0) || $0 == 0x00 }
        guard printable, bytes.contains(where: { (0x20...0x7E).contains($0) }) else {
            return Self.hexString(bytes)
        }
        let text = String(decoding: bytes.filter { $0 != 0x00 }, as: UTF8.self).trimmingCharacters(in: .whitespaces)
        return text.isEmpty ? Self.hexString(bytes) : "\"\(text)\""
    }

    private static func rendered(_ measurement: Measurement<Unit>) -> String {
        let value = String(format: "%.3f", measurement.value)
            .replacingOccurrences(of: #"\.?0+$"#, with: "", options: .regularExpression)
        let symbol = measurement.unit.symbol
        return symbol.isEmpty ? value : "\(value) \(symbol)"
    }

    private static func hex(_ byte: UInt8) -> String { String(format: "%02X", byte) }

    private static func hexString(_ bytes: [UInt8]) -> String {
        bytes.map { String(format: "%02X", $0) }.joined(separator: " ")
    }
}
