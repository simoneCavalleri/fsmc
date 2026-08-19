# FSM Core Runtime Test Suite (C++17)
add_executable(test_fsm test_fsm.cpp)
target_link_libraries(test_fsm PRIVATE Threads::Threads)
set_target_properties(test_fsm PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_test(NAME test_fsm COMMAND test_fsm)

# FSM Core Runtime Test Suite (Native C++20 with Concepts)
add_executable(test_fsm_cpp20 test_fsm.cpp)
target_link_libraries(test_fsm_cpp20 PRIVATE Threads::Threads)
set_target_properties(test_fsm_cpp20 PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
add_test(NAME test_fsm_cpp20 COMMAND test_fsm_cpp20)

# Diagram Parsers Test Suite (Mermaid & PlantUML)
add_executable(test_parser test_parser.cpp)
target_link_libraries(test_parser PRIVATE Threads::Threads)
add_test(NAME test_parser COMMAND test_parser)

# Code Generation Test Suite (C++17 and C++20)
add_executable(test_codegen test_codegen.cpp)
target_link_libraries(test_codegen PRIVATE Threads::Threads)
add_test(NAME test_codegen COMMAND test_codegen)

# Compiled Generated FSM Test Suite (Modular mode)
add_executable(test_generated_fsm test_generated_fsm.cpp)
target_link_libraries(test_generated_fsm PRIVATE Threads::Threads)
fsmc_target_sources(test_generated_fsm
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/../examples/connection_manager/connection.mmd
    NAME ConnectionMermaidFSM
    STANDARD 20
    NAMESPACE net_mermaid
    CONTEXT NetworkContext
    MODULAR
)
add_test(NAME test_generated_fsm COMMAND test_generated_fsm)

# C++17 Standalone Output Test Suite
add_executable(test_cpp17_standalone test_cpp17_standalone.cpp)
target_link_libraries(test_cpp17_standalone PRIVATE Threads::Threads)
set_target_properties(test_cpp17_standalone PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
fsmc_target_sources(test_cpp17_standalone
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/../examples/connection_manager/connection.puml
    NAME ConnectionFSM17
    STANDARD 17
    STANDALONE
    NAMESPACE net17
)
add_test(NAME test_cpp17_standalone COMMAND test_cpp17_standalone)

# C++20 Standalone Output Test Suite (Concepts & std::jthread)
add_executable(test_cpp20_standalone test_cpp20_standalone.cpp)
target_link_libraries(test_cpp20_standalone PRIVATE Threads::Threads)
set_target_properties(test_cpp20_standalone PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
fsmc_target_sources(test_cpp20_standalone
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/../examples/connection_manager/connection.puml
    NAME ConnectionFSM20
    STANDARD 20
    STANDALONE
    NAMESPACE net20
)
add_test(NAME test_cpp20_standalone COMMAND test_cpp20_standalone)

# UML 2.5 Hierarchical / Composite States (HFSM)
add_executable(test_hfsm test_hfsm.cpp)
target_link_libraries(test_hfsm PRIVATE Threads::Threads)
add_test(NAME test_hfsm COMMAND test_hfsm)

# UML 2.5 Choice Pseudostates
add_executable(test_choice test_choice.cpp)
target_link_libraries(test_choice PRIVATE Threads::Threads)
add_test(NAME test_choice COMMAND test_choice)

# UML 2.5 Internal Transitions
add_executable(test_internal_transition test_internal_transition.cpp)
target_link_libraries(test_internal_transition PRIVATE Threads::Threads)
add_test(NAME test_internal_transition COMMAND test_internal_transition)

# UML 2.5 History States
add_executable(test_history test_history.cpp)
target_link_libraries(test_history PRIVATE Threads::Threads)
add_test(NAME test_history COMMAND test_history)

# UML 2.5 Deferred Events
add_executable(test_deferred test_deferred.cpp)
target_link_libraries(test_deferred PRIVATE Threads::Threads)
add_test(NAME test_deferred COMMAND test_deferred)

# SysML v2 Textual Parser Test Suite
add_executable(test_sysml2_parser test_sysml2_parser.cpp)
target_link_libraries(test_sysml2_parser PRIVATE Threads::Threads)
add_test(NAME test_sysml2_parser COMMAND test_sysml2_parser)

# Cameo / MagicDraw OMG XMI Parser Test Suite
add_executable(test_cameo_parser test_cameo_parser.cpp)
target_link_libraries(test_cameo_parser PRIVATE Threads::Threads)
add_test(NAME test_cameo_parser COMMAND test_cameo_parser)

# W3C SCXML Parser Test Suite
add_executable(test_scxml_parser test_scxml_parser.cpp)
target_link_libraries(test_scxml_parser PRIVATE Threads::Threads)
add_test(NAME test_scxml_parser COMMAND test_scxml_parser)

# XState JSON Parser Test Suite
add_executable(test_json_parser test_json_parser.cpp)
target_link_libraries(test_json_parser PRIVATE Threads::Threads)
add_test(NAME test_json_parser COMMAND test_json_parser)

# Graphviz DOT Parser Test Suite
add_executable(test_dot_parser test_dot_parser.cpp)
target_link_libraries(test_dot_parser PRIVATE Threads::Threads)
add_test(NAME test_dot_parser COMMAND test_dot_parser)

# CMake integration test (verifying fsmc_target_sources macro)
add_executable(test_cmake_integration test_cmake_integration.cpp)
target_link_libraries(test_cmake_integration PRIVATE Threads::Threads)
fsmc_target_sources(test_cmake_integration
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/../examples/connection_manager/connection.mmd
    NAME ConnectionFSM
    STANDARD 20
    STANDALONE
    NAMESPACE net_cmake
)
add_test(NAME test_cmake_integration COMMAND test_cmake_integration)

# Composite Guards & Boolean Expressions Test Suite
add_executable(test_composite_guards test_composite_guards.cpp)
target_link_libraries(test_composite_guards PRIVATE Threads::Threads)
add_test(NAME test_composite_guards COMMAND test_composite_guards)

# Transition Observers & Tracing Test Suite
add_executable(test_observer test_observer.cpp)
target_link_libraries(test_observer PRIVATE Threads::Threads)
add_test(NAME test_observer COMMAND test_observer)

# Timed Transitions & Deadline Scheduling Test Suite
add_executable(test_timed_transitions test_timed_transitions.cpp)
target_link_libraries(test_timed_transitions PRIVATE Threads::Threads)
add_test(NAME test_timed_transitions COMMAND test_timed_transitions)

# Diagram Inter-Format Export Test Suite
add_executable(test_format_export test_format_export.cpp)
target_link_libraries(test_format_export PRIVATE Threads::Threads)
add_test(NAME test_format_export COMMAND test_format_export)



