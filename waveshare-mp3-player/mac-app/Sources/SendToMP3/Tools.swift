import Foundation

struct AppError: LocalizedError {
    let message: String
    init(_ message: String) { self.message = message }
    var errorDescription: String? { message }
}

struct ToolPaths {
    let ytdlp: URL
    let ffmpeg: URL
}

/// Finds yt-dlp / ffmpeg / deno (MacYTDL and Homebrew put them in /usr/local/bin or
/// /opt/homebrew/bin) and downloads private copies only when they are missing.
enum Tools {
    static let searchFolders = ["/opt/homebrew/bin", "/usr/local/bin"]

    private static var isAppleSilicon: Bool {
        #if arch(arm64)
        return true
        #else
        return false
        #endif
    }

    // Official yt-dlp universal macOS build.
    static let ytdlpURL = URL(string: "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_macos")!

    // Static ffmpeg builds (NOT verified from the Windows dev machine; if this link
    // ever breaks, installing MacYTDL or `brew install ffmpeg` also satisfies it).
    static var ffmpegURL: URL {
        URL(string: "https://ffmpeg.martin-riedl.de/redirect/latest/macos/\(isAppleSilicon ? "arm64" : "amd64")/release/ffmpeg.zip")!
    }

    // yt-dlp needs a JavaScript runtime for full YouTube support; it finds deno on PATH.
    static var denoURL: URL {
        URL(string: "https://github.com/denoland/deno/releases/latest/download/deno-\(isAppleSilicon ? "aarch64" : "x86_64")-apple-darwin.zip")!
    }

    /// PATH given to yt-dlp so it can find ffmpeg and deno.
    static var processPATH: String {
        ([PlayerConfig.toolsFolder.path] + searchFolders + ["/usr/bin", "/bin"]).joined(separator: ":")
    }

    static func find(_ name: String) -> URL? {
        for folder in [PlayerConfig.toolsFolder.path] + searchFolders {
            let candidate = URL(fileURLWithPath: folder).appendingPathComponent(name)
            if FileManager.default.isExecutableFile(atPath: candidate.path) { return candidate }
        }
        return nil
    }

    static func ensureInstalled(status: @escaping @MainActor (String) -> Void) async throws -> ToolPaths {
        try FileManager.default.createDirectory(at: PlayerConfig.toolsFolder, withIntermediateDirectories: true)

        let ytdlp: URL
        if let found = find("yt-dlp") {
            ytdlp = found
        } else {
            await status("First run: downloading the song downloader…")
            ytdlp = try await install(from: ytdlpURL, zipped: false, name: "yt-dlp")
        }

        let ffmpeg: URL
        if let found = find("ffmpeg") {
            ffmpeg = found
        } else {
            await status("First run: downloading the MP3 converter…")
            do {
                ffmpeg = try await install(from: ffmpegURL, zipped: true, name: "ffmpeg")
            } catch {
                throw AppError("Couldn't get ffmpeg (the MP3 converter). Install MacYTDL once, then try again.")
            }
        }

        if find("deno") == nil {
            await status("First run: downloading a helper for YouTube…")
            _ = try? await install(from: denoURL, zipped: true, name: "deno")  // optional
        }
        return ToolPaths(ytdlp: ytdlp, ffmpeg: ffmpeg)
    }

    private static func install(from url: URL, zipped: Bool, name: String) async throws -> URL {
        let (downloaded, response) = try await URLSession.shared.download(from: url)
        guard let http = response as? HTTPURLResponse, http.statusCode == 200 else {
            throw AppError("Download of \(name) failed.")
        }
        let target = PlayerConfig.toolsFolder.appendingPathComponent(name)
        try? FileManager.default.removeItem(at: target)

        if zipped {
            let unzipDir = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
            defer { try? FileManager.default.removeItem(at: unzipDir) }
            let code = try await ProcessRunner.run(
                URL(fileURLWithPath: "/usr/bin/ditto"), ["-x", "-k", downloaded.path, unzipDir.path])
            guard code == 0 else { throw AppError("Couldn't unzip \(name).") }
            guard let inner = FileManager.default.enumerator(at: unzipDir, includingPropertiesForKeys: nil)?
                .compactMap({ $0 as? URL })
                .first(where: { $0.lastPathComponent == name }) else {
                throw AppError("\(name) was not inside the download.")
            }
            try FileManager.default.moveItem(at: inner, to: target)
        } else {
            try FileManager.default.moveItem(at: downloaded, to: target)
        }
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: target.path)
        _ = try? await ProcessRunner.run(
            URL(fileURLWithPath: "/usr/bin/xattr"), ["-d", "com.apple.quarantine", target.path])
        return target
    }
}

/// Runs a command line tool and streams its output line by line.
enum ProcessRunner {
    @discardableResult
    static func run(_ executable: URL, _ arguments: [String],
                    environment: [String: String]? = nil,
                    onLine: (@Sendable (String) -> Void)? = nil) async throws -> Int32 {
        let process = Process()
        process.executableURL = executable
        process.arguments = arguments
        if let environment {
            process.environment = ProcessInfo.processInfo.environment.merging(environment) { _, new in new }
        }
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = pipe

        let buffer = LineBuffer(onLine: onLine)
        pipe.fileHandleForReading.readabilityHandler = { handle in
            let data = handle.availableData
            if data.isEmpty { return }
            buffer.append(data)
        }

        return try await withCheckedThrowingContinuation { continuation in
            process.terminationHandler = { finished in
                pipe.fileHandleForReading.readabilityHandler = nil
                buffer.append(pipe.fileHandleForReading.readDataToEndOfFile())
                buffer.flush()
                continuation.resume(returning: finished.terminationStatus)
            }
            do {
                try process.run()
            } catch {
                process.terminationHandler = nil
                continuation.resume(throwing: error)
            }
        }
    }
}

private final class LineBuffer: @unchecked Sendable {
    private var pending = Data()
    private let lock = NSLock()
    private let onLine: (@Sendable (String) -> Void)?

    init(onLine: (@Sendable (String) -> Void)?) { self.onLine = onLine }

    func append(_ data: Data) {
        lock.lock(); defer { lock.unlock() }
        pending.append(data)
        while let index = pending.firstIndex(where: { $0 == 0x0A || $0 == 0x0D }) {
            let line = String(decoding: pending[pending.startIndex..<index], as: UTF8.self)
            pending.removeSubrange(pending.startIndex...index)
            if !line.isEmpty { onLine?(line) }
        }
    }

    func flush() {
        lock.lock(); defer { lock.unlock() }
        if !pending.isEmpty { onLine?(String(decoding: pending, as: UTF8.self)) }
        pending.removeAll()
    }
}
