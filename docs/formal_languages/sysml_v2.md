# OMG Systems Modeling Language (SysML) v2

`fsmc` provides native parsing and emission for the **OMG SysML v2** textual state definition grammar, including hardware I/O ports with range contracts, internal attribute memory registers, and action behaviors.

---

## 1. SysML v2 Statechart Grammar Reference

`fsmc` supports the standard declarative SysML v2 statechart constructs:

### A. State Machine Definition & Domains
```sysml
package SpacecraftSubsystems {
    state def FlightController {
        // 1. Read-only Input Ports with formal range constraints
        in port sensor_altitude : Real { assert constraint { self >= 0.0 and self <= 100000.0; } }
        in port gps_lock : Boolean;

        // 2. Write-only Output Ports
        out port thruster_level : Real { assert constraint { self >= 0.0 and self <= 100.0; } }

        // 3. Persistent Internal Memory (Registers / z^-1 state)
        attribute orbit_count : Integer = 0;
        attribute fault_counter : Integer = 0;

        // 4. Initial transition entry point
        entry; then Preflight;
        
        // State definitions follow...
    }
}
```

---

### B. State Lifecycles & Nested Hierarchies (HFSM)
```sysml
state Preflight {
    entry action initializeSensors();
    do action runSelfTest();
    exit action logPreflightComplete();

    transition on EvArmed if in.gps_lock then ActiveFlight;
}

// Composite / Hierarchical State
state ActiveFlight {
    entry; then Ascending;

    state Ascending {
        transition on AltitudeReached if in.sensor_altitude > 10000.0 then Cruising;
    }

    state Cruising {
        transition on OrbitComplete do action { reg.orbit_count = reg.orbit_count + 1; } then Cruising;
    }

    // High-level interrupt transition out of composite state
    transition on EvEStop then EmergencyHold;
}

state EmergencyHold;
```

---

### C. Transition Syntax Varieties
`fsmc` parses both full explicit transition statements and shorthand forms:

| Transition Style | Syntax | Description |
| :--- | :--- | :--- |
| **Full Explicit** | `transition tr1 first Idle accept EvStart if in.gps_lock do action { out.thrust = 50.0; } then Running;` | Complete specification with explicit source, trigger, guard, action, and target. |
| **Shorthand Trigger** | `transition on EvStart to Running;` | Inline trigger inside a state block targeting `Running`. |
| **Timed Dwell** | `transition on after 500 ms then TimeoutFault;` | Discrete-time dwell delay transition. |
| **Guarded Shorthand** | `transition if in.sensor_altitude <= 0.0 then Landed;` | Immediate guard-evaluated transition. |

---

### D. Choice Pseudostates & Branching
```sysml
choice ClearanceCheck;

transition first Preflight accept EvRequestLaunch then ClearanceCheck;

transition check_ok   first ClearanceCheck if in.gps_lock then ActiveFlight;
transition check_fail first ClearanceCheck if not in.gps_lock then Preflight;
```

---

## 2. Formal Directives in SysML v2

You can embed formal verification specifications and requirement traceability annotations directly into SysML v2 comments and declarations:

```sysml
package MissionSafety {
    state def UAVControl {
        // Formal Safety Invariant: Never allow HighThrust while Landed
        @fsm:property InvariantNoThrustOnGround = "G !(Landed && HighThrust)";

        // Formal Liveness Property: When LowBattery occurs, the system must eventually Land
        @fsm:property ResponseLanding = "G (EvLowBattery -> F Landed)";

        state Landed {
            // Requirement Traceability Tag
            @fsm:req "REQ-UAV-001";
            
            // Defer sensor payload commands until flight is active
            @fsm:defer [EvStreamTelemetry, EvStartMission];
        }

        state HighThrust;
    }
}
```

---

## 3. End-to-End Example: SysML v2 to C++20 Header

=== "SysML v2 Source (`spacecraft.sysml`)"
    ```sysml
    package Aerospace {
        state def SpacecraftControl {
            in port battery_soc : Real { assert constraint { self >= 0.0 and self <= 100.0; } }
            out port heater_cmd : Real { assert constraint { self >= 0.0 and self <= 100.0; } }
            attribute cycles : Integer = 0;

            @fsm:property SafeBat = "G (EvCritBat -> F SafeMode)";

            entry; then Standby;

            state Standby {
                transition on EvLaunch if in.battery_soc > 20.0 then OrbitOps;
            }

            state OrbitOps {
                transition on EvCritBat if in.battery_soc <= 20.0 then SafeMode;
            }

            state SafeMode;
        }
    }
    ```

=== "Transpiled Mermaid (`spacecraft.mmd`)"
    ```mermaid
    stateDiagram-v2
        %% @fsm:name SpacecraftControl
        %% @fsm:port name=battery_soc type=Real dir=in min=0.0 max=100.0
        %% @fsm:port name=heater_cmd type=Real dir=out min=0.0 max=100.0
        %% @fsm:property name=SafeBat kind=Liveness formula="G (EvCritBat -> F SafeMode)"
        [*] --> Standby
        Standby --> OrbitOps: EvLaunch [in.battery_soc > 20.0]
        OrbitOps --> SafeMode: EvCritBat [in.battery_soc <= 20.0]
    ```

=== "Generated C++20 Header (`spacecraft_fsm.hpp`)"
    ```cpp
    #pragma once
    #include <fsm/backend/cpp/runtime/fsm.hpp>

    namespace Aerospace {

    struct InPorts {
        double battery_soc{0.0};
    };

    struct OutPorts {
        double heater_cmd{0.0};
    };

    struct Registers {
        int64_t cycles{0};
    };

    struct Standby {};
    struct OrbitOps {};
    struct SafeMode {};

    struct EvLaunch {};
    struct EvCritBat {};

    } // namespace Aerospace
    ```

---

## 4. CLI Invocations

```bash
# Formally verify SysML v2 specification (Interval Analysis + Model Checking)
fsmc -i spacecraft.sysml --verify

# Generate standalone C++20 header
fsmc -i spacecraft.sysml -o spacecraft_fsm.hpp --std 20

# Export canonical SMV logic for external nuXmv proof
fsmc -i spacecraft.sysml --export smv -o formal_model.smv

# Transpile SysML v2 into PlantUML or Mermaid diagram
fsmc -i spacecraft.sysml --export plantuml -o diagram.puml
```
