cmake_minimum_required(VERSION 3.14)
project(fsmc
    VERSION 1.0.0
    DESCRIPTION "A universal zero-overhead State Machine compiler for modern C++ (C++17/20)"
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
    add_compile_options(/W4 /WX- /permissive- /utf-8 /bigobj /FS)
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

# WebAssembly Playground target (when building with Emscripten)
option(FSMC_BUILD_WASM "Build WebAssembly playground" OFF)
if(FSMC_BUILD_WASM OR EMSCRIPTEN)
    add_executable(fsmc_wasm playground/fsmc_wasm.cpp)
    target_include_directories(fsmc_wasm PRIVATE include)
    set_target_properties(fsmc_wasm PROPERTIES
        CXX_STANDARD 20
        OUTPUT_NAME "fsmc"
        SUFFIX ".js"
    )
    target_link_options(fsmc_wasm PRIVATE
        "-lembind"
        "-sEXPORT_ES6=0"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=createFsmcModule"
        "-sALLOW_MEMORY_GROWTH=1"
    )
endif()

# Subdirectories
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(examples)
add_subdirectory(benchmarks)

# Package configuration & installation for find_package(fsmc REQUIRED)
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/fsmcConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fsmcConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/fsmcConfig.cmake"
    INSTALL_DESTINATION lib/cmake/fsmc
)

install(EXPORT fsmcTargets
    NAMESPACE fsmc::
    DESTINATION lib/cmake/fsmc
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/fsmcConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/fsmcConfigVersion.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FsmGenTools.cmake"
    DESTINATION lib/cmake/fsmc
)

install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY modules/ DESTINATION include/fsm/modules)

# Install shell completion scripts
install(FILES completions/fsmc.bash DESTINATION share/bash-completion/completions RENAME fsmc)
install(FILES completions/fsmc.zsh DESTINATION share/zsh/site-functions RENAME _fsmc)

