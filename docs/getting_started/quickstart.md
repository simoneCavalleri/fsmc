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

## Step 3: Generate the State Machine

=== "C++ Target (Production v0.5.0)"
    Compile the model into a standalone C++20 header with namespace `avionics` and class name `UavMissionFSM`:
    ```bash
    fsmc -i uav_mission.sysml -o uav_mission_fsm.hpp --target cpp --std 20 --standalone --namespace avionics --name UavMissionFSM
    ```

=== "Rust Target (Roadmap Preview)"
    > [!NOTE]
    > **Upcoming Target Preview**: Rust code generation is currently in development under the multi-target roadmap. In `v0.5.0`, the C++ target is the active production runtime.

    Compile the model into an idiomatic `#![no_std]` Rust module:
    ```bash
    fsmc -i uav_mission.sysml -o uav_mission_fsm.rs --target rust --namespace avionics
    ```

=== "C Target (Embedded C Roadmap)"
    > [!NOTE]
    > **Upcoming Target Preview**: ISO C99 embedded C code generation is currently in development under the multi-target roadmap. In `v0.5.0`, the C++ target is the active production runtime.

    Compile the model into deterministic, zero-heap C headers and sources:
    ```bash
    fsmc -i uav_mission.sysml -o uav_mission_fsm.h --target c --prefix avionics_
    ```

---

## Step 4: Write the Application Code

=== "C++ Target (Production v0.5.0)"
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

        // 5. Simulate battery drop in InPorts and evaluate continuous sampled step
        in.batteryLevel = 14.2; // Critical level (< 20.0)
        auto safe_res = fsm.step(in, out);
        if (safe_res.has_transitioned()) {
            std::cout << "Emergency fail-safe activated. Transitioned to: " 
                      << fsm.current_state_name() << "\n";
            // Output: ReturnToHome
        }

        return 0;
    }
    ```

=== "Rust Target (Roadmap Preview)"
    > [!NOTE]
    > **Upcoming Target Preview**: Preview of planned `#![no_std]` Rust application integration.

    Create `main.rs`:
    ```rust
    use avionics::uav_mission_fsm::*;

    fn main() {
        let mut fsm = UavMissionFsm::new(UavRegisters::default());
        let mut in_ports = UavInPorts { battery_level: 98.5, is_gps_locked: true };
        let mut out_ports = UavOutPorts::default();

        println!("Current State: {:?}", fsm.state());

        // 1. Dispatch calibration completion event
        fsm.dispatch(&Event::CalibrationOk, &in_ports, &mut out_ports);
        println!("Current State: {:?}", fsm.state());

        // 2. Dispatch takeoff command
        let res = fsm.dispatch(&Event::TakeoffCmd, &in_ports, &mut out_ports);
        if res.is_transitioned() {
            println!("Takeoff successful. Current State: {:?}", fsm.state());
        }

        // 3. Continuous sampled step (battery drop)
        in_ports.battery_level = 14.2;
        let safe_res = fsm.step(&in_ports, &mut out_ports);
        if safe_res.is_transitioned() {
            println!("Emergency fail-safe activated. Transitioned to: {:?}", fsm.state());
        }
    }
    ```

=== "C Target (Embedded C Roadmap)"
    > [!NOTE]
    > **Upcoming Target Preview**: Preview of planned ISO C99 embedded C application integration.

    Create `main.c`:
    ```c
    #include "uav_mission_fsm.h"
    #include <stdio.h>
    #include <assert.h>

    int main(void) {
        avionics_uav_fsm_t fsm;
        avionics_registers_t reg = {0};
        avionics_in_ports_t in = {.battery_level = 98.5f, .is_gps_locked = true};
        avionics_out_ports_t out = {0};

        avionics_uav_fsm_init(&fsm, &reg);
        printf("Current State: %d\n", fsm.current_state);

        /* 1. Dispatch calibration completion */
        avionics_uav_fsm_dispatch(&fsm, AVIONICS_EV_CALIB_OK, &in, &out);
        printf("Current State: %d\n", fsm.current_state);

        /* 2. Dispatch takeoff command */
        fsm_result_t res = avionics_uav_fsm_dispatch(&fsm, AVIONICS_EV_TAKEOFF, &in, &out);
        if (res == FSM_TRANSITIONED) {
            printf("Takeoff successful. Current State: %d\n", fsm.current_state);
        }

        /* 3. Continuous sampled step (battery drop) */
        in.battery_level = 14.2f;
        avionics_uav_fsm_step(&fsm, &in, &out);
        printf("Emergency fail-safe activated. Transitioned to: %d\n", fsm.current_state);

        return 0;
    }
    ```

---

## Step 5: Compile and Run

=== "C++ Target (Production v0.5.0)"
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

=== "Rust Target (Roadmap Preview)"
    > [!NOTE]
    > **Upcoming Target Preview**: Build command for future Rust target releases.

    ```bash
    cargo run --release
    ```

=== "C Target (Embedded C Roadmap)"
    > [!NOTE]
    > **Upcoming Target Preview**: Build command for future C target releases.

    ```bash
    gcc -std=c99 main.c uav_mission_fsm.c -o uav_app -Wall -Wextra -pedantic
    ./uav_app
    ```
