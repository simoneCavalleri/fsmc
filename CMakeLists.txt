cmake_minimum_required(VERSION 3.16)
project(fsmc
    VERSION 0.5.0
    DESCRIPTION "A universal zero-overhead State Machine compiler for modern C++ (C++17/20)"
    HOMEPAGE_URL "https://github.com/simoneCavalleri/fsmc"
    LANGUAGES CXX
)


# Standard C++ configuration
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for tooling (clangd, clang-tidy, IDEs)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output binaries directly to build/bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Project build options
option(FSMC_ENABLE_TESTING "Build the test suites" ON)
option(FSMC_ENABLE_EXAMPLES "Build example applications" ON)
option(FSMC_ENABLE_BENCHMARKS "Build micro-benchmarks" ON)
option(FSMC_ENABLE_SANITIZERS "Enable Address and Undefined sanitizers (Debug only)" OFF)
option(FSMC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (-Werror)" OFF)
option(FSMC_BUILD_WASM "Build WebAssembly playground (Emscripten)" OFF)

# Compiler warnings & quality flags
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /bigobj /FS)
    if(FSMC_WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    else()
        add_compile_options(/WX-)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-Wno-overlength-strings)
    endif()
    if(FSMC_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
    if(FSMC_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# Multithreading support
find_package(Threads REQUIRED)

# ==============================================================================
# Modular Interface Library Targets (fsmc::ir, fsmc::runtime, fsmc::compiler)
# ==============================================================================
add_library(fsmc_runtime INTERFACE)
add_library(fsmc::fsmc_runtime ALIAS fsmc_runtime)
add_library(fsmc::runtime ALIAS fsmc_runtime)
add_library(fsmc::ir ALIAS fsmc_runtime)
add_library(fsmc::middleend ALIAS fsmc_runtime)
add_library(fsmc::frontend ALIAS fsmc_runtime)
add_library(fsmc::backend ALIAS fsmc_runtime)
add_library(fsmc::compiler ALIAS fsmc_runtime)

target_include_directories(fsmc_runtime INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(fsmc_runtime INTERFACE cxx_std_17)
target_link_libraries(fsmc_runtime INTERFACE Threads::Threads)

# Include CMake code generation helper functions
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FsmcTools.cmake)

# WebAssembly Playground target (when building with Emscripten)
if(FSMC_BUILD_WASM OR EMSCRIPTEN)
    add_executable(fsmc_wasm playground/fsmc_wasm.cpp)
    target_link_libraries(fsmc_wasm PRIVATE fsmc_runtime)
    target_include_directories(fsmc_wasm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_target_properties(fsmc_wasm PROPERTIES
        CXX_STANDARD 20
        OUTPUT_NAME "fsmc"
        SUFFIX ".js"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/playground"
    )
    target_link_options(fsmc_wasm PRIVATE
        "-lembind"
        "-sEXPORT_ES6=0"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=createFsmcModule"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sENVIRONMENT=web,worker,node"
        "-sSINGLE_FILE=1"
    )
endif()

# Core CLI compiler & IR toolchain targets
add_subdirectory(tools)

# Testing
if(FSMC_ENABLE_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
if(FSMC_ENABLE_EXAMPLES)
    add_subdirectory(examples)
endif()

# Benchmarks
if(FSMC_ENABLE_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# ==============================================================================
# Automated Test Catalog Generator & Validator Targets
# ==============================================================================
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_custom_target(generate_test_catalog
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_test_catalog.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating docs/TEST_SUITE_CATALOG.md from in-code test comments..."
        VERBATIM
    )

    add_custom_target(check_test_catalog
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_test_catalog.py --check
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Validating that docs/TEST_SUITE_CATALOG.md is synchronized with in-code test comments..."
        VERBATIM
    )

    add_custom_target(generate_standalone_runtime
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_standalone_runtime.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating standalone runtime single-headers from include/fsm/backend/cpp/runtime/..."
        VERBATIM
    )

    add_custom_target(check_standalone_runtime
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_standalone_runtime.py --check
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking that standalone runtimes are synchronized with include/fsm/backend/cpp/runtime/..."
        VERBATIM
    )
endif()

# ==============================================================================
# Packaging & CMake FindPackage Installation Support
# ==============================================================================
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

install(TARGETS fsmc_runtime
    EXPORT fsmcTargets
    INCLUDES DESTINATION include
)

install(EXPORT fsmcTargets
    NAMESPACE fsmc::
    DESTINATION lib/cmake/fsmc
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/fsmcConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/fsmcConfigVersion.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FsmcTools.cmake"
    DESTINATION lib/cmake/fsmc
)

install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY modules/ DESTINATION include/fsm/modules)

# Install shell completion scripts
install(FILES completions/fsmc.bash DESTINATION share/bash-completion/completions RENAME fsmc)
install(FILES completions/fsmc.zsh DESTINATION share/zsh/site-functions RENAME _fsmc)
