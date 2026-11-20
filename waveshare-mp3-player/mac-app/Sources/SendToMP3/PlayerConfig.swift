import Foundation

/// Must match the firmware (src/player_config.h).
enum PlayerConfig {
    static let wifiName = "Addy's MP3-Player"
    static let wifiPassword = "whyisadityasupercool"
    static let baseURL = URL(string: "http://192.168.4.1")!

    /// Hidden file the firmware writes to the SD card root, so the app never
    /// copies songs onto some other USB stick by mistake.
    static let markerFile = ".addys-mp3-player"
    static let musicFolder = "Music"

    /// Where downloaded songs live on this Mac (visible in Finder > Music).
    static var libraryFolder: URL {
        let music = FileManager.default.urls(for: .musicDirectory, in: .userDomainMask).first
            ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Music")
        return music.appendingPathComponent("Addy's MP3 Player", isDirectory: true)
    }

    /// Private tool folder (yt-dlp, ffmpeg, deno) when they are not already installed.
    static var toolsFolder: URL {
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        return support.appendingPathComponent("SendToMP3/bin", isDirectory: true)
    }
}
