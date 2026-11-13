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

## 3. Compile-Time Simplification & Analysis

Before code generation, the compiler's middle-end passes normalize and analyze boolean guards and choice topologies:

- **Boolean Simplification**: Reduces double negations ($\neg(\neg A) \iff A$), tautologies, and De Morgan expressions.
- **Choice & Junction Flattening**: Collapses intermediate decision chains (`StateA -> Choice -> StateB`) into atomic guarded transitions ($(G_1 \land G_2)$ with fused actions $(A_1; A_2)$).
- **Dead-Guard & Overlap Detection**: Uses interval analysis to flag contradictory guards (`W0302`) and non-disjoint overlapping guards without explicit priority (`W0301`).

```
       [ Source State ] ─── Event E ───► [ Guard: x > 50  (Priority 1) ] ──► State A
                        ─── Event E ───► [ Guard: x <= 30 (Priority 1) ] ──► State B
                        ▲
                        └─► Disjoint Intervals => Deterministic
```

See the [Middle-End Passes Catalogue](../internals/middleend_passes.md) and [EFSM Interval Analysis](../verification_and_safety/interval_analysis.md) for compiler internals.

---

## 4. Transition Lifecycle & Execution Semantics

When transitions fire and during ongoing state residence, `fsmc` enforces a deterministic execution sequence:

### Four-Phase Transition Execution
1. **Guard Evaluation**: Pure predicate check. If `false`, the transition aborts without mutating state.
2. **Condition Action (Stateflow/UML)**: If the guard passes, condition actions execute immediately *before* state exit.
3. **Source Exit**: `on_exit` executes for exited source states (inner-to-outer up to the LCA).
4. **Transition Action**: Executes side-effects across `OutPorts`, `Registers`, and `Services`.
5. **Target Entry**: Updates the active state and executes `on_enter` (outer-to-inner from the LCA).

### Dual Actions, Continuous Activities & Change Triggers
- **Dual Action Execution**: Models combining a condition action and transition action (`[guard] { cond_act } / { trans_act }`) execute both actions in deterministic sequence.
- **Continuous In-State Activity (`do_activity`)**: States with continuous activities execute them synchronously on periodic `step()` / `tick()` steps.
- **Sampled Change Triggers (`when (pred)`)**: Continuous edge-triggered conditions compare the current predicate against a $z^{-1}$ previous-cycle register (`!prev_pred && pred`).

For C++ functor concepts (`fsm::Guard`, `fsm::Action`), signatures, and code examples, see **[Core C++ Building Blocks](../runtime_api/core_concepts.md)**.
