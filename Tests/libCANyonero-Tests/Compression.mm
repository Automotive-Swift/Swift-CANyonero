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
    uint8_t buffer[100];

    auto compressed = ::LZ4_compress_default(string.c_str(), (char*)buffer, int(string.end() - string.begin()), sizeof(buffer));
}

@end
