# `fsmc` Canonical Examples Suite

Welcome to the official example catalog for **`fsmc` (The Universal State Machine Compiler)** and **`fsm-opt` (Formal IR Optimizer & Model Checker)**.

The examples in this directory demonstrate the capabilities of the compiler across modern critical engineering domains (Aerospace Avionics, Automotive ISO 26262, Industrial Robotics, Telecom Protocols, and Embedded DSP Sensing). Every example is fully self-contained, adheres to the target-agnostic Intermediate Representation (`FsmIr`), exercises the middle-end pass pipeline, and provides an immediately runnable C++ verification harness.

---

## Progressive Architecture & Curriculum

```text
examples/
├── 00_standalone_iot_controller/   --> Pure C++20 Header-Only IoT Thermostat (Zero-Generator, Zero-Heap)
│   └── main.cpp                        (Strongly-typed payloads, guards on payloads, dispatch_result audit)
├── 01_basic_patterns/              --> Resilient network protocol session (connection handshake)
│   └── network_protocol/               (Time invariants, flight recorder, tick(dt), thread-safe async worker)
├── 02_advanced_semantics/          --> 6-DOF industrial robotic arm sequencer
│   └── robotic_arm_sequencer/          (Deep history [H*], compound substates, boundary action fusion)
├── 03_concurrency_and_timing/      --> Automotive Battery Management System (ISO 26262 ASIL-D)
│   └── automotive_bms/                 (Orthogonal regions, spsc_fsm lock-free queue, clock invariants)
├── 04_formal_verification/         --> DO-178C Level A fly-by-wire flight control mode manager
│   └── flight_control_modes/           (Formal LTL/CTL model checking, RTM traceability, nuXmv export)
├── 05_custom_toolchain/            --> Industrial sensor acquisition & DSP pipeline
│   └── plugin_and_pipeline/            (External Unix filter via stdin/stdout, dynamic C++ pass plugin)
├── CMakeLists.txt                 --> Master build integration for all example targets
└── run_all_examples.sh            --> Automated end-to-end CI/CLI test runner
```

---

## Example Catalog

| Directory | Domain & Case Study | Key Architecture & IR Features Demonstrated | Middle-End Passes / Runtime Features | Primary CLI / Test Command |
| :--- | :--- | :--- | :--- | :--- |
| [`00_standalone_iot_controller`](00_standalone_iot_controller/) | **Embedded IoT / HVAC Controller**<br>Smart Adaptive Thermostat | 100% Pure C++20 header-only engine without code generation, strongly-typed event payloads (`TemperatureTelemetry`, `TargetSetCmd`), conditional guards, exhaustive `dispatch_result` inspection, state lifecycle hooks. | `fsm::transition_table`<br>`fsm::row<...>::when<...>::then<...>`<br>Zero dynamic allocations (0 bytes heap) | `standalone_iot_controller_example` |
| [`01_basic_patterns/network_protocol`](01_basic_patterns/network_protocol/) | **Telecom / Network Protocols**<br>Resilient Network Session | SysML v2 / PlantUML handshake with retry counter, `fsm::with_trace_buffer<16>` circular flight recorder, state residence time invariants, deterministic `tick(dt)`, and `fsm::thread_safe_fsm` async worker. | `guard-simplification`<br>`dead-state-pruning`<br>`fsm::thread_safe_fsm`<br>`fsm::with_trace_buffer` | `fsm-opt connection.sysml --metrics`<br>`network_protocol_example` |
| [`02_advanced_semantics/robotic_arm_sequencer`](02_advanced_semantics/robotic_arm_sequencer/) | **Industrial Automation**<br>6-DOF Robotic Arm Toolhead | Multi-level deep history (`[H*]`), nested machining substates, safety interlock pausing, submachine tool calibration. | `history-lowering`<br>`boundary-action-fusion`<br>`dead-state-pruning` | `fsm-opt robotic_arm.puml --passes=history-lowering --emit-ir`<br>`robotic_arm_example` |
| [`03_concurrency_and_timing/automotive_bms`](03_concurrency_and_timing/automotive_bms/) | **Automotive (ISO 26262 ASIL-D)**<br>High-Voltage EV Battery Pack | 3 parallel orthogonal regions (`ThermalSupervision` \|\| `CellBalancing` \|\| `IsolationMonitoring`), clock state duration invariants (`stay <= 100ms`), priority preemption, and high-frequency lock-free event pipeline via `fsm::spsc_fsm`. | `orthogonal-interference`<br>`determinism-enforcement`<br>`clock-lowering`<br>`fsm::spsc_fsm` | `fsmc bms.sysml --check-races --strict-determinism`<br>`automotive_bms_example` |
| [`04_formal_verification/flight_control_modes`](04_formal_verification/flight_control_modes/) | **Aerospace (DO-178C Level A)**<br>Fly-By-Wire Flight Computer | Formal LTL/CTL temporal logic specifications (`@fsm:property`), DO-178C requirement traceability audit (`--req-audit`), automated RTM matrix export, nuXmv SMV model checking. | `model-checking-engine`<br>`rtm-traceability-audit`<br>`guard-simplification` | `fsmc fms.sysml --verify --req-audit --rtm-output rtm.json`<br>`flight_control_modes_example` |
| [`05_custom_toolchain/plugin_and_pipeline`](05_custom_toolchain/plugin_and_pipeline/) | **Embedded DSP Sensing**<br>Sensor Acquisition Pipeline | External Unix filter pipelines (`--pipe-through`), dynamic runtime C++ middle-end pass plugins (`--load-pass-plugin`), typed MBSE contracts. | `pipe-through`<br>`naming-audit` (Plugin)<br>`timed-invariants-verifier` | `fsm-opt sensor_pipeline.sysml --load-pass-plugin=... --pipe-through=...`<br>`sensor_pipeline_example` |

---

## Quickstart: Running All Examples

### 1. Build All Example Targets via CMake
All examples are integrated into the main project build tree and registered in CTest:

```bash
# Configure and build in Release or Debug mode
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run the test suite including all example runners
ctest --test-dir build --output-on-failure -R "_example"
```

### 2. One-Shot CLI Validation Script
To validate every example model across `fsmc` code generation, `fsm-opt` optimization passes, model checking, and executable harnesses in one step:

```bash
./examples/run_all_examples.sh
```

---

## Standards and Formal Formats Supported

Every example model is authored using industry-standard formal formats:
- **OMG SysML v2** (`.sysml`): Next-generation systems modeling language with native port contracts, state definitions, and requirement satisfaction semantics.
- **PlantUML** (`.puml`): Visual statechart diagrams annotated with `@fsm:*` formal directives.
- **Mermaid** (`.mmd`): Declarative web-native diagrams with embedded contracts.
- **nuXmv / SMV** (`.smv`): Symbolic formal verification specifications generated automatically from the Intermediate Representation.
