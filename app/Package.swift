// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "AgentPulseApp",
    platforms: [.macOS(.v13)],
    targets: [
        .executableTarget(
            name: "AgentPulseApp",
            path: "Sources/AgentPulseApp"
        )
    ]
)
