cmake_minimum_required(VERSION 3.20)
project(ulac-r VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------
# Build options
# ---------------------------------------------------------------
option(ULACR_BUILD_TOOLS  "Build encoder/decoder CLI tools" ON)
option(ULACR_BUILD_TESTS  "Build unit tests"                ON)
option(ULACR_DISABLE_RESEARCH_MODULES "Disable research modules for faster builds" ON)

# ---------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------
if(MSVC)
    add_compile_options(/W4 /utf-8)
    # Release-mode optimization
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " /O2 /GL")
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    # Release-mode optimization
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " -O3 -march=native")
endif()

# ---------------------------------------------------------------
# Core library
# ---------------------------------------------------------------
add_subdirectory(src)

# ---------------------------------------------------------------
# CLI tools
# ---------------------------------------------------------------
if(ULACR_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# ---------------------------------------------------------------
# Tests
# ---------------------------------------------------------------
if(ULACR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
