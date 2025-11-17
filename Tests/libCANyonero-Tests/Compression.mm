///
/// CANyonero. (C) 2022 - 2023 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#import <XCTest/XCTest.h>
#import <Foundation/Foundation.h>

#import <string>
#import <vector>
#import <iostream>

#import "lz4.h"

@interface Compression : XCTestCase

@end

@implementation Compression

-(void)testCompression {

    auto string = std::string("HALLO HALLO HALLO HALLO HALLO HALLO HALLO HALLO HALLO HALLO");
    std::vector<char> buffer(LZ4_compressBound(int(string.size())));

    auto compressed = ::LZ4_compress_default(string.c_str(), buffer.data(), int(string.size()), int(buffer.size()));
    XCTAssertGreaterThan(compressed, 0);
    
    std::vector<char> restored(string.size());
    auto restoredLength = ::LZ4_decompress_safe(buffer.data(), restored.data(), compressed, int(restored.size()));
    XCTAssertEqual(restoredLength, string.size());
    auto roundTrip = std::string(restored.begin(), restored.end());
    XCTAssertEqual(roundTrip, string);
}

-(void)testDecompressionFailsForSmallOutputBuffer {
    auto string = std::string("1234567890ABCDEFGHIJKLMNOP");
    std::vector<char> buffer(LZ4_compressBound(int(string.size())));
    auto compressed = ::LZ4_compress_default(string.c_str(), buffer.data(), int(string.size()), int(buffer.size()));
    XCTAssertGreaterThan(compressed, 0);

    std::vector<char> tooSmall(string.size() / 2);
    auto result = ::LZ4_decompress_safe(buffer.data(), tooSmall.data(), compressed, int(tooSmall.size()));
    XCTAssertLessThan(result, 0);
}

-(void)testBinaryPayloadRoundTrip {
    std::vector<char> payload;
    for (int i = 0; i < 512; ++i) {
        payload.push_back(static_cast<char>(i & 0xFF));
    }

    std::vector<char> buffer(LZ4_compressBound(int(payload.size())));
    auto compressed = ::LZ4_compress_default(payload.data(), buffer.data(), int(payload.size()), int(buffer.size()));
    XCTAssertGreaterThan(compressed, 0);
    
    std::vector<char> restored(payload.size());
    auto restoredLength = ::LZ4_decompress_safe(buffer.data(), restored.data(), compressed, int(restored.size()));
    XCTAssertEqual(restoredLength, payload.size());
    XCTAssertTrue(std::equal(payload.begin(), payload.end(), restored.begin()));
}

@end
