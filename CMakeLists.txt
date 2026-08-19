cmake_minimum_required(VERSION 3.14)
project(fsmc
    VERSION 1.0.0
    DESCRIPTION "A zero-overhead OMG UML 2.5 Finite State Machine compiler for C++17 and C++20"
    HOMEPAGE_URL "https://github.com/simoneCavalleri/fsmc"
    LANGUAGES CXX
)

# Enforce modern C++20 standard for the CLI tool
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for clang-tidy, clangd, and IDE tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Strict compiler warnings for high code quality
if(MSVC)
    add_compile_options(/W4 /WX-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor)
endif()

# Output binaries directly to build/bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Multithreading support
find_package(Threads REQUIRED)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Include CMake helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FsmGenTools.cmake)

# Testing support
enable_testing()

# Subdirectories
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(examples)

# Install CMake integration module
install(FILES cmake/FsmGenTools.cmake DESTINATION lib/cmake/fsmc)
install(DIRECTORY include/ DESTINATION include)
