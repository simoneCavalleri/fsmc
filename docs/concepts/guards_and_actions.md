# Guards and Action Effects

Guards and actions form the computational layer of Extended Finite State Machines (EFSM). In `fsmc`, computational datapath interactions are strictly partitioned across **4 orthogonal data domains**:

```
                       ┌─────────────────────────┐
                       │       InPorts (in)      │ ──> Read-Only (Sensor values, bus inputs)
                       │      Registers (reg)    │ ──> Encapsulated internal memory (z^-1)
                       │    Event Payload (evt)  │ ──> Ephemeral trigger parameters
                       └────────────┬────────────┘
                                    │
                         [ Guard Evaluation ]
                                    │ (if true)
                                    ▼
                       ┌─────────────────────────┐
                       │      OutPorts (out)     │ <── Write-Only (Actuator commands)
                       │      Registers (reg)    │ <── Mutated internal state
                       │      Services (srv)     │ <── Injected RPC / Side-effects
                       └─────────────────────────┘
```

---

## 1. Pure Guard Predicates

Guards are side-effect-free mathematical boolean functions evaluated during transition selection:

$$\text{Guard}: (\text{InPorts}, \text{Registers}, \text{Event}) \to \{\text{true}, \text{false}\}$$

- If the guard evaluates to `true`, the transition is enabled and can proceed to execution.
- If the guard evaluates to `false`, the transition is rejected, leaving the active state unchanged.
- Guards have read-only access to `InPorts`, `Registers`, and trigger payloads. They are strictly prohibited from mutating machine state.

=== "OMG SysML v2"
    ```sysml
    transition on FlightCommand
        if in.battery_soc >= 25.0 and reg.cycle_count < 1000
        then Navigating;
    ```

=== "W3C SCXML"
    ```xml
    <transition event="FlightCommand" cond="in.battery_soc &gt;= 25.0 &amp;&amp; reg.cycle_count &lt; 1000" target="Navigating"/>
    ```

---

## 2. Action Effects and Side-Effect Isolation

Actions execute computational side-effects when transitions fire or when states are entered or exited:

$$\text{Action}: (\text{OutPorts}, \text{Registers}, \text{Services}, \text{InPorts}, \text{Event}) \to \text{void}$$

- **OutPorts**: Written by actions to produce actuator commands or output signals.
- **Registers**: Mutated to update internal memory ($z^{-1}$, sequence counters, accumulator state).
- **Services**: Invoked to trigger external environment side-effects (logging, network packets, driver calls).

=== "OMG SysML v2"
    ```sysml
    transition climb_ok
        first Ascending
        accept AltitudeReached
        if in.battery_soc >= 20.0
        do action {
            out.climb_rate = 0.0;
            reg.waypoint_index = reg.waypoint_index + 1;
        }
        then WaypointNav;
    ```

=== "W3C SCXML"
    ```xml
    <transition event="AltitudeReached" cond="in.battery_soc &gt;= 20.0" target="WaypointNav">
      <assign location="out.climb_rate" expr="0.0"/>
      <assign location="reg.waypoint_index" expr="reg.waypoint_index + 1"/>
    </transition>
    ```

---

## 3. Compile-Time Boolean Algebra & Guard Simplification

During middle-end optimization, the `GuardSimplificationPass` analyzes and reduces composite boolean guard trees algebraically before verification or target emission:

- Double negation elimination: $\neg(\neg A) \iff A$
- Neutral elements: $A \land \text{true} \iff A$, $A \lor \text{false} \iff A$
- Dominant elements: $A \land \text{false} \iff \text{false}$, $A \lor \text{true} \iff \text{true}$
- De Morgan's laws: $\neg(A \land B) \iff \neg A \lor \neg B$

---

## 4. Choice and Junction Inlining (`ChoiceInliningPass`)

When models contain intermediate decision pseudostates (`<<choice>>` or `<<junction>>`), the middle-end optimizer inlines the decision tree directly into flat composite transitions.

For example, a transition path `StateA -> ChoiceNode -> StateB` with incoming guard $G_1$ and outgoing guard $G_2$ is transformed into a direct composite transition `StateA -> StateB` guarded by $(G_1 \land G_2)$ and executing combined actions $(A_1; A_2)$.

This eliminates intermediate pseudostate allocations and yields direct branch resolution in emitted targets.

---

## 5. Guard Satisfiability & Mutual Exclusivity (`GuardSatisfiabilityPass`)

During compilation and verification, `fsmc` runs an in-process abstract interpretation pass to evaluate guard constraints:

1. **Dead Guard Detection (`W0302`)**: Identifies guard conditions that are logically unsatisfiable or contradict variable domain bounds (e.g. `x > 50 && x < 20`).
2. **Mutual Exclusivity & Overlap Verification (`W0301`)**: When multiple transitions originate from the same state on the same event with identical priority, `GuardSatisfiabilityPass` proves whether their interval domains are provably disjoint (e.g., `x > 50` vs `x <= 50`). If guard conditions overlap without priority differentiation, a non-deterministic ambiguity warning is emitted.

```
       [ Source State ] ─── Event E ───► [ Guard: x > 50  (Priority 1) ] ──► State A
                        ─── Event E ───► [ Guard: x <= 30 (Priority 1) ] ──► State B
                        ▲
                        └─► Provably Disjoint Intervals => Deterministic & Verified!
```

---

## 6. C++20 Concept Constraints (`fsm::Guard`, `fsm::Action`)

In the modern C++20 runtime, transition definitions are constrained by compile-time concepts in [`fsm/backend/cpp/runtime/traits/concepts.hpp`](file:///home/simone/dev/github/fsmc/include/fsm/backend/cpp/runtime/traits/concepts.hpp):

- **`fsm::Guard<G, Event, State, InPorts, Registers, Services>`**: Ensures $G$ is a callable functor returning a type convertible to `bool`, accepting any valid subset of domain parameters.
- **`fsm::Action<A, Event, SrcState, DstState, InPorts, OutPorts, Registers, Services>`**: Ensures $A$ is a callable functor accepting any valid subset of lifecycle arguments.

Primitive scalar types (e.g., passing `int` instead of a functor) and mismatched signatures trigger clear, readable `static_assert` diagnostic messages during compilation.

---

## 7. Four-Phase Transition Lifecycle

When a valid transition fires, the runtime executes actions in strict four-phase sequence:

1. **Guard Evaluation**: Pure predicate check. If `false`, aborts without state mutation.
2. **Source Exit**: `on_exit(src_state, event, in, out, reg, srv)`
3. **Transition Action**: `action(event, src_state, dst_state, in, out, reg, srv)`
4. **State Transition & Target Entry**: Assigns active variant and executes `on_enter(dst_state, event, in, out, reg, srv)`.

For complete C++ runtime API details, see the dedicated **[Runtime C++ API](../runtime_api/index.md)** chapter.
