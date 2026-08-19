# `fsmc` Compiler Architecture

This document describes the internal design, compiler pipeline, and template metaprogramming techniques used by **`fsmc`**.

---

## 1. Compiler Pipeline

```
 Model File (.xmi / .scxml / .json / .dot / .sysml / .mmd / .puml)
          │
          ▼
┌───────────────────┐
│ Multi-Format      │  • Cameo / MagicDraw Parser (OMG XMI 2.x XML tokenizer)
│ Parser Interface  │  • W3C SCXML Parser (State Chart XML specification)
│                   │  • XState JSON Parser (Modern JSON Statecharts)
│                   │  • Graphviz DOT Parser (Unix graph grammar)
│                   │  • SysML v2 Parser (Native textual .sysml grammar)
│                   │  • PlantUML Parser (Recursive block tokenization)
│                   │  • Mermaid Parser (StateDiagram-v2 grammar)
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│ Abstract Syntax   │  • States, Events, Guards, Actions
│ Tree (FsmModel)   │  • HFSM Composite hierarchy, Choice nodes, History
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│ Semantic          │  • Reachability analysis
│ Validator         │  • Initial state resolution, Choice branch validation
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│ C++ Code          │  • Choice branch cartesian product flattening
│ Generator Engine  │  • Dual C++17 (SFINAE) / C++20 (Concepts) generator
└─────────┬─────────┘
          │
          ▼
 Generated Header (.hpp)
```

---

## 2. Dynamic Choice Node Flattening

In UML 2.5, a choice pseudostate $C$ conditionally directs control from one incoming transition $T_{in}: S_{src} \xrightarrow{E} C$ to several outgoing branches $T_{out, i}: C \xrightarrow{[G_i] / A_i} S_{dst, i}$.

Rather than instantiating an intermediate runtime pseudostate machine, `fsmc` flattens the choice branches at compile time by computing the Cartesian product of incoming transitions and outgoing branches:

$$\text{Transition}(S_{src}, E, S_{dst, i}) = \text{Guard}(G_{in} \land G_i) + \text{Action}(A_{in} \circ A_i)$$

This produces flat, zero-overhead compile-time `fsm::row` declarations that are directly inlined by modern C++ compilers.

---

## 3. Template Metaprogramming Runtime Engine

The runtime engine uses `std::variant`, `std::tuple`, and fold expressions to eliminate all virtual dispatch and dynamic allocation:

1. **State Storage**:
   ```cpp
   std::variant<State1, State2, State3, ...> current_state_;
   ```
2. **Transition Resolution**:
   Dispatches through `try_dispatch_tuple` using compile-time fold expressions over the `transition_table` tuple.
3. **Internal vs External Transitions**:
   - External transitions trigger state destruction, `on_exit()`, `Action()`, state reconstruction, and `on_enter()`.
   - Internal transitions (`RowType::is_internal == true`) invoke `Action()` in-place on the existing state instance with zero destruction or reconstruction.

---

## 4. Composite Guard AST & Recursive Parser

To support complex boolean logic in model diagrams (e.g. `[PowerOk && (!Fault || Override)]`), `fsmc` incorporates an expression tokenizer and recursive-descent parser (`GuardExpressionParser`):
- Generates a nested C++ type representation using variadic templates: `fsm::and_<PowerOk, fsm::or_<fsm::not_<Fault>, Override>>`.
- Extracts unique atomic guard identifiers to generate forward-declared stubs only for leaf predicates.
- Short-circuits evaluations at runtime with zero temporary objects.

---

## 5. Transition Observer & Tracing Hooks

The runtime engine provides an optional observer callback mechanism (`set_observer`):
- Zero overhead when inactive (`if (observer_) observer_(...)`).
- Notifies listeners with `fsm::transition_info` containing string views of the source state, target state, event, and `is_internal` flag.
- Thread-safe support in `thread_safe_fsm` through mutex-protected callback registration and notification.

---

## 6. HFSM Hierarchy & History State Resolution

1. **Parent Transition Inheritance (Flattening)**:
   - Outgoing transitions defined on parent macro-states are automatically propagated to all child sub-states during code generation, unless a child explicitly overrides the triggering event.
2. **Dynamic History Restoration**:
   - For composite states targeted with shallow history `State[H]` or deep history `State[H*]`, the runtime tracks the last active child sub-state and dynamically restores it upon re-entry.
   - If the macro-state has never been visited before, the machine defaults to the initial sub-state.
