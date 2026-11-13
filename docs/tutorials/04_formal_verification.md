# Step 4: Formal Verification & Model Checking

!!! note "Curriculum Continuity"
    In **[Step 3](03_hierarchical_hfsm.md)**, we structured the `FlightController` hierarchy and validated transition inheritance at runtime. Before deploying to production, we can mathematically check graph invariants, temporal logic specifications, and numeric guard domains at compile time.

In safety-critical systems (aerospace, automotive, medical devices, robotics), `fsmc` provides compile-time formal verification to check system properties before execution:

- Verifying graph invariants (**Deadlock**, **Livelock**, **Reachability**).
- Specifying and checking temporal logic properties (**LTL** and **CTL**).
- Performing **EFSM Interval Analysis** to validate numeric variable ranges and detect unreachable guard branches.
- Generating the **Requirement Traceability Matrix (RTM)** for verification and design assurance audits.
- Replaying event traces directly on `FsmIr` (`--simulate --trace`) and exporting violated properties as GoogleTest C++ cases (`--export-cex-gtest`).

---

## 1. Safety Checks with `fsmc --verify`

You can run automated static checks on any model directly from the command line:

```bash
fsmc -i flight_controller.sysml --verify
```

The model checking pass analyzes the state graph:

1. **Unreachable State Detection**: Flags states with no incoming valid transition paths.
2. **Choice Branch Completeness**: Verifies that `choice` pseudostates contain a mandatory fallback `else` or exhaustive guards.
3. **Deadlock / Sink State Traps**: Detects unintentional terminal states.

You can run the built-in formal verification pass or pass ad-hoc temporal logic formulas directly on the command line:

```bash
# Run formal verification (deadlock, interval analysis, invariants)
fsmc -i flight_controller.sysml --verify

# Verify a custom LTL safety property inline (no model annotation required)
fsmc -i flight_controller.sysml --verify --ltl "G !(Ascending && Emergency)"

# Verify a CTL reachability property
fsmc -i flight_controller.sysml --verify --ctl "EF Emergency"

# Export to nuXmv SMV logic for external symbolic verification
fsmc -i flight_controller.sysml --export smv -o flight_controller.smv
nuXmv flight_controller.smv
```

**Example verification report output:**
```text
============================================================================
 Formal Model Verification Report: FlightController
============================================================================
 Input File:       flight_controller.sysml
 States:           4
 Total Events:     4
 Transitions:      5
 Choice Nodes:     0
 Deferred Triggers:0
----------------------------------------------------------------------------
 Diagnostics:
  [WARNING] (W_EFSM_UNSATISFIABLE_GUARD): Guard 'in.battery_pct > 120' on transition 'Cruising -> Emergency' is unsatisfiable given variable/port 'battery_pct' range [0, 100]
  [INFO] (EventQueueBound): Estimated maximum event queue bound: 2 slots (max deferred: 0, max burst emits: 0).
  [INFO] (I_WCET_BOUND): Certified maximum micro-step bound per macro-step: 0 steps.
----------------------------------------------------------------------------
 Formal Temporal Properties (3):
  [PASSED] PropReachable (F Cruising)
  [PASSED] PropSafety (G !(Ascending && Emergency))
  [PASSED] cli_ltl_property (G !(Ascending && Emergency))
----------------------------------------------------------------------------
 Verification Status: PASSED (Model Sound & Properties Verified)
============================================================================
```

---

## 2. Specifying Temporal Logic Properties (LTL & CTL)

In formal verification, properties describe how systems behave over time:

- **Safety ("Bad things never happen")**: `G !(Ascending && Emergency)`
- **Liveness / Reachability ("Good things eventually happen")**: `F Cruising`

### Embedding Properties in Models

Attach formal properties directly inside SysML v2 models using `// @fsm:property` directives (or native `assert property` blocks):

```sysml
// @fsm:property name=PropSafety kind=Safety formula="G !(Ascending && Emergency)" req="REQ_FLIGHT_041"
// @fsm:property name=PropReachable kind=Reachability formula="F Cruising" req="REQ_FLIGHT_042"
state def FlightController {
    in port battery_pct : Integer { assert constraint { self >= 0 and self <= 100; } }

    entry; then Booting;
    state Booting;
    state Ascending {
        satisfy requirement REQ_FLIGHT_042;
    }
    state Cruising;
    state Emergency;
    // ...
}
```

When you run `fsmc --verify` (or export to nuXmv with `--export smv`), `fsmc` translates the statechart into a Kripke structure and checks that the formula holds across all reachable execution traces.

---

## 3. EFSM Abstract Interpretation (Interval Analysis)

When models define constrained input ports or registers, `fsmc` performs forward abstract interpretation over interval lattices `[min, max]`:

```sysml
in port battery_pct : Integer { assert constraint { self >= 0 and self <= 100; } }

transition t_low
    first Cruising
    accept LowBatteryWarning
    if in.battery_pct < 20
    then Emergency;

transition t_impossible
    first Cruising
    accept LowBatteryWarning
    if in.battery_pct > 120  // <-- triggers W_EFSM_UNSATISFIABLE_GUARD
    then Emergency;
```

`fsmc` statically computes that `battery_pct` is bounded by `[0, 100]` and emits:
```text
[WARNING] (W_EFSM_UNSATISFIABLE_GUARD): Guard 'in.battery_pct > 120' on transition 'Cruising -> Emergency' is unsatisfiable given variable/port 'battery_pct' range [0, 100]
```

---

## 4. Generating Requirement Traceability (RTM)

For formal requirement tracing and verification audits, link states with SysML `satisfy requirement` and tag formal properties with `req="..."`:

```sysml
// @fsm:property name=PropReachable kind=Reachability formula="F Cruising" req="REQ_FLIGHT_042"
state Ascending {
    satisfy requirement REQ_FLIGHT_042;
}
```

Run `fsmc -i flight_controller.sysml -o flight_controller.hpp --req-audit --rtm audit_report.md --rtm-format markdown` to print the audit summary and generate the Requirement Traceability Matrix:

| Requirement ID | Description | Covered States | Covered Transitions | Formal Properties | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `REQ_FLIGHT_042` | - | `Ascending` | - | `PropReachable` | **PASSED** |

---

## 5. Interactive IR Simulation & Counterexample Export

Before generating C++ code, you can test event sequences directly against canonical `FsmIr` using `fsmc --simulate`, and automatically turn violated LTL/CTL counterexamples into standalone GoogleTest C++ unit tests.

### Step-by-Step Trace Simulation (`--simulate`)

Create an `events.json` trace file (for example targeting `ClimbCmd` with an `assert constraint { target_alt >= 100 and target_alt <= 40000; }`):

```json
[
  {"event": "Takeoff"},
  {"event": "ClimbCmd", "payload": {"target_alt": 50}},
  {"event": "ClimbCmd", "payload": {"target_alt": 15000}}
]
```

Run the simulator:

```bash
fsmc -i flight_controller.sysml --simulate --trace events.json
```

**Output:**
```text
============================================================================
 fsmc IR Simulator : FlightController
 Initial State     : Booting
----------------------------------------------------------------------------
[STEP 0] Booting --(Takeoff)--> Ascending
[STEP 1] Ascending --(ClimbCmd)--> [REJECTED: invalid signal payload]
[STEP 2] Ascending --(ClimbCmd {target_alt=15000})--> Cruising
----------------------------------------------------------------------------
 Final State       : Cruising
============================================================================
```

Notice how `STEP 1` (`target_alt=50`) is automatically rejected before guard evaluation because it violates the signal payload validator (`>= 100`).

### Exporting Counterexamples to GoogleTest (`--export-cex-gtest`)

When `--verify` detects a temporal property violation, `--export-cex-gtest` writes a compilable GoogleTest regression test reproducing the exact counterexample trace:

```bash
fsmc -i flight_controller.sysml --verify --ltl "G !Emergency" --export-cex-gtest flight_cex_test.cpp
```

**Generated `flight_cex_test.cpp`:**
```cpp
// Auto-generated by fsmc --export-cex-gtest
#include <gtest/gtest.h>
#include "FlightController_fsm.hpp"

using namespace fsm_generated;

TEST(FlightControllerCounterexampleTest, Reproduces_cli_ltl_property) {
    // Violated property: G !Emergency
    FlightController sm;
    sm.dispatch(Takeoff{});
    sm.dispatch(ClimbCmd{});
    sm.dispatch(Fault{});
    EXPECT_TRUE(sm.is_in<Emergency>());
}
```

---

## Next Steps

Now that you can verify model safety and traceability, proceed to **[Step 5: Code Generation & Build Integration](05_code_generation_and_integration.md)** to integrate automated C++ builds into CMake and select an execution runtime.


