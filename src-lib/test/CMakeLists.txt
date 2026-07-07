cmake_minimum_required(VERSION 3.14)
project(efly-test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# DLL headers
set(EFL_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../include")

# DLL import library (Release build output, under src-lib/build)
set(EFL_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../build/Release")

# ---- efly_check (automated test suite) ----
add_executable(efly_check check.cpp)
target_include_directories(efly_check PRIVATE ${EFL_INCLUDE_DIR})
target_link_libraries(efly_check PRIVATE "${EFL_LIB_DIR}/efly_core.lib")
add_custom_command(TARGET efly_check POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${EFL_LIB_DIR}/efly_core.dll"
        "$<TARGET_FILE_DIR:efly_check>/efly_core.dll"
    COMMENT "Copying efly_core.dll to efly_check output dir"
)
