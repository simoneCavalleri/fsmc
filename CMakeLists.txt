cmake_minimum_required(VERSION 3.16)
project(fsmc
    VERSION 0.12.0
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

# Internal compile/link options target (never leaks to FetchContent or add_subdirectory consumers)
add_library(fsmc_project_options INTERFACE)

if(WIN32)
    target_compile_definitions(fsmc_project_options INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(MSVC)
    target_compile_options(fsmc_project_options INTERFACE /W4 /permissive- /utf-8 /bigobj /FS)
    if(FSMC_WARNINGS_AS_ERRORS)
        target_compile_options(fsmc_project_options INTERFACE /WX)
    else()
        target_compile_options(fsmc_project_options INTERFACE /WX-)
    endif()
else()
    target_compile_options(fsmc_project_options INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(fsmc_project_options INTERFACE -Wno-overlength-strings)
    endif()
    if(FSMC_WARNINGS_AS_ERRORS)
        target_compile_options(fsmc_project_options INTERFACE -Werror)
    endif()
    if(FSMC_ENABLE_SANITIZERS)
        target_compile_options(fsmc_project_options INTERFACE
            $<$<CONFIG:Debug>:-fsanitize=address,undefined>
            $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
        )
        target_link_options(fsmc_project_options INTERFACE
            $<$<CONFIG:Debug>:-fsanitize=address,undefined>
        )
    endif()
endif()

# Multithreading support (disabled on Emscripten/WASM single-threaded targets)
if(NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
endif()

# Fast compiler cache support (ccache) via modern compiler launcher
if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        message(STATUS "Found ccache: ${CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
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
if(NOT EMSCRIPTEN)
    target_link_libraries(fsmc_runtime INTERFACE Threads::Threads)
endif()

# ==============================================================================
# Modular Compiled Compiler Engine Targets (Strict DAG Layering)
#   fsmc_core      : IR + Diagnostics
#   fsmc_frontend  : 9 Formal & Diagram Parsers -> fsmc_core
#   fsmc_middleend : PassManager + 33 Passes + PluginLoader -> fsmc_core
#   fsmc_backend   : C++17/20 Codegen + Formal/Diagram/RTM/MC-DC -> fsmc_core
#   fsmc_compiler  : Aggregate INTERFACE target linking all 4 layers
# ==============================================================================
set(FSMC_PCH_HEADERS
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

# 1. fsmc_core (IR + Diagnostic)
file(GLOB_RECURSE FSMC_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ir/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/diagnostic/*.cpp"
)
add_library(fsmc_core STATIC ${FSMC_CORE_SOURCES})
add_library(fsmc::core ALIAS fsmc_core)
add_library(fsmc::ir ALIAS fsmc_core)
target_include_directories(fsmc_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(fsmc_core PRIVATE cxx_std_20)
target_compile_features(fsmc_core INTERFACE cxx_std_17)
target_link_libraries(fsmc_core PUBLIC fsmc_runtime PRIVATE $<BUILD_INTERFACE:fsmc_project_options>)
target_precompile_headers(fsmc_core PRIVATE ${FSMC_PCH_HEADERS})

# 2. fsmc_frontend (Parsers -> fsmc_core)
file(GLOB_RECURSE FSMC_FRONTEND_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/*.cpp"
)
add_library(fsmc_frontend STATIC ${FSMC_FRONTEND_SOURCES})
add_library(fsmc::frontend ALIAS fsmc_frontend)
target_compile_features(fsmc_frontend PRIVATE cxx_std_20)
target_link_libraries(fsmc_frontend PUBLIC fsmc_core PRIVATE $<BUILD_INTERFACE:fsmc_project_options>)
target_precompile_headers(fsmc_frontend REUSE_FROM fsmc_core)

find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    target_link_libraries(fsmc_frontend PRIVATE ZLIB::ZLIB)
    target_compile_definitions(fsmc_frontend PRIVATE FSMC_HAS_ZLIB=1)
endif()

# 3. fsmc_middleend (Passes + Analysis + PluginLoader -> fsmc_core)
file(GLOB_RECURSE FSMC_MIDDLEEND_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/middleend/*.cpp"
)
add_library(fsmc_middleend STATIC ${FSMC_MIDDLEEND_SOURCES})
add_library(fsmc::middleend ALIAS fsmc_middleend)
target_compile_features(fsmc_middleend PRIVATE cxx_std_20)
target_link_libraries(fsmc_middleend PUBLIC fsmc_core PRIVATE $<BUILD_INTERFACE:fsmc_project_options>)
if(NOT EMSCRIPTEN)
    target_link_libraries(fsmc_middleend PRIVATE ${CMAKE_DL_LIBS})
endif()
target_precompile_headers(fsmc_middleend REUSE_FROM fsmc_core)

# 4. fsmc_backend (Codegen + Serializers -> fsmc_core ONLY; zero frontend/middleend coupling)
file(GLOB_RECURSE FSMC_BACKEND_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/backend/*.cpp"
)
add_library(fsmc_backend STATIC ${FSMC_BACKEND_SOURCES})
add_library(fsmc::backend ALIAS fsmc_backend)
target_compile_features(fsmc_backend PRIVATE cxx_std_20)
target_link_libraries(fsmc_backend PUBLIC fsmc_core PRIVATE $<BUILD_INTERFACE:fsmc_project_options>)
target_precompile_headers(fsmc_backend REUSE_FROM fsmc_core)

# 5. fsmc_compiler (Backward-compatible aggregate INTERFACE target)
add_library(fsmc_compiler INTERFACE)
add_library(fsmc::fsmc_compiler ALIAS fsmc_compiler)
add_library(fsmc::compiler ALIAS fsmc_compiler)
target_link_libraries(fsmc_compiler INTERFACE
    fsmc_core
    fsmc_frontend
    fsmc_middleend
    fsmc_backend
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
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/fsmcConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fsmcConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/fsmcConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/fsmc"
)

install(TARGETS fsmc_runtime fsmc_core fsmc_frontend fsmc_middleend fsmc_backend fsmc_compiler
    EXPORT fsmcTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(EXPORT fsmcTargets
    NAMESPACE fsmc::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/fsmc"
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/fsmcConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/fsmcConfigVersion.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FsmcTools.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/fsmc"
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
