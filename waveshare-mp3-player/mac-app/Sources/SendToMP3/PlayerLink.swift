import AppKit
import CoreWLAN
import Foundation

/// "Send it to MP3 player": USB cable first, Wi-Fi second.
@MainActor
final class PlayerLink: ObservableObject {
    @Published private(set) var busy = false
    @Published private(set) var status = ""
    @Published private(set) var progress: Double?
    @Published private(set) var failed = false

    func send(_ songs: [Song]) async {
        guard !songs.isEmpty else {
            show("Get a song first, then send it.", failed: true)
            return
        }
        busy = true
        failed = false
        progress = nil
        defer { busy = false }

        do {
            if let volume = Self.findPlayerVolume() {
                try await sendOverUSB(songs, to: volume)
            } else {
                try await sendOverWiFi(songs)
            }
        } catch {
            show(error.localizedDescription, failed: true)
        }
    }

    // MARK: USB (player in "USB drive" mode shows up in Finder)

    static func findPlayerVolume() -> URL? {
        let volumes = FileManager.default.mountedVolumeURLs(
            includingResourceValuesForKeys: nil, options: [.skipHiddenVolumes]) ?? []
        return volumes.first {
            FileManager.default.fileExists(atPath: $0.appendingPathComponent(PlayerConfig.markerFile).path)
        }
    }

    private func sendOverUSB(_ songs: [Song], to volume: URL) async throws {
        show("Player found over USB…")
        let music = volume.appendingPathComponent(PlayerConfig.musicFolder, isDirectory: true)
        try FileManager.default.createDirectory(at: music, withIntermediateDirectories: true)
        // Keep Spotlight from filling the card with index files.
        FileManager.default.createFile(atPath: volume.appendingPathComponent(".metadata_never_index").path, contents: nil)

        let onPlayer = Set((try? FileManager.default.contentsOfDirectory(atPath: music.path)) ?? [])
        let todo = songs.filter { !onPlayer.contains(PlayerName.forSong($0.fileName)) }
        if todo.isEmpty {
            try await eject(volume, message: "Everything's already on the player.")
            return
        }

        for (index, song) in todo.enumerated() {
            show("Copying \(index + 1) of \(todo.count): \(song.title)")
            progress = Double(index) / Double(todo.count)
            let name = PlayerName.forSong(song.fileName)
            let partial = music.appendingPathComponent(".partial-\(name)")
            let destination = music.appendingPathComponent(name)
            try await Task.detached {
                // Plain byte copy: Finder-style copies add "._" metadata files to FAT cards.
                try PlayerLink.copyBytes(from: song.url, to: partial)
                try FileManager.default.moveItem(at: partial, to: destination)
            }.value
        }
        progress = 1
        try await eject(volume, message: "Sent \(todo.count) song\(todo.count == 1 ? "" : "s"). You can unplug the player.")
    }

    nonisolated static func copyBytes(from source: URL, to target: URL) throws {
        try? FileManager.default.removeItem(at: target)
        guard FileManager.default.createFile(atPath: target.path, contents: nil) else {
            throw AppError("Couldn't write to the player. Is the card full?")
        }
        let input = try FileHandle(forReadingFrom: source)
        let output = try FileHandle(forWritingTo: target)
        defer { try? input.close(); try? output.close() }
        while let chunk = try input.read(upToCount: 1 << 20), !chunk.isEmpty {
            try output.write(contentsOf: chunk)
        }
        try output.synchronize()
    }

    private func eject(_ volume: URL, message: String) async throws {
        show("Ejecting…")
        do {
            try NSWorkspace.shared.unmountAndEjectDevice(at: volume)
            show(message)
        } catch {
            show("\(message) Eject it in Finder before unplugging.")
        }
    }

    // MARK: Wi-Fi (player in "Add music" mode)

    private struct FileList: Decodable { let files: [String] }

    private func sendOverWiFi(_ songs: [Song]) async throws {
        show("Looking for the player on Wi-Fi…")
        if !(await Self.playerReachable()) {
            show("Joining “\(PlayerConfig.wifiName)”…")
            await Task.detached { Self.joinPlayerWiFi() }.value
            var found = false
            for _ in 0..<10 where !found {
                try await Task.sleep(nanoseconds: 1_500_000_000)
                found = await Self.playerReachable()
            }
            guard found else {
                throw AppError("""
                    Can't find the player. Plug it in with USB and tap “USB drive”, \
                    or tap “Add music” on it and join the Wi-Fi “\(PlayerConfig.wifiName)”, then try again.
                    """)
            }
        }

        let (data, _) = try await URLSession.shared.data(from: PlayerConfig.baseURL.appendingPathComponent("api/files"))
        let onPlayer = Set((try? JSONDecoder().decode(FileList.self, from: data).files) ?? [])
        let todo = songs.filter { !onPlayer.contains(PlayerName.forSong($0.fileName)) }

        for (index, song) in todo.enumerated() {
            show("Sending \(index + 1) of \(todo.count): \(song.title)")
            progress = Double(index) / Double(todo.count)
            try await upload(song)
        }
        progress = 1

        // Tell the player we're done so it turns Wi-Fi off and refreshes its song list.
        var done = URLRequest(url: PlayerConfig.baseURL.appendingPathComponent("api/done"))
        done.httpMethod = "POST"
        _ = try? await URLSession.shared.data(for: done)
        show(todo.isEmpty ? "Everything's already on the player."
                          : "Sent \(todo.count) song\(todo.count == 1 ? "" : "s"). The player's Wi-Fi is off again.")
    }

    private func upload(_ song: Song) async throws {
        // Raw body (the firmware's fast path): the MP3 bytes as-is, name in X-File-Name,
        // percent-encoded the same way as the web page's encodeURIComponent().
        let name = PlayerName.forSong(song.fileName)
        var allowed = CharacterSet.alphanumerics
        allowed.insert(charactersIn: "-_.!~*'()")
        let encodedName = name.addingPercentEncoding(withAllowedCharacters: allowed) ?? "song.mp3"

        var request = URLRequest(url: PlayerConfig.baseURL.appendingPathComponent("api/upload"))
        request.httpMethod = "PUT"
        request.timeoutInterval = 600
        request.setValue("audio/mpeg", forHTTPHeaderField: "Content-Type")
        request.setValue(encodedName, forHTTPHeaderField: "X-File-Name")
        let (data, response) = try await URLSession.shared.upload(for: request, fromFile: song.url)
        let code = (response as? HTTPURLResponse)?.statusCode ?? 0
        guard code == 200 else {
            let reason = String(data: data, encoding: .utf8) ?? ""
            throw AppError(code == 507 ? "The player's card is full." : "Upload of “\(song.title)” failed (\(code)). \(reason)")
        }
    }

    nonisolated static func playerReachable() async -> Bool {
        var request = URLRequest(url: PlayerConfig.baseURL.appendingPathComponent("api/status"))
        request.timeoutInterval = 2
        guard let result = try? await URLSession.shared.data(for: request) else { return false }
        return (result.1 as? HTTPURLResponse)?.statusCode == 200
    }

    /// Best effort: macOS may refuse without Location permission, then the user joins by hand.
    nonisolated static func joinPlayerWiFi() {
        guard let wifi = CWWiFiClient.shared().interface() else { return }
        if wifi.ssid() == PlayerConfig.wifiName { return }
        guard let networks = try? wifi.scanForNetworks(withName: PlayerConfig.wifiName),
              let network = networks.first else { return }
        try? wifi.associate(to: network, password: PlayerConfig.wifiPassword)
    }

    private func show(_ text: String, failed: Bool = false) {
        status = text
        self.failed = failed
    }
}

/// Same rule as the firmware's sanitizeUploadName(): printable ASCII only, FAT-safe,
/// so "is it already on the player?" checks match what the player actually saved.
enum PlayerName {
    static func forSong(_ fileName: String) -> String {
        let forbidden = Set("\\/:*?\"<>|".utf8)
        var stem = fileName
        if stem.lowercased().hasSuffix(".mp3") { stem.removeLast(4) }
        var bytes = stem.utf8.map { byte -> UInt8 in
            (byte < 0x20 || byte > 0x7E || forbidden.contains(byte)) ? UInt8(ascii: "_") : byte
        }
        if bytes.count > 96 { bytes = Array(bytes.prefix(96)) }
        while let first = bytes.first, first == UInt8(ascii: " ") || first == UInt8(ascii: ".") { bytes.removeFirst() }
        while let last = bytes.last, last == UInt8(ascii: " ") || last == UInt8(ascii: ".") { bytes.removeLast() }
        if bytes.isEmpty { bytes = Array("song".utf8) }
        return String(decoding: bytes, as: UTF8.self) + ".mp3"
    }
}
