# Changelog

All notable changes to the **`fsmc`** project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
