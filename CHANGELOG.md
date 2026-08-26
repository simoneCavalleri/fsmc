# Changelog

All notable changes to the **`fsmc`** project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
