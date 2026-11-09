cmake_minimum_required(VERSION 3.16)
project(fsmc
    VERSION 0.8.0
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

if(NOT DEFINED PROJECT_IS_TOP_LEVEL)
    string(COMPARE EQUAL "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}" PROJECT_IS_TOP_LEVEL)
endif()

# Output binaries directly to build/bin when building fsmc standalone
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

# Project build options
option(FSMC_ENABLE_TESTING "Build the test suites" ${PROJECT_IS_TOP_LEVEL})
option(FSMC_ENABLE_EXAMPLES "Build example applications" ${PROJECT_IS_TOP_LEVEL})
option(FSMC_ENABLE_BENCHMARKS "Build micro-benchmarks" ${PROJECT_IS_TOP_LEVEL})
option(FSMC_ENABLE_SANITIZERS "Enable Address and Undefined sanitizers (Debug only)" OFF)
option(FSMC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (-Werror)" OFF)

# Windows platform definitions
if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

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

# Fast compiler cache support (ccache)
if(PROJECT_IS_TOP_LEVEL)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        message(STATUS "Found ccache: ${CCACHE_PROGRAM}")
        set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
        set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "${CCACHE_PROGRAM}")
    endif()
endif()

# ==============================================================================
# Modular Interface Library Targets (fsmc::runtime)
# ==============================================================================
add_library(fsmc_runtime INTERFACE)
add_library(fsmc::fsmc_runtime ALIAS fsmc_runtime)
add_library(fsmc::runtime ALIAS fsmc_runtime)

target_include_directories(fsmc_runtime INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(fsmc_runtime INTERFACE cxx_std_17)
target_link_libraries(fsmc_runtime INTERFACE Threads::Threads)

# ==============================================================================
# Compiled Compiler Engine Target (fsmc_compiler)
# ==============================================================================
file(GLOB_RECURSE FSMC_COMPILER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_library(fsmc_compiler STATIC ${FSMC_COMPILER_SOURCES})
add_library(fsmc::fsmc_compiler ALIAS fsmc_compiler)
add_library(fsmc::compiler ALIAS fsmc_compiler)
add_library(fsmc::ir ALIAS fsmc_compiler)
add_library(fsmc::middleend ALIAS fsmc_compiler)
add_library(fsmc::frontend ALIAS fsmc_compiler)
add_library(fsmc::backend ALIAS fsmc_compiler)

target_include_directories(fsmc_compiler PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(fsmc_compiler PRIVATE cxx_std_20)
target_compile_features(fsmc_compiler INTERFACE cxx_std_17)
target_link_libraries(fsmc_compiler PUBLIC fsmc_runtime ${CMAKE_DL_LIBS})

find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    target_link_libraries(fsmc_compiler PRIVATE ZLIB::ZLIB)
    target_compile_definitions(fsmc_compiler PRIVATE FSMC_HAS_ZLIB=1)
endif()

# Native Precompiled Headers (PCH) in CMake 3.16+
target_precompile_headers(fsmc_compiler PRIVATE
    <string>
    <string_view>
    <vector>
    <map>
    <unordered_map>
    <memory>
    <algorithm>
    <utility>
    <cstdint>
)

# Include CMake code generation helper functions
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FsmcTools.cmake)


# Core CLI compiler & IR toolchain targets
add_subdirectory(tools)

# Python 3 interpreter for test scripts, catalog generation & doc quality gates
find_package(Python3 COMPONENTS Interpreter)

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
        COMMENT "Generating docs/reference/test_suite_catalog.md from in-code test comments..."
        VERBATIM
    )

    add_custom_target(check_test_catalog
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_test_catalog.py --check
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Validating that docs/reference/test_suite_catalog.md is synchronized with in-code test comments..."
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

    add_custom_target(check_docs
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify_docs.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Validating documentation veracity, links, freshness, and version synchronization..."
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

install(TARGETS fsmc_runtime fsmc_compiler
    EXPORT fsmcTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
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
