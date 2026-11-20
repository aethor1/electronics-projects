import Foundation

/// Paste a YouTube (or other yt-dlp supported) link -> 128 kb/s MP3 in the library folder.
@MainActor
final class Downloader: ObservableObject {
    @Published private(set) var busy = false
    @Published private(set) var status = ""
    @Published private(set) var progress: Double?
    @Published private(set) var failed = false

    func download(_ link: String, library: Library) async {
        let trimmed = link.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let url = URL(string: trimmed), let scheme = url.scheme?.lowercased(),
              scheme == "https" || scheme == "http", url.host != nil else {
            show("That doesn't look like a link. Copy it from the YouTube address bar.", failed: true)
            return
        }

        busy = true
        failed = false
        progress = nil
        defer { busy = false }

        do {
            let tools = try await Tools.ensureInstalled { [weak self] text in self?.show(text) }
            show("Getting the song…")
            let arguments = [
                "--no-playlist",            // a video link inside a playlist grabs just that video
                "-f", "bestaudio/best",
                "-x", "--audio-format", "mp3", "--audio-quality", "128K",
                "--embed-metadata",
                "--windows-filenames",      // the player's SD card is FAT32
                "--trim-filenames", "100",
                "--no-mtime",               // newest downloads sort first
                "--newline",
                "--ffmpeg-location", tools.ffmpeg.path,
                "-P", library.folder.path,
                "-o", "%(title)s.%(ext)s",
                trimmed,
            ]
            lastError = nil
            let code = try await ProcessRunner.run(
                tools.ytdlp, arguments, environment: ["PATH": Tools.processPATH]
            ) { line in
                Task { @MainActor [weak self] in self?.handle(line) }
            }
            library.reload()
            if code == 0 {
                progress = 1
                show("Saved. Hit “Send it to MP3 player” when you're ready.")
            } else {
                let hint = lastError ?? "yt-dlp exited with code \(code)."
                show("Couldn't get that one. \(hint)", failed: true)
            }
        } catch {
            show(error.localizedDescription, failed: true)
        }
    }

    /// yt-dlp breaks whenever YouTube changes things; this pulls the newest version.
    func updateDownloader() async {
        guard let ytdlp = Tools.find("yt-dlp") else { return }
        busy = true
        defer { busy = false }
        show("Updating the downloader…")
        let code = try? await ProcessRunner.run(ytdlp, ["-U"], environment: ["PATH": Tools.processPATH])
        show(code == 0 ? "Downloader is up to date." : "Update didn't work (MacYTDL can update it too).",
             failed: code != 0)
    }

    private var lastError: String?

    private func handle(_ line: String) {
        if line.hasPrefix("ERROR") { lastError = line }
        if line.hasPrefix("[download]"), let range = line.range(of: #"\d+(\.\d+)?%"#, options: .regularExpression) {
            let percent = Double(line[range].dropLast()) ?? 0
            progress = percent / 100 * 0.9
            status = "Downloading… \(Int(percent))%"
        } else if line.hasPrefix("[ExtractAudio]") {
            progress = 0.95
            status = "Converting to MP3…"
        }
    }

    private func show(_ text: String, failed: Bool = false) {
        status = text
        self.failed = failed
    }
}
