// swift-tools-version: 5.8
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "Swift-CANyonero",
    platforms: [
        .macOS(.v12)
    ],
    products: [
        .library(name: "libCANyonero", targets: ["libCANyonero"]),
        .library(name: "ObjC-CANyonero", targets: ["ObjC-CANyonero"]),
        //.library(name: "Swift-CANyonero", targets: ["Swift-CANyonero"]),
    ],
    dependencies: [
        .package(url: "https://github.com/Cornucopia-Swift/CornucopiaCore", branch: "master"),
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
