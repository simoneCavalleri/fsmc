# EFSM Data Path Interval Analysis & Contract Verification

Extended Finite State Machines (EFSM) couple discrete state transitions with continuous or numeric variables. In complex systems, state transitions depend on data path variables (e.g. `in.altitude > 1000.0`, `in.batterySoC < 20.0`, `reg.retryCount >= 3`).

`fsmc` includes the `EFSMIntervalAnalyzer`, a static abstract interpretation engine based on interval lattices $[\text{lo}, \text{hi}]$ that propagates variable and port domains forward across all reachable state paths to detect:
1. **Unsatisfiable transition guards** (dead branches).
2. **Out-of-range action assignments** violating `InPorts`, `OutPorts`, or `Registers` contracts.

---

## 1. Abstract Interpretation Engine

### Interval Lattice Mechanics
Each variable in `InPorts`, `OutPorts`, and `Registers` is assigned a domain interval $[\text{lo}, \text{hi}]$:

- **InPorts Contracts**: Initialized directly from port range annotations (`min_value`, `max_value`, e.g. `sensor_temp : [-50.0, 150.0]`).
- **Registers**: Initialized from their default initial value (e.g. `cycle_count = 0` $\implies [0, 0]$).
- **Assignments mutate intervals**:
  - `v += 5.0` $\implies [\text{lo} + 5, \text{hi} + 5]$
  - `v = 0` $\implies [0, 0]$
  - `v = in.sensor_temp` $\implies \text{Domain}(in.sensor_temp)$
- **State joins compute the least upper bound (hull)** across converging transition paths:
  - $[10, 20] \sqcup [30, 40] = [10, 40]$

---

## 2. Dead Branch & Unsatisfiable Guard Detection

When evaluating a guarded transition (e.g. `[in.batteryLevel > 120.0]`), the analyzer intersects the variable's current interval at that state with the guard constraint:

$$\text{Domain}(\text{batteryLevel}) \cap (120.0, +\infty) = [0.0, 100.0] \cap (120.0, +\infty) = \emptyset$$

Because the intersection is empty, the guard can never evaluate to `true` during execution. The compiler flags this dead branch with diagnostic `W_EFSM_UNSATISFIABLE_GUARD`:

```text
warning[W_EFSM_UNSATISFIABLE_GUARD]: Guard 'in.batteryLevel > 120.0' on transition 'Navigating -> TurboMode' is unsatisfiable given port 'batteryLevel' range [0, 100].
```

---

## 3. OutPort Contract Violation Detection

When a transition action assigns an out-of-range value to an `OutPort` with formal range bounds (e.g. `heater_power = 150.0f` on contract `[0.0, 100.0]`), the compiler detects the breach statically:

```text
warning[W_PORT_RANGE_VIOLATION]: Out-port 'heater_power' contract violation on transition 'Idle -> Heating': assigned range [150, 150] violates contract [0, 100].
```

---

---

## 4. Guard Mutual-Exclusivity & Ambiguity Analysis (`GuardSatisfiabilityPass`)

In addition to forward data-path propagation, `fsmc` includes the dedicated `GuardSatisfiabilityPass` in its middle-end verification pipeline to evaluate guard conjunctions across competing transition edges:

### Diagnostic Code Reference

| Code | Severity | Description | Condition |
| :--- | :---: | :--- | :--- |
| **`W0301`** | `Warning` | Potentially Overlapping Guards | Multiple transitions sharing `(source, event)` and identical priority have intersecting guard interval domains. |
| **`W0302`** | `Warning` | Dead / Unsatisfiable Guard | A single transition's guard specifies contradictory interval constraints (e.g. `x > 50 && x < 20`). |

### Concrete Examples

=== "Provably Disjoint (Verified Clean - 0 Warnings)"
    ```text
    State: Idle, Event: Tick
    ├─► Guard [x > 50]  (Domain: (50, +inf))  ──► State: Active (Priority 1)
    └─► Guard [x <= 30] (Domain: (-inf, 30]) ──► State: Off    (Priority 1)
    ```
    *Intersection:* $(50, +\infty) \cap (-\infty, 30] = \emptyset \implies$ **Provably mutually exclusive (0 warnings)**.

=== "Overlapping Conflict (`W0301`)"
    ```text
    State: Idle, Event: Tick
    ├─► Guard [x > 10] (Domain: (10, +inf)) ──► State: Active  (Priority 1)
    └─► Guard [x > 20] (Domain: (20, +inf)) ──► State: Pending (Priority 1)
    ```
    *Intersection:* $(10, +\infty) \cap (20, +\infty) = (20, +\infty) \neq \emptyset \implies$ **Emits `W0301` warning**.
    *(Resolution: Add explicit priority differentiation `priority: 1` vs `priority: 2` or restrict bounds).*

=== "Contradictory Dead Guard (`W0302`)"
    ```text
    State: Idle, Event: Tick
    └─► Guard [x > 100 && x < 50] (Domain: (100, +inf) ∩ (-inf, 50) = ∅) ──► State: Active
    ```
    *Intersection:* $\emptyset \implies$ **Emits `W0302` dead guard warning**.

---

## 5. Formal SMT & Model Checking Export

The computed interval bounds are automatically fed into the formal backend serializers:
- **nuXmv / NuSMV**: Bounded variables (`VAR sensor_temp : -50..150;`) and contract invariants (`INVAR sensor_temp >= -50 & sensor_temp <= 150;`).
- **SysML v2**: Exported `assert constraint { self >= min and self <= max; }`.

```bash
# Run the full formal verification pipeline via CLI
fsmc -i model.sysml --verify
```
