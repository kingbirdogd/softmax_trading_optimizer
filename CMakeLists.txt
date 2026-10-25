cmake_minimum_required(VERSION 3.20)

project(softmax_trading_optimizer
    VERSION 0.0.1
    DESCRIPTION "Softmax trading optimizer build with SuperCMake conventions"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SuperCMake)

super_init()

add_library(trading_core
    src/config.cpp
    src/models.cpp
    src/strategy.cpp
    src/optimizer.cpp
    src/replay.cpp
)

target_include_directories(trading_core PUBLIC include)

add_executable(trading_optimizer src/main.cpp)
target_link_libraries(trading_optimizer PRIVATE trading_core)

add_executable(unit_tests tests/test_core.cpp)
target_link_libraries(unit_tests PRIVATE trading_core GTest::gtest_main)

enable_testing()
include(GoogleTest)
gtest_discover_tests(unit_tests)
