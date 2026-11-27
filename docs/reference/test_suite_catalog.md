# Master Test Suite & Behavioral Verification Catalog

> **Note**: This catalog is automatically generated from in-code file and `@brief Test Intent` Doxygen comments across `tests/` and `examples/`.
> To update this file, run: `cmake --build build --target generate_test_catalog` or `python3 scripts/generate_test_catalog.py`.

**Total Documented Subsystems**: 12  
**Total Test Suites & Binaries**: 92  
**Total Documented Test Cases**: 838  

---

## Core Runtime Subsystem

| Suite File | Source Path | Test Cases | Scope Summary |
| :--- | :--- | :---: | :--- |
| [`test_async_and_guards.cpp`](../../tests/backend/cpp/runtime/test_async_and_guards.cpp) | `tests/backend/cpp/runtime/test_async_and_guards.cpp` | 13 | Unit test suite for asynchronous dispatching, thread-safe workers, and guard evaluation. |
| [`test_choice.cpp`](../../tests/backend/cpp/runtime/test_choice.cpp) | `tests/backend/cpp/runtime/test_choice.cpp` | 1 | Unit test suite for choice pseudostates and conditional branching in runtime. |
| [`test_composite_guards.cpp`](../../tests/backend/cpp/runtime/test_composite_guards.cpp) | `tests/backend/cpp/runtime/test_composite_guards.cpp` | 5 | Unit test suite for composite boolean guard expressions and combinators. |
| [`test_context_contract.cpp`](../../tests/backend/cpp/runtime/test_context_contract.cpp) | `tests/backend/cpp/runtime/test_context_contract.cpp` | 6 | Unit test suite for domain context contracts, signal validators, and compile-time safety. |
| [`test_deep_history_multi_level.cpp`](../../tests/backend/cpp/runtime/test_deep_history_multi_level.cpp) | `tests/backend/cpp/runtime/test_deep_history_multi_level.cpp` | 4 | Unit test suite for multi-level deep history pseudostate restoration. |
| [`test_deferred.cpp`](../../tests/backend/cpp/runtime/test_deferred.cpp) | `tests/backend/cpp/runtime/test_deferred.cpp` | 10 | Unit test suite for deferred event queueing, capacity limits, and replay semantics. |
| [`test_flight_recorder.cpp`](../../tests/backend/cpp/runtime/test_flight_recorder.cpp) | `tests/backend/cpp/runtime/test_flight_recorder.cpp` | 9 | Unit test suite for circular flight recorder, state history logging, and diagnostics. |
| [`test_fsm.cpp`](../../tests/backend/cpp/runtime/test_fsm.cpp) | `tests/backend/cpp/runtime/test_fsm.cpp` | 8 | Unit test suite for core synchronous and thread-safe FSM runtime execution. |
| [`test_hfsm.cpp`](../../tests/backend/cpp/runtime/test_hfsm.cpp) | `tests/backend/cpp/runtime/test_hfsm.cpp` | 2 | Unit test suite for hierarchical composite states and HFSM event dispatching. |
| [`test_history.cpp`](../../tests/backend/cpp/runtime/test_history.cpp) | `tests/backend/cpp/runtime/test_history.cpp` | 5 | Unit test suite for shallow and deep history pseudostates in runtime. |
| [`test_internal_transition.cpp`](../../tests/backend/cpp/runtime/test_internal_transition.cpp) | `tests/backend/cpp/runtime/test_internal_transition.cpp` | 2 | Unit test suite for internal state transitions without entry/exit execution. |
| [`test_observer.cpp`](../../tests/backend/cpp/runtime/test_observer.cpp) | `tests/backend/cpp/runtime/test_observer.cpp` | 4 | Unit test suite for observer pattern hooks and transition telemetry. |
| [`test_policy_config.cpp`](../../tests/backend/cpp/runtime/test_policy_config.cpp) | `tests/backend/cpp/runtime/test_policy_config.cpp` | 7 | Unit test suite for policy-based design and fluent state machine configuration. |
| [`test_ring_buffer_overflow.cpp`](../../tests/backend/cpp/runtime/test_ring_buffer_overflow.cpp) | `tests/backend/cpp/runtime/test_ring_buffer_overflow.cpp` | 3 | Unit test suite for ring buffer overflow policies (DropIncoming, DropOldest). |
| [`test_spsc_fsm.cpp`](../../tests/backend/cpp/runtime/test_spsc_fsm.cpp) | `tests/backend/cpp/runtime/test_spsc_fsm.cpp` | 6 | Unit test suite for Single-Producer Single-Consumer (SPSC) lock-free FSM runtime. |
| [`test_spsc_queue.cpp`](../../tests/backend/cpp/runtime/test_spsc_queue.cpp) | `tests/backend/cpp/runtime/test_spsc_queue.cpp` | 5 | Unit test suite for lock-free SPSC ring buffer memory management and lifecycles. |
| [`test_state_serialization.cpp`](../../tests/backend/cpp/runtime/test_state_serialization.cpp) | `tests/backend/cpp/runtime/test_state_serialization.cpp` | 1 | Unit tests for C++ runtime state snapshot binary serialization and deserialization. |
| [`test_thread_safe_stress.cpp`](../../tests/backend/cpp/runtime/test_thread_safe_stress.cpp) | `tests/backend/cpp/runtime/test_thread_safe_stress.cpp` | 5 | Unit test suite for high-concurrency stress testing of thread_safe_fsm. |
| [`test_timed_transitions.cpp`](../../tests/backend/cpp/runtime/test_timed_transitions.cpp) | `tests/backend/cpp/runtime/test_timed_transitions.cpp` | 14 | Unit test suite for timed transitions, timer cancellation, and discrete tick stepping. |
| [`test_traits_and_hooks.cpp`](../../tests/backend/cpp/runtime/test_traits_and_hooks.cpp) | `tests/backend/cpp/runtime/test_traits_and_hooks.cpp` | 13 | Unit test suite for type-list traits, metaprogramming algorithms, and reflection hooks. |
| [`test_zero_alloc_runtime.cpp`](../../tests/backend/cpp/runtime/test_zero_alloc_runtime.cpp) | `tests/backend/cpp/runtime/test_zero_alloc_runtime.cpp` | 8 | Unit test suite for zero-allocation runtime, static ring buffers, and embedded static vectors. |

## C++ Backend Codegen Subsystem

| Suite File | Source Path | Test Cases | Scope Summary |
| :--- | :--- | :---: | :--- |
| [`test_c_abi_zero_alloc.cpp`](../../tests/backend/cpp/test_c_abi_zero_alloc.cpp) | `tests/backend/cpp/test_c_abi_zero_alloc.cpp` | 0 | Tests for pure C99 zero-allocation ABI generation (*_c.h + *_c.cpp) over the unified C++17 FSM backend. |
| [`test_cpp17_standalone.cpp`](../../tests/backend/cpp/test_cpp17_standalone.cpp) | `tests/backend/cpp/test_cpp17_standalone.cpp` | 0 | Standalone verification executable for C++17 standalone code generation. |
| [`test_cpp20_standalone.cpp`](../../tests/backend/cpp/test_cpp20_standalone.cpp) | `tests/backend/cpp/test_cpp20_standalone.cpp` | 0 | Standalone verification executable for C++20 standalone code generation. |
| [`test_cpp_e2e_compiler.cpp`](../../tests/backend/cpp/test_cpp_e2e_compiler.cpp) | `tests/backend/cpp/test_cpp_e2e_compiler.cpp` | 9 | End-to-End integration and host compiler verification test suite for C++ Emitter (C++17 & C++20). |
| [`test_cpp_model_emitter.cpp`](../../tests/backend/cpp/test_cpp_model_emitter.cpp) | `tests/backend/cpp/test_cpp_model_emitter.cpp` | 40 | Unit verification suite for C++ model emitter syntax generation. |
| [`test_generated_fsm.cpp`](../../tests/backend/cpp/test_generated_fsm.cpp) | `tests/backend/cpp/test_generated_fsm.cpp` | 0 | Standalone executable validating compilation and runtime dispatch of generated FSM code. |
| [`test_split_ports_and_external_pods.cpp`](../../tests/backend/cpp/test_split_ports_and_external_pods.cpp) | `tests/backend/cpp/test_split_ports_and_external_pods.cpp` | 0 | Tests for external POD/enum type bindings (`external_binding`) and default split header emission (`*_ports.hpp` + `*_fsm.hpp`) with zero fsm.hpp dependency in the ports header. |
| [`test_typed_services_contract.cpp`](../../tests/backend/cpp/test_typed_services_contract.cpp) | `tests/backend/cpp/test_typed_services_contract.cpp` | 3 | Unit and E2E compile-time tests for explicitly typed Services (HAL / math libraries) and ActionCallOp. |

## Diagram & Emitter Backend Subsystem

| Suite File | Source Path | Test Cases | Scope Summary |
| :--- | :--- | :---: | :--- |
| [`test_companion_manifest_emitter.cpp`](../../tests/backend/diagram/test_companion_manifest_emitter.cpp) | `tests/backend/diagram/test_companion_manifest_emitter.cpp` | 5 | Unit test suite for companion manifest emission (YAML/JSON sidecar contracts). |
| [`test_diagram_export.cpp`](../../tests/backend/diagram/test_diagram_export.cpp) | `tests/backend/diagram/test_diagram_export.cpp` | 12 | Unit test suite for cross-format diagram and formal model serializers. |

## Multi-Format Emitter & Factory Subsystem

| Suite File | Source Path | Test Cases | Scope Summary |
| :--- | :--- | :---: | :--- |
| [`test_emitter_factory.cpp`](../../tests/backend/common/test_emitter_factory.cpp) | `tests/backend/common/test_emitter_factory.cpp` | 5 | Unit test suite for the multi-format emitter factory. |
| [`test_manifest_and_diagnostics.cpp`](../../tests/backend/common/test_manifest_and_diagnostics.cpp) | `tests/backend/common/test_manifest_and_diagnostics.cpp` | 0 | Tests for pure C++ SHA-256, *.manifest.json emission with cross-artifact IR hash consistency, and structured JSON diagnostics (--diagnostics=json) with counterexamples and suggested fixes. |

## MC/DC Verification & Harness Subsystem

| Suite File | Source Path | Test Cases | Scope Summary |
| :--- | :--- | :---: | :--- |
| [`test_mcdc_harness.cpp`](../../tests/backend/verification/test_mcdc_harness.cpp) | `tests/backend/verification/test_mcdc_harness.cpp` | 5 | Unit test suite for MC/DC test harness generation and independence pair analysis. |

## Formal Model Checking & nuXmv Subsystem

| Suite File | Source Path | Test Cases | Scope Summary |
| :--- | :--- | :---: | :--- |
| [`test_formal_roundtrip.cpp`](../../tests/backend/formal/test_formal_roundtrip.cpp) | `tests/backend/formal/test_formal_roundtrip.cpp` | 26 | Unit test suite verifying lossless roundtrip transpilation across all supported formats. |

## Requirements Traceability (RTM) Subsystem

| Suite File | Source Path | Test Cases | Scope Summary |
| :--- | :--- | :---: | :--- |
| [`test_rtm_emitter.cpp`](../../tests/backend/rtm/test_rtm_emitter.cpp) | `tests/backend/rtm/test_rtm_emitter.cpp` | 3 | Unit test suite for the Requirements Traceability Matrix (RTM) emitter. |

## Diagnostic Engine Subsystem

| Suite File | Source Path | Test Cases | Scope Summary |
| :--- | :--- | :---: | :--- |
| [`test_diagnostics.cpp`](../../tests/diagnostic/test_diagnostics.cpp) | `tests/diagnostic/test_diagnostics.cpp` | 3 | Unit test suite for the compiler diagnostic engine and source caret rendering. |

## Frontend Parser Subsystem

| Suite File | Source Path | Test Cases | Scope Summary |
| :--- | :--- | :---: | :--- |
| [`test_json_parser.cpp`](../../tests/frontend/common/test_json_parser.cpp) | `tests/frontend/common/test_json_parser.cpp` | 10 | Unit tests for front-end JSON statechart parser, composite states, transitions arrays, and port constraints. |
| [`test_parser_classification.cpp`](../../tests/frontend/common/test_parser_classification.cpp) | `tests/frontend/common/test_parser_classification.cpp` | 3 | Unit tests for front-end parser taxonomy and format classification (Formal vs Diagram). |
| [`test_parser_factory_and_lexer.cpp`](../../tests/frontend/common/test_parser_factory_and_lexer.cpp) | `tests/frontend/common/test_parser_factory_and_lexer.cpp` | 9 | Unit tests for ParserFactory dynamic parser resolution and LexerUtils lexical analysis routines. |
| [`test_parser_negative.cpp`](../../tests/frontend/common/test_parser_negative.cpp) | `tests/frontend/common/test_parser_negative.cpp` | 6 | Negative unit tests across front-end parsers verifying rejection of malformed, empty, or corrupt inputs. |
| [`test_xml_parser.cpp`](../../tests/frontend/common/test_xml_parser.cpp) | `tests/frontend/common/test_xml_parser.cpp` | 5 | Unit test suite for the lightweight XML parser and DOM abstraction. |
| [`test_diagram_sidecar.cpp`](../../tests/frontend/diagram/test_diagram_sidecar.cpp) | `tests/frontend/diagram/test_diagram_sidecar.cpp` | 5 | Unit test suite for the Diagram Sidecar Pattern combining diagram topology with companion manifests. |
| [`test_dot_parser.cpp`](../../tests/frontend/diagram/test_dot_parser.cpp) | `tests/frontend/diagram/test_dot_parser.cpp` | 22 | Unit test suite for the Graphviz DOT statechart parser. |
| [`test_mermaid_parser.cpp`](../../tests/frontend/diagram/test_mermaid_parser.cpp) | `tests/frontend/diagram/test_mermaid_parser.cpp` | 21 | Unit test suite for the Mermaid stateDiagram-v2 parser and frontend dialect. |
| [`test_multiline_actions.cpp`](../../tests/frontend/diagram/test_multiline_actions.cpp) | `tests/frontend/diagram/test_multiline_actions.cpp` | 0 | Unit test suite verifying multiline and braced composite action block parsing in diagram frontends (PlantUML, Mermaid). |
| [`test_plantuml_parser.cpp`](../../tests/frontend/diagram/test_plantuml_parser.cpp) | `tests/frontend/diagram/test_plantuml_parser.cpp` | 21 | Unit test suite for the PlantUML state diagram parser and frontend dialect. |
| [`test_directive_parser.cpp`](../../tests/frontend/directive/test_directive_parser.cpp) | `tests/frontend/directive/test_directive_parser.cpp` | 13 | Unit tests for front-end directive parser (@fsm:state, @fsm:defer, @fsm:signal, @fsm:port, @fsm:enum, |
| [`test_cameo_parser.cpp`](../../tests/frontend/formal/test_cameo_parser.cpp) | `tests/frontend/formal/test_cameo_parser.cpp` | 24 | Unit test suite for the Cameo Systems Modeler (OMG XMI 2.x) frontend parser. |
| [`test_scxml_parser.cpp`](../../tests/frontend/formal/test_scxml_parser.cpp) | `tests/frontend/formal/test_scxml_parser.cpp` | 21 | Unit test suite for the W3C SCXML (State Chart XML) frontend parser. |
| [`test_scxml_semantic_completeness.cpp`](../../tests/frontend/formal/test_scxml_semantic_completeness.cpp) | `tests/frontend/formal/test_scxml_semantic_completeness.cpp` | 5 | Unit test suite verifying W3C SCXML semantic completeness (parallel regions, final states, events). |
| [`test_smv_parser.cpp`](../../tests/frontend/formal/test_smv_parser.cpp) | `tests/frontend/formal/test_smv_parser.cpp` | 24 | Unit test suite for the nuXmv / SMV formal model frontend parser. |
| [`test_stateflow_parser.cpp`](../../tests/frontend/formal/test_stateflow_parser.cpp) | `tests/frontend/formal/test_stateflow_parser.cpp` | 20 | Unit test suite for the MathWorks Stateflow chart frontend parser. |
| [`test_stateflow_slx_ingestion.cpp`](../../tests/frontend/formal/test_stateflow_slx_ingestion.cpp) | `tests/frontend/formal/test_stateflow_slx_ingestion.cpp` | 5 | Unit test suite for direct ingestion of MathWorks Stateflow .slx container archives. |
| [`test_sysml2_advanced_constructs.cpp`](../../tests/frontend/formal/test_sysml2_advanced_constructs.cpp) | `tests/frontend/formal/test_sysml2_advanced_constructs.cpp` | 3 | Unit test suite for SysML v2 advanced constructs (nested/qualified packages, binding connectors, fork and join composite transitions). |
| [`test_sysml2_flight_control.cpp`](../../tests/frontend/formal/test_sysml2_flight_control.cpp) | `tests/frontend/formal/test_sysml2_flight_control.cpp` | 5 | Integration verification suite for OMG SysML v2 Flight Mission Controller pipeline. |
| [`test_sysml2_parser.cpp`](../../tests/frontend/formal/test_sysml2_parser.cpp) | `tests/frontend/formal/test_sysml2_parser.cpp` | 43 | Unit test suite for the OMG SysML v2 State Definition frontend parser. |
| [`test_sysml2_structured_data.cpp`](../../tests/frontend/formal/test_sysml2_structured_data.cpp) | `tests/frontend/formal/test_sysml2_structured_data.cpp` | 7 | Unit test suite for SysML v2 structured data types, enums, temporal triggers, and connection points. |

## Formal IR Subsystem

| Suite File | Source Path | Test Cases | Scope Summary |
| :--- | :--- | :---: | :--- |
| [`test_data_definitions.cpp`](../../tests/ir/test_data_definitions.cpp) | `tests/ir/test_data_definitions.cpp` | 6 | Unit tests for IR user-defined data structures: EnumDefinition, StructDefinition, and JSON schema serialization. |
| [`test_data_type.cpp`](../../tests/ir/test_data_type.cpp) | `tests/ir/test_data_type.cpp` | 9 | Unit tests for DataType target-agnostic type system, classification, parsing, and multi-language lowering. |
| [`test_efsm_metamodel_extensions.cpp`](../../tests/ir/test_efsm_metamodel_extensions.cpp) | `tests/ir/test_efsm_metamodel_extensions.cpp` | 5 | Unit tests for EFSM metamodel extensions: LValueTarget, ActionAssignment, StateTimeInvariant, and ConcurrencySemantics. |
| [`test_fsm_ir.cpp`](../../tests/ir/test_fsm_ir.cpp) | `tests/ir/test_fsm_ir.cpp` | 34 | Unit tests for core Intermediate Representation (FsmIr), state hierarchy, AST expressions, and serialization. |
| [`test_fsm_ir_semantic_completeness.cpp`](../../tests/ir/test_fsm_ir_semantic_completeness.cpp) | `tests/ir/test_fsm_ir_semantic_completeness.cpp` | 8 | Unit tests for target-agnostic FSM IR semantic completeness extensions: Terminate pseudostate, Timed Automata ClockDefinition, ActionAstNode primitive instructions, ChangeTrigger, typed transition endpoints, and ExecutionSemantics. |
| [`test_ir_json_and_integrity.cpp`](../../tests/ir/test_ir_json_and_integrity.cpp) | `tests/ir/test_ir_json_and_integrity.cpp` | 6 | Tests for strict canonical ir-json ingestion, schema version enforcement, and VerifyIrIntegrityPass. |

## Middle-End Verification & Transformation Subsystem

| Suite File | Source Path | Test Cases | Scope Summary |
| :--- | :--- | :---: | :--- |
| [`test_inlining_passes.cpp`](../../tests/middleend/canonicalization/test_inlining_passes.cpp) | `tests/middleend/canonicalization/test_inlining_passes.cpp` | 10 | Unit tests for submachine and choice pseudo-state inlining canonicalization passes. |
| [`test_orthogonal_product.cpp`](../../tests/middleend/canonicalization/test_orthogonal_product.cpp) | `tests/middleend/canonicalization/test_orthogonal_product.cpp` | 17 | Unit tests for OrthogonalProductPass Cartesian product canonicalization. |
| [`test_pass_manager.cpp`](../../tests/middleend/canonicalization/test_pass_manager.cpp) | `tests/middleend/canonicalization/test_pass_manager.cpp` | 9 | Unit tests for PassManager execution pipeline, pass chaining, and statistical profiling. |
| [`test_structural_lowering.cpp`](../../tests/middleend/canonicalization/test_structural_lowering.cpp) | `tests/middleend/canonicalization/test_structural_lowering.cpp` | 23 | Unit tests for Category A Structural Lowering Suite: HistoryLoweringPass, DeferredEventLoweringPass, BoundaryActionFusionPass, ForkJoinLoweringPass. |
| [`test_common_action_factoring.cpp`](../../tests/middleend/optimization/test_common_action_factoring.cpp) | `tests/middleend/optimization/test_common_action_factoring.cpp` | 6 | Unit tests for CommonActionFactoringPass redundant action hoist and sink optimizations. |
| [`test_constant_folding_and_minimization.cpp`](../../tests/middleend/optimization/test_constant_folding_and_minimization.cpp) | `tests/middleend/optimization/test_constant_folding_and_minimization.cpp` | 9 | Unit tests for ConstantFoldingPass guard evaluation and StateMinimizationPass equivalence partitioning. |
| [`test_datapath_optimizations.cpp`](../../tests/middleend/optimization/test_datapath_optimizations.cpp) | `tests/middleend/optimization/test_datapath_optimizations.cpp` | 17 | Unit tests for Category B Data-Path Optimizations: DeadActionEliminationPass, RegisterLivenessPass, TransitionFusionPass, CommonActionFactoringPass. |
| [`test_dead_state_pruning.cpp`](../../tests/middleend/optimization/test_dead_state_pruning.cpp) | `tests/middleend/optimization/test_dead_state_pruning.cpp` | 5 | Unit tests for DeadStatePruningPass reachability and dead transition analysis. |
| [`test_guard_simplification.cpp`](../../tests/middleend/optimization/test_guard_simplification.cpp) | `tests/middleend/optimization/test_guard_simplification.cpp` | 2 | Unit tests for GuardSimplificationPass boolean algebra reductions. |
| [`test_adversarial_qa.cpp`](../../tests/middleend/verification/test_adversarial_qa.cpp) | `tests/middleend/verification/test_adversarial_qa.cpp` | 0 | Adversarial, Chaos & Edge-Case QA Test Suite for fsmc. |
| [`test_concurrency_verification.cpp`](../../tests/middleend/verification/test_concurrency_verification.cpp) | `tests/middleend/verification/test_concurrency_verification.cpp` | 5 | Unit tests for concurrency safety, data race detection, and determinism verification passes. |
| [`test_efsm_interval_analysis.cpp`](../../tests/middleend/verification/test_efsm_interval_analysis.cpp) | `tests/middleend/verification/test_efsm_interval_analysis.cpp` | 10 | Unit tests for EFSMIntervalAnalyzer abstract interpretation and port interval contracts. |
| [`test_formal_safety_passes.cpp`](../../tests/middleend/verification/test_formal_safety_passes.cpp) | `tests/middleend/verification/test_formal_safety_passes.cpp` | 13 | Unit tests for Category D Formal Safety & Analysis Passes: LivelockAnalysisPass, PriorityConflictPass, TimedInvariantsVerifierPass, EventQueueBoundPass. |
| [`test_guard_satisfiability.cpp`](../../tests/middleend/verification/test_guard_satisfiability.cpp) | `tests/middleend/verification/test_guard_satisfiability.cpp` | 9 | Unit tests for GuardSatisfiabilityPass interval analysis, dead guards, and mutual exclusivity. |
| [`test_ltl_model_checker_extended.cpp`](../../tests/middleend/verification/test_ltl_model_checker_extended.cpp) | `tests/middleend/verification/test_ltl_model_checker_extended.cpp` | 0 | Unit tests for extended LTL model checking operators and EFSM interval integration. |
| [`test_model_checker_reachability.cpp`](../../tests/middleend/verification/test_model_checker_reachability.cpp) | `tests/middleend/verification/test_model_checker_reachability.cpp` | 11 | Unit tests for FSM formal validator, graph reachability, livelock cycles, and EFSM interval analysis. |
| [`test_model_checker_temporal_logic.cpp`](../../tests/middleend/verification/test_model_checker_temporal_logic.cpp) | `tests/middleend/verification/test_model_checker_temporal_logic.cpp` | 16 | Unit tests for LTL/CTL temporal logic parser, ModelChecker solver, and SMV formal specification generation. |
| [`test_timed_automata_verification.cpp`](../../tests/middleend/verification/test_timed_automata_verification.cpp) | `tests/middleend/verification/test_timed_automata_verification.cpp` | 4 | Unit tests for timed automata deadlock pass, SMV clock emission, and deterministic runtime timer management. |
| [`test_wcet_analysis.cpp`](../../tests/middleend/verification/test_wcet_analysis.cpp) | `tests/middleend/verification/test_wcet_analysis.cpp` | 4 | Unit tests for WcetAnalysisPass static execution time bounding and Zeno cycle detection. |

## Integration & Build Subsystem

| Suite File | Source Path | Test Cases | Scope Summary |
| :--- | :--- | :---: | :--- |
| [`test_full_7stage_pipeline.cpp`](../../tests/integration/pipeline/test_full_7stage_pipeline.cpp) | `tests/integration/pipeline/test_full_7stage_pipeline.cpp` | 2 | Integration tests for the verified 7-stage compilation pipeline, pass dependencies, and statistics. |
| [`test_pipeline_extensibility.cpp`](../../tests/integration/pipeline/test_pipeline_extensibility.cpp) | `tests/integration/pipeline/test_pipeline_extensibility.cpp` | 4 | Integration tests for pipeline extensibility, external Unix pipe filters, and dynamic plugin loading. |
| [`test_cli_driver_errors.cpp`](../../tests/integration/test_cli_driver_errors.cpp) | `tests/integration/test_cli_driver_errors.cpp` | 14 | Comprehensive regression tests for controlled fsmc and fsm-opt driver failure contracts. |
| [`test_cmake_integration.cpp`](../../tests/integration/test_cmake_integration.cpp) | `tests/integration/test_cmake_integration.cpp` | 0 | Integration test verifying the `fsm_target_sources` CMake build-system macro. |
| [`test_differential_semantics.cpp`](../../tests/integration/test_differential_semantics.cpp) | `tests/integration/test_differential_semantics.cpp` | 10 | Differential verification suite comparing IrSimulator (reference oracle) against generated C++17 and C++20 FSMs. |
| [`test_fsm_opt_cli_options.cpp`](../../tests/integration/test_fsm_opt_cli_options.cpp) | `tests/integration/test_fsm_opt_cli_options.cpp` | 14 | Comprehensive verification suite for all fsm-opt command-line options. |
| [`test_fsmc_cli_options.cpp`](../../tests/integration/test_fsmc_cli_options.cpp) | `tests/integration/test_fsmc_cli_options.cpp` | 27 | Comprehensive verification suite for all fsmc command-line options. |
| [`test_golden_ir_invariance.cpp`](../../tests/integration/test_golden_ir_invariance.cpp) | `tests/integration/test_golden_ir_invariance.cpp` | 9 | Golden IR cross-format roundtrip and middle-end pass invariance test suite. |
| [`test_multiformat_presets_roundtrip.cpp`](../../tests/integration/test_multiformat_presets_roundtrip.cpp) | `tests/integration/test_multiformat_presets_roundtrip.cpp` | 6 | Formal Verification and End-to-End Roundtrip Suite for Canonical Multi-Format Presets. |
