import Foundation

/// A human-readable interpretation of a single request or response PDU.
///
/// `Detail` stays structured instead of pre-formatted text so that the same
/// annotation can be rendered to a colored terminal, a plain log or JSON
/// without the annotator having to know which of them is the destination.
struct Annotation {

    enum Severity {
        case request
        case ok
        case pending
        case warning
        case error
    }

    enum Detail {
        case field(label: String, value: String)
        case measurement(label: String, value: String)
        case dtc(code: String, explanation: String?, status: [String])
        case note(String)
    }

    let severity: Severity
    let headline: String
    let details: [Detail]

    init(severity: Severity, headline: String, details: [Detail] = []) {
        self.severity = severity
        self.headline = headline
        self.details = details
    }
}
