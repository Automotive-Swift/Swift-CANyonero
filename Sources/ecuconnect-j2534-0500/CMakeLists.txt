cmake_minimum_required(VERSION 3.16)
project(ecuconnect_j2534_v0500 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DLL_NAME)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(DLL_NAME "ecuconnect050064")
    else()
        set(DLL_NAME "ecuconnect050032")
    endif()
endif()

string(REGEX REPLACE "\\.dll$" "" DLL_NAME_BASE "${DLL_NAME}")

if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0601)
    add_definitions(-DUNICODE -D_UNICODE)
    add_definitions(-DNOMINMAX)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
endif()

add_library(ecuconnect_j2534_v0500 SHARED
    ../ecuconnect-j2534/src/dllmain.cpp
    src/j2534_api.cpp
    src/ecuconnect.cpp
    ../ecuconnect-j2534/src/transport_tcp.cpp
    ../ecuconnect-j2534/src/canyonero_protocol.cpp
)

target_include_directories(ecuconnect_j2534_v0500 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../ecuconnect-j2534/include
)

if(WIN32)
    target_link_libraries(ecuconnect_j2534_v0500 PRIVATE ws2_32)
endif()

set_target_properties(ecuconnect_j2534_v0500 PROPERTIES
    OUTPUT_NAME "${DLL_NAME_BASE}"
    PREFIX ""
    DEFINE_SYMBOL "J2534_EXPORTS"
)

target_sources(ecuconnect_j2534_v0500 PRIVATE src/ecuconnect.def)
