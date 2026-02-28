import Foundation
import LineNoise

enum InteractiveHistory {
    private static let maxEntries: UInt = 1000
    private static let historyDirEnv = "ECUCONNECT_TOOL_HISTORY_DIR"

    private static func makeHistoryDirectoryURL() -> URL? {
        let fileManager = FileManager.default
        let env = ProcessInfo.processInfo.environment[historyDirEnv]?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        let baseDirectory: URL
        if env.isEmpty {
            baseDirectory = fileManager.homeDirectoryForCurrentUser
                .appendingPathComponent(".ecuconnect-tool", isDirectory: true)
                .appendingPathComponent("history", isDirectory: true)
        } else {
            let expanded = (env as NSString).expandingTildeInPath
            baseDirectory = URL(fileURLWithPath: expanded, isDirectory: true)
        }

        do {
            try fileManager.createDirectory(at: baseDirectory, withIntermediateDirectories: true, attributes: nil)
            return baseDirectory
        } catch {
            return nil
        }
    }

    static func configure(_ lineNoise: LineNoise, scope: String) -> String? {
        lineNoise.setHistoryMaxLength(maxEntries)
        guard let dirURL = makeHistoryDirectoryURL() else {
            return nil
        }
        let historyPath = dirURL.appendingPathComponent("\(scope).history", isDirectory: false).path
        do {
            try lineNoise.loadHistory(fromFile: historyPath)
        } catch {
            // Ignore first-run/missing-file errors.
        }
        return historyPath
    }

    static func persist(_ lineNoise: LineNoise, historyPath: String?) {
        guard let historyPath else {
            return
        }
        do {
            try lineNoise.saveHistory(toFile: historyPath)
        } catch {
            // Best-effort persistence: keep shell interactive even if disk writes fail.
        }
    }
}
