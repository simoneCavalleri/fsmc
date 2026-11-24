# Changelog

All notable changes to the **`fsmc`** project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.14.0] - 2026-11-24

### Added
- **Native Pure-C99 Bare-Metal Backend (`--target c99`, zero C++ compiler on target)**:
  - Added `C99Generator` (`include/fsm/backend/c99/c99_generator.hpp`, `src/backend/c99/c99_generator.cpp`) and `C99CodegenBackend : ICodegenBackend` wired to `PassManager::create_lowering_pipeline(TargetCapabilities::c99_baremetal())`.
  - Emits strict, zero-heap, MISRA-friendly ISO C99 (`<stem>.h` + `<stem>.c`) compiling and linking with only a C99 compiler (`-std=c99 -Wall -Wextra -Werror -pedantic`), supporting flat states, guards, registers, local POD structs, `external_ref` (`#include` + `typedef`), `services_t` callbacks with `void* user_data`, `step`, timed `tick` (`after`/`every`), FNV-1a checksummed binary `save_snapshot`/`restore_snapshot`, and `ir_sha256()`.
  - Added `--target c99` in `fsmc` CLI and `TARGET c99` / `EMIT_MANIFEST` in `cmake/FsmcTools.cmake`.
- **System-Composer Manifest Metadata (`emitted_events`, `timers`, `artifacts.target`)**:
  - Extended `ManifestEmitter` and `schemas/fsm_manifest.schema.json` with `artifacts.target` (`"cpp17"` | `"c99"`), `emitted_events` (port-routed `SignalEmitOp` signals), and `timers` (`TimeTrigger` state/duration/periodic rows), validated in CI by `scripts/verify_schemas.py`.
- **OMG SysML v2.0 Standard Conformance & Native `calc def` Service Lowering**:
  - Added native OMG SysML v2.0 parsing for `calc def <name> { in <p> : <T>; return : <R>; }` (mapping directly to pure `ServiceMethodSignature`), directional `in`/`out` parameters on `state def`, `attribute <name> : <T> := <init>;`, `assign <target> := <expr>;` statements, and `accept after <N> [ms]` triggers.
  - Lowered service calls inside action blocks (e.g. `assign pwm_duty := compute_duty(temp_c);` or `log_fault(42);`) directly into typed `ActionCallOp` instructions with `result_target`.
- **3-Way Differential Verification & Showcase 08**:
  - Added 500-step deterministic seeded randomized differential test verifying step-by-step state and register equivalence across `IrSimulator == C++17 == Native Pure-C99` (including mid-trace snapshot/restore and byte-identical determinism).
  - Added Showcase `examples/08_baremetal_c99_controller/` compiled and linked purely in C99 (`LINKER_LANGUAGE C`, zero C++ runtime).

---

## [0.13.0] - 2026-11-24

### Added
- **Direct Canonical `FsmIr` Ingestion (`--input-format=ir-json`) & Mandatory `verify-ir-integrity` Gate**:
  - Formalized the canonical `FsmIr` JSON contract (`ir_schema_version: "1.0"`) with `schemas/fsm_ir.schema.json` and lossless byte-identical round-trip between `--emit-ir` and `IrJsonParser` (`.ir.json`), including `source_line` preservation on states and transitions.
  - Added mandatory `VerifyIrIntegrityPass` (`verify-ir-integrity`) at the front of all middle-end pipelines enforcing root invariants (`E_IR_ROOT`), ID/FQN/symbol uniqueness (`E_IR_DUP_ID`), acyclic hierarchy (`E_IR_HIERARCHY`), valid transition endpoints (`E_IR_DANGLING_REF`), and datapath/service type safety (`E_IR_TYPE_MISMATCH`).
- **Explicit Typed `Services` Contract (`ServiceMethodSignature`) & Zero-Vtable Verification**:
  - Added `ServiceMethodSignature` and `ActionCallOp::result_target` to `FsmIr` with full `ir-json` round-trip, `@fsm:service` directives (SysML v2 & diagrams), YAML/JSON sidecar `services:` support, and integrity checks.
  - Generated C++17 expression-SFINAE `<Model>ServicesContract<Srv>` and `<Model>WithServices<CustomServices>` for compile-time validation of injected math / HAL services (supporting normal, overloaded, and template methods with zero vtable overhead).
- **Language-Agnostic External Type References (`external_ref`) & Default Split Headers (`*_ports.hpp` + `*_fsm.hpp`)**:
  - Added `ExternalTypeRef` (`module`, `symbol`) on `TypeDefinition` (via `.ir.json`, `@fsm:external_type` directives, and sidecar manifests) so external PODs/enums are referenced via `#include` + `using` aliases without regenerating duplicate structs across multi-FSM systems.
  - Switched file output to split mode by default (`*_ports.hpp` with zero `fsm.hpp` dependency + `*_fsm.hpp`), keeping `--standalone` as an explicit single-header option; updated `FsmcTools.cmake` and all examples.
- **Zero-Allocation Pure C99 ABI Generator (`--emit-c-abi`)**:
  - Added `CAbiGenerator` (`*_c.h` + `*_c.cpp`) emitting strict C99 `-pedantic` `extern "C"` headers with flat & local POD `struct` ports, statically aligned opaque storage, `<prefix>_step`, `<prefix>_tick`, `<prefix>_save_snapshot` / `<prefix>_restore_snapshot`, `<prefix>_services_t` callbacks with `void* user_data`, `<prefix>_ir_sha256()`, and zero-heap C++17 placement-new bridges verified by compile-time `static_assert`.
- **System Manifest (`*.manifest.json`), Real-Time Scheduling Bounds, Pure C++ SHA-256 & Structured Diagnostics (`--diagnostics=json`)**:
  - Added zero-dependency FIPS 180-4 SHA-256 (`fsm/ir/sha256.hpp`) and cross-artifact `ir_sha256` attestation across `*.manifest.json`, `*_fsm.hpp` (`inline constexpr <Model>IrSha256`), and `*_c.h` (`<PREFIX>_IR_SHA256` / `<prefix>_ir_sha256()`).
  - Added `ManifestEmitter` (`--emit-manifest[=<file>]`) with real-time `scheduling` bounds (`wcet_max_microsteps`, `max_event_queue_bound`, `zeno_free`), `schemas/fsm_manifest.schema.json`, and automated CI JSON Schema validation (`scripts/verify_schemas.py`).
  - Extended `DiagnosticEngine::render_json()` (`--diagnostics=<text|json|github>`) with structured `counterexample` (populated by `ModelCheckingPass`) and `suggested_fixes`.
  - Added seeded randomized differential testing verifying step-by-step parity across `IrSimulator == C++17 == C99 ABI` (including mid-trace snapshot/restore).

### Changed
- **Unified Modern C++17 Backend (Zero C++17/C++20 Bifurcation)**:
  - Consolidated `Cpp17StandaloneRuntime` and `Cpp20StandaloneRuntime` into a single `CppStandaloneRuntime` compiling cleanly under C++17, C++20, and C++23.
  - Canonical transition sorting (`FsmGraphOps::sort_transitions_by_priority`) now deterministically orders transitions originating from `initial_state` first, followed by alphabetical source order and priority.

### Removed
- Removed `CppStandard`, `GeneratorOptions::cpp_standard`, CLI `--std` / `--c++17` / `--c++20`, `FsmcTools.cmake` `STANDARD`, and legacy `cpp17_standalone_runtime.hpp`.

## [0.12.0] - 2026-11-24

### Added
- **Clean Default & `--freestanding` Zero-STL Profile**:
  - Added `--freestanding` CLI option in `fsmc`, `FREESTANDING` option in `FsmcTools.cmake`, and `FSMC_FREESTANDING` runtime profile emitting zero-STL C++17/C++20 headers (no `<iostream>`, `<string>`, `<functional>`, `<mutex>`, `<thread>`), verified under `-fno-exceptions -fno-rtti -Wall -Wextra -Werror` with zero heap allocations.
  - Changed the default primary FSM alias to use `::fsm::no_observer` and removed `<iostream>` from generated headers.
- **Extensible `ICodegenBackend` & `TargetCapabilities`-Driven Lowering**:
  - Added `fsm::ir::TargetCapabilities` presets (`cpp17()`, `cpp20()`, `c99_baremetal()`, `rust_no_std()`) and `fsm::backend::ICodegenBackend` / `CppCodegenBackend`.
  - Added `PassManager::add_lowering_passes_for()` and `PassManager::create_lowering_pipeline()` to dynamically schedule structural lowering passes based on target capabilities.
- **Full HFSM/EFSM `IrSimulator` & Differential Verification Suite**:
  - Extended `IrSimulator` with hierarchical LCA `on_enter`/`on_exit`/`do_activity` ordering, shallow/deep history restoration, deferred event queuing/recall, fail-closed `strict_guards` mode, and structured JSON export via `--simulate --trace-out <file.json>`.
  - Added `test_differential_semantics` CTest suite comparing `IrSimulator` against compiled C++17 and C++20 generated FSMs.
- **Pipeline Introspection & Per-Pass IR Verification (`fsm-opt`)**:
  - Added `fsm-opt --dump-pipeline` (`PassManager::dump_pipeline()`) and `--verify-ir` (`PassManager::set_verify_ir_each_pass(true)`) verifying `FsmGraphOps::is_well_formed()` after every pass (`E_IR_VERIFY`).
- **Fuzzing, C++17 Runtime Matrix & Scaling Benchmarks**:
  - Added `libFuzzer` harnesses under `tests/fuzz/` (`-DFSMC_ENABLE_FUZZERS=ON`), C++17 runtime compatibility suites (`test_runtime_cpp17_suite`, `test_zero_alloc_runtime_cpp17`), `PassManager` property-based idempotency/determinism tests, and 50/200/500-state compiler scaling benchmarks.

### Changed
- **Strict 5-Library CMake DAG & Isolated Project Options**:
  - Split compiled compiler engine into strict DAG targets (`fsmc_core`, `fsmc_frontend`, `fsmc_middleend`, `fsmc_backend`, `fsmc_driver_lib`) with internal `fsmc_project_options` target isolation, `GNUInstallDirs`, Emscripten guards, and `FSMC_HOST_EXECUTABLE` + `ALLOW_DIAGRAM_CODEGEN` in `FsmcTools.cmake`.
  - Migrated `IParser`/`IEmitter` to `DiagnosticEngine&` with multi-file `SourceManager` and `NO_COLOR`, unified `PassRegistry` (eliminating 33 wrapper classes and duplicate `FsmValidator`), added `AnalysisManager` caching and O(1) `FsmIr` state hash indices, and cleaned C++ syntax leakage from IR.
  - Aligned `conanfile.py` (component targets + `with_zlib`), `vcpkg.json` (`version` + dependencies), and CI/Release workflows (full SDK archives, `find_package(fsmc)` consumer verification, `check_test_catalog`, and TSan coverage for `test_spsc_fsm` and `test_flight_recorder`).

### Fixed
- **Codegen Security & Runtime Concurrency**:
  - Enforced fail-closed codegen on unresolved formal expressions and sanitized model names, namespaces, strings, and comments against injection.
  - Resolved SPSC/thread-safe runtime concurrency and timer issues (atomic double-buffered snapshots, generation-tagged `cancel_timer()`, RAII `PluginLoader` ABI v1 handshake, and `PassManager` destructor ordering before `dlclose`).
- **Formal Verification Soundness, Parsers & Determinism**:
  - Fixed negation handling in `EFSMIntervalAnalyzer`, `!=`/`||` handling in `GuardSatisfiabilityPass`, CTL/stuttering semantics in `ModelChecker`, iterator invalidation in `WcetAnalysisPass`, and deterministic `ir.signals` ordering in `FsmGraphOps::canonicalize()`.
  - Fixed `SimpleJsonParser` exponential duplication (`O(2^D)` -> `O(N)`), recursion depth limits across all parsers, `evaluate_numeric` UB, truncated `.slx` `Z_OK` handling, `SIGPIPE` in `PipeThroughPass`, and configurable `nuXmv` timeout.

## [0.11.0] - 2026-11-19

### Added
- **Cross-Format Golden IR & Differential E2E Verification**:
  - Added the Golden IR invariance test suite verifying lossless metadata and semantics preservation across all 9 modeling formats (`json`, `scxml`, `cameo`, `sysml2`, `smv`, `dot`, `plantuml`, `mermaid`, `stateflow` `.xml`/`.slx`) and middle-end passes.
  - Added compiled differential E2E tests (`-O0` vs `-O2`) covering C++20 hierarchy/Local/Internal transitions, C++17 `ShallowHistory`, modular non-standalone multi-TU linkage (`fsm/fsm.hpp`), `Parallel` orthogonal regions (`ForkJoinLoweringPass` + `OrthogonalProductPass`), and native `Deferred` events.
  - Added randomized property-based middle-end fuzzing in `PassManager`, adversarial malformed/truncated/deeply-nested parser fuzzing across all 9 parsers, and expanded unit coverage for `PluginLoader`, `PipeThroughPass`, `SubmachineInliningPass`, `StateTimeInvariant`, `DataType`, and `OptDriver`.
- **CI & Sanitizer Hardening**:
  - Added a dedicated `sanitizers-tsan` ThreadSanitizer CI job running concurrency tests (`test_thread_safe_stress`, `test_spsc_queue`, `test_observer`, `test_timed_transitions`, `test_ring_buffer_overflow`, `test_async_and_guards`).
  - Extended `clang-tidy` CI execution across all `src/` and `tools/` translation units.

### Changed
- **Middle-End Guard & Trigger Canonicalization**:
  - Centralized symbolic guard resolution (`resolve_transition_guard`) and canonical trigger matching helpers across middle-end analysis and optimization passes.
  - Clarified `DeferredEventLoweringPass` as an abstract/structural lowering pass for targets without native deferred-event queues, preserving `StateNode::deferred_events` in the 7-stage C++ runtime pipeline.

### Fixed
- **C++ Runtime Concurrency (TSan)**:
  - Eliminated a use-after-free shutdown race in `worker_thread_controller` (`include/fsm/backend/cpp/runtime/detail/worker_thread_controller.hpp`) when a worker observer invokes `stop_worker()` on itself (`exit_mutex_`, `exit_cv_`, and RAII loop-exit guard), and synchronized `ExceptionHandler_Registration_InvokedOnFailure` in `test_async_and_guards.cpp`.
- **9-Format Parser & Serializer Roundtrip Fidelity**:
  - Fixed `Sysml2Serializer` and `Sysml2Parser` to preserve non-default `FormalProperty` kinds (`Reachability`, `Liveness`, `Invariant`, `DeadlockFreedom`), requirement IDs, and descriptions alongside native `assert property` statements without duplicating properties.
  - Fixed roundtrip preservation of `TimeTrigger` (`after`/`every`), `ChangeTrigger` (`when(...)`), `TransitionEdgeKind::Local` and `Internal`, `ChoiceNode`, `Fork`/`Join`/`EntryPoint`/`ExitPoint`/`Final`, `traceability_reqs`, `time_invariant`, `deferred_events`, and `SignalDefinition` attributes/validators across SCXML, Cameo XMI, SysML v2, SMV, Stateflow, PlantUML, Mermaid, DOT, and JSON.
  - Hardened parser edge cases including quoted/escaped DOT labels and cluster names, MATLAB `%` comments and `~=`/`~` operators in Stateflow, `GuardExpressionParser` quoted logical literals, `Sysml2Parser` signed `int` / fractional `after` durations / nested substate parents, and XState JSON `"type": "final"` mapping.
- **Middle-End Pass Correctness & C++ Codegen**:
  - Fixed `BoundaryActionFusionPass` LCA handling for `TransitionEdgeKind::Local`, `DeferredEventLoweringPass` skipping `Local` transitions, `WcetAnalysisPass` excluding non-zero-time triggers from microstep bounds, and `LivelockAnalysisPass` / `DeadStatePruningPass` / `FsmValidator` traversing multi-endpoint `Fork`/`Join` transitions.
  - Fixed symbolic guard resolution via `FsmIr::guards` across `PriorityConflictPass`, `GuardSatisfiabilityPass`, `DeadActionEliminationPass`, `RegisterLivenessPass`, and `McdcHarnessGenerator`.
  - Fixed `OrthogonalProductPass` propagation of `deferred_events` / `traceability_reqs`, `ChoiceInliningPass` unreachable choice pruning, `ConstantFoldingPass` preservation of `do_activity` / `ActionCallOp`, and `CppModelEmitter` enum widening, literal qualification, non-constexpr string payload constructors, and guard functor shadowing.

---

## [0.10.0] - 2026-11-13

### Added
- **Typed Event Payloads End-to-End**:
  - `SemanticAnalyzer` validates payload member accesses (`payload.*`, `cmd.*`, `event.*`, `<payload_binding>.*`) against `SignalDefinition::attributes`, forbids payload references in triggerless contexts (`on_enter`, `on_exit`, `AnonymousTrigger`, `TimeTrigger`), and enforces `trivially_copyable` signal attributes under `SPSC` / `RealTime` policies.
  - `EFSMIntervalAnalyzer` propagates bounded numeric intervals from `SignalDefinition::validators` and transition guards into target-state register intervals.
  - `fsm::dispatch()` and `spsc_fsm::post()` validate `.is_valid()` on typed signal payloads (`reject_invalid_payload`, zero-overhead `if constexpr` when absent) and `spsc_fsm` enforces `std::is_trivially_copyable_v<event_variant>` at compile time.
  - `CppModelEmitter` supports `payload` qualification and omits unsafe eventless guard overloads when guards reference event payloads.
- **IR Simulator & Verification DX**:
  - Added `fsmc --simulate` / `--trace <file.json>` (`IrSimulator`) for direct `FsmIr` trace execution with payload validator enforcement.
  - Added `fsmc --export-cex-gtest <file.cpp>` (`CounterexampleGTestExporter`) to turn formal verification counterexamples into compilable GoogleTest C++ cases.
  - Added `--base-url` option to `scripts/generate_test_catalog.py` for direct repository links.

### Changed
- **IR & Middle-End Convergence**:
  - Replaced C++-coupled `"fsm::and_<...>"` string concatenation in `ConnectiveJunctionChainingPass` and `ChoiceInliningPass` with target-agnostic `GuardAstNode` composition, lowered to `::fsm::and_<...>` / `::fsm::or_<...>` / `::fsm::not_<...>` in `CppModelEmitter`.
  - Extended `CppModelEmitter::emit_actions` to lower `ActionSignature::instructions` (`StoreOp`, `PortWriteOp`, `PortReadOp`, `ActionCallOp`, `SignalEmitOp`) alongside `assignments`.
  - Standardized `TransitionEdge` guard mutations via `set_guard()` / `set_guard_ast()` and tightened EFSM trigger/guard equivalence in `StateMinimizationPass`.
- **Documentation & Verification Gate Overhaul**:
  - Overhauled the MkDocs Material documentation hierarchy, aligned CLI flags, formal language guides, runtime C++ API signatures, IR/pass references, and tutorials with the compiler implementation, and strengthened `scripts/verify_docs.py` content, link, and freshness checks.

### Fixed
- **C++ Code Generation & Compiler Portability**:
  - Prevented `-Wunused-parameter` errors under AppleClang / Clang `-Werror` in `CppModelEmitter::emit_actions` by not marking `Services& srv` as used for comment-only `SignalEmitOp` instructions and by marking `in`/`reg` used for `PortReadOp` only when `destination.name` is non-empty.
  - Appended `f` suffixes to floating-point literals assigned to `Float32` variables and output ports in generated action bodies, eliminating `-Wconversion` narrowing warnings/errors under strict compiler flags.
  - Deduplicated emitted action bodies when multiple transitions share the same action name in `CppModelEmitter::emit_actions`.
  - Updated `qualify_expression` in `CppModelEmitter` to escape C++ keyword member accesses (`expr.keyword` → `expr.keyword_`) and resolve already-escaped C++ keyword variable and port identifiers (`reg.<kw>_`, `in.<kw>_`, `out.<kw>_`) in guards and expressions.
  - Qualified `fsm_generated::signal{}` explicitly in the keyword-escaping E2E test to avoid name lookup ambiguity with C `::signal` from `<sys/signal.h>` on macOS.
- **Frontends, Parsers & Canonical IR Serialization**:
  - Fixed `Sysml2Parser` signal validator parsing to ingest standalone `assert constraint { ... }` blocks and inline attribute `assert { ... }` constraints inside `item def` (normalizing `self`, `and`, `or`, `not`), match forward-declared signal events, and sanitize target variable identifiers in assignment and `++`/`--` statements.
  - Fixed `ScxmlParser` `<datamodel>` `<data>` type inference when `type` is omitted (`bool`, `std::string`, `double`, `int32_t`, falling back to `uint32_t` instead of defaulting all types to `uint32_t`).
  - Added top-level XState-style `"context"` object deserialization with automatic type inference (`bool`, `int32_t`, `double`, `std::string`) to `FsmIrDeserializer::deserialize_json`.
  - Fixed `FsmIrSerializer::serialize_json` and `FsmIrDeserializer::deserialize_json` to preserve the `guards` array (`name`, `description`, `raw_expression`, `normalized_expression`) across JSON roundtrips.
  - Fixed `GuardExpressionParser::parse` to handle `else`, `otherwise`, and `default` branch keywords without creating invalid atomic guard structs.
- **Simulator & Counterexample Verification DX**:
  - Fixed `IrSimulator` guard evaluation to traverse structured `GuardAstNode` combinators, evaluate lowered `fsm::and_`/`or_`/`not_` expressions, and resolve named `GuardModel` entries from `FsmIr::guards`.
  - Fixed `CounterexampleGTestExporter::export_gtest` to emit `using namespace <ns>;` (propagated from `--namespace`) and instantiate `<ModelName> sm;` instead of `<ModelName>FSM sm;` so exported GTest counterexamples compile directly against generated headers.

---

## [0.9.0] - 2026-11-09

### Added
- **Formal Verification & Abstract Interpretation Soundness**:
  - Implemented direct greatest fixed-point computation for CTL $AG(P)$ formulas using `pre_all` iteration, eliminating over-approximation inversion from negated reachability.
  - Implemented relational operator inversion for formula negations (`!(a == b)` $\rightarrow$ `a != b`, `!(a < b)` $\rightarrow$ `a >= b`) to preserve sound three-valued interval evaluation.
  - Added support for top-level disjunctions (`||`, convex hull joining) and conjunctions (`&&`, interval intersection) in `EFSMIntervalAnalyzer::parse_guard_domain`.
  - Added support for compound assignment operators (`+=`, `-=`, `*=`, `/=`) in EFSM interval data-path propagation.
  - Introduced explicit `EFSMFindingKind` enum (`UnsatisfiableGuard`, `PortRangeViolation`, `VariableRangeViolation`), guaranteeing that dead transition pruning is restricted strictly to provably unsatisfiable guards.
- **Backend Synthesizers & Formats**:
  - Added word-boundary token validation to `SmvSerializer::to_smv_predicate` for domain prefix stripping (`in.`, `out.`, `reg.`), preventing accidental identifier substring mangling.
  - Added strict uppercase `TRUE`/`FALSE` boolean variable initialization in SMV output for seamless compatibility with strict nuXmv / NuSMV checkers.
  - Enhanced `McdcHarnessGenerator` with automatic AST variable collection and C++ parameter identifier sanitization for compound guard expressions.
  - Added PlantUML `@fsm:submachine` directive support for modular subchart inclusion.
- **Automated Documentation Freshness & Veracity Quality Gate**:
  - Added dedicated CMake target `check_docs` and CTest test `test_documentation_freshness` (expanding test suite to 94 tests).
  - Integrated automated validation in GitHub Actions CI for version synchronization across project files, broken link and anchor detection, and test catalog freshness.

### Fixed
- **Memory Safety & Runtime Concurrency**:
  - Fixed use-after-free and integer overflow vulnerabilities in `fsm::deserialize`.
  - Hardened Stateflow `.slx` parser against ZIP bombs and heap buffer over-reads.
  - Corrected memory ordering (`acquire`/`release`) in `spsc_fsm` and `spsc_ring_buffer`, and added `mutation_guard` to prevent worker thread livelocks upon exceptions.
  - Eliminated data races on diagnostic counters, reentrancy depth, and potential self-deadlocks during recursive `step()`/`tick()` in `thread_safe_fsm`.
  - Fixed double invocation of `do_activity` in `step(dt)` and corrected Lowest Common Ancestor (LCA) resolution for composite state transitions.
- **Frontend & Canonical IR**:
  - Fixed struct metadata overwrites by user types in `FsmIrSerializer` and `FsmIrDeserializer`.
  - Preserved bidirectional `InOut` port contracts and extended metadata fields (`min`, `max`, `unit`, `register_index`).
  - Fixed word-boundary checks for `to` target states and forward-referencing in SysML v2 statecharts.
  - Fixed unary minus negation formatting in `ExpressionAstNode::to_string()` for negative literals (`-(-x)` wrapped in parentheses).
- **Optimization Passes**:
  - Preserved initial state entry actions and opaque callbacks in `BoundaryActionFusionPass`.
  - Removed premature break on intra-region events and fixed iterator invalidation in `OrthogonalProductPass`.
  - Enforced `External` transition kind and fixed multi-level descendant traversal in `HistoryLoweringPass`.
  - Converted `RegisterLivenessPass` into a formal edge-based dataflow analysis.
  - Fixed false-positive recursive inlining detection on default names (`"Fsm"` / `"MyStateMachine"`) in `SubmachineInliningPass`.
- **Tooling & Build System**:
  - Fixed CMake and Conan target aliases and modernized include path configurations.
  - Diverted CLI informative messages to `std::cerr` to prevent Unix pipe corruption when stdout is redirected.
  - Enforced full codebase conformity to `clang-format` under `-Werror`.

---

## [0.8.0] - 2026-10-02

### Added
- **Dedicated CLI Integration Test Suites & Option Normalization (`fsmc`, `fsm-opt`)**:
  - Implemented end-to-end option coverage tests in `test_fsmc_cli_options` (19 test scenarios) and `test_fsm_opt_cli_options` (10 test scenarios), expanding the automated test suite to 93 CTest targets and 496 documented test scenarios in the test catalog.
  - Added support across all options in `fsmc` and `fsm-opt` for `--option=value` assignment style alongside standard space-separated syntax (`--input=`, `--output=`, `--target=`, `--lang=`, `--name=`, `--namespace=`, `--ns=`, `--package=`, `--format=`, `--sidecar=`, `--export=`, `-e=`, `--export-runtime=`, `--submachine-dir=`, `--rtm-format=`, `--std=`, `--engine=`, `--ltl=`, `--ctl=`).
  - Added full Unix Standard Input (`-` or `/dev/stdin`) and Standard Output (`-` or `/dev/stdout`) support for piping statechart specifications into `fsmc` and `fsm-opt`, with automatic content-based format detection (`ParserFactory::detect_format_from_content`).
- **Comprehensive Benchmark Suite & Concurrency Measurements (`benchmarks/benchmarks.cpp`)**:
  - Implemented true inter-thread lock-free SPSC handoff benchmarks (`BM_SPSC_ThreadHandshake_LockFree`, 316M ops/sec across 2 threads).
  - Added single-transition ping-pong (`BM_Dispatch_SingleTransition_PingPong`) and hierarchical substate transition benchmarks (`BM_Dispatch_HFSM_SubStatePingPong`).
  - Added fast zero-allocation CTest quality gate `test_zero_alloc_benchmark_gate` executing in CI pipelines.
- **Tutorial Curriculum & Developer Onboarding Overhaul (`docs/tutorials/`, `examples/`)**:
  - Restructured tutorial curriculum around immediate executable C++ workflows starting from Step 1 ("60-Second Quick Win" in Tutorial 1).
  - Added "3 Ways to Use `fsmc`" (Pure C++ DSL, Automated CMake Pipeline, Standalone Transpilation) to tutorial index and example catalog.
  - Added "The Binding Contract (Rosetta Stone)" mapping SysML v2 / Stateflow constructs to generated C++ structs, port contracts, and service interfaces.
  - Added runnable, zero-allocation C++20 verification harnesses to Tutorials 1, 2, and 3 demonstrating event dispatch, EFSM datapath contracts, and hierarchical transition inheritance.
- **Extended Runtime Semantics & Concurrency (`do_activity`, Sampled Change Triggers, Dual Action Execution)**:
  - Added periodic in-state continuous behavior execution via `do_activity` with deterministic tick rate in `fsm::deterministic_timer` and lifecycle trait dispatch.
  - Added discrete sampled change triggers (`SampledChangeTriggerPass`, `fsm::change_<Port, Predicate>`) synthesizing change detector registers ($z^{-1}$) and edge-trigger guards for continuous stream signals.
  - Added dual action execution semantics support for Stateflow/UML condition actions and transition actions via `fsm::seq_<Action1, Action2>` sequentially executed during transition traversal.
- **Formal Verification & Middle-End Semantic Analysis**:
  - Implemented Native Polynomial CTL Model Checking (`ModelChecker`, `LtlParser` extended to CTL grammar with `EX`, `EF`, `EG`, `AX`, `AF`, `AG`, `E[P U Q]`, `A[P U Q]`) via recursive symbolic fixed-point evaluation.
  - Implemented EFSM Abstract Interpretation Widening $\nabla$ (`EfsmIntervalAnalysis`) computing post-fixed-point bounds on integer/float registers across loop iterations and pruning statically unsatisfiable dead transitions.
- **Connective Junction Chaining & Optimization Pass**:
  - Implemented `ConnectiveJunctionChainingPass` and integrated into Stage 2 (Structural Lowering Suite) of the 7-stage verified compiler pipeline as well as standard pipeline and `fsm-opt`.
  - Atomically chains multi-hop connective junction paths into composite transitions, collapsing intermediate micro-states and fusing condition/transition actions into deterministic compound actions.
  - Added `--emit-stateflow` export option in `fsm-opt` and `--diagnostic-format=text|json|github` in both `fsmc` and `fsm-opt` with GitHub Actions workflow annotation formatting (`::warning`, `::error`).
- **Zero-Heap Time-Travel Snapshot Recorder**:
  - Implemented `fsm::snapshot_recorder<Capacity, MaxSize>` circular flight recorder with $O(1)$ ring buffer storage for deterministic state machine state checkpoints, rollback, and time-travel replay.
- **Hierarchical State Introspection & Explicit State Machine Reset**:
  - Enhanced `is_in<State>()` / `is_in_state<State>()` across `fsm`, `spsc_fsm`, and `thread_safe_fsm` to recursively check leaf and composite ancestor states in $O(1)$ constexpr time via compile-time reflection traits.
  - Implemented explicit, thread-safe `reset()` capability restoring initial state, default data registers, and active timer tables across all runtime engines.
  - Added multi-level typed deep history queries `get_history<ParentState>()` and introspection predicates `history_is<ParentState, TargetLeaf>()`.
- **Stateflow & Diagram Sidecar Parser Enhancements**:
  - Supported Stateflow state actions (`en:`, `du:`, `ex:`), temporal logic triggers (`every(N, sec)`), and XML numeric entity decoding (`&#...;`).
  - Implemented `GuardModel`-aware recursive boolean expression reconstruction in `to_diagram_string`.
- **Industrial Showcase 06: Stateflow Digital Twin ECU**:
  - Created end-to-end industrial digital twin demonstration (`examples/06_stateflow_digital_twin_ecu`) modeling an automotive powertrain engine ECU with multi-hop chained connective junctions, overheat anomaly detection, and supervisor-initiated time-travel state rollback.

### Fixed
- **Compiler Compatibility & Clean Build Flags**:
  - Restricted `-Wmismatched-new-delete` and `-Wsized-deallocation` diagnostic pragmas to GCC to resolve Clang `-Werror` build failures in benchmark suites.
  - Fixed portable `NUXMV_BIN` path resolution across CI validation scripts (`test_nuxmv_matrix.py`, `verify_diagram_renderings.py`).
  - Pruned obsolete prototype directories (`modules/`) and shell completions from repository and CMake install targets.
  - Relocated MkDocs theme overrides to `docs/overrides/` and aligned Conan source exports.
- **Modular Code Generation Parity with `--export-runtime` (`--modular`, `--runtime-header`)**:
  - Aligned `--modular` code generation to emit `#include "fsm.hpp"` by default, matching the standalone runtime exported by `--export-runtime <dir|file>`.
  - Removed internal and non-existent include directives (`#include "fsm/backend/cpp/runtime/fsm.hpp"`, `thread_safe_fsm.hpp`, `spsc_fsm.hpp`) which failed compilation against exported runtimes where all runtime engines are unified in `fsm.hpp`.
  - Added support for `--modular=<hdr>` and `--runtime-header <hdr>` (e.g. `--modular=<fsm/fsm.hpp>`, `--runtime-header="custom/fsm.hpp"`) for explicit include paths.
  - Added unified umbrella header `include/fsm/fsm.hpp` to the repository.
- **Runtime Exporter (`--export-runtime`)**:
  - Fixed directory handling: automatically appends `fsm.hpp` when given an existing directory or target path without file extension.
  - Fixed generated header pollution: eliminated invalid dummy namespace and empty `using  = ::fsm::make_fsm<...>` declarations in exported standalone runtimes, directly emitting clean `Cpp17StandaloneRuntime` or `Cpp20StandaloneRuntime` headers.
- **Formal Verification CLI Argument Ingestion (`--ltl`, `--ctl`) in `fsmc`**:
  - Integrated `LtlPropertyParser::parse` on CLI-injected `--ltl` and `--ctl` formulas to construct valid AST expression trees instead of raw atomic proposition strings, resolving verification engine failures on temporal formulas.
- **Diagnostic Rigor & `-Werror` Precision in `fsm-opt`**:
  - Fixed false-positive `-Werror` failures on sound models: informational diagnostic `Note` messages (e.g., WCET calculations or queue bound metrics) no longer trigger compilation aborts. `-Werror` now strictly triggers exclusively on `Warning`, `Fatal`, `Error`, or `SafetyCritical` diagnostics.
  - Integrated unknown pass detection in `--passes=` with `DiagnosticEngine`: unrecognized passes are now formatted diagnostics that strictly cause compilation failure under `-Werror`.
  - Added strict validation for `--diagnostic-format`, rejecting unsupported formats with an actionable error.
- **Conflicting CLI Options Detection**:
  - Added warning diagnostic (`W0102`, `W0103`) when mutually conflicting flags are supplied (e.g. `--standalone` vs `--modular`, `--c++17` vs `--c++20`), failing compilation under `-Werror`.
- **Hierarchical Deferred Event Inheritance**: Inherit deferred events from parent and ancestor composite states down to active leaf substates (`is_deferred_event_v`, `any_state_has_deferred`).
- **Deep History in Static `parent_type` Hierarchies**: Fixed deep history tracking, recording, and typed retrieval (`record_ancestor_history`, `history_is`, `get_history<Parent>()`) to resolve static ancestor hierarchies via `parent_type` and `get_state_name_static`.
- **Orthogonal Region Boundary Transitions**:
  - Fixed substate external exit transitions to replicate across all active Cartesian product states containing the substate (`OrthogonalProductPass`).
  - Fixed external entry transitions targeting a substate to automatically activate the initial substate in all companion orthogonal regions.
- **C++ Code Generation**:
  - Emitted default constructor for generated signal types containing struct/variant attributes to enable uninitialized variant construction.
  - Supported compound assignment code emission (`+=`, `-=`, `*=`, `/=`) in generated actions.
- **Category B Data-Path Optimizations & State Minimization**:
  - Preserved individual struct member writes in Dead Action Elimination (`DeadActionEliminationPass`), preventing false-positive dead store removal.
  - Prevented illegal action hoisting and sinking into pseudostates in `CommonActionFactoringPass`.
  - Prevented incorrect state mergers in `StateMinimizationPass` when states have multiple guarded transitions for the same event or unmodeled state activities (`do_activity`, time invariants).
- **Formal Frontends & MBSE Ingestion**:
  - Preserved atomic raw expressions and boolean operator precedence in SCXML, SMV, Stateflow, and Cameo resolvers.
  - Fixed fractional duration unit truncation (e.g., `1.5s` $\to$ `1500ms`) in SysML v2 and Stateflow parsers before unit conversion.
  - Supported nested parenthesis conditions, set membership (`state in { ... }`), and boolean literal normalization in SMV parser.
  - Disambiguated action delimiter slash (`/`) from guard division operators in diagram parsers.
  - Preserved SCXML history pseudostate targets across roundtrip serialization.
- **Verification, Diagnostics & Lowering**:
  - Detected livelock cycles on explicit `"anonymous_event"` and `"completion_event"` transitions in `LivelockAnalysisPass` and `FsmValidator`.
  - Resolved boundary action fusion premature clearing and preserved `TransitionKind::External` on self-transitions through junctions and choice nodes.
  - Prevented deterministic timer reentrancy cascades and modulo-by-zero errors.
  - Fixed strict weak ordering violation in `FsmGraphOps::sort_transitions_by_priority`.
- **Runtime API Ergonomics**:
  - Cleaned up `spsc_fsm` producer API by removing the redundant `push` alias in favor of the canonical `post`, and formally documented concurrency thread domains (Producer, Consumer, Concurrent Reader).

## [0.7.0] - 2026-09-29

### Added
- **MathWorks Simulink Stateflow `.slx` Native OPC Container Ingestion (`StateflowParser`)**:
  - Implemented zero-dependency, native Open Packaging Convention (OPC) / ZIP archive decompression extracting `simulink/stateflow.xml` directly from `.slx` model archives without external tools or MATLAB dependencies.
  - Full Stateflow chart roundtrip serialization and transpilation across SysML v2, SCXML, Cameo XMI, PlantUML, Mermaid, DOT, and Canonical JSON.
- **OMG SysML v2 Full Grammar Ingestion (`Sysml2Parser`)**:
  - Added recursive nested state hierarchy with fully qualified name resolution (`Parent::Child`).
  - Added port binding and connector declarations (`bind c1.p1 = c2.p2`).
  - Added orthogonal concurrency constructs: parallel state regions, `fork` splits, and `join` synchronizations.
- **Diagram Composite Braced Action Blocks (`PlantUmlParser`, `MermaidParser`)**:
  - Supported braced multiline action blocks (`/ { out.p = val; reg.v++; doWork(); }`) containing compound statements, assignments, and method invocations mapped into structured `ActionAssignment` instructions.
- **Extended Symbolic LTL Model Checking (`ModelChecker`, `LtlParser`)**:
  - Added Strong Until (`U`) temporal operator solved via backward fixed-point computation.
  - Added Next (`X`) operator and Next-Response verification (`G (P -> X Q)`).
  - Added Recurrence (`G F P`) and Persistence (`F G P`) liveness properties via Tarjan strongly connected component (SCC) reachability.
  - Integrated EFSM data-path interval abstract interpretation directly into property evaluation.
- **Zero-Heap Binary State Snapshot Serialization (`serialization.hpp`)**:
  - Embedded binary snapshot serialization and restoration for RTOS checkpoints, NVRAM retention across reboots, and hot-standby redundancy.
  - Captures active state index, history cache, active timers, residence time invariants, and datapath registers with defensive 32-bit FNV-1a checksum and magic word validation (`0x46534D43`).
  - Dual API: raw pointer + capacity for C++17 and `std::span` overloads for C++20.
- **Streamlined, Intuitive & Elegant CLI Ergonomics (`fsmc_options.hpp`, `fsmc_driver.hpp`)**:
  - Added slim option aliases: `-N, --ns <ns>`, `-p, --pipeline <mode>`, `--7stage`, `--prune`, `--no-simplify`, `--inline`, `--strict`, `--races`, `--rtm <file>`, `--harness <file>`, `--allow-diagram`, `-V`.

---

## [0.6.0] - 2026-09-11

### Breaking Changes
- **C++ Backend Preflight Lowering Contracts**:
  - Enforced strict compile-time preflight validation rejecting unlowered structural pseudostates (`Fork`, `Join`, `Choice`, `Junction` via diagnostic `ECPP009`) and unlowered parallel orthogonal regions (`ECPP010`) before C++ code emission.
  - Implemented combinatorial state bound on Cartesian product state generation (`OrthogonalProductPass`), aborting with diagnostic `EORTHO003` if product state count exceeds 1024 states.
- **Clean Nested Namespaces Architecture**:
  - Reorganized compiler and intermediate representation symbols into dedicated, clean nested namespaces:
    - `fsm::ir`: `FsmIr`, `StateNode`, `TransitionEdge`, `PortDefinition`, `RegisterDefinition`, `EnumDefinition`, `StructDefinition`, `DataType`, `FormalProperty`, `ConcurrencySemantics`.
    - `fsm::frontend`: `Sysml2Parser`, `ScxmlParser`, `StateflowParser`, `CameoParser`, `PlantUmlParser`, `MermaidParser`, `DotParser`, `JsonParser`, `XmlParser`, `DiagramSidecarLoader`.
    - `fsm::middleend`: `PassManager`, `IPass`, analysis and transformation passes across 7 stages, and `PluginLoader`.
    - `fsm::backend`: `CppGenerator`, `CppModelEmitter`, `CppBackendValidator`, diagram emitters (`PlantUmlEmitter`, `MermaidEmitter`, `Sysml2Emitter`, `StateflowSerializer`, `CameoSerializer`), `SmvEmitter`, `McdcHarnessGenerator`, `RtmEmitter`, `CompanionManifestEmitter`.
    - `fsm::diagnostic`: `DiagnosticEngine`, `Diagnostic`, `DiagnosticSeverity`.
- **JSON IR Serialization Refactoring (`fsm/ir/`)**:
  - Separated monolithic JSON handling into distinct compiled units: `fsm::ir::FsmIrSerializer` and `fsm::ir::FsmIrDeserializer`.

### Added
- **C++ Runtime State Time Invariants**:
  - Implemented zero-overhead state residence time invariant manager `detail::invariant_manager<Table, HasInvariants>`. For state machines without time invariants, the manager occupies 0 bytes (`[[no_unique_address]]`), strictly preserving `sizeof(MinimalFSM) <= 32`.
  - Added compile-time `max_stay_duration_ms` permanence limit trait emission in `CppModelEmitter`.
  - Added runtime invariant queries and callbacks across synchronous `fsm`, `spsc_fsm`, and `thread_safe_fsm`: `is_invariant_satisfied()`, `has_invariant_violation()`, `last_invariant_violation()`, and `on_invariant_violation(callback)` / `set_invariant_violation_handler(callback)`.
  - Added `fsm::invariant_violation_info` structure capturing violated state, elapsed permanence time, and maximum stay duration.
- **Structural Lowering & Boundary Action Fusion**:
  - **LCA Action Fusion (`BoundaryActionFusionPass`)**: Spliced hierarchical transition actions into strict Lowest Common Ancestor (LCA) order: leaf exit path $\to$ transition action $\to$ leaf entry path, clearing lowered state hooks to eliminate duplicate action executions.
  - **Orthogonal Product Lowering (`OrthogonalProductPass`)**: Synthesized Cartesian product states ($S_A \times S_B$) flattening concurrent orthogonal regions into deterministic single-active-state automata, with action deduplication and intra-region transition isolation.
  - **Choice & Junction Inlining (`ChoiceInliningPass`)**: Inlined dynamic choice and junction nodes into composite guarded transitions directly on model states.
  - **Fork & Join Lowering (`ForkJoinLoweringPass`)**: Lowered fork splits and join rendezvous barriers into product-state transitions.
  - **History & Deferred Lowering (`HistoryLoweringPass`, `DeferredEventLoweringPass`)**: Lowered shallow/deep history into shadow state registers and deferred events into bounded static queues.
- **Middle-End 7-Stage Pipeline & 30 Available Passes**:
  - Multi-stage pass manager pipeline (`create_verified_7stage_pipeline`) with explicit dependency and prerequisite validation.
  - 28 registered standard passes across 7 formal stages:
    - Normalization: `HierarchyFlatteningPass`, `ChoiceInliningPass`, `ForkJoinLoweringPass`, `HistoryLoweringPass`, `DeferredEventLoweringPass`, `BoundaryActionFusionPass`.
    - Orthogonal Decomposition: `OrthogonalProductPass`.
    - Local Optimization: `DeadActionEliminationPass`, `CommonActionFactoringPass`, `TransitionFusionPass`.
    - Dataflow & Datapath: `RegisterLivenessPass`, `ConstantFoldingPass`, `EFSMDataPathPass`.
    - Global Graph Simplification: `DeadStatePruningPass`, `GuardSimplificationPass`, `StateMinimizationPass`.
    - Static Safety & Determinism: `PriorityConflictPass`, `LivelockAnalysisPass`, `TimedDeadlockPass`, `TimedInvariantsVerifierPass`, `EventQueueBoundPass`, `GuardSatisfiabilityPass`, `WcetAnalysisPass`.
    - Formal Verification: `ModelCheckingPass` (LTL/CTL).
  - Extensibility: `PipeThroughPass` (`--pipe-through <cmd>`) for Unix streaming filter pipelines and dynamic runtime C++ plugins (`--load-pass-plugin <path.so>`) via `dlopen`.
- **Frontend MBSE Semantic Completeness**:
  - **SysML v2 Structured Types & Triggers**: `enum def`, `struct def` / `datatype def`, dot-notation member access in guards and actions (`in.telemetry.altitude > 1000.0`), `accept after <duration>`, `accept at <time>`, `entry point`/`exit point`, `fork`/`join`, signal send actions (`do send <Signal>(...) via <Port>`).
  - **W3C SCXML Completeness**: `<parallel id="...">` orthogonal regions, internal event cascades (`<send>`, `<raise>`), state termination and completion events via `<final>` and `done.state.<id>`.
  - **MathWorks Simulink Stateflow Ingestion & Serialization**: Native parser (`StateflowParser`) and serializer (`StateflowSerializer`) for Simulink Stateflow charts, enabling closed-loop lossless 8-format roundtrip conversion across SysML v2, SCXML, Stateflow, Cameo XMI, PlantUML, Mermaid, Graphviz DOT, and Canonical JSON.
  - **Diagram Companion Manifest Sidecars**: Companion sidecar configuration manifests (`-s, --sidecar <file.yaml|json>`) for non-textual or informal diagram formats.
- **C++ Runtime & Safety Tooling**:
  - **Deterministic Real-Time Timers**: Integrated `deterministic_timer_manager` into `fsm`, `spsc_fsm`, and `thread_safe_fsm` with `sm.tick(dt)` and `sm.step(dt)`, plus timer expiry callbacks `sm.tick(dt, on_expired)`.
  - **Non-Default-Constructible Services**: Added constructor support and forwarding for service interfaces that do not provide a default constructor.
  - **Blackbox Flight Recorder (`with_trace_buffer<N>`)**: Zero-allocation circular ring buffer (`TraceBuffer<Capacity>`) logging transition history, states, events, and tick timestamps.
  - **MC/DC Test Harness Synthesis (`--emit-test-harness <file>`)**: Automated derivation of independence pairs ($n+1$) from composite boolean guards (`McdcHarnessGenerator`), synthesizing complete GoogleTest harnesses.
  - **Requirements Traceability Matrix (`--req-audit`, `--rtm-output`)**: Formal verification audit mapping `@fsm:req` directives to states, transitions, and properties with report generation in Markdown and JSON.
  - **Synchronized Standalone Runtimes**: Single-header self-contained runtimes `cpp17_standalone_runtime.hpp` and `cpp20_standalone_runtime.hpp`.
- **CLI Robustness & Error Contracts**:
  - Comprehensive argument and input validation across `fsmc` and `fsm-opt`, guaranteeing graceful non-zero termination with stable diagnostic categories on missing inputs, unknown options, missing option arguments, nonexistent files, or malformed models without abnormal process crashes.
- **Showcase Examples**:
  - **Standalone Embedded IoT Controller** (`examples/00_standalone_iot_controller/`): Zero-dependency single-header deployment targeting bare-metal microcontrollers and embedded Linux gateways.
  - **Avionics Flight Management System (FMS)** (`examples/04_formal_verification/flight_control_modes/`): Dual-axis flight guidance modes with formal LTL/CTL model checking and MC/DC test harness generation.
- **Build Infrastructure**:
  - Added `fsmc_compiler` static library target with native Precompiled Headers (PCH) in CMake.
  - Scaled test infrastructure to **84 CTest targets** (100% pass rate) with automated verification catalog validation (`scripts/generate_test_catalog.py`).

### Changed
- **Metamodel Header Organization**:
  - Relocated metamodel and graph headers into clean include layout under `include/fsm/ir/`: `clock_definition.hpp`, `concurrency_semantics.hpp`, `data_type.hpp`, and `fsm_graph_ops.hpp`.
- **Runtime Include Paths**:
  - Standardized C++ runtime headers under `<fsm/backend/cpp/runtime/...>`.

### Removed
- **Decommissioning of WebAssembly Playground**:
  - Removed deprecated in-repo browser playground (`playground/`) to focus engineering resources on native CLI tooling (`fsmc`, `fsm-opt`), hard real-time zero-overhead C++ runtime engines, and formal MBSE interoperability.

### Fixed
- **Destruction Order Crash**: Fixed heap corruption / segfault when non-default-constructible services or registered resources were destructed out of order.
- **OrthogonalProductPass Zombie Transitions**: Fixed invalid transitions referring to pruned source or target sub-states during Cartesian product lowering.
- **Pinned Parent Invalidation**: Preserved hierarchical state parenting during deep copy and transformation passes (`pinned_parent`).
- **Cross-Platform nuXmv & Plugin Discovery**: Fixed binary path lookup for nuXmv and dynamic shared library loading (`.so` / `.dylib`) in `PluginLoader`.
- **Qualified Service & Port Emitted Types**: Fixed namespace collision in `CppModelEmitter` where emitted `fsm::no_services`, `fsm::no_ports`, and `fsm::no_registers` conflicted with user namespaces ending in `::fsm` (e.g. `uav::fsm`).
- **CLI Error Handling & Stability**: Eliminated abnormal crashes and unhandled exceptions on invalid CLI flags, nonexistent files, or malformed models.

---

## [0.5.0] - 2026-09-03

### Breaking Changes
- **Safe-by-Design Concurrency (`thread_safe_fsm.hpp`)**:
  - Removed direct unsynchronized mutable `registers()` from `thread_safe_fsm`. All register accesses in multi-threaded environments are now forced to go through thread-safe APIs: `with_registers([](auto& r) { ... })`, `update_registers(reg)`, or `snapshot_registers()`.
- **Policy-Based Configuration (`config.hpp`, `fsm.hpp`, `spsc_fsm.hpp`, `thread_safe_fsm.hpp`)**:
  - Introduced `fsm::config<Table, Policies...>` and semantic modifier traits (`with_registers<T>`, `with_ports<In, Out>`, `with_services<Srv>`, `with_observer<Obs>`, `with_deferred_capacity<N>`, `with_queue_capacity<N>`), eliminating the need to pass up to 8 positional template arguments.
  - Added modern factory aliases: `fsm::make_fsm<Table, Policies...>`, `fsm::make_spsc_fsm<Table, Policies...>`, and `fsm::make_thread_safe_fsm<Table, Policies...>`.

### Added
- **Integrated Formal Verification CLI (`tools/fsmc`)**:
  - Added the `verify` sub-command and `--verify` / `--check` flags with automated temporal property validation.
  - Added CLI flags `--engine=auto|nuxmv`, `--ltl "<formula>"`, and `--ctl "<formula>"` allowing ad-hoc temporal property verification directly from the command line.
  - Formatted formal model checking report with pass/violation diagnostics and counterexample execution trace generation.
- **Modern Tabbed IDE Playground (`fsmc Studio`)**:
  - Redesigned the WebAssembly playground into a spacious 2-pane Tabbed IDE Workbench:
    - **Main Canvas Tabs**: `Split View` (Editor + Diagram side-by-side), `Statechart Editor`, `Topological Graph`, and `Generated C++`.
    - **Dockable Inspector Tabs**: `Simulator & Datapath` and `Formal Verification & SMT`.
    - **Interactive 4-Domain Datapath**: Live sliders for continuous numeric `InPorts`, toggle switches for boolean signals, dynamic register memory chips, and actuator status LEDs.
    - **Dual Execution Models**: Reactive event dispatch and periodic cyclic sampled execution (`Clock Step(dt)`).
    - **IDE Status Bar**: Bottom status bar displaying active preset, source format, line/char count, SMT soundness, and zero-allocation target.
    - **Visual Polish**: Replaced all emojis with clean geometric inline SVG icons, streamlined non-wrapping header toolbar, and direct SVG diagram export.
- **Frontend C++ Keyword Escaping (`parser_interface.hpp`)**:
  - Added C++ reserved keyword detection and escaping utilities (`is_cpp_keyword`, `escape_cpp_keyword`), preventing syntax errors when state/event identifiers collide with C++ keywords.
- **Expanded Unit Test Suite**:
  - Added dedicated test suite `test_policy_config.cpp` for policy extraction and safe concurrency.
  - Added tests for compile-time type list lookup, non-default-constructible payloads and services, and zero-allocation interval parsing.

### Refactored & Modularized
- **Web Playground Architecture Modularization (`playground/src/`)**:
  - Refactored monolithic 2800-line `playground.js` into 16 clean, focused ES native modules under `playground/src/` (`presets.js`, `wasm_bridge.js`, `fsm_utils.js`, `parsers/`, `serializers/`, `cpp_generator.js`, `model_manager.js`, `graph_renderer.js`, `viewport.js`, `simulator.js`, `app.js`).
  - Added zero-dependency build bundler `scripts/bundle_playground.py` generating standalone `playground.js`, guaranteeing 100% zero-CORS local execution over `file://` protocol alongside full HTTP/MkDocs compatibility.
  - Implemented dynamic, universal datapath engine supporting SysML v2, SCXML, Cameo, JSON, PlantUML, and Mermaid diagrams with automated live guard threshold evaluation and action execution.
  - Implemented composite shallow history memory (`[H]`) restoring remembered sub-states across transitions.
- **Monolithic Header Modularization (`include/fsm/backend/cpp/runtime/`)**:
  - Decomposed monolithic `hook_traits.hpp` into dedicated Single-Responsibility Principle components:
    - `traits/lifecycle_traits.hpp`: State lifecycle hooks (`on_entry`, `on_exit`).
    - `traits/guard_traits.hpp`: Guard invocation and satisfiability traits.
    - `traits/action_traits.hpp`: Transition action invocation traits.
    - `traits/hook_traits.hpp`: Unified backward-compatible trait aggregator.
  - Modularized `thread_safe_fsm.hpp` by extracting `detail/worker_thread_controller.hpp`, `detail/diagnostic_handlers.hpp`, and `detail/thread_safe_policy_adapter.hpp`.
  - Modularized policy configuration adapters into `detail/fsm_policy_adapter.hpp` and `detail/spsc_policy_adapter.hpp`.

### Performance & Optimization
- **Middle-End Analysis & Performance (`efsm_interval_analysis.hpp`)**:
  - Replaced dynamic regex recompilation in `parse_guard_domain` with a fast zero-allocation tokenizer using `std::string_view` and relational operator scanning.
  - Cached static regular expressions in `apply_assignment`.

### Documentation Suite Overhaul
- Restructured documentation into 6 user-centric pillars: Getting Started, Design Patterns & Tutorials, Multi-Target Specification & Roadmaps, Formal Verification, Integration & Build Systems, Reference.
- Added comprehensive Developer Quick Reference Cheat Sheet, Engine Selection Matrix, and Formal Safety Pattern Guide.
- Added multi-target roadmap and preview specifications for Rust (`build.rs`) and C (`fsmc.h`).
- Extended build systems guide with CMake, Meson, Make, and Bazel integration examples.
- Harmonized 6-step sequential tutorials with unified navigation footers and corrected build targets.
- Enhanced layout width to 86rem and removed right-hand TOC distraction for focused technical reading.

### Fixed & Hardened
- **Thread Safety & Data Race Fixes (`spsc_fsm.hpp`)**:
  - Eliminated concurrency data race in `spsc_fsm::is_in<State>()` and `is_in_state<State>()` by comparing the atomic state index against compile-time type index (`type_list_index_of_v`), removing unsynchronized reads to underlying non-thread-safe `fsm_.is_in<State>()`.
  - Added compile-time validation asserting `std::is_trivially_copyable_v<Registers>` in `spsc_fsm` to guarantee sound seqlock snapshots free of torn-pointer reads.
  - Fixed unsigned arithmetic wrap-around in `type_list_index_of` during recursive compile-time type searches.
  - Modernized `spsc_ring_buffer.hpp` with in-place destructor slot destruction (`get_slot(tail)->~T()`), removed artificial `is_default_constructible<T>` constraint, and fixed modular size arithmetic under counter wrap-around.
  - Fixed dummy `services_type` stack allocation in `fsm::fsm::step()`, `fsm::fsm::dispatch()`, and `transition_executor` to safely support non-default-constructible service instances via deferred resolution.

### Supply-Chain & Tooling
- Added SHA256 cryptographic hash pinning to GoogleTest `FetchContent_Declare`.
- Updated test directory mappings and deduplicated globbing in `scripts/generate_test_catalog.py`.
- Synchronized standalone single-header runtimes (`cpp17_standalone_runtime.hpp`, `cpp20_standalone_runtime.hpp`) with modularized runtime traits and `config.hpp`.


---

## [0.4.1] - 2026-09-02

### Fixed
- **Lock-Free Concurrency & Sanitizer Cleanliness (`spsc_fsm.hpp`)**:
  - Replaced dynamic variant visitation in `spsc_fsm::state_name()` with compile-time table lookup (`detail::get_state_name_by_index`), eliminating data races under AddressSanitizer and UndefinedBehaviorSanitizer.
  - SPSC state inspection is now 100% thread-safe with atomic acquire-release semantics.
- **SMV Parser Robustness (`smv_parser.hpp`)**:
  - Fixed case target colon parsing when transition guards contain namespace `::` qualifiers (e.g. `fsm::and_`).
  - Improved handling of parenthesized clauses in transition conditions.
- **Immediate / Eventless Transition Serialization (`mermaid_parser.hpp`, `mermaid_serializer.hpp`, `json_serializer.hpp`)**:
  - Eliminated spurious `AnonymousEvent` string injection when parsing and rendering guarded transitions without event triggers.
  - Mapped eventless immediate transitions in XState JSON to standard `"always"` property key.
- **Clean SMV Emission (`smv_serializer.hpp`)**:
  - Maintained pure, standard SMV output for nuXmv model checking without polluting comment directives.

### CI/CD & Quality Gates
- **Modernized GitHub Actions CI Pipeline**:
  - Added `hendrikmuhs/ccache-action` reducing clean CI build times from ~4 min to ~35 sec.
  - Added AddressSanitizer and UndefinedBehaviorSanitizer automated matrix job (`-fsanitize=address,undefined`).
  - Added GitHub CodeQL security analysis workflow for C++ and Python.
  - Added automated nuXmv 2.0.0 symbolic model checking verification matrix.
  - Added strict MkDocs documentation build verification (`mkdocs build --strict`).
  - Enforced zero compiler warnings (`-DFSMC_WARNINGS_AS_ERRORS=ON` / `-Werror`).

### Documentation & Tooling
- **Neutral Compiler Positioning & Pluggable Architecture**:
  - Repositioned `fsmc` documentation as a universal, modular compiler infrastructure with an extensible backend architecture.
  - Expanded Lossless Diagram Directives (`@fsm:*`) reference for PlantUML, Mermaid, DOT, SCXML.
  - Expanded in-depth reference chapters for SysML v2, Cameo XMI, W3C SCXML, and nuXmv formal verification.
  - Reorganized Cross-Format Feature Matrix into clean, categorized comparison tables.
  - Synchronized `vcpkg.json`, Conan recipes, CMake install rules, and standalone single-header runtimes (`0.4.1`).
- **Playground Enhancements**:
  - Restricted the left editor format dropdown strictly to Authoring formats for 100% conservative round-trip fidelity.

---

## [0.4.0] - 2026-09-01

### Added
- **Orthogonal Data Domains Architecture**:
  - Replaced monolithic `Context` with 4 strictly isolated memory domains: `InPorts` (read-only), `OutPorts` (write-only), `Registers` (encapsulated $z^{-1}$ internal memory), and `Services` (injected abstract interface for non-deterministic side-effects).
  - Event payloads are strictly ephemeral stack/variant objects with explicit constructors and `[[nodiscard]] constexpr bool is_valid() const noexcept` validator methods.
- **Dual-Paradigm Execution Model (Sampled Step + Reactive Dispatch)**:
  - Synchronous continuous execution loop via `step(in, out, srv)` for periodic sampled systems (IEC 61131-3 / SCADE Lustre model).
  - Asynchronous event-driven execution via `dispatch(ev, in, out, srv)`.
  - Canonical *Read-Execute-Write* (Latching Pattern) ensuring immunity to torn-reads and race conditions.
  - Asynchronous event push standardized on `post(Event&&)` across both `fsm::thread_safe_fsm` and lock-free `fsm::spsc_fsm`.
- **NumericDomain Port Contracts & Validation**:
  - Full support for formal port range contracts (`min_value`, `max_value`, `assert constraint`) in SysML v2 (`in port`, `out port`), PlantUML (`@fsm:port`), Mermaid, JSON, SMV, and SCXML.
  - C++ emitter automatically generates inline `[[nodiscard]] constexpr bool validate_contracts() const noexcept` on `InPorts` and `OutPorts`.
  - Middle-end `EFSMIntervalAnalyzer` validates port and register assignments against contracts and flags `W_PORT_RANGE_VIOLATION` / `W_VARIABLE_RANGE_VIOLATION`.
- **Compile-Time Poison Asserts & Doxygen-Documented Codegen**:
  - Added compile-time static assertions in `hook_traits.hpp` explicitly rejecting legacy v0.3.0 `operator()(Context&)` signatures.
  - Generated C++ headers now emit comprehensive, formal Doxygen docstrings (`@file`, `@struct`, `@typedef`, `@brief`, `@satisfies`, `@return`).
  - Non-polymorphic runtime (`!std::is_polymorphic_v`), zero vtable, zero dynamic heap allocations.
- **Canonical Runtime Relocation & Thematic Test Architecture**:
  - Canonical header-only runtime relocated under `include/fsm/backend/cpp/runtime/` with runtime unit tests in `tests/backend/cpp/runtime/`.
  - Reorganized `tests/backend/cpp/` into thematic modules (`test_cpp_model_emitter.cpp`, `test_cpp_e2e_compiler.cpp`, CMake targets).
  - E2E integration test suite validating host `g++` compilation under `-Wall -Wextra -Werror -pedantic -Wconversion` on both `-std=c++17` and `-std=c++20`.
- **WebAssembly Interactive Playground & Visual Serializer Enhancements**:
  - Full WebAssembly (`fsmc.wasm` / `fsmc.js`) module compiled with Embind and C++20 for instant in-browser compilation, validation, optimization, and diagram export.
  - Robust SysML v2 parser supporting inline port constraints and compound `do { ... }` action blocks.
  - Dagre-safe Mermaid state diagram serialization resolving child-to-ancestor transitions and history restoration (`[H]`, `[H*]`) without layout cycles.
  - Lossless closed-loop cross-format transpilation across SysML v2, PlantUML, Mermaid, SCXML, Cameo XMI, and JSON.
- **Educational Overhaul & Dedicated Engineering Guides**:
  - Added dedicated *Universal Runtime Fundamentals* chapter and *5 Architectural Design Patterns / Anti-Patterns*.
  - Added capstone *Step 6: Real-World Case Study (Autonomous UAV Flight Computer)* from SysML v2 model to C++20 real-time loop.
  - Added dedicated *Unit Testing Guide* with GoogleTest, Catch2, Mock Services, and Invariant validation recipes.
  - Added dedicated *FAQ & Troubleshooting Guide* with compiler error diagnostics and architectural checklists.
  - Added *Dual-Paradigm Temporal Models Guide* contrasting discrete sampled dwell timers (`in_state_for<N>`) with continuous physical timers (`post_state_timeout`).

### Breaking Changes
- **Complete Removal of Monolithic `Context`**:
  - `Context`, `no_context`, `with_context()`, and the `--context` CLI option have been eliminated.
  - All guards and actions must migrate to partitioned signatures (`guard(in, reg)` / `action(out, reg, srv)`).
- **Removal of Legacy SPSC Producer Aliases**:
  - Removed `fsm::spsc_fsm::send()` and `fsm::spsc_fsm::enqueue()`. All asynchronous event producers must call `post(Event&&)`.
- **CMake Macro Update**:
  - Removed `CONTEXT` argument from `fsmc_target_sources(...)`.

---

## [0.3.0] - 2026-08-26

### Added
- **Lock-Free `fsm::spsc_fsm` Runtime Wrapper (`spsc_fsm.hpp`)**:
  - Wait-Free $O(1)$ single-producer enqueue mechanism for Interrupt Service Routines (ISR) and hard real-time sensor tasks.
  - Dedicated single-consumer execution (`process_one()`, `run_until_empty()`).
  - Seqlock synchronization for consistent lock-free atomic `snapshot_context()` and `with_context()` across concurrent reader threads.
- **Model Checker EFSM Data Path Interval Analysis (`efsm_interval_analysis.hpp`)**:
  - Forward abstract interpretation engine propagating numeric variable bounds across reachable statechart paths.
  - Automatic static detection of dead branches and unsatisfiable guards (`W_EFSM_UNSATISFIABLE_GUARD`).
- **Requirement Traceability Matrix (RTM) Structured Output (`rtm_emitter.hpp`)**:
  - Full audit reports in GitHub-Flavored Markdown and machine-readable JSON formats linking `@fsm:req` tags, covered states, covered transitions, and formal LTL/CTL verification outcomes.
  - Added CLI flags `--rtm-output <file>` and `--rtm-format <json|md>`.
- **Formal Middle-End `ChoiceInliningPass` (`choice_inlining_pass.hpp`)**:
  - Cartesian product flattening for `StateKind::Choice` and `StateKind::Junction` nodes on the `FsmIr` graph.
  - Automatic combination of branch guards via `fsm::and_` and concatenation of action assignments.
- **Automatic Resolved EFSM Guard Codegen**:
  - Parser normalization of boolean operators (`and/or/not` $\rightarrow$ `&&/||/!`) and automatic binding to context variables (`ctx.<var>`).
  - C++ emitter produces direct `return ctx.<var> <op> <val>;` expressions instead of stub `// TODO` functors.
- **Transition Traceability in `dispatch_result` (`transition_trace`)**:
  - Zero-overhead trace metadata carrying source state, target state, event, guard, action, and transition kind.
- **Modular CLI Tool Architecture**:
  - Refactored `fsmc` and `fsm-opt` into dedicated options and driver modules (`tools/common/`, `tools/fsmc/`, `tools/fsm-opt/`).

### Breaking Changes
- **Removed `static_thread_safe_fsm.hpp`**: Replaced entirely with the lock-free, wait-free `fsm::spsc_fsm` and `fsm::thread_safe_fsm`. Legacy aliases have been removed.

---

## [0.2.0] - 2026-08-26

### Added
- **Formal Model Checkers & Temporal Logic**:
  - Linear Temporal Logic (LTL) and Computation Tree Logic (CTL) specification verification (`ModelChecker`).
  - SMV / nuXmv discrete-time tick emitter and verification roundtrip.
- **Hierarchical and History Pseudostates**:
  - Native support for Composite States (HFSM), Shallow `[H]` and Deep `[H*]` multi-level history recovery.
- **Lossless Diagram transpilation**:
  - Bi-directional transpilation between SysML v2, Cameo XMI, W3C SCXML, Mermaid, PlantUML, Graphviz DOT, and XState JSON.
- **Standalone Runtime Bundler**:
  - Automated python toolchain (`generate_standalone_runtime.py`) producing zero-dependency C++17 and C++20 single-header runtimes.
