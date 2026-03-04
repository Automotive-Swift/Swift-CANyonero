cmake_minimum_required(VERSION 3.16)
project(ecos_socketcan_bridge LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(ecos-socketcan-bridge
    main.cpp
    ../libCANyonero/Protocol.cpp
    ../libCANyonero/lz4.c
)

target_include_directories(ecos-socketcan-bridge PRIVATE
    ../libCANyonero/include
)

target_compile_options(ecos-socketcan-bridge PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(ecos-socketcan-bridge PRIVATE pthread)
