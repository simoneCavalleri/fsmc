# Architecture & Core Concepts

This section explains the theoretical foundations, metamodel concepts, and real-time execution semantics implemented by **`fsmc`**.

---

## Section Contents

| Topic | Description | Link |
| :--- | :--- | :--- |
| **States & HFSM Hierarchy** | Atomic states, composite submachines, shallow `[H]` & deep `[H*]` history pseudostates. | [States & Hierarchy](states_and_hierarchy.md) |
| **Transitions & Triggers** | Event signals with typed payloads, timed triggers (`after`/`every`), and internal transitions. | [Transitions & Triggers](transitions_and_events.md) |
| **Guards & Action Effects** | Boolean condition trees (`and`, `or`, `not`), choice nodes, entry/exit/do effect actions. | [Guards & Actions](guards_and_actions.md) |
| **Memory & Real-Time Guarantees** | Deterministic O(1) dispatching, zero heap allocations, and WCET bounds. | [Memory & Real-Time](memory_and_realtime.md) |


---

## Core Semantic Model

`fsmc` implements the **OMG UML 2.5 / SysML v2 State Machine semantics** tailored for safety-critical embedded systems. Every state machine model is canonicalized into a strongly-typed Intermediate Representation (`FsmIr`) ensuring mathematical determinism and formal verifiable soundness before code emission.
