# MathWorks Simulink Stateflow

`fsmc` provides native ingestion and roundtrip serialization for **MathWorks Simulink Stateflow** models exported to XML or JSON formats. This enables `fsmc` to act as an open-source, vendor-independent bridge from Simulink Stateflow designs to zero-allocation C++17/20, formal model checkers, and MBSE specifications without requiring commercial code generation toolboxes.

---

## 1. Supported Stateflow Elements

`fsmc` maps the core hierarchical statechart constructs of MathWorks Stateflow directly into its unified `FsmIr`:

| Stateflow Construct | XML Representation | `fsmc` Semantic Mapping |
| :--- | :--- | :--- |
| **Chart Root** | `<Stateflow><machine><chart name="...">` | Root `FsmIr` model and state machine class |
| **Exclusive State (OR)** | `<state name="..." decomposition="EXCLUSIVE_OR">` | Standard atomic or composite state |
| **Parallel State (AND)** | `<state name="..." decomposition="PARALLEL_AND">` | Orthogonal parallel execution region |
| **History Junction** | `<junction type="HISTORY">` | Deep history pseudostate (`DeepHistory`) |
| **Transitions** | `<transition><src id="..."><dst id="...">` | Directed `TransitionEdge` |
| **Transition Label** | `<labelString>Event [Guard] / { Action }</labelString>` | Trigger, guard condition, and action effect |
| **Temporal Logic** | `after(N, sec)`, `after(N, msec)` | Deterministic timed transition (`TimeTrigger`) |
| **State Actions** | `en: entryAction(); du: during(); ex: exit();` | State entry, continuous step, and exit actions |

---

## 2. Transition Label Syntax & Temporal Logic

Stateflow utilizes a compact, expressive label format for transitions:

```text
trigger [guard] / { action }
```

`fsmc` automatically decomposes this label format:
* **Trigger**: Event name (e.g., `EvArmed`, `StepTick`).
* **Guard**: Boolean expression enclosed in square brackets (e.g., `[in.sensor_altitude > 1000.0]`).
* **Action Effect**: C-like assignment or routine call enclosed in curly braces (e.g., `/{ reg.mode = 1; }`).

### Temporal Logic Triggers
Simulink Stateflow's temporal operators are natively recognized and translated into deterministic timer triggers:

* `after(500, msec)` or `after(0.5, sec)`: Translated into a `TimeTrigger` with duration `500ms`.
* During execution, the synchronous runtime steps these timers deterministically using `sm.tick(dt)`.

---

## 3. Lossless Directives in Stateflow Models

To enrich Stateflow models with hardware I/O ports, range bounds, enums, structs, and formal temporal properties (LTL/CTL), `fsmc` supports embedding directives inside standard XML comments:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<Stateflow>
  <machine name="FlightControllerMachine">
    <!-- @fsm:enum name=FlightMode values=Idle,Arming,Cruising,Emergency -->
    <!-- @fsm:port name=sensor_altitude type=float dir=in min=0.0 max=50000.0 unit="[m]" -->
    <!-- @fsm:port name=thrust_cmd type=float dir=out min=0.0 max=100.0 unit="[%]" -->
    <!-- @fsm:register name=cycles_elapsed type=uint32_t init=0 -->
    <!-- @fsm:property name=SafeCeiling kind=Safety ltl="G (sensor_altitude < 50000.0)" -->

    <chart name="FlightController" initial="Preflight">
      <state name="Preflight" SSID="1">
        <labelString>en: out.thrust_cmd = 0.0;</labelString>
      </state>

      <state name="InFlight" SSID="2">
        <labelString>en: out.thrust_cmd = 75.0;</labelString>
      </state>

      <transition SSID="10">
        <src name="Preflight"/>
        <dst name="InFlight"/>
        <labelString>EvArm [in.sensor_altitude >= 0.0] / { reg.cycles_elapsed = 0; }</labelString>
      </transition>

      <transition SSID="11">
        <src name="InFlight"/>
        <dst name="Preflight"/>
        <labelString>after(30, sec)</labelString>
      </transition>
    </chart>
  </machine>
</Stateflow>
```

---

## 4. CLI Workflow & Roundtrip Serialization

### Ingesting Stateflow and Generating C++ Code
`fsmc` automatically detects Stateflow XML files by extension (`.sfx`, `.stateflow`, `.xml`) or by inspecting the `<Stateflow>` root element:

```bash
# Ingest Stateflow XML and emit zero-allocation C++17 header
fsmc -i FlightController.xml -o FlightController.hpp --target cpp

# Generate C++ with active object asynchronous wrapper and MC/DC harness
fsmc -i FlightController.xml -o FlightController.hpp --emit-test-harness FlightController_mcdc_test.cpp
```

### Exporting Any Model to Stateflow
You can translate models from any supported format (SysML v2, SCXML, PlantUML, Mermaid, Cameo XMI) into Stateflow XML:

```bash
# Transpile SysML v2 to MathWorks Stateflow XML
fsmc -i FlightController.sysml --export stateflow -o FlightController_sf.xml
```

The resulting XML can be directly imported into MathWorks Simulink or round-tripped back through `fsmc` without loss of structure or metadata.
