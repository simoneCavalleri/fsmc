# fsmc — Universal State Machine Compiler

**`fsmc`** (Finite State Machine Compiler) is an open-source, format-agnostic compiler and formal verification toolchain for Model-Based statecharts. It bridges high-level modeling specifications—such as OMG SysML v2, W3C SCXML, and Cameo / MagicDraw (XMI)—with verification engines, format transpilers, and deterministic execution runtimes.

The architecture is centered around a strongly typed canonical Intermediate Representation (**`FsmIr`**), decoupling frontend model ingestion from middle-end verification passes and backend target emission.

---

## Architecture Overview

```mermaid
flowchart LR
    subgraph Frontend["1. Frontend Ingestion"]
        SysML["OMG SysML v2"]
        XMI["Cameo / MagicDraw XMI"]
        SCXML["W3C SCXML"]
        Diagrams["PlantUML / Mermaid / DOT / JSON"]
    end

    subgraph MiddleEnd["2. Canonical IR & Verification"]
        IR["Canonical AST (FsmIr)<br/>Partitioned Memory Model"]
        Passes["Optimization Passes"]
        SMT["EFSM Interval & Guard Analysis"]
        MC["nuXmv Model Checking (LTL / CTL)"]
        Interval["Contract Range Validation"]
    end

    subgraph Backend["3. Backend Targets & Emitters"]
        Transpile["Lossless Transpilation<br/>(SysML v2, SCXML, Diagrams)"]
        RTM["Traceability Matrix (RTM)"]
        CppGen["C++17 / C++20 Runtime Target<br/>(Zero-Heap Reference Backend)"]
    end

    Frontend --> IR
    IR --> Passes
    Passes --> SMT
    Passes --> MC
    Passes --> Interval
    Passes --> Backend
```

### Compiler Subsystems

1. **Frontend Ingestion**: Parses statechart models from OMG SysML v2, W3C SCXML, Cameo (XMI 2.1), PlantUML, Mermaid, Graphviz DOT, and XState JSON into the unified `FsmIr` AST.
2. **Middle-End Analysis & Formal Verification**:
    - **Structural Passes**: Detects unreachable states, conflicting transitions, deadlocks, and incomplete choice paths.
    - **EFSM Invariant & Guard Analysis**: Evaluates datapath contracts, range constraints, and guard satisfiability via static abstract interpretation over interval lattices.
    - **Symbolic Model Checking**: Proves temporal safety and liveness formulas specified in Linear Temporal Logic (LTL) and Computation Tree Logic (CTL) natively and exports to nuXmv / SMV.
    - **Contract Validation**: Propagates value bounds over numeric variables and validates input/output port range contracts.
3. **Backend Target Emission**:
    - **Model Transpilation**: Converts models losslessly between supported representation formats (SysML v2, SCXML, PlantUML, Mermaid, DOT).
    - **Formal Logic Emitters**: Generates symbolic transition systems for external model checkers (SMV / nuXmv).
    - **Traceability Matrices**: Generates formal Requirement Traceability Matrices (RTM) in CSV, JSON, and Markdown formats linking `@fsm:req` annotations to model elements.
    - **Target Code Generation**: Emits standalone, zero-heap C++17 or C++20 header files with strict 4-domain memory partitioning (`InPorts`, `OutPorts`, `Registers`, `Services`), designed with an open architecture to support additional target languages.

---

## Core Capabilities

| Capability | Description | Reference Documentation |
| :--- | :--- | :--- |
| **Universal Transpilation** | Ingest any supported format and export to any target format (e.g. SysML v2 to SCXML, Cameo to PlantUML). | [Modeling Languages](formal_languages/index.md) |
| **Formal Verification** | Prove temporal safety properties (LTL/CTL) and datapath invariants at compile time before deployment. | [Verification & Safety](verification_and_safety/index.md) |
| **Partitioned Memory Model** | Replaces unstructured context objects with 4 segregated domains: `InPorts`, `OutPorts`, `Registers`, and `Services`. | [Core Concepts](concepts/index.md) |
| **Deterministic Execution** | C++ reference backend operates with 0 bytes heap allocation, 0 virtual tables, and $O(1)$ dispatch time. | [Memory & Real-Time](runtime_api/memory_and_realtime.md) |
| **Interactive Web Playground** | Ingest, verify, transpile, and simulate statecharts directly in the browser via WebAssembly. | [Web Playground](playground/index.md) |

---

## Quick Example: Ingestion to Code Execution

### 1. Statechart Definition

Define a state machine using your preferred input format:

=== "OMG SysML v2 (`mission.sysml`)"
    ```sysml
    package MissionSystem {
        state def UavMission {
            in port has_gps_lock : Boolean;
            in port battery_percent : Integer { assert constraint { self >= 0 and self <= 100; } }
            out port motor_active : Boolean;
            attribute waypoints_completed : Integer = 0;

            entry; then SensorCalib;

            state SensorCalib {
                transition on CalibrationOk do out.motor_active = true; then SystemReady;
            }
            state SystemReady {
                transition on TakeoffCmd if in.has_gps_lock then Navigating;
            }
            state Navigating {
                transition if in.battery_percent < 20 then ReturnToHome;
                transition on AreaReached do reg.waypoints_completed += 1; then Navigating;
            }
            state ReturnToHome;
        }
    }
    ```

=== "PlantUML (`mission.puml`)"
    ```plantuml
    @startuml
    [*] --> SensorCalib

    SensorCalib --> SystemReady : CalibrationOk / out.motor_active = true
    SystemReady --> Navigating : TakeoffCmd [in.has_gps_lock]
    
    Navigating --> ReturnToHome : [in.battery_percent < 20]
    Navigating --> Navigating : AreaReached / reg.waypoints_completed += 1
    @enduml
    ```

=== "W3C SCXML (`mission.scxml`)"
    ```xml
    <scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="SensorCalib">
      <state id="SensorCalib">
        <transition event="CalibrationOk" target="SystemReady"/>
      </state>
      <state id="SystemReady">
        <transition event="TakeoffCmd" cond="in.has_gps_lock" target="Navigating"/>
      </state>
      <state id="Navigating">
        <transition cond="in.battery_percent &lt; 20" target="ReturnToHome"/>
      </state>
      <state id="ReturnToHome"/>
    </scxml>
    ```

=== "Mermaid (`mission.mmd`)"
    ```mermaid
    stateDiagram-v2
        [*] --> SensorCalib
        SensorCalib --> SystemReady : CalibrationOk
        SystemReady --> Navigating : TakeoffCmd
        Navigating --> ReturnToHome : [battery_percent < 20]
    ```

---

### 2. Compilation and Verification via CLI

The `fsmc` command-line interface provides unified access to all compiler pipeline stages:

=== "Formal Verification"
    ```bash
    # Run middle-end verification passes (deadlock, completeness, LTL/CTL model checking)
    fsmc -i mission.sysml --verify
    ```

=== "Format Transpilation"
    ```bash
    # Transpile SysML v2 to W3C SCXML
    fsmc -i mission.sysml --format scxml -o mission.scxml

    # Transpile Cameo XMI to PlantUML
    fsmc -i cameo_model.xml --format puml -o model.puml
    ```

=== "Requirement Traceability (RTM)"
    ```bash
    # Export Requirement Traceability Matrix for verification audits
    fsmc -i mission.sysml --rtm-output rtm_matrix.md
    ```

=== "Code Generation"
    ```bash
    # Generate standalone C++17 / C++20 single-header runtime (Production)
    fsmc -i mission.sysml -o uav_fsm.hpp --target cpp --std 20 --standalone

    # Generate Rust no_std module (In Development)
    fsmc -i mission.sysml -o uav_fsm.rs --target rust

    # Generate ISO C99 header & implementation (Planned)
    fsmc -i mission.sysml -o uav_fsm.h --target c
    ```

---

### 3. Application Integration

Execute transitions using the segregated 4-domain memory model across target languages:

=== "C++ Target (Production v0.5.0)"
    ```cpp
    #include "uav_fsm.hpp"
    #include <iostream>
    #include <cassert>

    int main() {
        using namespace MissionSystem;

        // 1. Initialize State Machine with Partitioned Memory
        UavMissionRegisters reg{};
        UavMissionServices srv{};
        UavMissionFSM fsm(reg, srv);

        UavMissionInPorts in{.has_gps_lock = true, .battery_percent = 100};
        UavMissionOutPorts out{};

        // 2. Reactive Event Dispatching
        fsm.dispatch(CalibrationOk{}, in, out);
        assert(fsm.is_in<SystemReady>());
        assert(out.motor_active == true);

        fsm.dispatch(TakeoffCmd{}, in, out);
        assert(fsm.is_in<Navigating>());

        // 3. Continuous Control Loop Step (Sampled Inputs)
        in.battery_percent = 15; // Low battery condition
        fsm.step(in, out);       // Evaluates continuous transitions
        assert(fsm.is_in<ReturnToHome>());

        return 0;
    }
    ```

=== "Rust Target (Roadmap Preview)"
    > [!NOTE]
    > **Roadmap Preview**: Rust `#![no_std]` code generation is an upcoming roadmap feature. C++ is the active production runtime in `v0.5.0`.

    ```rust
    // Generated Rust no_std State Machine
    use uav_fsm::prelude::*;

    fn main() {
        let mut fsm = UavMissionFsm::new(UavRegisters::default());
        let mut in_ports = UavInPorts { has_gps_lock: true, battery_percent: 100 };
        let mut out_ports = UavOutPorts::default();

        // 1. Reactive Event Dispatching
        fsm.dispatch(&Event::CalibrationOk, &in_ports, &mut out_ports);
        assert_eq!(fsm.state(), State::SystemReady);

        // 2. Continuous Control Loop Step (Sampled Inputs)
        in_ports.battery_percent = 15;
        fsm.step(&in_ports, &mut out_ports);
        assert_eq!(fsm.state(), State::ReturnToHome);
    }
    ```

=== "C Target (Embedded C Roadmap)"
    > [!NOTE]
    > **Roadmap Preview**: ISO C99 code generation is an upcoming roadmap feature. C++ is the active production runtime in `v0.5.0`.

    ```c
    #include "uav_fsm.h"
    #include <assert.h>

    int main(void) {
        uav_fsm_t fsm;
        uav_registers_t reg = {0};
        uav_in_ports_t in = {.has_gps_lock = true, .battery_percent = 100};
        uav_out_ports_t out = {0};

        uav_fsm_init(&fsm, &reg);

        /* 1. Reactive Event Dispatching */
        uav_fsm_dispatch(&fsm, UAV_EV_CALIB_OK, &in, &out);
        assert(fsm.current_state == UAV_STATE_READY);

        /* 2. Continuous Control Loop Step */
        in.battery_percent = 15;
        uav_fsm_step(&fsm, &in, &out);
        assert(fsm.current_state == UAV_STATE_RTH);

        return 0;
    }
    ```

---

## Documentation Directory

- **[Getting Started](getting_started/index.md)**: System requirements, installation methods (CMake FetchContent, Conan, source builds), CLI reference, and build integration.
- **[Step-by-Step Tutorials](tutorials/index.md)**: Progressive tutorials covering model design, datapath variables, hierarchical states (HFSM), formal verification, and code generation.
- **[Architecture & Concepts](concepts/index.md)**: Semantics of the canonical IR, MBSE 4-domain memory architecture, and real-time execution guarantees.
- **[Modeling Languages](formal_languages/index.md)**: Specifications and examples for SysML v2, Cameo XMI, SCXML, PlantUML, Mermaid, DOT, and JSON.
- **[Verification & Safety](verification_and_safety/index.md)**: Formal verification using built-in model checking, EFSM interval analysis, nuXmv SMV export, and RTM generation.
- **[Runtime C++ API](runtime_api/index.md)**: Synchronous Dual-Paradigm Core, Lock-Free SPSC, Thread-Safe MPSC, and Tracing API reference.
- **[Compiler Internals](internals/architecture.md)**: Compiler pipeline internals, IR AST specification, pass manager, and contributor guide.
- **[Interactive Playground](playground/index.md)**: In-browser compiler and simulation environment running via WebAssembly.

---

## License

`fsmc` is open-source software licensed under the [MIT License](file:///home/simone/dev/github/fsmc/LICENSE).
