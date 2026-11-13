# `fsmc` Compiler Architecture & Formal IR Infrastructure

This document details the internal design, modular compiler pipeline, Intermediate Representation (IR), middle-end pass manager, diagnostic engine, and template metaprogramming techniques used by **`fsmc`**.

---

## 1. Modular Repository & Layer Organization

The repository is structured into distinct, decoupled compiler layers on top of a common, strongly-typed Intermediate Representation:

```text
fsmc/
├── include/fsm/
│   ├── ir/          # Unified AST & Semantic Model (FsmIr, StateNode, TransitionEdge, PortDefinition, FNV-1a IDs, EFSM vars, LTL/INVAR, Serializer/Deserializer)
│   ├── middleend/   # PassManager, Dead State Pruning, Determinism, Guard Satisfiability, Inlining, TimedDeadlockPass, EFSM Interval Analysis & ModelChecker
│   ├── diagnostic/  # Rich DiagnosticEngine with ANSI colors, SourceSpan, and visual carets
│   ├── frontend/    # Two-Category Parser Ingestion Infrastructure & ParserFactory
│   │   ├── common/  # IParser interface, ParserFactory, XState JSON parser
│   │   ├── formal/  # High-semantics Formal Models (SysML v2, Stateflow XML, Cameo XMI, W3C SCXML, nuXmv SMV)
│   │   └── diagram/ # Visual Diagram Sketch Notations (PlantUML, Mermaid, Graphviz DOT)
│   └── backend/     # Target Code Generators, EmitterFactory & Zero-Overhead C++ Runtime
│       ├── cpp/     # C++17/C++20 Standalone Bundles & Modular Generators
│       │   └── runtime/ # Canonical Zero-Heap Real-Time Runtime Engine (fsm, spsc_fsm, thread_safe_fsm, static_ring_buffer)
│       ├── diagram/ # Visual Diagram Serializers (PlantUML, Mermaid, DOT, JSON)
│       ├── formal/  # Formal Model Serializers (Cameo XMI, SCXML, Stateflow XML, SMV, SysML v2)
│       └── rtm/     # Formal Requirement Traceability Matrix (RTM) Emitter
├── tools/
│   ├── fsmc/        # Primary Multi-Format Compiler Driver CLI
│   └── fsm-opt/     # Standalone Formal IR Optimizer, Linter & Roundtrip Formatter CLI
├── examples/        # Aerospace, Automotive ECU, and Resilient IoT Showcases
├── tests/           # Modular GoogleTest Suites (85 test suites / 94 CTest targets)
│   ├── backend/     # Codegen, roundtrip lossless export, and backend/cpp/runtime tests
│   ├── frontend/    # Frontend tests partitioned into formal/ and diagram/
│   ├── ir/          # Serialization and AST integrity
│   ├── middleend/   # PassManager, deadlocks, guard satisfiability, and model checking
│   └── integration/ # CMake build integration and multi-target suites
└── docs/            # Formal IR Specification, Architecture & Developer Guides
```

---

## 2. Compiler Pipeline

`fsmc` operates as a multi-stage compiler structured in three distinct tiers: **Frontend Ingestion**, **Middle-End Pass Pipeline**, and **Backend Code Generators & Emitters**.

```
  Model File (.sysml / .xmi / .scxml / .puml / .mmd / .dot / .json / .sfx / .stateflow / .smv)
           │
           ▼
┌─────────────────────────────────────────────────────────────┐
│ 1. Frontend Ingestion & ParserFactory                       │
│  • Formal Models (include/fsm/frontend/formal/ & common/):   │
│    - SysML v2 Parser (Native textual .sysml grammar)        │
│    - Cameo / MagicDraw Parser (OMG XMI 2.x XML parser)      │
│    - W3C SCXML Parser (State Chart XML specification)       │
│    - MathWorks Stateflow Parser (Simulink XML/SLX/SFX)      │
│    - XState JSON Parser (Structured JSON Statecharts)       │
│    - nuXmv / SMV Parser (Formal symbolic specification)     │
│  • Visual Diagrams (include/fsm/frontend/diagram/):         │
│    - PlantUML Parser (State diagram block tokenization)     │
│    - Mermaid Parser (StateDiagram-v2 grammar)               │
│    - Graphviz DOT Parser (Unix graph grammar)               │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. Formal Intermediate Representation (FsmIr)               │
│  • Strongly-typed hierarchical AST & semantic model         │
│  • Deterministic 64-bit FNV-1a IDs for states & transitions │
│  • MBSE Typed Ports: InPorts, OutPorts with range contracts │
│  • Structured Triggers: SignalTrigger, TimeTrigger, Anon    │
│  • Composable boolean GuardAstNode (AND, OR, NOT)           │
│  • Extended finite variables with Physical Units & Types    │
│  • Formal temporal properties (LTLSPEC, INVARSPEC)          │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. Middle-End Optimizer & Verifier (33 Registered Passes)   │
│  • Stage 1: Canonicalization & Semantic Validation          │
│  • Stage 2: Structural Lowering (LCA Action Fusion, Choice, │
│             Fork/Join, Orthogonal Product, History, Deferred│
│             Connective Junction Chaining)                   │
│  • Stage 3: Formal Safety & Invariant Verification (Livelock│
│             Priority Conflict, Timed Invariants, Event Queue│
│  • Stage 4: Symbolic Model Checking (Deadlocks, LTL/CTL)    │
│  • Stage 5: Optimization (Constant Folding, Dead State/Act, │
│             Action Factoring, Transition Fusion, Minimiz.)  │
│  • Stage 6: Data-Path Optimization (Intervals, Liveness)    │
│  • Stage 7: Backend Handoff (Determinism, WCET Analysis)    │
│  • Extensibility: PipeThroughPass & Dynamic C++ Pass Plugins │
│  • Rich DiagnosticEngine (Rust/Clang-style visual carets)   │
└──────────────────────────────┬──────────────────────────────┘
                               │
            ┌──────────────────┴──────────────────┐
            ▼                                     ▼
┌───────────────────────────────┐   ┌───────────────────────────┐
│ 4a. C++ Code Generator Engine │   │ 4b. Diagram & SMV Emitters│
│  • Backend Preflight Validator│   │  • SysML v2 Serializer    │
│  • LCA Boundary Action Fusion │   │  • Stateflow Serializer   │
│  • Cartesian Product Lowering │   │  • PlantUML Serializer    │
│  • Standalone (SSOT bundled)  │   │  • Mermaid Serializer     │
│  • Modular C++ (.hpp/.cpp)    │   │  • Cameo XMI Serializer   │
│  • C++17 (SFINAE) / C++20     │   │  • SCXML Serializer       │
│  • Zero-heap embedded runtime │   │  • Graphviz DOT Serializer│
│  • State Residence Invariants │   │  • JSON IR Serializer     │
│  • Deterministic timer manager│   │  • nuXmv / SMV Serializer │
│  • Blackbox Flight Recorder   │   │  • RTM Traceability Matrix│
│  • MC/DC Test Harness Gen     │   │  • Companion Manifest Side│
│  • SPSC & Thread-safe wrappers│   │                           │
└───────────────┬───────────────┘   └─────────────┬─────────────┘
                ▼                                 ▼
       Generated C++ Header               Exported Diagram / SMV
```

---

## 3. Frontend Classification (`FrontendKind`)

`fsmc` classifies parsers into two distinct categories defined in `include/fsm/frontend/common/parser_interface.hpp`:

```cpp
enum class FrontendKind : std::uint8_t {
    Formal,   ///< Formal metamodels: SysML v2, Stateflow, W3C SCXML, Cameo XMI, XState JSON, nuXmv/SMV
    Diagram   ///< Visual diagram notations: PlantUML, Mermaid, Graphviz DOT
};
```

1. **`FrontendKind::Formal` (`include/fsm/frontend/formal/` and `common/json_parser.hpp`)**:
    - Formal specifications and structured metamodels; C++ code generation proceeds directly without requiring `--allow-diagram`.
2. **`FrontendKind::Diagram` (`include/fsm/frontend/diagram/`)**:
    - Visual diagram notations where types and metadata may be supplied via `@fsm:*` directives or `.fsm.yaml` sidecars.
    - C++ code generation requires explicit confirmation via `--allow-diagram` (or `--allow-diagram-codegen`) and emits `warning[W0301]`.

---

## 4. Middle-End Pass Pipeline (`PassManager`)

The middle-end decouples graph transformations, semantic checks, and optimizations into independent, composable `IPass` components:

```cpp
class IPass {
public:
    virtual ~IPass() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual std::string description() const = 0;
    virtual bool run(FsmIr& ir, DiagnosticEngine& diag) = 0;
};
```

### Standard Passes
1. **`HierarchyCanonicalizationPass`**:
    - Computes deterministic FNV-1a identifiers for all states and transitions.
    - Reconciles parent-child relationships and fully qualified names (`FQN`).
    - Sorts nodes and transitions into canonical, reproducible order.
2. **`ChoiceCompletenessPass`**:
    - Inspects UML 2.5 `<<choice>>` pseudostates.
    - Detects missing unconditional fallback branches (`else`/`default`), preventing runtime state machine stalls.
    - Flags conflicting duplicate guard conditions on choice branches.
3. **`TimedDeadlockPass`**:
    - Analyzes all `TimeTrigger` transitions in the state hierarchy.
    - Flags zero-duration timeouts (`after(0ms)`) which cause instantaneous livelocks.
    - Detects racing timeouts originating from the same source state with identical or overlapping durations without explicit priority guards.
4. **`ModelSafetyVerifierPass`**:
    - Performs formal graph reachability from the root initial state.
    - Flags unreachable state islands (`W0201`).
    - Detects deadlock / trap states (`W0202`) that possess incoming transitions but lack outgoing transitions.


---

## 5. Rich Compiler Diagnostic Engine (`DiagnosticEngine`)

`fsmc` incorporates a diagnostic engine providing colored terminal formatting with visual carets and suggestions:

```
warning[W0103]: Choice pseudostate 'ClearanceChoice' lacks an unconditional else/default fallback branch (potential stall).
  --> mission.puml:42:7
   |
 42 | state ClearanceChoice <<choice>>
   |       ^~~~~~~~~~~~~~~
   = help: add an unconditional fallback transition 'ClearanceChoice --> DefaultState'
```

---

## 6. Bounded Dynamic Choice Node Flattening

In UML 2.5, a choice pseudostate `C` conditionally directs control from an incoming transition `Tin: Ssrc -> C` to several outgoing branches `Tout_i: C -> [Gi] / Ai -> Sdst_i`.

### Flattening Strategies & Cardinality Evaluation
In `cpp_model_emitter.hpp`, the compiler evaluates the Cartesian product cardinality for each choice pseudostate:

```
K = Nin * Nout
```

where `Nin` is the number of incoming transitions and `Nout` is the number of outgoing choice branches.

1. **Small Cardinality (K <= 16) — Combinatorial Template Expansion**:
    - Synthesizes direct atomic transitions:
      ```
      Transition(Ssrc, E, Sdst_i) = Guard(Gin and Gi) + Action(Ain ; Ai)
      ```

    - Advantage: Single-cycle atomic dispatch with zero intermediate states and O(1) dispatch execution.
2. **Large Cardinality (K > 16) — Direct Choice Node Routing**:
    - Bypasses combinatorial multiplication by routing incoming transitions to the choice pseudostate and evaluating prioritized choice guards sequentially.
    - Advantage: Prevents exponential compilation times, excessive template instantiation depth, and binary size growth in embedded environments.

---

## 7. Real-Time C++ Runtime Architecture Summary

The C++ runtime headers (`include/fsm/backend/cpp/runtime/`) implement zero-heap execution primitives shared between standalone and modular generated code:

1. **Deterministic Ring Buffers (`fsm::static_ring_buffer<T, Capacity, Policy>`)**: Fixed-capacity queues configurable with `OverflowPolicy::DropOldest`, `OverflowPolicy::DropIncoming`, or `OverflowPolicy::AssertOnOverflow`.
2. **Lock-Free SPSC Engine (`fsm::spsc_fsm<Table, InPorts, OutPorts, Registers, Services, Capacity, InitialState, DeferredCapacity>`)**: Wait-free single-producer/single-consumer ISR queue paired with seqlock `Registers` snapshots (enforcing `std::is_trivially_copyable_v<Registers>`).
3. **Thread-Safe Async Engine (`fsm::thread_safe_fsm`)**: Dual-mutex (`dispatch_mutex_` + `queue_mutex_`) design with snapshot notification dispatch and safe shutdown draining.
4. **Deterministic Timer Manager (`fsm::deterministic_timer_manager<MaxTimers>`)**: Tick-driven hardware timing via `tick(delta_ms, callback)` without OS timer threads.

For full template parameters, behavioral contracts, and usage examples, see the [C++ Runtime Architecture & API Reference](../runtime_api/index.md).


---

## 8. Multi-Format Model Ecosystem & Formal Verification Role

`fsmc` establishes a clear architectural boundary between **Executable Statechart Modeling** and **Formal Symbolic Verification**:

```text
 ┌─────────────────────────────────────────────────────────────────────────────────┐
 │                   Authoring & Visual Statechart Formats                         │
 │  • OMG SysML v2 (.sysml)       • W3C SCXML (.scxml)     • Cameo XMI (.cameo)    │
 │  • PlantUML (@startuml)        • Mermaid (stateDiagram) • XState JSON (.json)   │
 │  • Graphviz DOT (.dot)         • MathWorks Stateflow XML (.xml / .sfx)          │
 │                                                                                 │
 │  Semantics: Hierarchical HFSM, Typed Signals, Abstract Action Signatures,       │
 │             Deferred Events, Physical Quantity Constraints, Target Codegen.     │
 └───────────────────────────────────────┬─────────────────────────────────────────┘
                                         │
                                         ▼
                       ┌───────────────────────────────────┐
                       │   fsmc Core Compiler & IR         │
                       │ (Semantic Validation & Optimizer) │
                       └─────────────────┬─────────────────┘
                                         │
                   ┌─────────────────────┴─────────────────────┐
                   ▼                                           ▼
 ┌───────────────────────────────────┐       ┌───────────────────────────────────┐
 │ Target Runtime Codegen Engines    │       │ Formal Model Checking Target      │
 │ (e.g., C++17 / C++20 Zero-Alloc)  │       │ (nuXmv / NuSMV Symbolic Verifier) │
 │ • Header-only standalone library  │       │ • Kripke Structure (S, S0, R, L)  │
 │ • Lock-free SPSC / Ring Buffer    │       │ • Discrete Timed Automata clocks  │
 │ • Async Future / Polling Engine   │       │ • LTLSPEC / INVARSPEC / CTLSPEC   │
 │ • Extensible for future targets   │       │                                   │
 └───────────────────────────────────┘       └───────────────────────────────────┘
```

### Distinct Roles in the Compiler Architecture

1. **Authoring & Executable Modeling (Language-Agnostic Frontends & Emitters)**:
   - **SysML v2, SCXML, Cameo XMI, Stateflow XML, PlantUML, Mermaid, JSON, DOT**: Designed to model operational behavior with rich, language-agnostic software semantics (composite state trees, abstract entry/exit/do action signatures, event deferrals, physical unit constraints).
   - Serve as primary authoring languages for continuous roundtrip, semantic analysis, and target code generation (such as C++17/20 bare-metal runtimes).

2. **Formal Verification Sink (`nuXmv / SMV`)**:
   - **Role**: Serves as a pure, standard **Symbolic Model Checking Target** for mission-critical and safety-critical verification.
   - **Mathematical Formalism**: Emits a standard finite Kripke structure `M = <S, S0, R, L>` with explicit transition relations (`ASSIGN next(state) := case ... esac;`), finite-domain variables (`0..100`, `boolean`), discrete clock counters (`timer_<state> : 0..N`), and temporal logic goals (`LTLSPEC`, `INVARSPEC`).
   - **Design Philosophy**: SMV is kept clean and canonical—free of unnatural pseudo-directives—so that emitted files are immediately verifiable by external tools (`nuxmv`, `NuSMV`, `MathSAT`) without preprocessing.

