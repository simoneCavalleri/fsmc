# MathWorks Simulink Stateflow

`fsmc` provides native ingestion and roundtrip serialization for **MathWorks Simulink Stateflow** models exported to XML or JSON formats. This enables `fsmc` to act as an open-source, vendor-independent bridge from Simulink Stateflow designs to zero-allocation C++17/20, formal model checkers, and MBSE specifications without requiring commercial code generation toolboxes.

---

## 1. Supported Stateflow Elements

`fsmc` maps the core hierarchical statechart constructs of MathWorks Stateflow directly into its unified `FsmIr`:

| Stateflow Construct | XML Representation | `fsmc` Semantic Mapping |
| :--- | :--- | :--- |
| **Chart Root** | `<Stateflow><machine><Children><chart id="..."><P Name="name">...</P>` | Root `FsmIr` model and state machine class |
| **Exclusive State (OR)** | `<state SSID="..."><P Name="type">OR_STATE</P><P Name="decomposition">CLUSTER_STATE</P>` | Standard atomic or composite state |
| **Parallel State (AND)** | `<state SSID="..."><P Name="decomposition">SET_STATE</P>` with `AND_STATE` child regions | Orthogonal parallel execution region |
| **Connective Junction** | `<junction SSID="..."><P Name="type">CONNECTIVE_JUNCTION</P></junction>` | Multi-hop flowchart junction lowered via `ConnectiveJunctionChainingPass` |
| **History Junction** | `<junction SSID="..."><P Name="type">HISTORY_JUNCTION</P></junction>` | History pseudostate (`ShallowHistory` / `DeepHistory`) |
| **Data & Events** | `<data SSID="..." name="..."><P Name="scope">INPUT_DATA\|OUTPUT_DATA\|LOCAL_DATA</P>` / `<event>` | Segregated `InPorts`, `OutPorts`, `Registers`, and input events |
| **Transitions** | `<transition SSID="..."><P Name="executionOrder">...</P><src><P Name="SSID">...</P></src><dst>...</dst>` | Ranked directed `TransitionEdge` (default transition omits `<src>`) |
| **Transition Label** | `<P Name="labelString">Event [Guard] { CondAction } / { TransAction }</P>` | Trigger, guard condition, condition action, and transition action |
| **Temporal Logic** | `after(N, sec)`, `after(N, msec)`, `every(N, msec)` | Deterministic timed transition (`TimeTrigger`) |
| **State Actions** | `entry: ...; during: ...; exit: ...;` inside `<P Name="labelString">` | State entry, continuous step (`do_activity`), and exit actions |

---

## 2. Transition Label Syntax & Execution Semantics

Stateflow utilizes a compact, expressive label format supporting both **Condition Actions** and **Transition Actions**:

```text
trigger [guard] { condition_action } / { transition_action }
```

`fsmc` automatically decomposes and compiles this label format:
* **Trigger**: Event name (e.g., `EvArmed`, `StepTick`).
* **Guard**: Boolean expression enclosed in square brackets (e.g., `[in.sensor_altitude > 1000.0]`).
* **Condition Action**: Action enclosed in curly braces *before* the slash (e.g., `{ checkSensors(); }`). Executed immediately when the guard evaluates to true, even before subsequent junction evaluation.
* **Transition Action**: Action enclosed in curly braces *after* the slash (e.g., `/ { setDriveRpm(); }`). Executed when the destination state is taken.
* **Dual Action Execution**: When both actions are specified, `fsmc` synthesizes a deterministic sequence `::fsm::seq_<ConditionAction, TransitionAction>` preserving Stateflow simulation semantics.

### Connective Junction Chaining (`ConnectiveJunctionChainingPass`)

Stateflow models frequently organize complex branching logic through networks of connective junctions:

```mermaid
flowchart LR
    Standby["Standby"] -->|EvStart / checkSensors()| J1(("Junction 1"))
    J1 -->|"[sensors_ok == 1]"| J2(("Junction 2"))
    J1 -->|"[sensors_ok == 0] / faultAlert()"| Emergency["EmergencyShutdown"]
    J2 -->|"[eco_requested == 0] / setDriveRpm()"| Drive["Drive"]
    J2 -->|"[eco_requested == 1] / setEcoRpm()"| Eco["EcoMode"]
```

`fsmc`'s `ConnectiveJunctionChainingPass` (Stage 2) recursively expands all acyclic paths through connective junctions:
1. Intermediate micro-states (`J1`, `J2`) are pruned.
2. Guards along the path are conjoined: `fsm::and_<sensors_ok__1, eco_requested__0>`.
3. Actions along the path are fused: `checkSensors_setDriveRpm`.
4. Emitted C++ transition tables execute direct atomic jumps (`Standby -> Drive`), yielding zero state allocation and minimal cycle latency.

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
  <machine id="1">
    <P Name="name">FlightControllerMachine</P>
    <!-- @fsm:enum FlightMode : uint8_t { Idle = 0, Arming = 1, Cruising = 2, Emergency = 3 } -->
    <!-- @fsm:port name=sensor_altitude type=float dir=in min=0.0 max=50000.0 unit="[m]" -->
    <!-- @fsm:port name=thrust_cmd type=float dir=out min=0.0 max=100.0 unit="[%]" -->
    <!-- @fsm:var name=cycles_elapsed type=uint32_t init=0 -->
    <!-- @fsm:property name=SafeCeiling kind=Safety ltl="G (sensor_altitude < 50000.0)" -->
    <Children>
      <chart id="2">
        <P Name="name">FlightController</P>
        <P Name="decomposition">CLUSTER_CHART</P>
        <Children>
          <state SSID="10">
            <P Name="labelString">Preflight&#10;entry: out.thrust_cmd = 0.0</P>
            <P Name="type">OR_STATE</P>
            <P Name="decomposition">CLUSTER_STATE</P>
          </state>

          <state SSID="11">
            <P Name="labelString">InFlight&#10;entry: out.thrust_cmd = 75.0</P>
            <P Name="type">OR_STATE</P>
            <P Name="decomposition">CLUSTER_STATE</P>
          </state>

          <data SSID="20" name="sensor_altitude">
            <P Name="scope">INPUT_DATA</P>
            <P Name="dataType">single</P>
          </data>
          <data SSID="21" name="thrust_cmd">
            <P Name="scope">OUTPUT_DATA</P>
            <P Name="dataType">single</P>
          </data>
          <data SSID="22" name="cycles_elapsed">
            <P Name="scope">LOCAL_DATA</P>
            <P Name="dataType">uint32</P>
          </data>
          <event SSID="25" name="EvArm">
            <P Name="scope">INPUT_EVENT</P>
          </event>

          <transition SSID="30">
            <P Name="executionOrder">1</P>
            <dst><P Name="SSID">10</P></dst>
          </transition>

          <transition SSID="31">
            <P Name="labelString">EvArm [in.sensor_altitude &gt;= 0.0] / { reg.cycles_elapsed = 0 }</P>
            <P Name="executionOrder">1</P>
            <src><P Name="SSID">10</P></src>
            <dst><P Name="SSID">11</P></dst>
          </transition>

          <transition SSID="32">
            <P Name="labelString">after(30, sec)</P>
            <P Name="executionOrder">1</P>
            <src><P Name="SSID">11</P></src>
            <dst><P Name="SSID">10</P></dst>
          </transition>
        </Children>
      </chart>
    </Children>
  </machine>
</Stateflow>
```

---

## 4. CLI Workflow & Roundtrip Serialization

### Ingesting Stateflow and Generating C++ Code
`fsmc` automatically detects Stateflow models by extension (`.slx`, `.sfx`, `.stateflow`, `.xml`) or by inspecting file contents:

```bash
# Ingest native MathWorks Simulink .slx package directly
fsmc -i Model.slx -o Model.hpp --target cpp

# Ingest Stateflow XML and emit zero-allocation C++20 header
fsmc -i FlightController.xml -o FlightController.hpp --target cpp

# Generate C++ with active object asynchronous wrapper and MC/DC harness
fsmc -i FlightController.xml -o FlightController.hpp --emit-test-harness FlightController_mcdc_test.cpp
```

### Direct `.slx` OPC Container Ingestion
`fsmc` includes native OPC (Open Packaging Convention) / ZIP container extraction with built-in Deflate decompression. It reads `.slx` models directly from disk without requiring MATLAB, Simulink, or external decompression tools like `unzip`:
- Automatically locates and inspects `simulink/stateflow.xml` and `simulink/blockdiagram.xml`.
- Streams compressed block XML and extracts charts, states, junctions, and transitions into `FsmIr`.

### Exporting Any Model to Stateflow
You can translate models from any supported format (SysML v2, SCXML, PlantUML, Mermaid, Cameo XMI) into Stateflow XML:

```bash
# Transpile SysML v2 to MathWorks Stateflow XML
fsmc -i FlightController.sysml --export stateflow -o FlightController_sf.xml
```

The resulting XML can be directly imported into MathWorks Simulink or round-tripped back through `fsmc` without loss of structure or metadata.
