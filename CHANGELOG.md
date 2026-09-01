# Changelog

All notable changes to the **`fsmc`** project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.4.0] - 2026-09-01

### 🚀 Added
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

### 💥 Breaking Changes
- **Complete Removal of Monolithic `Context`**:
  - `Context`, `no_context`, `with_context()`, and the `--context` CLI option have been eliminated.
  - All guards and actions must migrate to partitioned signatures (`guard(in, reg)` / `action(out, reg, srv)`).
- **Removal of Legacy SPSC Producer Aliases**:
  - Removed `fsm::spsc_fsm::send()` and `fsm::spsc_fsm::enqueue()`. All asynchronous event producers must call `post(Event&&)`.
- **CMake Macro Update**:
  - Removed `CONTEXT` argument from `fsmc_target_sources(...)`.

---

## [0.3.0] - 2026-08-26

### 🚀 Added
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

### 💥 Breaking Changes
- **Removed `static_thread_safe_fsm.hpp`**: Replaced entirely with the lock-free, wait-free `fsm::spsc_fsm` and `fsm::thread_safe_fsm`. Legacy aliases have been removed.

---

## [0.2.0] - 2026-08-26

### 🚀 Added
- **Formal Model Checkers & Temporal Logic**:
  - Linear Temporal Logic (LTL) and Computation Tree Logic (CTL) specification verification (`ModelChecker`).
  - SMV / nuXmv discrete-time tick emitter and verification roundtrip.
- **Hierarchical and History Pseudostates**:
  - Native support for Composite States (HFSM), Shallow `[H]` and Deep `[H*]` multi-level history recovery.
- **Lossless Diagram transpilation**:
  - Bi-directional transpilation between SysML v2, Cameo XMI, W3C SCXML, Mermaid, PlantUML, Graphviz DOT, and XState JSON.
- **Standalone Runtime Bundler**:
  - Automated python toolchain (`generate_standalone_runtime.py`) producing zero-dependency C++17 and C++20 single-header runtimes.
