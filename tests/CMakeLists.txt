#Ensure test assertions remain active across all compilers in Release builds
if (MSVC)
add_compile_options(/ UNDEBUG) else() add_compile_options(-UNDEBUG - Wno - unused - const - variable) endif()

#Google Test via FetchContent
    include(FetchContent) FetchContent_Declare(
        googletest URL https :  // github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        URL_HASH SHA256 = 1f357c27ca988c3f7c6b4bf68a9395005ac6761f034046e9dde0896e3aba00e4 DOWNLOAD_EXTRACT_TIMESTAMP
            TRUE) set(gtest_force_shared_crt ON CACHE BOOL
                      "" FORCE) set(BUILD_GMOCK OFF CACHE BOOL "" FORCE) set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

        FetchContent_MakeAvailable(googletest)

#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
#Helper Function for DRY Test Declarations
#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
            function(add_fsm_unit_test TEST_NAME) set(oneValueArgs STANDARD DIR) set(
                multiValueArgs SOURCES EXTRA_LIBS) cmake_parse_arguments(ARG
                                                                         ""
                                                                         "${oneValueArgs}"
                                                                         "${multiValueArgs}" ${ARGN})

                if (NOT ARG_SOURCES) if (ARG_DIR) set(ARG_SOURCES ${ARG_DIR} / ${TEST_NAME}.cpp) else() set(
                    ARG_SOURCES ${TEST_NAME}.cpp) endif() endif()

                    add_executable(${TEST_NAME} ${ARG_SOURCES}) target_link_libraries(${
                        TEST_NAME} PRIVATE fsmc_runtime GTest::gtest GTest::gtest_main ${ARG_EXTRA_LIBS})

                        if (ARG_STANDARD) set_target_properties(${TEST_NAME} PROPERTIES CXX_STANDARD ${
                            ARG_STANDARD} CXX_STANDARD_REQUIRED ON) else() set_target_properties(${
                            TEST_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON) endif()

                            add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME}) endfunction()

#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
#C++ Runtime Tests(tests / backend / cpp / runtime /)
#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
                                add_fsm_unit_test(
                                    test_fsm DIR backend / cpp /
                                    runtime STANDARD 17) add_fsm_unit_test(test_fsm_cpp20 DIR backend / cpp /
                                                                           runtime STANDARD 20 SOURCES backend / cpp /
                                                                           runtime / test_fsm.cpp) add_fsm_unit_test(test_hfsm
                                                                                                                         DIR backend /
                                                                                                                     cpp / runtime) add_fsm_unit_test(test_choice DIR backend / cpp / runtime) add_fsm_unit_test(test_internal_transition DIR backend / cpp / runtime) add_fsm_unit_test(test_history DIR backend / cpp / runtime) add_fsm_unit_test(test_deep_history_multi_level DIR backend / cpp / runtime) add_fsm_unit_test(test_deferred
                                                                                                                                                                                                                                                                                                                                                                                                                                      DIR backend /
                                                                                                                                                                                                                                                                                                                                                                                                                                  cpp /
                                                                                                                                                                                                                                                                                                                                                                                                                                  runtime) add_fsm_unit_test(test_composite_guards
                                                                                                                                                                                                                                                                                                                                                                                                                                                                 DIR backend /
                                                                                                                                                                                                                                                                                                                                                                                                                                                             cpp / runtime) add_fsm_unit_test(test_observer DIR backend / cpp / runtime) add_fsm_unit_test(test_timed_transitions
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               DIR backend /
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           cpp /
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           runtime) add_fsm_unit_test(test_thread_safe_stress DIR backend / cpp / runtime) add_fsm_unit_test(test_async_and_guards DIR backend / cpp / runtime) add_fsm_unit_test(test_zero_alloc_runtime
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      DIR backend /
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  cpp /
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  runtime) add_fsm_unit_test(test_spsc_queue DIR backend / cpp / runtime) add_fsm_unit_test(test_context_contract DIR backend / cpp / runtime) add_fsm_unit_test(test_traits_and_hooks
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     DIR backend /
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 cpp / runtime) add_fsm_unit_test(test_ring_buffer_overflow
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      DIR backend /
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  cpp /
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  runtime) add_fsm_unit_test(test_spsc_fsm DIR backend / cpp / runtime) add_fsm_unit_test(test_policy_config
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              DIR backend /
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          cpp /
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          runtime)

#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
#Frontend Common, Directives& Lexer Tests(tests / frontend /)
#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
                                    add_fsm_unit_test(
                                        test_directive_parser
                                            DIR frontend /
                                        directive) add_fsm_unit_test(test_parser_classification DIR frontend /
                                                                     common) add_fsm_unit_test(test_parser_negative DIR
                                                                                                   frontend /
                                                                                               common) add_fsm_unit_test(test_parser_factory_and_lexer DIR frontend / common)

#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
#Frontend Diagram Parser Tests(tests / frontend / diagram /)
#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
                                        add_fsm_unit_test(
                                            test_parser
                                                DIR frontend /
                                            diagram
                                                SOURCES
                                                    frontend /
                                            diagram / test_plantuml_parser.cpp) add_fsm_unit_test(test_json_parser DIR frontend /
                                                                                                  diagram) add_fsm_unit_test(test_dot_parser DIR frontend / diagram)

#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
#Frontend Formal Parser Tests(tests / frontend / formal /)
#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
                                            add_fsm_unit_test(
                                                test_sysml2_parser DIR frontend /
                                                formal) add_fsm_unit_test(test_sysml2_flight_control DIR frontend / formal)
                                                add_fsm_unit_test(test_sysml2_structured_data DIR frontend / formal) add_fsm_unit_test(
                                                    test_cameo_parser DIR
                                                        frontend /
                                                    formal) add_fsm_unit_test(test_scxml_parser DIR frontend / formal)
                                                    add_fsm_unit_test(test_scxml_semantic_completeness DIR frontend / formal) add_fsm_unit_test(
                                                        test_stateflow_parser
                                                            DIR frontend /
                                                        formal) add_fsm_unit_test(test_smv_parser DIR frontend / formal)

#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
#Formal IR Tests(tests / ir /)
#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
                                                        add_fsm_unit_test(test_fsm_ir DIR ir)

#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
#Middle - End& Verification Tests(tests / middleend /)
#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
                                                            add_fsm_unit_test(test_data_definitions_ir DIR middleend) add_fsm_unit_test(
                                                                test_pass_manager DIR
                                                                    middleend) add_fsm_unit_test(test_middleend_passes DIR
                                                                                                     middleend /
                                                                                                 passes)
                                                                add_fsm_unit_test(
                                                                    test_middleend_v060_passes DIR middleend /
                                                                    passes EXTRA_LIBS ${
                                                                        CMAKE_DL_LIBS}) add_fsm_unit_test(test_model_checker DIR middleend / analysis)
                                                                    add_fsm_unit_test(test_model_checker_ltl DIR middleend / analysis) add_fsm_unit_test(
                                                                        test_timed_smv DIR middleend /
                                                                        analysis) add_fsm_unit_test(test_guard_satisfiability DIR middleend / analysis)

#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
#Diagnostic Tests(tests / diagnostic /)
#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
                                                                        add_fsm_unit_test(
                                                                            test_diagnostics DIR diagnostic)

#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
#Diagram& Formal Emitter Tests(tests / backend /)
#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
                                                                            add_fsm_unit_test(
                                                                                test_emitter_factory DIR backend)
                                                                                add_fsm_unit_test(
                                                                                    test_format_export DIR backend /
                                                                                    diagram
                                                                                        SOURCES
                                                                                            backend /
                                                                                    diagram /
                                                                                    test_diagram_export
                                                                                        .cpp) add_fsm_unit_test(test_lossless_roundtrip DIR backend /
                                                                                                                formal SOURCES backend /
                                                                                                                formal /
                                                                                                                test_formal_roundtrip
                                                                                                                    .cpp) add_fsm_unit_test(test_rtm_emitter DIR backend /
                                                                                                                                            rtm)

#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
#C++ Code Generator Backend Tests(tests / backend / cpp /)
#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
                                                                                    add_fsm_unit_test(
                                                                                        test_cpp_model_emitter DIR
                                                                                            backend /
                                                                                        cpp) add_fsm_unit_test(test_cpp_e2e_compiler DIR
                                                                                                                   backend /
                                                                                                               cpp STANDARD 20)

#Compiled Generated FSM(Modular C++ 20)
                                                                                        add_fsm_unit_test(
                                                                                            test_generated_fsm DIR
                                                                                                backend /
                                                                                            cpp)
                                                                                            fsmc_target_sources(
                                                                                                test_generated_fsm DIAGRAMS
                                                                                                    ${CMAKE_CURRENT_SOURCE_DIR} /
                                                                                                        ../
                                                                                                examples /
                                                                                                connection_manager /
                                                                                                connection.mmd NAME
                                                                                                    ConnectionMermaidFSM
                                                                                                        STANDARD 20 NAMESPACE net_mermaid MODULAR)

#C++ 17 Standalone Output
                                                                                                add_fsm_unit_test(test_cpp17_standalone DIR backend / cpp STANDARD 17) fsmc_target_sources(
                                                                                                    test_cpp17_standalone DIAGRAMS ${
                                                                                                        CMAKE_CURRENT_SOURCE_DIR} /
                                                                                                        ../
                                                                                                    examples /
                                                                                                    connection_manager /
                                                                                                    connection.puml NAME ConnectionFSM17
                                                                                                        STANDARD 17 STANDALONE
                                                                                                            NAMESPACE net17)

#C++ 20 Standalone Output
                                                                                                    add_fsm_unit_test(
                                                                                                        test_cpp20_standalone
                                                                                                            DIR backend /
                                                                                                        cpp STANDARD 20)
                                                                                                        fsmc_target_sources(
                                                                                                            test_cpp20_standalone DIAGRAMS ${
                                                                                                                CMAKE_CURRENT_SOURCE_DIR} /
                                                                                                                ../
                                                                                                            examples /
                                                                                                            connection_manager /
                                                                                                            connection.puml
                                                                                                                NAME ConnectionFSM20
                                                                                                                    STANDARD 20 STANDALONE NAMESPACE
                                                                                                                        net20)

#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
#Integration Tests(tests / integration /)
#== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
                                                                                                            add_fsm_unit_test(
                                                                                                                test_cmake_integration DIR integration
                                                                                                                    STANDARD 20)
                                                                                                                fsmc_target_sources(
                                                                                                                    test_cmake_integration DIAGRAMS
                                                                                                                        ${CMAKE_CURRENT_SOURCE_DIR} /
                                                                                                                            .
                                                                                                                            ./
                                                                                                                    examples /
                                                                                                                    connection_manager /
                                                                                                                    connection
                                                                                                                            .mmd NAME ConnectionFSM
                                                                                                                                STANDARD 20 STANDALONE
                                                                                                                                    NAMESPACE
                                                                                                                                        net_cmake)

                                                                                                                    add_fsm_unit_test(
                                                                                                                        test_playground_presets_roundtrip
                                                                                                                            DIR integration
                                                                                                                                STANDARD 20)
                                                                                                                        target_compile_definitions(
                                                                                                                            test_playground_presets_roundtrip
                                                                                                                                PRIVATE FSMC_INCLUDE_DIR =
                                                                                                                                    "${PROJECT_SOURCE_DIR}/include")
