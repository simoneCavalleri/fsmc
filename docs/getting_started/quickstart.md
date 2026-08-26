# Quickstart Tutorial

This tutorial walks through creating a complete, production-ready aerospace sub-system state machine from scratch, verifying its safety properties, and embedding it in C++.

---

## Scenario: Autonomous UAV Flight Controller

We will model a flight mission state machine with three operating modes:
1. **Preflight**: Sensor calibration and motor arming.
2. **Navigating**: Waypoint navigation with emergency low-battery fail-safe return.
3. **Landed**: Final disarm and shutdown.

---

## Step 1: Author the State Machine Model

Choose your preferred modeling syntax to define the UAV state machine:

=== "OMG SysML v2 (`uav_mission.sysml`)"
    ```sysml
    package UavMissionSystem {
        state def UavMissionStatechart {
            attribute batteryLevel : Real = 100.0;
            attribute isGpsLocked : Boolean = true;
            attribute waypointReached : Boolean = false;

            entry; then state SensorCalib;

            state SensorCalib {
                transition on CalibrationOk to SystemReady;
            }
            state SystemReady {
                transition on TakeoffCmd if isGpsLocked to WaypointNav;
            }
            state WaypointNav {
                transition on AreaReached do waypointReached = true; to HoverPause;
                transition on LowBatteryEvent if batteryLevel < 20.0 to ReturnToHome;
            }
            state HoverPause {
                transition on ResumeMissionCmd to WaypointNav;
                transition on LowBatteryEvent if batteryLevel < 20.0 to ReturnToHome;
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
    SystemReady --> WaypointNav : TakeoffCmd [isGpsLocked]
    
    WaypointNav --> HoverPause : AreaReached / waypointReached=true
    WaypointNav --> ReturnToHome : LowBatteryEvent [batteryLevel < 20.0]
    
    HoverPause --> WaypointNav : ResumeMissionCmd
    HoverPause --> ReturnToHome : LowBatteryEvent [batteryLevel < 20.0]
    
    ReturnToHome --> Landed : TouchdownEvent
    Landed --> [*] : ShutdownCmd
    @enduml
    ```

=== "W3C SCXML (`uav_mission.scxml`)"
    ```xml
    <scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="SensorCalib">
        <datamodel>
            <data id="batteryLevel" expr="100.0"/>
            <data id="isGpsLocked" expr="true"/>
            <data id="waypointReached" expr="false"/>
        </datamodel>
        <state id="SensorCalib">
            <transition event="CalibrationOk" target="SystemReady"/>
        </state>
        <state id="SystemReady">
            <transition event="TakeoffCmd" cond="isGpsLocked" target="WaypointNav"/>
        </state>
        <state id="WaypointNav">
            <transition event="AreaReached" target="HoverPause">
                <assign location="waypointReached" expr="true"/>
            </transition>
            <transition event="LowBatteryEvent" cond="batteryLevel &lt; 20.0" target="ReturnToHome"/>
        </state>
        <state id="HoverPause">
            <transition event="ResumeMissionCmd" target="WaypointNav"/>
            <transition event="LowBatteryEvent" cond="batteryLevel &lt; 20.0" target="ReturnToHome"/>
        </state>
        <state id="ReturnToHome">
            <transition event="TouchdownEvent" target="Landed"/>
        </state>
        <state id="Landed">
            <transition event="ShutdownCmd" target="FinalShutdown"/>
        </state>
        <final id="FinalShutdown"/>
    </scxml>
    ```

=== "Mermaid (`uav_mission.mmd`)"
    ```mermaid
    stateDiagram-v2
        [*] --> SensorCalib
        SensorCalib --> SystemReady: CalibrationOk
        SystemReady --> WaypointNav: TakeoffCmd [isGpsLocked]
        WaypointNav --> HoverPause: AreaReached
        WaypointNav --> ReturnToHome: LowBatteryEvent [batteryLevel < 20.0]
        HoverPause --> WaypointNav: ResumeMissionCmd
        HoverPause --> ReturnToHome: LowBatteryEvent [batteryLevel < 20.0]
        ReturnToHome --> Landed: TouchdownEvent
        Landed --> [*]: ShutdownCmd
    ```


---

## Step 2: Formal Verification and Analysis

Before generating any code, run `fsmc` in verification mode to check for reachability, deadlocks, and interval consistency:

```bash
fsmc -i uav_mission.sysml --verify
```

Expected output:
```text
============================================================================
 Formal Model Verification Report: UavMissionStatechart
============================================================================
 Input File:       uav_mission.sysml
 States:           6
 Total Events:     6
 Transitions:      7
 Choice Nodes:     0
 Deferred Triggers:0
----------------------------------------------------------------------------
 Diagnostics:
  (No warnings or errors detected. Model is formally sound!)
----------------------------------------------------------------------------
 Verification Status: PASSED (Model Sound)
============================================================================
```

---

## Step 3: Generate the C++ State Machine

Compile the model into a standalone C++20 header with namespace `avionics` and class name `UavMissionFSM`:

```bash
fsmc -i uav_mission.sysml -o uav_mission_fsm.hpp --std 20 --namespace avionics --name UavMissionFSM
```

---

## Step 4: Write the Application Code

Create `main.cpp`:

```cpp
#include "uav_mission_fsm.hpp"
#include <iostream>

int main() {
    // 1. Initialize context struct
    avionics::UavMissionFSMContext ctx;
    ctx.batteryLevel = 98.5;
    ctx.isGpsLocked = true;

    // 2. Instantiate zero-allocation synchronous state machine
    avionics::UavMissionFSM fsm(ctx);

    std::cout << "Current State: " << fsm.current_state_name() << "\n";
    // Output: SensorCalib

    // 3. Dispatch calibration completion event
    fsm.dispatch(avionics::CalibrationOk{});
    std::cout << "Current State: " << fsm.current_state_name() << "\n";
    // Output: SystemReady

    // 4. Dispatch takeoff command (guard evaluated against ctx.isGpsLocked)
    auto result = fsm.dispatch(avionics::TakeoffCmd{});
    if (result.is_success()) {
        std::cout << "Takeoff successful. Current State: " << fsm.current_state_name() << "\n";
        // Output: WaypointNav
    }

    // 5. Simulate battery drop in context and trigger emergency transition
    ctx.batteryLevel = 14.2; // Critical level (< 20.0)
    auto safe_res = fsm.dispatch(avionics::LowBatteryEvent{});
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
g++ -std=c++20 main.cpp -o uav_app
./uav_app
```

Output:
```text
Current State: SensorCalib
Current State: SystemReady
Takeoff successful. Current State: WaypointNav
Emergency fail-safe activated. Transitioned to: ReturnToHome
```
