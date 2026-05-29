cmake_minimum_required(VERSION 3.24)
project(ArcLoRaM_Tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# CTest = CMake's built-in test runner ("ctest" command)
enable_testing()

# FetchContent downloads Unity automatically on first configure (~3-10 s)
include(FetchContent)
FetchContent_Declare(
    unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG        v2.6.0
    GIT_SHALLOW    TRUE   # only the tagged commit, not the full history
)
FetchContent_MakeAvailable(unity)   # creates the "unity" CMake link target

add_subdirectory(stubs)
add_subdirectory(unit)
