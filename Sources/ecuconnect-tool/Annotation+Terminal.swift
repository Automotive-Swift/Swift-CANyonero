import Chalk
import Foundation

extension Annotation.Severity {

    var symbol: String {
        switch self {
            case .request: "»"
            case .ok:      "✓"
            case .pending: "…"
            case .warning: "!"
            case .error:   "✗"
        }
    }

    var color: Chalk.Color {
        switch self {
            case .request: .cyan
            case .ok:      .green
            case .pending: .yellow
            case .warning: .yellow
            case .error:   .red
        }
    }
}

extension Annotation.Detail {

    var rendered: String {
        switch self {
            case .field(let label, let value):
                "\(label, color: .white, style: .dim): \(value)"

            case .measurement(let label, let value):
                "\(label, color: .white, style: .dim): \(value, color: .cyan, style: .bold)"

            case .dtc(let code, let explanation, let status):
                Self.renderedDTC(code: code, explanation: explanation, status: status)

            case .note(let text):
                "\(text, color: .white, style: .dim)"
        }
    }

    private static func renderedDTC(code: String, explanation: String?, status: [String]) -> String {
        var line = "\(code, color: .magenta, style: .bold)"
        if let explanation {
            line += " — \(explanation)"
        }
        guard !status.isEmpty else { return line }
        return line + " \("[\(status.joined(separator: ", "))]", color: .white, style: .dim)"
    }
}

extension Annotation {

    /// Headline prefixed by the severity marker, followed by indented details.
    var terminalLines: [String] {
        var lines = [" \(self.severity.symbol, color: self.severity.color)  \(self.headline, color: self.severity.color)"]
        for detail in self.details {
            lines.append("     \(detail.rendered)")
        }
        return lines
    }

    func print() {
        for line in self.terminalLines {
            Swift.print(line)
        }
    }
}
