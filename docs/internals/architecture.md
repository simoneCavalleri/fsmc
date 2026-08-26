# `fsmc` Compiler Architecture & Formal IR Infrastructure

This document details the internal design, modular compiler pipeline, Intermediate Representation (IR), middle-end pass manager, diagnostic engine, and template metaprogramming techniques used by **`fsmc`**.

---

## 1. Modular Repository & Layer Organization

The repository is structured into distinct, decoupled compiler layers on top of a common, strongly-typed Intermediate Representation:

```text
fsmc/
├── include/fsm/
│   ├── ir/          # Unified AST & Semantic Model (FsmIr, StateNode, TransitionEdge, FNV-1a IDs, EFSM vars, LTL/INVAR)
│   ├── middleend/   # PassManager, Dead State Pruning, Determinism, Guard Simplification, Inlining, TimedDeadlockPass & ModelChecker
│   ├── diagnostic/  # Rich DiagnosticEngine with ANSI colors, SourceSpan, and visual carets
│   ├── frontend/    # Two-Category Parser Ingestion Infrastructure & ParserFactory
│   │   ├── formal/  # High-Semantics Formal Models (SysML v2, W3C SCXML, Cameo / MagicDraw XMI, nuXmv SMV)
│   │   └── diagram/ # Visual Diagram Sketch Notations (PlantUML, Mermaid, Graphviz DOT, XState JSON)
│   ├── backend/     # Target Code Generators (C++17/20 Bare-Metal) & EmitterFactory (8 Serializers including nuXmv SMV)
│   └── runtime/cpp/ # Canonical Real-Time Runtime Engine (fsm, lock-free SPSC FIFO, static buffer, timers, trait introspection)
├── tools/
│   ├── fsmc/        # Primary Multi-Format Compiler Driver CLI
│   └── fsm-opt/     # Standalone Formal IR Optimizer, Linter & Roundtrip Formatter CLI
├── playground/      # Interactive WebAssembly Browser Playground (fsmc.wasm)
├── examples/        # Aerospace, Automotive ECU, and Resilient IoT Showcases
├── tests/           # Modular GoogleTest Suites (49 suites, 100% pass)
│   ├── core/        # Runtime engine, HFSM, choice, history, observers, timers
│   ├── frontend/    # Frontend tests partitioned into formal/ and diagram/
│   │   ├── formal/  # Tests for SysML v2, SCXML, Cameo, and SMV parsers
│   │   └── diagram/ # Tests for PlantUML, Mermaid, DOT, and JSON parsers
│   ├── ir/          # Serialization and AST integrity
│   ├── middleend/   # PassManager, deadlocks, timed analysis, and model checking
│   ├── backend/     # Codegen, roundtrip lossless export, and emitters
│   └── integration/ # CMake build integration and multi-target suites
└── docs/            # Formal IR Specification, Architecture & Developer Guides
```

---

## 2. Compiler Pipeline

`fsmc` operates as a multi-stage compiler structured in three distinct tiers: **Frontend Ingestion**, **Middle-End Pass Pipeline**, and **Backend Code Generators & Emitters**.

```
  Model File (.sysml / .xmi / .scxml / .puml / .mmd / .dot / .json / .smv)
           │
           ▼
┌─────────────────────────────────────────────────────────────┐
│ 1. Frontend Ingestion & ParserFactory                       │
│  • Formal Models (include/fsm/frontend/formal/):            │
│    - SysML v2 Parser (Native textual .sysml grammar)        │
│    - Cameo / MagicDraw Parser (OMG XMI 2.x XML parser)      │
│    - W3C SCXML Parser (State Chart XML specification)       │
│    - nuXmv / SMV Parser (Formal symbolic specification)     │
│  • Visual Diagrams (include/fsm/frontend/diagram/):         │
│    - PlantUML Parser (State diagram block tokenization)     │
│    - Mermaid Parser (StateDiagram-v2 grammar)               │
│    - Graphviz DOT Parser (Unix graph grammar)               │
│    - XState JSON Parser (Modern JSON Statecharts)           │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. Formal Intermediate Representation (FsmIr)               │
│  • Strongly-typed hierarchical AST & semantic model         │
│  • Deterministic 64-bit FNV-1a IDs for states & transitions │
│  • Structured Triggers: SignalTrigger, TimeTrigger, Anon    │
│  • Composable boolean GuardAstNode (AND, OR, NOT)           │
│  • Extended finite variables with Physical Units & Types    │
│  • Formal temporal properties (LTLSPEC, INVARSPEC)          │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. Middle-End Optimizer & Verifier (PassManager)            │
│  • DeadStatePruningPass (Prunes unreachable states & dead tr)│
│  • DeterminismEnforcementPass (Detects nondeterministic br) │
│  • GuardSimplificationPass (Algebraic boolean optimization)  │
│  • SubmachineInliningPass (Inlines modular submachines)     │
│  • TimedDeadlockPass (Detects 0ms timeouts & racing timers) │
│  • OrthogonalInterferencePass (Detects concurrent races)    │
│  • Formal ModelChecker (Temporal LTL/CTL & Safety Invariants)│
│  • Rich DiagnosticEngine (Rust/Clang-style visual carets)   │
└──────────────────────────────┬──────────────────────────────┘
                               │
            ┌──────────────────┴──────────────────┐
            ▼                                     ▼
┌───────────────────────────────┐   ┌───────────────────────────┐
│ 4a. C++ Code Generator Engine │   │ 4b. Diagram & SMV Emitters│
│  • Bounded Choice Flattening  │   │  • SysML v2 Serializer    │
│  • Standalone (SSOT bundled)  │   │  • PlantUML Serializer    │
│  • Modular C++ (.hpp/.cpp)    │   │  • Mermaid Serializer     │
│  • C++17 (SFINAE) / C++20     │   │  • Cameo XMI Serializer   │
│  • Zero-heap embedded runtime │   │  • SCXML Serializer       │
│  • Thread-safe async wrappers │   │  • Graphviz DOT Serializer│
│  • Deterministic timer manager│   │  • JSON IR Serializer     │
│  • Ring buffer overflow policy│   │  • nuXmv / SMV Serializer │
└───────────────┬───────────────┘   └─────────────┬─────────────┘
                ▼                                 ▼
       Generated C++ Header               Exported Diagram / SMV
```

---

## 3. Frontend Classification (`FrontendKind`)

`fsmc` classifies parsers into two distinct categories defined in `include/fsm/frontend/parser_interface.hpp`:

```cpp
enum class FrontendKind : std::uint8_t {
    Formal,   ///< Strict formal metamodel: SysML v2, W3C SCXML, Cameo/MagicDraw XMI, nuXmv/SMV
    Diagram   ///< Visual diagram notation: PlantUML, Mermaid, Graphviz DOT, XState JSON
};
```

1. **`FrontendKind::Formal` (`include/fsm/frontend/formal/`)**:
    - Backed by formal specifications where variables, types, units, and transitions have precise mathematical semantics.
    - Code generation proceeds directly without heuristic warnings.
2. **`FrontendKind::Diagram` (`include/fsm/frontend/diagram/`)**:
    - Visual sketching notations where state charts are descriptive. Types and payloads are inferred or supplemented via lossless `@fsm:` directives.
    - Codegen requires explicit confirmation via `--allow-diagram-codegen` and emits warning `warning[W0301]: Untyped or inferred symbol in diagram source`.

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

## 7. Hard Real-Time, Zero-Heap C++ Runtime Architecture

The C++ runtime engine (`include/fsm/runtime/cpp/`) is designed for mission-critical, hard real-time embedded environments:

1. **Deterministic Ring Buffers & Overflow Policies**:
    - `fsm::static_ring_buffer<T, Capacity, Policy>` provides fixed-capacity zero-heap event buffering with configurable overflow policies:
        - `OverflowPolicy::DropOldest`: Overwrites oldest unconsumed event (telemetry/streaming mode).
        - `OverflowPolicy::DropIncoming`: Discards new event on full queue (resilient backpressure).
        - `OverflowPolicy::AssertOnOverflow`: Traps execution immediately (hard real-time safety critical).
2. **Lock-Free SPSC Engine (`fsm::spsc_fsm`)**:
    - `fsm::spsc_fsm<Table, Context, Capacity, InitialState>` combines compile-time state machine folding with fixed-capacity static ring buffers, providing wait-free O(1) ISR event production and lock-free seqlock context reading.
3. **Synchronous Deterministic Timer Manager**:
    - `fsm::deterministic_timer_manager<MaxTimers>` manages state machine timeout events via discrete `tick(delta_ms, callback)` invocations, perfectly matching hardware tick timers (SysTick) without background threads.


---

## 8. Composite Guard AST & Recursive Parser

To support complex boolean logic in model diagrams (e.g. `[PowerOk && (!Fault || Override)]`), `fsmc` incorporates an expression tokenizer and recursive-descent parser (`GuardExpressionParser`):

- Generates a nested C++ type representation using variadic templates: `fsm::and_<PowerOk, fsm::or_<fsm::not_<Fault>, Override>>`.
- Extracts unique atomic guard identifiers to generate forward-declared stubs only for leaf predicates.
- Short-circuits evaluations at runtime with zero temporary objects.

---

## 9. HFSM Hierarchy & History State Resolution

1. **Parent Transition Inheritance (Flattening)**:
    - Outgoing transitions defined on parent macro-states are automatically propagated to all child sub-states during code generation, unless a child explicitly overrides the triggering event.
2. **Dynamic History Restoration**:
    - For composite states targeted with shallow history `State[H]` or deep history `State[H*]`, the runtime tracks the last active child sub-state and dynamically restores it upon re-entry.
    - If the macro-state has never been visited before, the machine defaults to the initial sub-state.

---

## 10. Thread-Safe Concurrency & Polling Architecture

`fsm::thread_safe_fsm` delivers high-throughput concurrent event handling while strictly preventing data races, deadlocks, and use-after-free conditions:

1. **Dual Mutex Model**:
    - `dispatch_mutex_`: Serializes state mutations and action execution.
    - `queue_mutex_`: Protects the internal event FIFO queue independently from dispatch execution.
2. **Snapshot-Based Notification Dispatch**:
    - Transition information and observer callbacks are captured under `dispatch_mutex_` and invoked outside the lock, preventing deadlocks when observers self-post events.
3. **Single-Consumer Polling Guard & O(1) Queue**:
    - Backed by `std::deque<event_handler>`, `process_one()` achieves deterministic O(1) front popping.
    - Guarded by atomic test-and-set (`is_polling_`), enforcing single-consumer sequential event consumption.
4. **Deterministic Lifecycle & Safe Shutdown**:
    - Worker thread ID (`worker_thread_id_`) and stopping thread ID (`stopping_thread_id_`) are tracked via lock-free atomics.
    - External event enqueueing is safely rejected during shutdown (`is_stopping_`), while cascading events generated by active actions are fully drained before destruction.


---

## 11. Multi-Format Model Ecosystem & Formal Verification Role

`fsmc` establishes a clear architectural boundary between **Executable Statechart Modeling** and **Formal Symbolic Verification**:

```text
 ┌─────────────────────────────────────────────────────────────────────────────────┐
 │                   Authoring & Visual Statechart Formats                         │
 │  • OMG SysML v2 (.sysml)       • W3C SCXML (.scxml)     • Cameo XMI (.cameo)    │
 │  • PlantUML (@startuml)        • Mermaid (stateDiagram) • XState JSON (.json)   │
 │  • Graphviz DOT (.dot)                                                          │
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
   - **SysML v2, SCXML, Cameo XMI, PlantUML, Mermaid, JSON, DOT**: Designed to model operational behavior with rich, language-agnostic software semantics (composite state trees, abstract entry/exit/do action signatures, event deferrals, physical unit constraints).
   - Serve as primary authoring languages for continuous roundtrip, semantic analysis, and target code generation (such as C++17/20 bare-metal runtimes).

2. **Formal Verification Sink (`nuXmv / SMV`)**:
   - **Role**: Serves as a pure, standard **Symbolic Model Checking Target** for mission-critical and safety-critical verification (DO-178C, ISO 26262, ECSS).
   - **Mathematical Formalism**: Emits a standard finite Kripke structure `M = <S, S0, R, L>` with explicit transition relations (`ASSIGN next(state) := case ... esac;`), finite-domain variables (`0..100`, `boolean`), discrete clock counters (`timer_<state> : 0..N`), and temporal logic goals (`LTLSPEC`, `INVARSPEC`).
   - **Design Philosophy**: SMV is kept clean and canonical—free of unnatural pseudo-directives—so that emitted files are immediately verifiable by external tools (`nuxmv`, `NuSMV`, `MathSAT`) without preprocessing.

