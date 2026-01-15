cmake_minimum_required(VERSION 3.18)
project(ecuconnect_tool LANGUAGES C CXX)

find_package(pybind11 CONFIG REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(LIBCAN_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/../../Sources/libCANyonero/Protocol.cpp
)
set(LZ4_SOURCE
    ${CMAKE_CURRENT_LIST_DIR}/../../Sources/libCANyonero/lz4.c
)
set(LIBCAN_INCLUDE
    ${CMAKE_CURRENT_LIST_DIR}/../../Sources/libCANyonero/include
)

pybind11_add_module(canyonero_py
    ${CMAKE_CURRENT_LIST_DIR}/src/canyonero_py.cpp
    ${LIBCAN_SOURCES}
    ${LZ4_SOURCE}
)

target_include_directories(canyonero_py PRIVATE ${LIBCAN_INCLUDE})

target_compile_options(canyonero_py PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-pedantic>
)

install(TARGETS canyonero_py LIBRARY DESTINATION ecuconnect_tool)
