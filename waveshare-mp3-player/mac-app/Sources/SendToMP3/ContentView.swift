import SwiftUI
import UniformTypeIdentifiers

/// Palette loosely based on a cream-and-plum handheld.
private enum Palette {
    static let shell = Color(red: 0.93, green: 0.91, blue: 0.86)
    static let screen = Color(red: 0.16, green: 0.17, blue: 0.20)
    static let screenText = Color(red: 0.80, green: 0.93, blue: 0.78)
    static let accent = Color(red: 0.55, green: 0.18, blue: 0.42)
    static let accentDark = Color(red: 0.40, green: 0.11, blue: 0.30)
    static let ink = Color(red: 0.20, green: 0.19, blue: 0.22)
}

struct ContentView: View {
    @EnvironmentObject private var library: Library
    @EnvironmentObject private var downloader: Downloader
    @EnvironmentObject private var player: PlayerLink
    @State private var link = ""
    @State private var dropTargeted = false

    var body: some View {
        VStack(spacing: 18) {
            header
            linkCard
            songScreen
            sendButton
        }
        .padding(22)
        .background(Palette.shell)
        .foregroundStyle(Palette.ink)
    }

    private var header: some View {
        VStack(spacing: 4) {
            Text("Addy's MP3 Player")
                .font(.system(size: 26, weight: .heavy, design: .rounded))
            Text("Paste a YouTube link, get the song, send it over.")
                .font(.system(size: 13, design: .rounded))
                .foregroundStyle(Palette.ink.opacity(0.65))
        }
        .frame(maxWidth: .infinity)
    }

    private var linkCard: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 10) {
                TextField("https://www.youtube.com/watch?v=…", text: $link)
                    .textFieldStyle(.plain)
                    .font(.system(size: 14, design: .rounded))
                    .padding(.horizontal, 12)
                    .padding(.vertical, 10)
                    .background(RoundedRectangle(cornerRadius: 10).fill(.white))
                    .overlay(RoundedRectangle(cornerRadius: 10).stroke(Palette.ink.opacity(0.15)))
                    .onSubmit(getSong)
                    .disabled(downloader.busy)

                Button(action: getSong) {
                    Text(downloader.busy ? "Working…" : "Get song")
                        .font(.system(size: 14, weight: .bold, design: .rounded))
                        .padding(.horizontal, 16)
                        .padding(.vertical, 10)
                        .background(RoundedRectangle(cornerRadius: 10).fill(Palette.ink))
                        .foregroundStyle(.white)
                }
                .buttonStyle(.plain)
                .disabled(downloader.busy || link.isEmpty)
                .keyboardShortcut(.defaultAction)
            }
            if !downloader.status.isEmpty {
                StatusLine(text: downloader.status, progress: downloader.busy ? downloader.progress : nil,
                           failed: downloader.failed)
            }
        }
    }

    /// The "screen" of the handheld: the songs saved on this Mac.
    private var songScreen: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text("SONGS ON THIS MAC")
                    .font(.system(size: 11, weight: .bold, design: .monospaced))
                Spacer()
                Text("\(library.songs.count)")
                    .font(.system(size: 11, weight: .bold, design: .monospaced))
            }
            .foregroundStyle(Palette.screenText.opacity(0.7))
            .padding(.horizontal, 14)
            .padding(.vertical, 10)

            if library.songs.isEmpty {
                VStack(spacing: 8) {
                    Image(systemName: "music.note")
                        .font(.system(size: 30))
                    Text("No songs yet.\nPaste a link above, or drop MP3s here.")
                        .multilineTextAlignment(.center)
                        .font(.system(size: 13, design: .monospaced))
                }
                .foregroundStyle(Palette.screenText.opacity(0.6))
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                ScrollView {
                    LazyVStack(spacing: 2) {
                        ForEach(library.songs) { song in
                            SongRow(song: song)
                                .contextMenu {
                                    Button("Show in Finder") { library.showInFinder(song) }
                                    Button("Remove from this Mac") { library.moveToTrash(song) }
                                }
                        }
                    }
                    .padding(.horizontal, 8)
                    .padding(.bottom, 8)
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(RoundedRectangle(cornerRadius: 16).fill(Palette.screen))
        .overlay(RoundedRectangle(cornerRadius: 16)
            .stroke(dropTargeted ? Palette.accent : Palette.ink.opacity(0.4), lineWidth: dropTargeted ? 3 : 6))
        .onDrop(of: [.fileURL], isTargeted: $dropTargeted) { providers in
            for provider in providers {
                _ = provider.loadObject(ofClass: URL.self) { url, _ in
                    if let url { Task { @MainActor in library.importFiles([url]) } }
                }
            }
            return true
        }
    }

    private var sendButton: some View {
        VStack(spacing: 10) {
            Button {
                Task { await player.send(library.songs) }
            } label: {
                HStack(spacing: 10) {
                    Image(systemName: player.busy ? "arrow.triangle.2.circlepath" : "paperplane.fill")
                    Text(player.busy ? "Sending…" : "Send it to MP3 player")
                }
                .font(.system(size: 18, weight: .heavy, design: .rounded))
                .frame(maxWidth: .infinity)
                .padding(.vertical, 16)
                .background(
                    RoundedRectangle(cornerRadius: 16)
                        .fill(Palette.accent)
                        .shadow(color: Palette.accentDark, radius: 0, x: 0, y: 5)
                )
                .foregroundStyle(.white)
            }
            .buttonStyle(.plain)
            .disabled(player.busy || downloader.busy)

            if !player.status.isEmpty {
                StatusLine(text: player.status, progress: player.busy ? player.progress : nil, failed: player.failed)
            } else {
                Text("Plug in with USB (tap “USB drive” on the player) or tap “Add music” for Wi-Fi.")
                    .font(.system(size: 12, design: .rounded))
                    .foregroundStyle(Palette.ink.opacity(0.6))
                    .multilineTextAlignment(.center)
            }
        }
    }

    private func getSong() {
        let text = link
        Task {
            await downloader.download(text, library: library)
            if !downloader.failed { link = "" }
        }
    }
}

private struct SongRow: View {
    let song: Song

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: "music.note")
                .foregroundStyle(Palette.accent.opacity(0.9))
            Text(song.title)
                .lineLimit(1)
                .truncationMode(.middle)
            Spacer()
            Text(song.sizeText)
                .foregroundStyle(Palette.screenText.opacity(0.5))
        }
        .font(.system(size: 13, design: .monospaced))
        .foregroundStyle(Palette.screenText)
        .padding(.horizontal, 10)
        .padding(.vertical, 7)
        .background(RoundedRectangle(cornerRadius: 8).fill(Color.white.opacity(0.04)))
    }
}

private struct StatusLine: View {
    let text: String
    let progress: Double?
    let failed: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            if let progress {
                ProgressView(value: progress).tint(Palette.accent)
            }
            Text(text)
                .font(.system(size: 12, weight: failed ? .semibold : .regular, design: .rounded))
                .foregroundStyle(failed ? Color.red : Palette.ink.opacity(0.75))
                .fixedSize(horizontal: false, vertical: true)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}
