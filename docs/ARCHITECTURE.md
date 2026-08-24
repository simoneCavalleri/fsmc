# `fsmc` Compiler Architecture & Formal IR Infrastructure

This document details the internal design, modular compiler pipeline, Intermediate Representation (IR), middle-end pass manager, diagnostic engine, and template metaprogramming techniques used by **`fsmc`**.

---

## 1. Modular Repository & Layer Organization

The repository is structured into distinct, decoupled compiler layers on top of a common, strongly-typed Intermediate Representation:

```text
fsmc/
├── include/fsm/
│   ├── ir/          # Unified AST & Semantic Model (FsmIr, StateNode, TransitionEdge, FNV-1a IDs)
│   ├── middleend/   # PassManager, Optimization, Canonicalization & Model Checking Passes
│   ├── diagnostic/  # Rich DiagnosticEngine with ANSI colors, SourceSpan, and visual carets
│   ├── frontend/    # 7 Universal Ingestion Parsers (PlantUML, Mermaid, SysML v2, XMI, SCXML, JSON, DOT)
│   ├── backend/     # Target Code Generators (C++17/20 Bare-Metal) & Graphical Emitters
│   └── runtime/     # Embedded Hard Real-Time Runtime (fsm, lock-free SPSC FIFO, static buffer)
├── tools/
│   ├── fsmc/        # Primary Multi-Format Compiler Driver CLI
│   └── fsm-opt/     # Standalone Formal IR Optimizer, Linter & Roundtrip Formatter CLI
├── examples/        # Aerospace, Automotive ECU, and Resilient IoT Showcases
├── tests/           # Modular GoogleTest Suites (39 suites, 100% pass)
└── docs/            # Formal IR Specification & Developer Guides
```

---

## 2. Compiler Pipeline

`fsmc` operates as a multi-stage compiler structured in three distinct tiers: **Frontend Ingestion**, **Middle-End Pass Pipeline**, and **Backend Code Generators & Emitters**.

```
 Model File (.xmi / .scxml / .json / .dot / .sysml / .mmd / .puml)
          │
          ▼
┌─────────────────────────────────────────────────────────────┐
│ 1. Frontend Parsers (Direct FsmIr Emission)                 │
│  • Cameo / MagicDraw Parser (OMG XMI 2.x XML tokenizer)     │
│  • W3C SCXML Parser (State Chart XML specification)         │
│  • XState JSON Parser (Modern JSON Statecharts)             │
│  • Graphviz DOT Parser (Unix graph grammar)                 │
│  • SysML v2 Parser (Native textual .sysml grammar)          │
│  • PlantUML Parser (Recursive block tokenization)           │
│  • Mermaid Parser (StateDiagram-v2 grammar)                 │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. Formal Intermediate Representation (FsmIr)               │
│  • Strongly-typed hierarchical AST & semantic model         │
│  • Deterministic 64-bit FNV-1a IDs for states & transitions │
│  • Composable boolean GuardAstNode (AND, OR, NOT)           │
│  • Signals, Payloads, Validators, and Action signatures     │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. Middle-End Optimizer & Verifier (PassManager)            │
│  • HierarchyCanonicalizationPass (FQN, parent/child link)   │
│  • ChoiceCompletenessPass (Fallback branches, dup guards)   │
│  • ModelSafetyVerifierPass (Reachability, trap deadlocks)   │
│  • Rich DiagnosticEngine (Rust/Clang-style visual carets)   │
└──────────────────────────────┬──────────────────────────────┘
                               │
            ┌──────────────────┴──────────────────┐
            ▼                                     ▼
┌───────────────────────────────┐   ┌───────────────────────────┐
│ 4a. C++ Code Generator Engine │   │ 4b. Diagram Emitters      │
│  • Choice branch flattening   │   │  • PlantUML Emitter       │
│  • Standalone & Modular C++   │   │  • Mermaid Emitter        │
│  • C++17 (SFINAE) / C++20     │   │  • SysML v2 Emitter       │
│  • Zero-heap embedded runtime │   │  • Lossless JSON IR       │
└───────────────┬───────────────┘   └─────────────┬─────────────┘
                ▼                                 ▼
       Generated C++ Header               Exported Diagram
```

---

## 3. Middle-End Pass Pipeline (`PassManager`)

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
3. **`ModelSafetyVerifierPass`**:
   - Performs formal graph reachability from the root initial state.
   - Flags unreachable state islands (`W0201`).
   - Detects deadlock / trap states (`W0202`) that possess incoming transitions but lack outgoing transitions.

---

## 4. Rich Compiler Diagnostic Engine (`DiagnosticEngine`)

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

## 5. Dynamic Choice Node Flattening

In UML 2.5, a choice pseudostate $C$ conditionally directs control from an incoming transition $T_{in}: S_{src} \xrightarrow{E} C$ to several outgoing branches $T_{out, i}: C \xrightarrow{[G_i] / A_i} S_{dst, i}$.

Rather than instantiating an intermediate runtime pseudostate machine, `fsmc` flattens the choice branches at compile time by computing the Cartesian product of incoming transitions and outgoing branches:

$$\text{Transition}(S_{src}, E, S_{dst, i}) = \text{Guard}(G_{in} \land G_i) + \text{Action}(A_{in} \circ A_i)$$

This produces flat, zero-overhead compile-time `fsm::row` declarations that are directly inlined by modern C++ compilers.

---

## 6. Template Metaprogramming Runtime Engine

The runtime engine uses `std::variant`, `std::tuple`, and fold expressions to eliminate all virtual dispatch and dynamic allocation:

1. **State Storage**:
   ```cpp
   std::variant<State1, State2, State3, ...> current_state_;
   ```
2. **Transition Resolution**:
   Dispatches through `try_dispatch_tuple` using compile-time fold expressions over the `transition_table` tuple.
3. **Internal vs External Transitions**:
   - **External transitions**: Trigger state destruction, `on_exit()`, `Action()`, state reconstruction, and `on_enter()`.
   - **Internal transitions**: (`RowType::is_internal == true`) invoke `Action()` in-place on the existing state instance with zero destruction or reconstruction.

---

## 7. Composite Guard AST & Recursive Parser

To support complex boolean logic in model diagrams (e.g. `[PowerOk && (!Fault || Override)]`), `fsmc` incorporates an expression tokenizer and recursive-descent parser (`GuardExpressionParser`):
- Generates a nested C++ type representation using variadic templates: `fsm::and_<PowerOk, fsm::or_<fsm::not_<Fault>, Override>>`.
- Extracts unique atomic guard identifiers to generate forward-declared stubs only for leaf predicates.
- Short-circuits evaluations at runtime with zero temporary objects.

---

## 8. HFSM Hierarchy & History State Resolution

1. **Parent Transition Inheritance (Flattening)**:
   - Outgoing transitions defined on parent macro-states are automatically propagated to all child sub-states during code generation, unless a child explicitly overrides the triggering event.
2. **Dynamic History Restoration**:
   - For composite states targeted with shallow history `State[H]` or deep history `State[H*]`, the runtime tracks the last active child sub-state and dynamically restores it upon re-entry.
   - If the macro-state has never been visited before, the machine defaults to the initial sub-state.

---

## 9. Thread-Safe Concurrency, Lifecycle & Polling Architecture

`fsm::thread_safe_fsm` delivers high-throughput concurrent event handling while strictly preventing data races, deadlocks, and use-after-free conditions:

1. **Dual Mutex Model**:
   - `dispatch_mutex_`: Serializes state mutations and action execution.
   - `queue_mutex_`: Protects the internal event FIFO queue independently from dispatch execution.
2. **Snapshot-Based Notification Dispatch**:
   - Transition information and observer callbacks are captured under `dispatch_mutex_` and invoked outside the lock, preventing deadlocks when observers self-post events.
3. **Single-Consumer Polling Guard & $O(1)$ Queue**:
   - Backed by `std::deque<event_handler>`, `process_one()` achieves deterministic $O(1)$ front popping.
   - Guarded by atomic test-and-set (`is_polling_`), enforcing single-consumer sequential event consumption.
4. **Deterministic Lifecycle & Safe Shutdown**:
   - Worker thread ID (`worker_thread_id_`) and stopping thread ID (`stopping_thread_id_`) are tracked via lock-free atomics.
   - External event enqueueing is safely rejected during shutdown (`is_stopping_`), while cascading events generated by active actions are fully drained before destruction.
   - Non-blocking `request_stop()` allows worker callbacks to initiate clean cooperative shutdowns without self-join deadlocks.
