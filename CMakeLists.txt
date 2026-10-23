cmake_minimum_required(VERSION 3.20)
project(redis_clone VERSION 0.1.0 LANGUAGES CXX)

# Enforce C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for clang-tidy / clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Strict warning flags across all targets
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Sanitizer options (AddressSanitizer and UndefinedBehaviorSanitizer)
option(ENABLE_SANITY "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(ENABLE_SANITY)
    if(NOT MSVC)
        message(STATUS "Enabling AddressSanitizer and UndefinedBehaviorSanitizer")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,undefined)
    else()
        message(WARNING "Sanitizers via -fsanitize are not configured for MSVC in this preset.")
    endif()
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# Core library target
add_library(redis_clone_core STATIC
    src/util/socket_util.cpp
    src/server/poller.cpp
    src/server/client.cpp
    src/server/event_loop.cpp
    src/store/hash_table.cpp
    src/store/ttl_heap.cpp
    src/store/database.cpp
    src/protocol/frame_parser.cpp
    src/protocol/command.cpp
)
target_include_directories(redis_clone_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Server executable target
add_executable(redis_clone_server src/main.cpp)
target_link_libraries(redis_clone_server PRIVATE redis_clone_core)

# Enable testing with GoogleTest
enable_testing()
include(CTest)

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
# Prevent GoogleTest from overriding parent project's compiler runtime settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Unit test suite target
add_executable(unit_tests
    tests/unit/test_smoke.cpp
    tests/unit/test_poller.cpp
    tests/unit/test_client.cpp
    tests/unit/test_event_loop.cpp
    tests/unit/test_hash_table.cpp
    tests/unit/test_frame_parser.cpp
    tests/unit/test_ttl_heap.cpp
)
target_link_libraries(unit_tests PRIVATE redis_clone_core GTest::gtest_main)
add_test(NAME unit_tests COMMAND unit_tests)

# LibFuzzer target
option(ENABLE_FUZZING "Build libFuzzer targets" OFF)
if(ENABLE_FUZZING)
    add_executable(fuzz_frame_parser tests/fuzz/fuzz_frame_parser.cpp)
    target_compile_options(fuzz_frame_parser PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(fuzz_frame_parser PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_libraries(fuzz_frame_parser PRIVATE redis_clone_core)
endif()


