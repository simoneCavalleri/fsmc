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

            entry; then state SensorCalib;

            state SensorCalib {
                transition on CalibrationOk to SystemReady;
            }
            state SystemReady {
                transition on TakeoffCmd if in.isGpsLocked to WaypointNav;
            }
            state WaypointNav {
                transition on AreaReached do out.waypointReached = true; to HoverPause;
                transition on LowBatteryEvent if in.batteryLevel < 20.0 to ReturnToHome;
                transition continuous_low_battery if in.batteryLevel < 20.0 to ReturnToHome;
            }
            state HoverPause {
                transition on ResumeMissionCmd to WaypointNav;
                transition on LowBatteryEvent if in.batteryLevel < 20.0 to ReturnToHome;
            }
            state ReturnToHome {
                transition on TouchdownEvent to Landed;
            }
            state Landed {
                transition on ShutdownCmd to FinalShutdown;
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
[INFO] Running Pass: HierarchyCanonicalizationPass ... OK (0.02ms)
[INFO] Running Pass: GuardSimplificationPass ... OK (0.01ms)
[INFO] Running Pass: EFSMDataPathPass ... OK (0.04ms)
[INFO] Running Pass: ModelSafetyVerifier ... OK (0.03ms)
============================================================================
 Verification Status: PASSED (Model Sound)
============================================================================
```

---

## Step 3: Generate the C++ State Machine

Compile the model into a standalone C++20 header with namespace `avionics` and class name `UavMissionFSM`:

```bash
fsmc -i uav_mission.sysml -o uav_mission_fsm.hpp --std 20 --standalone --namespace avionics --name UavMissionFSM
```

---

## Step 4: Write the Application Code

Create `main.cpp`:

```cpp
#include "uav_mission_fsm.hpp"
#include <cassert>
#include <iostream>

int main() {
    using namespace avionics;

    // 1. Initialize internal registers and services
    UavMissionFSMRegisters reg{0};
    UavMissionFSMObserver obs;
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

    // 5. Simulate battery drop in InPorts and evaluate continuous sampled step
    in.batteryLevel = 14.2; // Critical level (< 20.0)
    auto safe_res = fsm.step(in, out);
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
g++ -std=c++20 main.cpp -o uav_app -Wall -Wextra -Werror -pedantic
./uav_app
```

Output:
```text
Current State: SensorCalib
Current State: SystemReady
Takeoff successful. Current State: WaypointNav
Emergency fail-safe activated. Transitioned to: ReturnToHome
```
