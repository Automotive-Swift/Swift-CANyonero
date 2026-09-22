import Foundation
import Swift_Automotive_Client

/// Naming tables for things ISO 14229/15031 defines but that `Swift-Automotive`
/// does not (yet) carry as localizable text: service names, negative response
/// codes, data identifiers and mode-09 info types.
///
/// These belong into the shared annotation data package eventually — see
/// `Swift-Automotive/docs/ANNOTATION_LIBRARY_PLAN.md`. Everything that *is* already available
/// (PID names, measurement specs, DTC explanations) is deliberately not
/// duplicated here.
enum DiagnosticNames {

    /// OBD2 occupies services 0x01…0x0A; above that we are in UDS/KWP territory.
    static func isOBD2Service(_ sid: UInt8) -> Bool { (0x01...0x0A).contains(sid) }

    static func serviceName(_ sid: UInt8) -> String {
        switch sid {
            // SAE J1979 (OBD2)
            case 0x01: "Show Current Data"
            case 0x02: "Show Freeze Frame Data"
            case 0x03: "Show Stored DTCs"
            case 0x04: "Clear DTCs"
            case 0x05: "Oxygen Sensor Monitoring Test Results"
            case 0x06: "On-Board Monitoring Test Results"
            case 0x07: "Show Pending DTCs"
            case 0x08: "Control On-Board System"
            case 0x09: "Request Vehicle Information"
            case 0x0A: "Show Permanent DTCs"

            // ISO 14229 (UDS)
            case 0x10: "Diagnostic Session Control"
            case 0x11: "ECU Reset"
            case 0x14: "Clear Diagnostic Information"
            case 0x19: "Read DTC Information"
            case 0x22: "Read Data By Identifier"
            case 0x23: "Read Memory By Address"
            case 0x24: "Read Scaling Data By Identifier"
            case 0x27: "Security Access"
            case 0x28: "Communication Control"
            case 0x29: "Authentication"
            case 0x2A: "Read Data By Periodic Identifier"
            case 0x2C: "Dynamically Define Data Identifier"
            case 0x2E: "Write Data By Identifier"
            case 0x2F: "Input/Output Control By Identifier"
            case 0x31: "Routine Control"
            case 0x34: "Request Download"
            case 0x35: "Request Upload"
            case 0x36: "Transfer Data"
            case 0x37: "Request Transfer Exit"
            case 0x38: "Request File Transfer"
            case 0x3D: "Write Memory By Address"
            case 0x3E: "Tester Present"
            case 0x84: "Secured Data Transmission"
            case 0x85: "Control DTC Setting"
            case 0x86: "Response On Event"
            case 0x87: "Link Control"

            // ISO 14230 (KWP2000)
            case 0x12: "Read Freeze Frame Data (KWP)"
            case 0x13: "Read Diagnostic Trouble Codes (KWP)"
            case 0x17: "Read Status Of DTCs (KWP)"
            case 0x18: "Read DTCs By Status (KWP)"
            case 0x1A: "Read ECU Identification (KWP)"
            case 0x20: "Stop Diagnostic Session (KWP)"
            case 0x21: "Read Data By Local Identifier (KWP)"
            case 0x30: "Input/Output Control By Local Identifier (KWP)"
            case 0x32: "Stop Routine By Local Identifier (KWP)"
            case 0x33: "Request Routine Results By Local Identifier (KWP)"

            default: "Service 0x\(String(format: "%02X", sid))"
        }
    }

    /// ISO 14229-1 Annex A.1, extended by the KWP2000 and vendor codes that
    /// `UDS.NegativeResponseCode` already models.
    static func negativeResponseName(_ nrc: UInt8) -> String {
        switch nrc {
            case 0x10: "General Reject"
            case 0x11: "Service Not Supported"
            case 0x12: "Sub-Function Not Supported"
            case 0x13: "Incorrect Message Length Or Invalid Format"
            case 0x14: "Response Too Long"
            case 0x21: "Busy — Repeat Request"
            case 0x22: "Conditions Not Correct"
            case 0x23: "Routine Not Complete Or Service In Process"
            case 0x24: "Request Sequence Error"
            case 0x25: "No Response From Subnet Component"
            case 0x26: "Failure Prevents Execution Of Requested Action"
            case 0x31: "Request Out Of Range"
            case 0x33: "Security Access Denied"
            case 0x34: "Authentication Required"
            case 0x35: "Invalid Key"
            case 0x36: "Exceeded Number Of Attempts"
            case 0x37: "Required Time Delay Not Expired"
            case 0x38: "Secure Data Transmission Required"
            case 0x39: "Secure Data Transmission Not Allowed"
            case 0x3A: "Secure Data Verification Failed"
            case 0x40: "Download Not Accepted"
            case 0x41: "Improper Download Type"
            case 0x42: "Cannot Download To Specified Address"
            case 0x43: "Cannot Download Number Of Bytes Requested"
            case 0x50: "Upload Not Accepted"
            case 0x51: "Improper Upload Type"
            case 0x52: "Cannot Upload From Specified Address"
            case 0x53: "Cannot Upload Number Of Bytes Requested"
            case 0x70: "Upload/Download Not Accepted"
            case 0x71: "Transfer Data Suspended"
            case 0x72: "General Programming Failure"
            case 0x73: "Wrong Block Sequence Counter"
            case 0x77: "Block Transfer Data Checksum Error"
            case 0x78: "Request Correctly Received — Response Pending"
            case 0x7E: "Sub-Function Not Supported In Active Session"
            case 0x7F: "Service Not Supported In Active Session"
            case 0x80: "Service Not Supported In Active Diagnostic Mode"
            case 0x81: "RPM Too High"
            case 0x82: "RPM Too Low"
            case 0x83: "Engine Is Running"
            case 0x84: "Engine Is Not Running"
            case 0x85: "Engine Run Time Too Low"
            case 0x86: "Temperature Too High"
            case 0x87: "Temperature Too Low"
            case 0x88: "Vehicle Speed Too High"
            case 0x89: "Vehicle Speed Too Low"
            case 0x8A: "Throttle/Pedal Too High"
            case 0x8B: "Throttle/Pedal Too Low"
            case 0x8C: "Transmission Range Not In Neutral"
            case 0x8D: "Transmission Range Not In Gear"
            case 0x8F: "Brake Switch Not Closed"
            case 0x90: "Shifter Lever Not In Park"
            case 0x91: "Torque Converter Clutch Locked"
            case 0x92: "Voltage Too High"
            case 0x93: "Voltage Too Low"
            case 0xF1: "Gateway Locked Communication"
            case 0xFA: "Checksum Error"
            case 0xFB: "ECU Erasing Flash"
            case 0xFC: "ECU Programming Flash"
            case 0xFD: "Erasing Error"
            case 0xFE: "Programming Error"
            default:   "Unknown NRC"
        }
    }

    /// SAE J1979 mode 09 info types. The regular PID table does not apply here,
    /// because mode 09 reuses the same numbers for different meanings.
    static func vehicleInformationName(_ infoType: UInt8) -> String {
        switch infoType {
            case 0x00: "Supported Info Types (0x01-0x20)"
            case 0x01: "VIN Message Count"
            case 0x02: "Vehicle Identification Number (VIN)"
            case 0x03: "Calibration ID Message Count"
            case 0x04: "Calibration IDs"
            case 0x05: "Calibration Verification Number Message Count"
            case 0x06: "Calibration Verification Numbers (CVN)"
            case 0x07: "In-Use Performance Tracking Message Count"
            case 0x08: "In-Use Performance Tracking (spark ignition)"
            case 0x09: "ECU Name Message Count"
            case 0x0A: "ECU Name"
            case 0x0B: "In-Use Performance Tracking (compression ignition)"
            case 0x0D: "ECU Serial Number"
            case 0x0F: "Exhaust Regulation Or Type Approval Number"
            case 0x20: "Supported Info Types (0x21-0x40)"
            default:   "Info Type 0x\(String(format: "%02X", infoType))"
        }
    }

    /// ISO 14229-1 Annex C — the standardized data identifiers. Vendor-specific
    /// ranges are reported as a range hint rather than a name.
    static func dataIdentifierName(_ did: UInt16) -> String? {
        if let name = Self.standardDataIdentifiers[did] { return name }

        switch did {
            case 0xF1A0...0xF1EF: return "Vehicle manufacturer specific"
            case 0xF1F0...0xF1FF: return "System supplier specific"
            case 0xF200...0xF2FF: return "Periodic data identifier"
            case 0xF300...0xF3FF: return "Dynamically defined data identifier"
            case 0xF400...0xF5FF: return "OBD data identifier"
            case 0xF600...0xF6FF: return "OBD monitor data identifier"
            case 0xF700...0xF7FF: return "OBD auxiliary data identifier"
            case 0xF800...0xF8FF: return "OBD info type data identifier"
            case 0xF900...0xF9FF: return "Tachograph data identifier"
            case 0xFA00...0xFA0F: return "Airbag deployment data identifier"
            default: return nil
        }
    }

    private static let standardDataIdentifiers: [UInt16: String] = [
        0xF180: "Boot software identification",
        0xF181: "Application software identification",
        0xF182: "Application data identification",
        0xF183: "Boot software fingerprint",
        0xF184: "Application software fingerprint",
        0xF185: "Application data fingerprint",
        0xF186: "Active diagnostic session",
        0xF187: "Vehicle manufacturer spare part number",
        0xF188: "Vehicle manufacturer ECU software number",
        0xF189: "Vehicle manufacturer ECU software version number",
        0xF18A: "System supplier identifier",
        0xF18B: "ECU manufacturing date",
        0xF18C: "ECU serial number",
        0xF18D: "Supported functional units",
        0xF18E: "Vehicle manufacturer kit assembly part number",
        0xF18F: "Regulation X software identification numbers",
        0xF190: "VIN",
        0xF191: "Vehicle manufacturer ECU hardware number",
        0xF192: "System supplier ECU hardware number",
        0xF193: "System supplier ECU hardware version number",
        0xF194: "System supplier ECU software number",
        0xF195: "System supplier ECU software version number",
        0xF196: "Exhaust regulation or type approval number",
        0xF197: "System name or engine type",
        0xF198: "Repair shop code or tester serial number",
        0xF199: "Programming date",
        0xF19A: "Calibration repair shop code",
        0xF19B: "Calibration date",
        0xF19C: "Calibration equipment software number",
        0xF19D: "ECU installation date",
        0xF19E: "ASAM/ODX file identifier",
        0xF19F: "Entity",
        0xFA10: "Number of EDR devices",
        0xFA11: "EDR identification",
        0xFA12: "EDR device address information",
        0xFF00: "UDS version",
    ]

    /// Expand a DTC status byte (ISO 14229-1 Annex D) into short labels.
    static func statusFlags(_ mask: Automotive.DTC.StatusMask) -> [String] {
        var flags: [String] = []
        if mask.contains(.testFailed) { flags.append("testFailed") }
        if mask.contains(.testFailedThisOperationCycle) { flags.append("testFailedThisCycle") }
        if mask.contains(.pendingDTC) { flags.append("pending") }
        if mask.contains(.confirmedDTC) { flags.append("confirmed") }
        if mask.contains(.testNotCompletedSinceLastClear) { flags.append("testNotCompletedSinceClear") }
        if mask.contains(.testFailedSinceLastClear) { flags.append("testFailedSinceClear") }
        if mask.contains(.testNotCompletedThisOperationCycle) { flags.append("testNotCompletedThisCycle") }
        if mask.contains(.warningIndicatorRequested) { flags.append("warningIndicatorRequested") }
        return flags
    }
}
