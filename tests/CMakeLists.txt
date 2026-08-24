# Ensure test assertions remain active across all compilers in Release builds
if(MSVC)
    add_compile_options(/UNDEBUG)
else()
    add_compile_options(-UNDEBUG -Wno-unused-const-variable)
endif()

# Google Test via FetchContent
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# ==============================================================================
# Helper Function for DRY Test Declarations
# ==============================================================================
function(add_fsm_unit_test TEST_NAME)
    set(oneValueArgs STANDARD)
    set(multiValueArgs SOURCES EXTRA_LIBS)
    cmake_parse_arguments(ARG "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_SOURCES)
        set(ARG_SOURCES ${TEST_NAME}.cpp)
    endif()

    add_executable(${TEST_NAME} ${ARG_SOURCES})
    target_link_libraries(${TEST_NAME} PRIVATE fsmc_runtime GTest::gtest GTest::gtest_main ${ARG_EXTRA_LIBS})

    if(ARG_STANDARD)
        set_target_properties(${TEST_NAME} PROPERTIES CXX_STANDARD ${ARG_STANDARD} CXX_STANDARD_REQUIRED ON)
    else()
        set_target_properties(${TEST_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    endif()

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# ==============================================================================
# Core Runtime Tests
# ==============================================================================
add_fsm_unit_test(test_fsm STANDARD 17)
add_fsm_unit_test(test_fsm_cpp20 STANDARD 20 SOURCES test_fsm.cpp)
add_fsm_unit_test(test_hfsm)
add_fsm_unit_test(test_choice)
add_fsm_unit_test(test_internal_transition)
add_fsm_unit_test(test_history)
add_fsm_unit_test(test_deep_history_multi_level)
add_fsm_unit_test(test_deferred)
add_fsm_unit_test(test_composite_guards)
add_fsm_unit_test(test_observer)
add_fsm_unit_test(test_timed_transitions)
add_fsm_unit_test(test_thread_safe_stress)
add_fsm_unit_test(test_async_and_guards)
add_fsm_unit_test(test_zero_alloc_runtime)
add_fsm_unit_test(test_spsc_queue)
add_fsm_unit_test(test_context_contract)

# ==============================================================================
# Frontend Parser Tests
# ==============================================================================
add_fsm_unit_test(test_parser)
add_fsm_unit_test(test_sysml2_parser)
add_fsm_unit_test(test_cameo_parser)
add_fsm_unit_test(test_scxml_parser)
add_fsm_unit_test(test_json_parser)
add_fsm_unit_test(test_dot_parser)
add_fsm_unit_test(test_directive_parser)
add_fsm_unit_test(test_parser_negative)

# ==============================================================================
# Formal IR & Middle-End Tests
# ==============================================================================
add_fsm_unit_test(test_fsm_ir)
add_fsm_unit_test(test_model_checker)
add_fsm_unit_test(test_pass_manager)
add_fsm_unit_test(test_diagnostics)
add_fsm_unit_test(test_format_export)
add_fsm_unit_test(test_lossless_roundtrip)

# ==============================================================================
# Code Generation & Integration Tests
# ==============================================================================
add_fsm_unit_test(test_codegen)
add_fsm_unit_test(test_cpp_modular_generator)

# Compiled Generated FSM (Modular C++20)
add_fsm_unit_test(test_generated_fsm)
fsmc_target_sources(test_generated_fsm
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/../examples/connection_manager/connection.mmd
    NAME ConnectionMermaidFSM
    STANDARD 20
    NAMESPACE net_mermaid
    CONTEXT NetworkContext
    MODULAR
)

# C++17 Standalone Output
add_fsm_unit_test(test_cpp17_standalone STANDARD 17)
fsmc_target_sources(test_cpp17_standalone
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/../examples/connection_manager/connection.puml
    NAME ConnectionFSM17
    STANDARD 17
    STANDALONE
    NAMESPACE net17
)

# C++20 Standalone Output
add_fsm_unit_test(test_cpp20_standalone STANDARD 20)
fsmc_target_sources(test_cpp20_standalone
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/../examples/connection_manager/connection.puml
    NAME ConnectionFSM20
    STANDARD 20
    STANDALONE
    NAMESPACE net20
)

# CMake target_sources integration test
add_fsm_unit_test(test_cmake_integration STANDARD 20)
fsmc_target_sources(test_cmake_integration
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/../examples/connection_manager/connection.mmd
    NAME ConnectionFSM
    STANDARD 20
    STANDALONE
    NAMESPACE net_cmake
)
