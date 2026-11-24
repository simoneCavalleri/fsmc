# Google Test via FetchContent (isolated from fsmc_project_options)
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    URL_HASH SHA256=1f357c27ca988c3f7c6b4bf68a9395005ac6761f034046e9dde0896e3aba00e4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# Test-only compile options (assertions active even in Release builds; linked after fsmc_project_options)
add_library(fsmc_test_options INTERFACE)
if(MSVC)
    target_compile_options(fsmc_test_options INTERFACE /UNDEBUG)
else()
    target_compile_options(fsmc_test_options INTERFACE -UNDEBUG -Wno-unused-const-variable)
endif()

# ==============================================================================
# Helper Function for DRY Test Declarations
# ==============================================================================
function(add_fsm_unit_test TEST_NAME)
    set(oneValueArgs STANDARD DIR)
    set(multiValueArgs SOURCES EXTRA_LIBS)
    cmake_parse_arguments(ARG "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_SOURCES)
        if(ARG_DIR)
            set(ARG_SOURCES ${ARG_DIR}/${TEST_NAME}.cpp)
        else()
            set(ARG_SOURCES ${TEST_NAME}.cpp)
        endif()
    endif()

    add_executable(${TEST_NAME} ${ARG_SOURCES})
    target_link_libraries(${TEST_NAME} PRIVATE fsmc_compiler fsmc_runtime fsmc_project_options fsmc_test_options GTest::gtest GTest::gtest_main ${ARG_EXTRA_LIBS})

    if(ARG_STANDARD)
        set_target_properties(${TEST_NAME} PROPERTIES CXX_STANDARD ${ARG_STANDARD} CXX_STANDARD_REQUIRED ON)
    else()
        set_target_properties(${TEST_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    endif()

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# ==============================================================================
# C++ Runtime Tests (tests/backend/cpp/runtime/)
# ==============================================================================
add_fsm_unit_test(test_fsm DIR backend/cpp/runtime STANDARD 17)
add_fsm_unit_test(test_fsm_cpp20 DIR backend/cpp/runtime STANDARD 20 SOURCES backend/cpp/runtime/test_fsm.cpp)
add_fsm_unit_test(test_hfsm DIR backend/cpp/runtime)
add_fsm_unit_test(test_choice DIR backend/cpp/runtime)
add_fsm_unit_test(test_internal_transition DIR backend/cpp/runtime)
add_fsm_unit_test(test_history DIR backend/cpp/runtime)
add_fsm_unit_test(test_deep_history_multi_level DIR backend/cpp/runtime)
add_fsm_unit_test(test_deferred DIR backend/cpp/runtime)
add_fsm_unit_test(test_composite_guards DIR backend/cpp/runtime)
add_fsm_unit_test(test_observer DIR backend/cpp/runtime)
add_fsm_unit_test(test_timed_transitions DIR backend/cpp/runtime)
add_fsm_unit_test(test_thread_safe_stress DIR backend/cpp/runtime)
add_fsm_unit_test(test_async_and_guards DIR backend/cpp/runtime)
add_fsm_unit_test(test_zero_alloc_runtime DIR backend/cpp/runtime)
add_fsm_unit_test(test_spsc_queue DIR backend/cpp/runtime)
add_fsm_unit_test(test_context_contract DIR backend/cpp/runtime)
add_fsm_unit_test(test_traits_and_hooks DIR backend/cpp/runtime)
add_fsm_unit_test(test_ring_buffer_overflow DIR backend/cpp/runtime)
add_fsm_unit_test(test_spsc_fsm DIR backend/cpp/runtime)
add_fsm_unit_test(test_policy_config DIR backend/cpp/runtime)
add_fsm_unit_test(test_flight_recorder DIR backend/cpp/runtime)

# C++17 Runtime Compatibility Matrix (pure runtime tests compiled strictly under C++17)
add_executable(test_runtime_cpp17_suite
    backend/cpp/runtime/test_async_and_guards.cpp
    backend/cpp/runtime/test_flight_recorder.cpp
    backend/cpp/runtime/test_observer.cpp
    backend/cpp/runtime/test_policy_config.cpp
    backend/cpp/runtime/test_ring_buffer_overflow.cpp
    backend/cpp/runtime/test_spsc_fsm.cpp
    backend/cpp/runtime/test_spsc_queue.cpp
    backend/cpp/runtime/test_state_serialization.cpp
    backend/cpp/runtime/test_thread_safe_stress.cpp
    backend/cpp/runtime/test_timed_transitions.cpp
    backend/cpp/runtime/test_traits_and_hooks.cpp
)
target_link_libraries(test_runtime_cpp17_suite PRIVATE fsmc_runtime fsmc_project_options fsmc_test_options GTest::gtest GTest::gtest_main)
set_target_properties(test_runtime_cpp17_suite PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_test(NAME test_runtime_cpp17_suite COMMAND test_runtime_cpp17_suite)

add_executable(test_zero_alloc_runtime_cpp17 backend/cpp/runtime/test_zero_alloc_runtime.cpp)
target_link_libraries(test_zero_alloc_runtime_cpp17 PRIVATE fsmc_runtime fsmc_project_options fsmc_test_options GTest::gtest GTest::gtest_main)
set_target_properties(test_zero_alloc_runtime_cpp17 PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_test(NAME test_zero_alloc_runtime_cpp17 COMMAND test_zero_alloc_runtime_cpp17)

# ==============================================================================
# Formal IR Tests (tests/ir/)
# ==============================================================================
add_fsm_unit_test(test_fsm_ir DIR ir)
add_fsm_unit_test(test_data_type DIR ir)
add_fsm_unit_test(test_efsm_metamodel_extensions DIR ir)
add_fsm_unit_test(test_fsm_ir_semantic_completeness DIR ir)
add_fsm_unit_test(test_data_definitions DIR ir)
add_fsm_unit_test(test_ir_json_and_integrity DIR ir)

# ==============================================================================
# Frontend Common, Directives & Lexer Tests (tests/frontend/)
# ==============================================================================
add_fsm_unit_test(test_directive_parser DIR frontend/directive)
add_fsm_unit_test(test_parser_classification DIR frontend/common)
add_fsm_unit_test(test_parser_negative DIR frontend/common)
add_fsm_unit_test(test_parser_factory_and_lexer DIR frontend/common)
add_fsm_unit_test(test_xml_parser DIR frontend/common)
add_fsm_unit_test(test_json_parser DIR frontend/common)

# ==============================================================================
# Frontend Diagram Parser Tests (tests/frontend/diagram/)
# ==============================================================================
add_fsm_unit_test(test_plantuml_parser DIR frontend/diagram)
add_fsm_unit_test(test_mermaid_parser DIR frontend/diagram)
add_fsm_unit_test(test_dot_parser DIR frontend/diagram)
add_fsm_unit_test(test_diagram_sidecar DIR frontend/diagram)
add_fsm_unit_test(test_multiline_actions DIR frontend/diagram)

# ==============================================================================
# Frontend Formal Parser Tests (tests/frontend/formal/)
# ==============================================================================
add_fsm_unit_test(test_sysml2_parser DIR frontend/formal)
add_fsm_unit_test(test_sysml2_flight_control DIR frontend/formal)
add_fsm_unit_test(test_sysml2_structured_data DIR frontend/formal)
add_fsm_unit_test(test_sysml2_advanced_constructs DIR frontend/formal)
add_fsm_unit_test(test_cameo_parser DIR frontend/formal)
add_fsm_unit_test(test_scxml_parser DIR frontend/formal)
add_fsm_unit_test(test_scxml_semantic_completeness DIR frontend/formal)
add_fsm_unit_test(test_stateflow_parser DIR frontend/formal)
if(ZLIB_FOUND)
    add_fsm_unit_test(test_stateflow_slx_ingestion DIR frontend/formal EXTRA_LIBS ZLIB::ZLIB)
else()
    add_fsm_unit_test(test_stateflow_slx_ingestion DIR frontend/formal)
endif()
add_fsm_unit_test(test_smv_parser DIR frontend/formal)

# ==============================================================================
# Middle-End Canonicalization Passes (tests/middleend/canonicalization/)
# ==============================================================================
add_fsm_unit_test(test_structural_lowering DIR middleend/canonicalization)
add_fsm_unit_test(test_pass_manager DIR middleend/canonicalization)
add_fsm_unit_test(test_inlining_passes DIR middleend/canonicalization)
add_fsm_unit_test(test_orthogonal_product DIR middleend/canonicalization)

# ==============================================================================
# Middle-End Optimization Passes (tests/middleend/optimization/)
# ==============================================================================
add_fsm_unit_test(test_datapath_optimizations DIR middleend/optimization)
add_fsm_unit_test(test_common_action_factoring DIR middleend/optimization)
add_fsm_unit_test(test_guard_simplification DIR middleend/optimization)
add_fsm_unit_test(test_dead_state_pruning DIR middleend/optimization)
add_fsm_unit_test(test_constant_folding_and_minimization DIR middleend/optimization)

# ==============================================================================
# Middle-End Verification & Formal Safety Passes (tests/middleend/verification/)
# ==============================================================================
add_fsm_unit_test(test_formal_safety_passes DIR middleend/verification)
add_fsm_unit_test(test_concurrency_verification DIR middleend/verification)
add_fsm_unit_test(test_wcet_analysis DIR middleend/verification)
add_fsm_unit_test(test_efsm_interval_analysis DIR middleend/verification)
add_fsm_unit_test(test_model_checker_reachability DIR middleend/verification)
add_fsm_unit_test(test_model_checker_temporal_logic DIR middleend/verification)
add_fsm_unit_test(test_ltl_model_checker_extended DIR middleend/verification)
add_fsm_unit_test(test_timed_automata_verification DIR middleend/verification)
add_fsm_unit_test(test_guard_satisfiability DIR middleend/verification)
add_fsm_unit_test(test_adversarial_qa DIR middleend/verification)

# ==============================================================================
# Diagnostic Tests (tests/diagnostic/)
# ==============================================================================
add_fsm_unit_test(test_diagnostics DIR diagnostic)

# ==============================================================================
# Diagram & Formal Emitter Tests (tests/backend/)
# ==============================================================================
add_fsm_unit_test(test_emitter_factory DIR backend/common)
add_fsm_unit_test(test_diagram_export DIR backend/diagram)
add_fsm_unit_test(test_companion_manifest_emitter DIR backend/diagram)
add_fsm_unit_test(test_formal_roundtrip DIR backend/formal)
add_fsm_unit_test(test_rtm_emitter DIR backend/rtm)
add_fsm_unit_test(test_mcdc_harness DIR backend/verification)

# ==============================================================================
# C++ Code Generator Backend Tests (tests/backend/cpp/)
# ==============================================================================
add_fsm_unit_test(test_cpp_model_emitter DIR backend/cpp)
add_fsm_unit_test(test_cpp_e2e_compiler DIR backend/cpp STANDARD 20)
add_fsm_unit_test(test_state_serialization DIR backend/cpp/runtime STANDARD 20)

# Compiled Generated FSM (Modular C++20)
add_fsm_unit_test(test_generated_fsm DIR backend/cpp)
fsmc_target_sources(test_generated_fsm
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/../examples/01_basic_patterns/network_protocol/connection.mmd
    NAME ConnectionMermaidFSM
    STANDARD 20
    NAMESPACE net_mermaid
    MODULAR
    ALLOW_DIAGRAM_CODEGEN
)

# C++17 Standalone Output
add_fsm_unit_test(test_cpp17_standalone DIR backend/cpp STANDARD 17)
fsmc_target_sources(test_cpp17_standalone
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/../examples/01_basic_patterns/network_protocol/connection.puml
    NAME ConnectionFSM17
    STANDARD 17
    STANDALONE
    NAMESPACE net17
    ALLOW_DIAGRAM_CODEGEN
)

# C++20 Standalone Output
add_fsm_unit_test(test_cpp20_standalone DIR backend/cpp STANDARD 20)
fsmc_target_sources(test_cpp20_standalone
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/../examples/01_basic_patterns/network_protocol/connection.puml
    NAME ConnectionFSM20
    STANDARD 20
    STANDALONE
    NAMESPACE net20
    ALLOW_DIAGRAM_CODEGEN
)

# ==============================================================================
# Integration & End-to-End Pipeline Tests (tests/integration/)
# ==============================================================================
add_fsm_unit_test(test_full_7stage_pipeline DIR integration/pipeline)
add_fsm_unit_test(test_pipeline_extensibility DIR integration/pipeline EXTRA_LIBS ${CMAKE_DL_LIBS})
set_target_properties(test_pipeline_extensibility PROPERTIES ENABLE_EXPORTS ON)
target_compile_definitions(test_pipeline_extensibility PRIVATE FSMC_TEST_CXX_COMPILER="${CMAKE_CXX_COMPILER}")
add_fsm_unit_test(test_cli_driver_errors DIR integration EXTRA_LIBS fsmc_driver_lib)

add_fsm_unit_test(test_fsmc_cli_options DIR integration EXTRA_LIBS fsmc_driver_lib)

add_fsm_unit_test(test_fsm_opt_cli_options DIR integration EXTRA_LIBS fsmc_driver_lib)

add_fsm_unit_test(test_cmake_integration DIR integration STANDARD 20)
fsmc_target_sources(test_cmake_integration
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/../examples/01_basic_patterns/network_protocol/connection.mmd
    NAME ConnectionFSM
    STANDARD 20
    STANDALONE
    NAMESPACE net_cmake
    ALLOW_DIAGRAM_CODEGEN
)

add_fsm_unit_test(test_multiformat_presets_roundtrip DIR integration STANDARD 20)
target_compile_definitions(test_multiformat_presets_roundtrip PRIVATE FSMC_INCLUDE_DIR="${PROJECT_SOURCE_DIR}/include")
add_fsm_unit_test(test_golden_ir_invariance DIR integration STANDARD 20)
add_fsm_unit_test(test_differential_semantics DIR integration STANDARD 20 EXTRA_LIBS fsmc_driver_lib)

if(Python3_FOUND)
    add_test(NAME test_documentation_freshness
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/verify_docs.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
endif()

# ==============================================================================
# Optional libFuzzer Harnesses (Clang + ASan/UBSan)
# ==============================================================================
option(FSMC_ENABLE_FUZZERS "Build libFuzzer targets under tests/fuzz/ (requires Clang)" OFF)
if(FSMC_ENABLE_FUZZERS)
    foreach(FUZZ_TARGET fuzz_all_parsers fuzz_expressions_and_guards fuzz_json_deserializer)
        add_executable(${FUZZ_TARGET} fuzz/${FUZZ_TARGET}.cpp)
        target_link_libraries(${FUZZ_TARGET} PRIVATE fsmc_compiler)
        set_target_properties(${FUZZ_TARGET} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
        target_compile_options(${FUZZ_TARGET} PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
        target_link_options(${FUZZ_TARGET} PRIVATE -fsanitize=fuzzer,address,undefined)
    endforeach()
endif()
