import AppKit
import Foundation

struct Song: Identifiable, Hashable {
    let url: URL
    let size: Int64
    var id: URL { url }
    var fileName: String { url.lastPathComponent }
    var title: String { url.deletingPathExtension().lastPathComponent }
    var sizeText: String { ByteCountFormatter.string(fromByteCount: size, countStyle: .file) }
}

/// The MP3s saved on this Mac, newest first.
@MainActor
final class Library: ObservableObject {
    @Published private(set) var songs: [Song] = []
    let folder = PlayerConfig.libraryFolder

    init() {
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        reload()
    }

    func reload() {
        let keys: Set<URLResourceKey> = [.fileSizeKey, .contentModificationDateKey]
        let items = (try? FileManager.default.contentsOfDirectory(
            at: folder, includingPropertiesForKeys: Array(keys), options: [.skipsHiddenFiles])) ?? []
        songs = items
            .filter { $0.pathExtension.lowercased() == "mp3" }
            .map { url -> (Song, Date) in
                let values = try? url.resourceValues(forKeys: keys)
                return (Song(url: url, size: Int64(values?.fileSize ?? 0)),
                        values?.contentModificationDate ?? .distantPast)
            }
            .sorted { $0.1 > $1.1 }
            .map(\.0)
    }

    func moveToTrash(_ song: Song) {
        try? FileManager.default.trashItem(at: song.url, resultingItemURL: nil)
        reload()
    }

    func showInFinder(_ song: Song? = nil) {
        if let song {
            NSWorkspace.shared.activateFileViewerSelecting([song.url])
        } else {
            NSWorkspace.shared.open(folder)
        }
    }

    /// Lets someone drag MP3s from Finder into the window.
    func importFiles(_ urls: [URL]) {
        for url in urls where url.pathExtension.lowercased() == "mp3" {
            let target = folder.appendingPathComponent(url.lastPathComponent)
            if !FileManager.default.fileExists(atPath: target.path) {
                try? FileManager.default.copyItem(at: url, to: target)
            }
        }
        reload()
    }
}
