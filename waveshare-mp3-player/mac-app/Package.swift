// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "SendToMP3",
    platforms: [.macOS(.v13)],
    targets: [
        .executableTarget(
            name: "SendToMP3",
            path: "Sources/SendToMP3",
            linkerSettings: [.linkedFramework("CoreWLAN")]
        )
    ]
)
