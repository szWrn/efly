cmake_minimum_required(VERSION 3.14)
project(efly-tools)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# DLL headers
set(EFL_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../include")

# DLL import library (Release build output, under src-lib/build)
set(EFL_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../build/Release")

# ---- efly_cli (interactive command-line tool) ----
add_executable(efly_cli cli.cpp)
target_include_directories(efly_cli PRIVATE ${EFL_INCLUDE_DIR})
target_link_libraries(efly_cli PRIVATE "${EFL_LIB_DIR}/efly_core.lib")
add_custom_command(TARGET efly_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${EFL_LIB_DIR}/efly_core.dll"
        "$<TARGET_FILE_DIR:efly_cli>/efly_core.dll"
    COMMENT "Copying efly_core.dll to efly_cli output dir"
)
