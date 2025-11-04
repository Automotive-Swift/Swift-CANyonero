// swift-tools-version: 5.8
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "Swift-CANyonero",
    platforms: [
        .macOS(.v13)
    ],
    products: [
        .library(name: "libCANyonero", targets: ["libCANyonero"]),
        .library(name: "ObjC-CANyonero", targets: ["ObjC-CANyonero"]),
        //.library(name: "Swift-CANyonero", targets: ["Swift-CANyonero"]),
        .executable(name: "ecuconnect-tool", targets: ["ecuconnect-tool"]),
    ],
    dependencies: [
        .package(url: "https://github.com/Cornucopia-Swift/CornucopiaCore", branch: "master"),
        .package(url: "ssh://git@gitlab.com/a11086/swift-automotive", branch: "master"),
        //.package(path: "../Swift-Automotive"),
        .package(url: "https://github.com/apple/swift-argument-parser", from: "1.2.0"),
        .package(url: "https://github.com/mxcl/Chalk", branch: "master"),
        .package(url: "https://github.com/andybest/linenoise-swift", branch: "master"),
        .package(url: "https://github.com/jkandzi/Progress.swift", branch: "master"),
        .package(url: "https://github.com/mickeyl/Socket.swift", branch: "noTLS"),
    ],
    targets: [
        .target(
            name: "libCANyonero",
            dependencies: [],
            cxxSettings: [
                .unsafeFlags(["-Werror", "-pedantic"])
            ]
        ),
        .target(
            name: "ObjC-CANyonero",
            dependencies: ["libCANyonero"],
            cxxSettings: [
                .unsafeFlags(["-Werror", "-pedantic"])
            ]
        ),
        /*
        .target(
            name: "Swift-CANyonero",
            dependencies: [
                "libCANyonero",
                "CornucopiaCore",
            ],
            swiftSettings: [.unsafeFlags([
                "-I", "Sources/libCANyonero",
                "-cxx-interoperability-mode=swift-5.9",
            ])]
        ),
        */
        .testTarget(
            name: "libCANyonero-Tests",
            dependencies: [
                "libCANyonero"
            ]
        ),
        .testTarget(
            name: "ObjC-CANyonero-Tests",
            dependencies: [
                "ObjC-CANyonero"
            ]
        ),
        .executableTarget(
            name: "ecuconnect-tool",
            dependencies: [
                .product(name: "ArgumentParser", package: "swift-argument-parser"),
                .product(name: "Chalk", package: "Chalk"),
                .product(name: "CornucopiaCore", package: "CornucopiaCore"),
                .product(name: "Swift-Automotive-Client", package: "Swift-Automotive"),
                .product(name: "LineNoise", package: "linenoise-swift"),
                .product(name: "Progress", package: "Progress.swift"),
                .product(name: "SocketSwift", package: "Socket.swift"),
            ]
        )

        /*
        ,
        .testTarget(
            name: "Swift-CANyonero-Tests",
            dependencies: [
                "Swift-CANyonero"
            ],
            swiftSettings: [.unsafeFlags([
                "-I", "Sources/libCANyonero",
                "-cxx-interoperability-mode=swift-5.9",
            ])]
        ),
        */
    ],
    cLanguageStandard: .c99,
    cxxLanguageStandard: .cxx20
)
