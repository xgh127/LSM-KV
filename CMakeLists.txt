cmake_minimum_required(VERSION 3.15)
project(LSM_KV
    VERSION 0.1.0
    DESCRIPTION "A modern C++ LSM-Tree KV storage engine (S0 skeleton)"
    LANGUAGES CXX)

# ----------------------------------------------------------------------------
# C++ standard
# ----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ----------------------------------------------------------------------------
# Module options
# ----------------------------------------------------------------------------
option(LSM_BUILD_TESTS       "Build unit/integration tests"             ON)
option(LSM_BUILD_BENCHMARK   "Build google-benchmark targets"          OFF)
option(LSM_USE_SYSTEM_GTEST  "Use pre-installed gtest instead of built-in mini_test" OFF)
option(LSM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors for project code" OFF)
option(LSM_SANITIZE          "Enable sanitizers (Address/UB/Thread)"   OFF)

# ----------------------------------------------------------------------------
# Default build type
# ----------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ----------------------------------------------------------------------------
# Compiler flags
# ----------------------------------------------------------------------------
set(LSM_WARNING_FLAGS_GNU   -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wconversion)
set(LSM_WARNING_FLAGS_MSVC  /W4 /permissive-)

add_library(lsm_warning_flags INTERFACE)
target_compile_options(lsm_warning_flags INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang,MinGW>:${LSM_WARNING_FLAGS_GNU}>
    $<$<CXX_COMPILER_ID:MSVC>:${LSM_WARNING_FLAGS_MSVC}>)

if(LSM_WARNINGS_AS_ERRORS)
    target_compile_options(lsm_warning_flags INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang,MinGW>:-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/WX>)
endif()

if(LSM_SANITIZE)
    if(MSVC)
        message(WARNING "LSM_SANITIZE not yet configured for MSVC; ignored.")
    else()
        target_compile_options(lsm_warning_flags INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(lsm_warning_flags INTERFACE
            -fsanitize=address,undefined)
    endif()
endif()

# ----------------------------------------------------------------------------
# Engine library
# ----------------------------------------------------------------------------
add_library(mini_lsm STATIC
    src/memtable.cpp
    src/sstable.cpp
    src/sstable_builder.cpp
    src/vlog.cpp
    src/compaction.cpp
    src/iterator.cpp
    src/manifest.cpp
    src/wal.cpp
    src/lsm_engine.cpp)

target_include_directories(mini_lsm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(mini_lsm PUBLIC lsm_warning_flags)

# Need Threads on POSIX
find_package(Threads REQUIRED)
target_link_libraries(mini_lsm PUBLIC Threads::Threads)

# Static-link MinGW runtime DLLs (libgcc_s_*, libstdc++-6, libwinpthread-1)
# so the resulting .exe runs on machines that don't have MinGW on PATH
# (avoids STATUS_ENTRYPOINT_NOT_FOUND 0xC0000139).
if(MINGW)
    target_link_options(mini_lsm PUBLIC -static-libgcc -static-libstdc++ -static)
endif()

# ----------------------------------------------------------------------------
# CTest
# ----------------------------------------------------------------------------
if(LSM_BUILD_TESTS)
    enable_testing()

    if(LSM_USE_SYSTEM_GTEST)
        find_package(GTest REQUIRED)
        set(LSM_TEST_LIBS GTest::gtest GTest::gtest_main)
    else()
        # Use self-hosted minimal test framework (tests/mini_test.hpp).
        # It provides gtest-like macros as a single header.
        set(LSM_TEST_LIBS "")
        message(STATUS "LSM-KV: using built-in mini_test framework (no external gtest dependency)")
    endif()

    add_subdirectory(tests)
endif()

if(LSM_BUILD_BENCHMARK)
    # Placeholder for Stage S4:
    # include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/benchmark.cmake)
    message(STATUS "LSM-KV: LSM_BUILD_BENCHMARK is ON but no benchmarks defined yet (S0 skeleton).")
endif()