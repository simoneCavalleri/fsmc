# Step 7: Stateflow Digital Twin ECU & Time-Travel Rollback

In this tutorial, we implement an automotive **Electronic Control Unit (ECU)** power and drive mode manager using **MathWorks Stateflow XML (`.sfx`)**, optimize it using the compiler's middle-end **Connective Junction Chaining Pass**, and integrate it with a zero-heap **Time-Travel Snapshot Recorder** for real-time fault recovery and digital twin telemetry.

---

## 1. System Overview & Engineering Requirements

In safety-critical automotive systems (ISO 26262 ASIL-B/D), electronic control units must:
1. **Model Complex Multi-Branch Decisions**: Handle startup prechecks, driving mode selection, and sensor validation without proliferating intermediate dummy states.
2. **Execute Continuous Activities**: Perform continuous background checks (`do_activity`) during nominal state residence.
3. **Handle Edge Triggers Reliably**: Detect discrete signal transitions ($\Delta s = s[k] - s[k-1]$) using sampled change triggers ($z^{-1}$).
4. **Time-Travel Rollback for Digital Twins & HIL**: In Hardware-In-the-Loop (HIL) diagnostics or supervisory digital twin monitoring, capture execution snapshots with zero heap allocation and rewind the state machine back to a validated checkpoint if an anomaly or transient fault occurs.

```mermaid
flowchart TD
    subgraph Model ["MathWorks Stateflow (ecu.sfx)"]
        SF1["Standby State"] --> EvStart["EvStart"]
        EvStart --> J1{"Junction Precheck"}
        J1 -->|"[sensors_ok == 1]"| J2{"Junction Mode"}
        J1 -->|"[default]"| StandbyAlert["Standby / Fault Alert"]
        J2 -->|"[eco_requested == 0]"| Drive["Drive State"]
        J2 -->|"[eco_requested == 1]"| EcoDrive["EcoDrive State"]
    end

    subgraph Optimization ["fsmc / fsm-opt Compiler"]
        Pass["ConnectiveJunctionChainingPass<br/>(Stage 2 Middle-End)"]
        Pass --> Collapsed["Atomic Compound Transitions<br/>(Conjoined Guards & Action Sequences)"]
    end

    subgraph Runtime ["Zero-Heap C++ Runtime"]
        FSM["EcuEngineFSM<br/>(fsm::fsm)"]
        Recorder["fsm::snapshot_recorder<16, 512><br/>(Circular Ring Buffer)"]
        FSM <-->|record() / rewind()| Recorder
    end

    Model --> Optimization
    Optimization --> Runtime
```

---

## 2. Stateflow Source Specification (`ecu.sfx`)

The state machine is authored directly in Simulink Stateflow XML format:

```xml
<?xml version="1.0" encoding="utf-8"?>
<Stateflow>
  <machine id="1" name="EcuEngineModel">
    <chart id="2" name="EcuEngineChart">
      
      <!-- States -->
      <state id="10" name="Standby">
        <type>OR</type>
      </state>
      <state id="20" name="Drive">
        <type>OR</type>
      </state>
      <state id="30" name="EcoDrive">
        <type>OR</type>
      </state>
      <state id="40" name="LimpHome">
        <type>OR</type>
      </state>

      <!-- Connective Flow Junctions -->
      <junction id="100" type="CONNECTIVE"/>
      <junction id="101" type="CONNECTIVE"/>

      <!-- Default Transition -->
      <transition id="1">
        <src id="0"/>
        <dst id="10"/>
      </transition>

      <!-- Multi-Hop Connective Flow Paths -->
      <transition id="2">
        <src id="10"/>
        <dst id="100"/>
        <trigger>EvStart</trigger>
        <action>checkSensors()</action>
      </transition>

      <transition id="3">
        <src id="100"/>
        <dst id="101"/>
        <guard>sensors_ok == 1</guard>
      </transition>

      <transition id="4">
        <src id="101"/>
        <dst id="20"/>
        <guard>eco_requested == 0</guard>
        <action>setDriveRpm()</action>
      </transition>

      <transition id="5">
        <src id="101"/>
        <dst id="30"/>
        <guard>eco_requested == 1</guard>
        <action>setEcoRpm()</action>
      </transition>

      <!-- Anomaly & Shutdown Transitions -->
      <transition id="7">
        <src id="20"/>
        <dst id="40"/>
        <trigger>EvOverheat</trigger>
        <guard>engine_temp > 105.0</guard>
        <action>activateCooling()</action>
      </transition>

      <transition id="8">
        <src id="20"/>
        <dst id="10"/>
        <trigger>EvStop</trigger>
        <action>shutdownEngine()</action>
      </transition>

    </chart>
  </machine>
</Stateflow>
```

---

## 3. Connective Junction Chaining

In standard statechart semantics, connective junctions are transient decision points that must execute without resting. Rather than synthesizing synthetic intermediate states or complex runtime interpreters, `fsmc` features the **Connective Junction Chaining Pass** (`connective-junction-chaining`) in Stage 2 of the middle-end pipeline:

$$\text{Standby} \xrightarrow{\text{EvStart}} J_{100} \xrightarrow{[\text{sensors\_ok}]} J_{101} \xrightarrow{[\text{eco\_requested} == 0]} \text{Drive}$$

The pass performs depth-first search along outgoing junction transitions:
1. **Guard Conjunction**: Conjoins all condition predicates along the path into a single composite boolean AST (`and_(sensors_ok == 1, eco_requested == 0)`).
2. **Action Sequentialization**: Sequences all condition and transition actions along the path in strict FIFO order (`checkSensors(); setDriveRpm();`).
3. **Direct Edge Synthesis**: Replaces the multi-hop flowchart with direct edges between the original source state (`Standby`) and the destination leaf states (`Drive`, `EcoDrive`).
4. **Dead Junction Elimination**: Eliminates the intermediate junctions $J_{100}$ and $J_{101}$ from the target state space.

You can inspect the transformed IR using `fsm-opt`:

```bash
fsm-opt ecu.sfx --passes=connective-junction-chaining --emit-ir
```

---

## 4. Time-Travel Snapshot Recording (`fsm::snapshot_recorder`)

The generated state machine header exposes zero-heap serialization via `fsm.serialize()` and `fsm.deserialize()`. The `fsm::snapshot_recorder<Capacity, MaxSnapshotSize>` container wraps this capability in a circular ring buffer for runtime diagnostic recording and rollback:

```cpp
#include "fsm/backend/cpp/runtime/snapshot_recorder.hpp"

// Allocate fixed-size ring buffer: 16 snapshots, max 512 bytes per snapshot
fsm::snapshot_recorder<16, 512> recorder;

// 1. Record snapshot at key operational checkpoint
recorder.record(ecu, 200 /* Tag: HealthyDrive */, current_timestamp_us());

// 2. State machine encounters an unexpected anomaly or unverified edge
if (sensor_glitch_detected) {
    // Rewind back to the pre-fault verified checkpoint:
    bool ok = recorder.rewind_to_checkpoint(ecu, 200 /* HealthyDrive */);
    assert(ok && ecu.is_in_state<Drive>());
}
```

### Key Technical Properties:
- **Zero Heap Allocations**: All entries are pre-allocated in a contiguous `std::array` within the recorder instance.
- **FNV-1a Checksum Protection**: Every snapshot computes a 32-bit checksum; `rollback()` and `rewind_to_checkpoint()` reject corrupted memory buffers before modifying the state machine.
- **Microsecond Timestamping & Tags**: Checkpoints support custom application tags and microsecond time stamps for telemetry replay.

---

## 5. Complete C++ Digital Twin Harness

Below is the complete, runnable C++ test harness (`main.cpp`):

```cpp
#include <cassert>
#include <cstdint>
#include <iostream>

#include "ecu_fsm.hpp"
#include "fsm/backend/cpp/runtime/snapshot_recorder.hpp"

namespace automotive::ecu {

struct EcuServices : public EcuEngineFSMServices {
    int actions_logged = 0;
    bool sensors_ok_val = true;
    bool eco_requested_val = false;
    double engine_temp_val = 90.0;

    void checkSensors_setDriveRpm() override {
        ++actions_logged;
        std::cout << "  [ECU Action] Chained: checkSensors() -> setDriveRpm() (2200 RPM)\n";
    }

    void checkSensors_setEcoRpm() override {
        ++actions_logged;
        std::cout << "  [ECU Action] Chained: checkSensors() -> setEcoRpm() (1400 RPM)\n";
    }

    void activateCooling() override {
        ++actions_logged;
        std::cout << "  [ECU Action] activateCooling() -> Radiator fan 100% PWM\n";
    }

    void shutdownEngine() override {
        ++actions_logged;
        std::cout << "  [ECU Action] shutdownEngine() -> Fuel injection cutoff\n";
    }
};

} // namespace automotive::ecu

int main() {
    using namespace automotive::ecu;

    EcuServices srv;
    EcuEngineFSM ecu;
    ecu.set_services(srv);

    // Static ring buffer: 16 snapshots, 512 bytes each (0 heap allocations)
    ::fsm::snapshot_recorder<16, 512> recorder;

    // Step 1: Initial Standby state
    assert(ecu.is_in_state<Standby>());
    recorder.record(ecu, 100 /* Standby */, 1000);
    std::cout << "[Step 1] Initialized in Standby state (Snapshot 1 recorded)\n";

    // Step 2: Dispatch EvStart through chained junctions
    srv.sensors_ok_val = true;
    srv.eco_requested_val = false;
    auto res = ecu.dispatch(EvStart{});
    assert(res.is_success());
    assert(ecu.is_in_state<Drive>());
    std::cout << "[Step 2] EvStart processed: arrived at Drive state via chained junctions\n";

    // Record verified operational checkpoint
    recorder.record(ecu, 200 /* Tag: HealthyDrive */, 2000);

    // Step 3: Simulate overheat anomaly triggering transition to LimpHome
    srv.engine_temp_val = 112.0;
    auto res_overheat = ecu.dispatch(EvOverheat{});
    assert(res_overheat.is_success());
    assert(ecu.is_in_state<LimpHome>());
    std::cout << "[Step 3] Overheat anomaly detected: entered LimpHome state\n";

    // Step 4: Supervisory Digital Twin Time-Travel Rollback
    std::cout << "[Step 4] Supervisory HIL Digital Twin triggers Time-Travel Rollback...\n";
    bool rollback_ok = recorder.rewind_to_checkpoint(ecu, 200 /* HealthyDrive */);
    assert(rollback_ok);
    assert(ecu.is_in_state<Drive>());
    std::cout << "[Step 4] Rollback Successful! Active state restored to: Drive\n";

    // Step 5: Graceful stop
    auto res_stop = ecu.dispatch(EvStop{});
    assert(res_stop.is_success());
    assert(ecu.is_in_state<Standby>());
    std::cout << "[Step 5] Graceful engine shutdown complete.\n";

    std::cout << "\n[SUCCESS] Stateflow Digital Twin ECU & Snapshot Rollback validated!\n";
    return 0;
}
```

---

## 6. Building and Running

Compile and run the showcase directly using CMake:

```bash
# Build the showcase executable
cmake --build build --target stateflow_digital_twin_ecu_example

# Run the executable
./build/bin/stateflow_digital_twin_ecu_example
```

Expected output:
```text
============================================================================
 Showcase 06: Stateflow Digital Twin ECU & Zero-Heap Snapshot Recorder
============================================================================

[Step 1] Initializing Digital Twin ECU in Standby state...
  -> Snapshot 1 recorded (Tag: 100 [Standby], Size: 36 bytes)

[Step 2] Dispatching EvStart (sensors_ok=true, eco_requested=false)...
  [ECU Action] Chained: checkSensors() -> setDriveRpm() (Target: 2200 RPM)
  -> Connective Junction Chaining Pass collapsed 2 intermediate junctions!
  -> Active state arrived atomically at: Drive
  -> Snapshot 2 recorded (Tag: 200 [HealthyDrive])

[Step 3] Simulating coolant radiator failure: engine_temp rises to 112 deg C...
  [ECU Action] activateCooling() -> Radiator fan 100% PWM
  -> Overheat guard triggered -> Active state transitioned to: LimpHome

[Step 4] Supervisory HIL Digital Twin triggers Time-Travel Rollback...
  -> Restoring ECU to previous checkpoint 200 (HealthyDrive)...
  -> Rollback Successful! Active state restored to: Drive

[Step 5] Graceful engine shutdown...
  [ECU Action] shutdownEngine() -> Injection cutoff
  -> Active state returned to: Standby

============================================================================
 [SUCCESS] Stateflow Digital Twin ECU & Snapshot Rollback Showcase PASSED!
============================================================================
```

---

## 7. Key Takeaways

1. **Stateflow Flowchart Collapsing**: The middle-end `connective-junction-chaining` pass collapses arbitrary directed acyclic flow graphs of connective junctions into single-step compound transitions, avoiding runtime overhead.
2. **Dual Action Modeling**: Chained actions across multi-hop edges are combined using `fsm::seq_`, maintaining strict sequential execution semantics.
3. **Zero-Heap Time-Travel Rollback**: `fsm::snapshot_recorder` provides hardware-in-the-loop (HIL) systems and digital twin monitors with bounded memory snapshot history and checksum-validated rollback capabilities.
