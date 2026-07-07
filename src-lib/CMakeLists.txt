cmake_minimum_required(VERSION 3.14)
project(efly-lib)

# ---- vcpkg integration ----
# Build command:
#   cmake -S . -B build -DCMAKE_PREFIX_PATH="%cd%/vcpkg_installed/x64-windows"
#   cmake --build build --config Release
#
# Or if VCPKG_ROOT is set globally, use the toolchain:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

# Fallback: if toolchain doesn't set prefix, point at local vcpkg install
if(NOT DEFINED CMAKE_PREFIX_PATH OR CMAKE_PREFIX_PATH STREQUAL "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows")
        set(CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows"
            CACHE STRING "Vcpkg install prefix")
    endif()
endif()

find_package(Boost CONFIG REQUIRED COMPONENTS system)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Source files ----
set(EFL_SOURCES
    src/dll_main.cpp
    src/udp_server.cpp
    src/protocol_handler.cpp
    src/transfer_manager.cpp
    src/peer_discovery.cpp
    src/packet_buffer.cpp
    src/file_io.cpp
)

set(EFL_HEADERS
    include/efly/efly_api.h
    include/efly/efly_types.h
    include/efly/udp_server.h
    include/efly/protocol_handler.h
    include/efly/transfer_manager.h
    include/efly/peer_discovery.h
    include/efly/packet_buffer.h
    include/efly/file_io.h
)

# ---- Build shared library ----
add_library(efly_lib SHARED ${EFL_SOURCES} ${EFL_HEADERS})

target_include_directories(efly_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(efly_lib
    PRIVATE
        Boost::system
        ws2_32
)

# DLL export macro
target_compile_definitions(efly_lib PRIVATE EFL_API_EXPORTS _WIN32_WINNT=0x0A00)

# Output as efly_core to avoid name clash with Tauri's Rust cdylib
set_target_properties(efly_lib PROPERTIES OUTPUT_NAME "efly_core")
