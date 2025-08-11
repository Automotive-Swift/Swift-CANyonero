# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build and Test Commands

### Building the Package
```bash
# Build all targets
swift build

# Build specific target
swift build --target libCANyonero
swift build --target ObjC-CANyonero
```

### Running Tests
```bash
# Run all tests
swift test

# Run specific test target
swift test --filter libCANyonero-Tests
swift test --filter ObjC-CANyonero-Tests

# Run specific test case
swift test --filter ISOTP_Transmit_Valid
```

### ESP-IDF Build (for embedded deployment)
The project includes ESP-IDF support for deployment on ESP32 hardware. The CMakeLists.txt and idf_component.yml files configure the libCANyonero library as an ESP-IDF component.

## Architecture Overview

### Three-Layer Structure

1. **libCANyonero** (C++ Core)
   - Location: `Sources/libCANyonero/`
   - Core protocol implementation in C++20
   - Key components:
     - `Protocol.hpp/cpp`: CANyonero wire protocol with PDU structure `[ATT:UInt8 | TYP:UInt8 | LEN:UInt16 | payload]`
     - `ISOTP.hpp`: ISO-TP (ISO 15765-2) implementation for segmented message transfer
     - `Helpers.hpp`: Utility functions and type definitions
     - `lz4.h/c`: LZ4 compression support

2. **ObjC-CANyonero** (Objective-C Wrapper)
   - Location: `Sources/ObjC-CANyonero/`
   - Objective-C++ wrapper around libCANyonero
   - Provides Foundation framework integration

3. **Swift-CANyonero** (Swift Layer - Currently Disabled)
   - Location: `Sources/Swift-CANyonero/`
   - Currently commented out in Package.swift
   - Requires Swift 5.9+ C++ interoperability mode

### Protocol Implementation

The CANyonero protocol communicates with embedded hardware running CANyonerOS (FreeRTOS-based). Key protocol features:

- **PDU Structure**: Fixed 4-byte header with ATT=0x1F, TYP (command type), LEN (payload length up to 0xFFFF)
- **Channel Protocols**: Raw CAN, ISOTP, CAN-FD, ISOTP-FD, KLINE, ENET
- **ISOTP Implementation**: 
  - Transceiver class handles segmentation/reassembly
  - Supports Single Frame (SF), First Frame (FF), Consecutive Frame (CF), Flow Control (FC)
  - Maximum transfer size: 4095 bytes (0xFFF)
  - Padding byte: 0xAA

### Testing Structure

Tests are organized by component with comprehensive ISOTP protocol testing:
- `Tests/libCANyonero-Tests/`: Core C++ tests including compression and ISOTP scenarios
- `Tests/libCANyonero-Tests/ISOTP/`: Detailed ISOTP test cases for valid/invalid frame handling
- `Tests/ObjC-CANyonero-Tests/`: Objective-C wrapper tests

All tests use XCTest framework and are written in Objective-C++ (.mm files).

## Important Protocol Constants

- ATT byte: `0x1F` (protocol identifier)
- ISOTP padding: `0xAA`
- Maximum PDU length: `0x10003` (header + max payload)
- ISOTP max transfer: `0xFFF` (4095 bytes)

## Compilation Flags

The project enforces strict compilation with `-Werror` and `-pedantic` flags for C++ code, ensuring high code quality standards.