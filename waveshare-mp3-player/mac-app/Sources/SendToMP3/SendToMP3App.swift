import SwiftUI

@main
struct SendToMP3App: App {
    @StateObject private var library = Library()
    @StateObject private var downloader = Downloader()
    @StateObject private var player = PlayerLink()

    var body: some Scene {
        WindowGroup("Addy's MP3 Player") {
            ContentView()
                .environmentObject(library)
                .environmentObject(downloader)
                .environmentObject(player)
                .frame(minWidth: 460, idealWidth: 500, minHeight: 600, idealHeight: 680)
        }
        .windowResizability(.contentMinSize)
        .commands {
            CommandGroup(after: .appSettings) {
                Button("Update Song Downloader") {
                    Task { await downloader.updateDownloader() }
                }
                Button("Show Songs in Finder") { library.showInFinder() }
            }
        }
    }
}
