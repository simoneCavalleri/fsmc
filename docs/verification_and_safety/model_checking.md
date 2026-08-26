# Formal Model Checking (LTL / CTL)

`fsmc` features an integrated formal model checker that exhaustively verifies temporal logic specifications against the statechart's Kripke transition structure before code generation.

---

## 1. Supported Temporal Logic Operators

The verification engine supports Linear Temporal Logic (LTL) and Computation Tree Logic (CTL) formulas:

| Operator | Syntax | Semantics | Practical Aerospace / Systems Meaning |
| :--- | :--- | :--- | :--- |
| **Globally (Always)** | `G (P)` | Property $P$ holds in every reachable state. | Safety invariant (e.g. system never exceeds critical temperature). |
| **Finally (Eventually)**| `F (P)` | Property $P$ is guaranteed to be reached. | Termination / mission completion guarantee. |
| **Response / Leadsto** | `G (P -> F Q)` | Whenever trigger/state $P$ occurs, state $Q$ is eventually entered. | Request-Response guarantee (e.g. low battery always triggers landing). |
| **Next State** | `X (P)` | Property $P$ holds in the immediate next state. | Step-by-step sequencing requirement. |
| **Until** | `P U Q` | Property $P$ holds continuously until $Q$ becomes true. | Holding invariant during transient phases. |
| **Mutual Exclusion** | `G (!(A && B))`| States $A$ and $B$ can never be simultaneously active. | Proves orthogonal safety separation. |

---

## 2. Defining Properties in Formal Models

Temporal properties can be embedded directly in SysML v2 models or PlantUML diagrams using the `@fsm:property` directive:

```sysml
package FlightControl {
    state def AircraftMission {
        // ... states and transitions ...

        // Formal Safety Property: Mutual exclusion between Preflight and InFlight
        @fsm:property DisjointPreflightFlight = "G (!(Preflight && InFlight))";

        // Formal Liveness Property: Low battery always leads to Landed
        @fsm:property SafeLandingOnLowBattery = "G (LowBattery -> F Landed)";
    }
}
```

---

## 3. Running Verification via CLI

To verify all temporal formulas, deadlock traps, and livelock cycles in a model:

```bash
fsmc -i flight_mission.sysml --verify
```

If a temporal formula fails, the model checker outputs the counterexample trace demonstrating the sequence of events and states that led to the violation:

```text
[ERROR] Formal Property 'SafeLandingOnLowBattery' VIOLATED!
Counterexample Trace:
  1. State: CruiseFlight (batteryLevel = 15.0)
  2. Event: LowBattery
  3. Transition: CruiseFlight -> HoverPause (Loop trap: No outgoing path to Landed)
```
