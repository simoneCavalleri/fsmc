# Quickstart Tutorial

This tutorial walks through creating a complete, production-ready aerospace sub-system state machine from scratch, verifying its safety properties, and embedding it in C++ using **`fsmc`**.

---

## Scenario: Autonomous UAV Flight Controller

We will model a flight mission state machine with three operating modes:

1. **Preflight**: Sensor calibration and motor arming.
2. **Navigating**: Waypoint navigation with emergency low-battery fail-safe return.
3. **Landed**: Final disarm and shutdown.

---

## Step 1: Author the State Machine Model

Choose your preferred modeling syntax to define the UAV state machine with typed I/O ports and internal registers:

=== "OMG SysML v2 (`uav_mission.sysml`)"
    ```sysml
    package UavMissionSystem {
        state def UavMissionStatechart {
            in port isGpsLocked : Boolean;
            in port batteryLevel : Real { assert constraint { self >= 0.0 and self <= 100.0; } }
            out port waypointReached : Boolean;
            attribute cycleCounter : Integer = 0;

            entry; then SensorCalib;

            state SensorCalib {
                transition cal first SensorCalib accept CalibrationOk then SystemReady;
            }
            state SystemReady {
                transition takeoff first SystemReady accept TakeoffCmd if in.isGpsLocked then WaypointNav;
            }
            state WaypointNav {
                transition wp first WaypointNav accept AreaReached do action { out.waypointReached = true; } then HoverPause;
                transition low1 first WaypointNav accept LowBatteryEvent if in.batteryLevel < 20.0 then ReturnToHome;
            }
            state HoverPause {
                transition res first HoverPause accept ResumeMissionCmd then WaypointNav;
                transition low2 first HoverPause accept LowBatteryEvent if in.batteryLevel < 20.0 then ReturnToHome;
            }
            state ReturnToHome {
                transition td first ReturnToHome accept TouchdownEvent then Landed;
            }
            state Landed {
                transition sd first Landed accept ShutdownCmd then FinalShutdown;
            }
            state FinalShutdown;
        }
    }
    ```

=== "PlantUML (`uav_mission.puml`)"
    ```plantuml
    @startuml
    [*] --> SensorCalib

    SensorCalib --> SystemReady : CalibrationOk
    SystemReady --> WaypointNav : TakeoffCmd [in.isGpsLocked]
    
    WaypointNav --> HoverPause : AreaReached / out.waypointReached=true
    WaypointNav --> ReturnToHome : LowBatteryEvent [in.batteryLevel < 20.0]
    
    HoverPause --> WaypointNav : ResumeMissionCmd
    HoverPause --> ReturnToHome : LowBatteryEvent [in.batteryLevel < 20.0]
    
    ReturnToHome --> Landed : TouchdownEvent
    Landed --> [*] : ShutdownCmd
    @enduml
    ```

---

## Step 2: Formally Verify the State Machine

Run `fsmc` with the `--verify` option to run middle-end passes (deadlock detection, choice completeness, interval domain analysis):

```bash
fsmc -i uav_mission.sysml --verify
```

Expected output:
```text
============================================================================
 Formal Model Verification Report: UavMissionStatechart
============================================================================
 Input File:       uav_mission.sysml
 States:           7
 Total Events:     7
 Transitions:      8
 Choice Nodes:     0
 Deferred Triggers:0
----------------------------------------------------------------------------
 Diagnostics:
  (No warnings or errors detected. Model is formally sound!)
----------------------------------------------------------------------------
 Verification Status: PASSED (Model Sound & Properties Verified)
============================================================================
```

---

## Step 3: Generate the Standalone C++20 Header

Compile the verified model into a self-contained, zero-allocation C++20 header inside namespace `avionics`:

```bash
fsmc -i uav_mission.sysml -o uav_mission_fsm.hpp --target cpp --std 20 --standalone --namespace avionics --name UavMissionFSM
```

!!! tip "Compiling From Visual Diagrams"
    If you chose `uav_mission.puml` instead of SysML v2, add `--allow-diagram` to permit C++ code generation from a visual diagram format.

---

## Step 4: Write the C++ Application

Create `main.cpp`:

```cpp
#include "uav_mission_fsm.hpp"
#include <cassert>
#include <iostream>

int main() {
    using namespace avionics;

    // 1. Initialize internal registers and state machine
    UavMissionFSMRegisters reg{0};
    UavMissionFSM fsm(reg);

    // 2. Setup I/O port structures
    UavMissionFSMInPorts in;
    in.batteryLevel = 98.5;
    in.isGpsLocked = true;
    UavMissionFSMOutPorts out;

    assert(in.validate_contracts());
    std::cout << "Current State: " << fsm.current_state_name() << "\n";
    // Output: SensorCalib

    // 3. Dispatch calibration completion event
    fsm.dispatch(CalibrationOk{}, in, out);
    std::cout << "Current State: " << fsm.current_state_name() << "\n";
    // Output: SystemReady

    // 4. Dispatch takeoff command (guard evaluated against in.isGpsLocked)
    auto result = fsm.dispatch(TakeoffCmd{}, in, out);
    if (result.is_success()) {
        std::cout << "Takeoff successful. Current State: " << fsm.current_state_name() << "\n";
        // Output: WaypointNav
    }

    // 5. Simulate battery drop in InPorts and dispatch LowBatteryEvent
    in.batteryLevel = 14.2; // Critical level (< 20.0)
    auto safe_res = fsm.dispatch(LowBatteryEvent{}, in, out);
    if (safe_res.is_success()) {
        std::cout << "Emergency fail-safe activated. Transitioned to: "
                  << fsm.current_state_name() << "\n";
        // Output: ReturnToHome
    }

    return 0;
}
```

---

## Step 5: Compile and Run

```bash
g++ -std=c++20 main.cpp -o uav_app -Wall -Wextra -Werror -pedantic -pthread
./uav_app
```

Output:
```text
Current State: SensorCalib
Current State: SystemReady
Takeoff successful. Current State: WaypointNav
Emergency fail-safe activated. Transitioned to: ReturnToHome
```

For planned Rust (`no_std`) and ISO C99 target backends, see the [Multi-Target Roadmap](../runtime_api/multi_target_roadmap.md).
