cmake_minimum_required(VERSION 3.16)
project(ecuconnect_j2534 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# DLL output name (can be overridden via -DDLL_NAME=...)
if(NOT DLL_NAME)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(DLL_NAME "ecuconnect64")
    else()
        set(DLL_NAME "ecuconnect32")
    endif()
endif()

# Strip .dll extension if provided
string(REGEX REPLACE "\\.dll$" "" DLL_NAME_BASE "${DLL_NAME}")

message(STATUS "Building: ${DLL_NAME_BASE}.dll (${CMAKE_GENERATOR_PLATFORM})")

# Windows-specific settings
if(WIN32)
    # BLE L2CAP requires Windows 10 1803+ (build 17134)
    # TCP transport still works on Windows 7+
    add_definitions(-D_WIN32_WINNT=0x0A00)  # Windows 10
    add_definitions(-DNTDDI_VERSION=0x0A000004)  # Windows 10 1803 (RS4)
    add_definitions(-DUNICODE -D_UNICODE)
    add_definitions(-DNOMINMAX)             # Prevent min/max macro conflicts
    add_definitions(-DWIN32_LEAN_AND_MEAN)  # Reduce Windows header bloat
endif()

# J2534 driver DLL
add_library(ecuconnect_j2534 SHARED
    src/j2534_api.cpp
    src/ecuconnect.cpp
    src/transport_tcp.cpp
    src/transport_ble.cpp
    src/canyonero_protocol.cpp
)

target_include_directories(ecuconnect_j2534 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link Windows networking
if(WIN32)
    target_link_libraries(ecuconnect_j2534 PRIVATE ws2_32)

    # BLE L2CAP transport: currently a stub implementation
    # Full implementation requires Windows SDK 10.0.19041+ with C++/WinRT
    # To enable full BLE support, uncomment the following when SDK is available:
    # if(MSVC)
    #     target_compile_options(ecuconnect_j2534 PRIVATE /await)
    #     target_link_libraries(ecuconnect_j2534 PRIVATE windowsapp RuntimeObject)
    # endif()
endif()

# Export definitions for J2534 API
set_target_properties(ecuconnect_j2534 PROPERTIES
    OUTPUT_NAME "${DLL_NAME_BASE}"
    PREFIX ""
    DEFINE_SYMBOL "J2534_EXPORTS"
)

# Use .def file for clean exports (MSVC and MinGW)
target_sources(ecuconnect_j2534 PRIVATE src/ecuconnect.def)

# Install rules
install(TARGETS ecuconnect_j2534
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
)
