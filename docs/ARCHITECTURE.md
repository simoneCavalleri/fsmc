# `fsmc` Compiler Architecture

This document describes the internal design, compiler pipeline, and template metaprogramming techniques used by **`fsmc`**.

---

## 1. Compiler Pipeline

```
 Diagram (.mmd / .puml)
          │
          ▼
┌───────────────────┐
│ Multi-Format      │  • PlantUML Parser (Recursive block tokenization)
│ Parser Interface  │  • Mermaid Parser (StateDiagram-v2 grammar)
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
