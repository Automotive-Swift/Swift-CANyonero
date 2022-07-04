// swift-tools-version: 5.7
import PackageDescription

let package = Package(
    name: "Swift-CANyonero",
    platforms: [
        .iOS(.v13),
        .macOS(.v11),
        .tvOS(.v13),
        .watchOS(.v6),
        //.linux
    ],
    products: [
        .library(
            name: "Swift-CANyonero",
            targets: ["Swift-CANyonero"]),
    ],
    dependencies: [
        .package(url: "https://github.com/Automotive-Swift/Swift-CAN", branch: "master"),
        .package(url: "https://github.com/Cornucopia-Swift/CornucopiaCore", branch: "master")
    ],
    targets: [
        .target(
            name: "Swift-CANyonero",
            dependencies: [
                "CornucopiaCore",
                "Swift-CAN"
            ]
        ),
        .testTarget(
            name: "Swift-CANyoneroTests",
            dependencies: ["Swift-CANyonero"]),
    ]
)
