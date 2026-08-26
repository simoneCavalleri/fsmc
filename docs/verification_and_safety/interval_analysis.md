# EFSM Data Path Interval Analysis

Extended Finite State Machines (EFSM) couple discrete state transitions with continuous or numeric variables. In complex systems, state transitions often depend on data path variables (e.g. `altitude > 1000.0`, `batterySoC < 20.0`, `retryCount >= 3`).

`fsmc` includes the `EFSMIntervalAnalyzer`, a static abstract interpretation engine based on interval lattices `[lo, hi]` that propagates variable domains forward across all reachable state paths.

---

## 1. Abstract Interpretation Engine

### Interval Lattice Mechanics
Each variable in the state machine context is assigned a domain interval `[lo, hi]`:

- Initial state values seed the entry interval (e.g. `batteryLevel = 100.0` => `[100.0, 100.0]`).
- Assignments mutate intervals:
  - `v += 5.0` => `[lo + 5, hi + 5]`
  - `v = 0` => `[0, 0]`
- State joins compute the least upper bound (hull) across converging transition paths:
  - `[10, 20] join [30, 40] = [10, 40]`

---

## 2. Dead Branch & Unsatisfiable Guard Detection

When evaluating a guarded transition (e.g. `[batteryLevel > 120.0]`), the analyzer intersects the variable's current interval at that state with the guard constraint:

```
Domain(batteryLevel) ∩ (120.0, +∞) = [0.0, 100.0] ∩ (120.0, +∞) = ∅ (Empty set)
```


Because the intersection is empty, the guard can never evaluate to `true` during execution. The compiler flags this dead branch with diagnostic `W_EFSM_UNSATISFIABLE_GUARD`:

```text
warning[W0402]: Guard predicate '[batteryLevel > 120.0]' on transition 'Navigating -> TurboMode' is statically unsatisfiable.
  Variable 'batteryLevel' has interval domain [0.0, 100.0] at source state 'Navigating'.
  This transition branch is dead code and will never fire.
```

---

## 3. Integration with Optimization Passes

When `-O2` or `--prune-dead-states` is enabled:

1. `EFSMDataPathPass` computes the fixed-point interval state.
2. Unsatisfiable transitions are stripped from the `FsmIr` graph.
3. Any states that become unreachable as a result of dead transition removal are automatically pruned by `DeadStatePruningPass`.
4. The generated C++ code is smaller, cleaner, and free of dead conditional logic.
