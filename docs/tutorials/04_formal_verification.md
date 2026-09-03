# Tutorial 4: Formal Verification & Model Checking

In safety-critical systems (aerospace, automotive, medical devices, robotics), `fsmc` provides compile-time formal verification to mathematically prove system properties before execution:

- Verifying graph invariants (**Deadlock**, **Livelock**, **Reachability**).
- Specifying and checking temporal logic properties (**LTL** and **CTL**).
- Performing **EFSM Interval Analysis** to validate numeric variable ranges and detect unreachable guard branches.
- Generating the **Requirement Traceability Matrix (RTM)** for certification audits (DO-178C / ISO 26262).

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

---

## 2. Specifying Temporal Logic Properties (LTL & CTL)

In formal verification, properties describe how systems behave over time:

- **Safety ("Bad things never happen")**: `G (not Emergency and not MotorOverheat)`
- **Liveness ("Good things eventually happen")**: `G (LaunchCmd -> F InOrbit)`

### Embedding LTL Specifications in Models

You can attach formal properties directly to SysML v2 specifications or diagram directives:

```sysml
state def SpacecraftController {
    // Safety: The thrusters must never fire if the propellant valve is closed
    //@fsm:ltl G !(ThrusterActive && ValveClosed)

    // Liveness: Whenever an abort signal is received, the spacecraft must eventually enter SafeHold
    //@fsm:ltl G (AbortCmd -> F SafeHold)

    entry; then Booting;
    state Booting;
    state Operational;
    state SafeHold;
    // ...
}
```

When you run `fsmc --verify` (or export to nuXmv with `--export smv`), `fsmc` translates the statechart into a Kripke structure and verifies that the formula holds across **100% of all reachable execution traces**.

---

## 3. EFSM Abstract Interpretation (Interval Analysis)

When models define numerical variables, `fsmc` performs forward abstract interpretation over interval lattices `[min, max]`:

```sysml
attribute battery_pct: Integer = 100;

// ...
transition t_recharge
    first Cruising
    accept LowBatteryWarning
    if [battery_pct < 20]
    then ReturningHome;

transition t_impossible
    first Cruising
    accept LowBatteryWarning
    if [battery_pct > 120]  // <-- fsmc flags: W_EFSM_UNSATISFIABLE_GUARD!
    then Emergency;
```

`fsmc` statically computes that `battery_pct` is bounded by `[0, 100]` and warns:
```text
warning[W0402]: Unsatisfiable guard [battery_pct > 120] on transition t_impossible.
               Branch is unreachable and will be eliminated during optimization.
```

---

## 4. Generating Requirement Traceability (RTM)

For DO-178C, ISO 26262, or IEC 62304 certification audits, tag model elements with `@fsm:req`:

```sysml
//@fsm:req REQ-FLIGHT-042: Autonomous altitude hold loop
transition hold_alt
    first Ascending
    accept TargetAltitudeReached
    then Cruising;
```

Run `fsmc --req-audit --rtm-output audit_report.md` to produce a complete requirement coverage matrix:

| Requirement ID | Description | Covered States | Covered Transitions | Formal Verification |
| :--- | :--- | :--- | :--- | :--- |
| `REQ-FLIGHT-042` | Autonomous altitude hold loop | `Ascending`, `Cruising` | `hold_alt` | `PASSED (LTL-01)` |

---

## Next Steps

Now that your state machine is formally proven, let's learn how to **compile it and integrate it into your application** safely in **[Tutorial 5: Code Generation & Build Integration](05_code_generation_and_integration.md)**.

---

<div style="display: flex; justify-content: space-between; align-items: center; margin-top: 2rem; padding-top: 1rem; border-top: 1px solid var(--fsmc-border);">
    <a href="03_hierarchical_hfsm.md" style="font-weight: 600; color: var(--fsmc-primary);">← Tutorial 3: HFSM & History</a>
    <a href="05_code_generation_and_integration.md" style="font-weight: 600; color: var(--fsmc-primary);">Tutorial 5: Codegen & Integration →</a>
</div>
