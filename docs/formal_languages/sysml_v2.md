# OMG Systems Modeling Language (SysML) v2.0

`fsmc` provides native parsing, verification, and lossless emission for the **OMG SysML v2.0** textual state machine notation (`SysML.xtext` / `KerMLExpressions.xtext`), including typed `port def`s with conjugation (`~`), `item def` signal payloads, `calc def` pure functions, `assert constraint` state invariants, and `assign` / `send` action behaviors.

---

## 1. SysML v2.0 Statechart Grammar Reference

### A. Package Imports, Data Definitions & State Machine Parameters

In standard OMG SysML v2.0, package imports declare explicit visibility (`private import` or `public import`), scalar I/O parameters use directional features (`in`, `out`, `inout`), and internal persistent registers use `attribute` with `:=` initialization:

```sysml
package SpacecraftSubsystems {
    private import ScalarValues::*;
    private import SI::*;

    enum def FlightPhase {
        enum Preflight;
        enum Ascent;
        enum Cruise;
        enum Descent;
    }

    item def NavigationVector {
        attribute latitude : Real;
        attribute longitude : Real;
        attribute altitude : Real;
    }

    item def EvArmed;
    item def EvOrbitComplete;
    item def EvEStop;

    state def FlightController {
        // 1. Read-only Input Parameters with formal range constraints
        in sensor_altitude : Real {
            assert constraint { sensor_altitude >= 0.0 and sensor_altitude <= 100000.0 }
        }
        in gps_lock : Boolean;

        // 2. Write-only Output Parameters
        out thruster_level : Real {
            assert constraint { thruster_level >= 0.0 and thruster_level <= 100.0 }
        }
        out active_phase : FlightPhase;

        // 3. Persistent Internal Memory (Registers / z^-1 state)
        attribute orbit_count : Integer := 0;
        attribute fault_counter : Integer := 0;

        // 4. Initial transition entry point
        entry; then Preflight;

        // State definitions follow...
    }
}
```

---

### B. State Lifecycles, Nested Hierarchies (HFSM) & Parallel States

```sysml
state Preflight {
    entry action initializeSensors;
    do action runSelfTest;
    exit action logPreflightComplete;

    transition first Preflight accept EvArmed if gps_lock then ActiveFlight;
}

// Composite / Hierarchical State
state ActiveFlight {
    entry; then Ascending;

    state Ascending {
        transition first Ascending if sensor_altitude > 10000.0 then Cruising;
    }

    state Cruising {
        transition first Cruising
            accept EvOrbitComplete
            do action CountOrbit {
                assign orbit_count := orbit_count + 1;
            }
            then Cruising;
    }

    // High-level preemption out of composite state
    transition first ActiveFlight accept EvEStop then EmergencyHold;
}

// Concurrent Orthogonal Regions ('parallel' follows the state name in SysML v2.0)
state Supervision parallel {
    state ThermalRegion {
        entry; then Nominal;
        state Nominal;
    }
    state IsolationRegion {
        entry; then Healthy;
        state Healthy;
    }
}

state EmergencyHold;
```

!!! warning "MBSE Exchange vs Synchronous C++ Backend (`ECPP006`)"
    State-level `do action <Activity>;` (`do_activity`) is preserved in `FsmIr` for lossless MBSE model exchange (SysML v2.0 $\leftrightarrow$ SCXML $\leftrightarrow$ Cameo XMI $\leftrightarrow$ Stateflow). The zero-heap synchronous C++ reference backend rejects unlowered `do_activity` with diagnostic `ECPP006` because synchronous state machines execute discrete zero-time run-to-completion steps.

---

### C. Transition Syntax Varieties

| Construct | Standard SysML v2.0 Syntax | Description |
| :--- | :--- | :--- |
| **Explicit Transition** | `transition tr1 first Idle accept EvStart if gps_lock and ready do action { assign thrust := 50.0; } then Running;` | Complete transition with source, signal trigger, KerML boolean guard (`and`/`or`/`not`), assignment, and target. |
| **Payload Binding** | `transition first Standby accept alert : EvOverTemp via link do action { assign peak := alert.peak_temp; } then Cooling;` | Binds incoming `item def` signal occurrence `alert` and reads its attributes. |
| **Relative Time (`after`)** | `transition first Active accept after 500 [ms] then TimeoutFault;` | Discrete-time dwell timeout transition (`private import SI::*;`). |
| **Periodic Time (`every`)** | `transition first Active accept every 20 [ms] then SampleStep;` | Periodic tick-driven transition. |
| **Change Trigger (`when`)** | `transition first Monitoring accept when (sensor_temp >= 85) then Alarm;` | Sampled condition change trigger. |
| **Connection Points** | `entry point En1; exit point Ex1;` | Structured entry and exit connection points across composite state boundaries. |

---

### D. Multi-FSM System Architecture: `private import`, Conjugated `port def` (`~`), `calc def` & Outbox

`fsmc` compiles multi-file OMG SysML v2.0 system models featuring shared protocol packages (`private import <Pkg>::*;`), typed `item def` signals, pure `calc def` functions, directional `port def` interfaces, conjugated ports (`~PortDef`), `part def` with `exhibit state`, and standard `item` instantiation + `send ... via ...`:

```sysml
// shared_protocol.sysml
package SharedProtocol {
    private import ScalarValues::*;

    item def EvSample;
    item def EvOverTemp {
        attribute peak_temp : Integer;
    }

    port def ThermalLinkPort {
        in attribute temp_c : Integer;
        in item alert : EvOverTemp;
    }
}
```

```sysml
// thermal_subsystem.sysml
package ThermalSubsystem {
    private import ScalarValues::*;
    private import SharedProtocol::*;

    part def SensorUnit {
        port link : ~ThermalLinkPort; // Conjugated (~): flips in <-> out
        exhibit state sm : SensorMonitorFsm {
            ref :>> link = SensorUnit::link;
        }
    }

    part def CoolerUnit {
        port link : ThermalLinkPort;
        exhibit state sm : CoolingActuatorFsm {
            ref :>> link = CoolerUnit::link;
        }
    }

    state def SensorMonitorFsm {
        in sensor_temp_c : Integer;
        ref port link : ~ThermalLinkPort;
        attribute alerts_sent : Integer := 0;

        entry; then Monitoring;
        state Monitoring;
        state OverTempLatched;

        transition t_overtemp
            first Monitoring
            accept EvSample
            if sensor_temp_c >= 85
            do action RaiseThermalAlert {
                assign link.temp_c := sensor_temp_c;
                assign alerts_sent := alerts_sent + 1;
                item msg : EvOverTemp {
                    attribute :>> peak_temp = sensor_temp_c;
                }
                send msg via link;
            }
            then OverTempLatched;
    }

    state def CoolingActuatorFsm {
        out fan_pwm : Integer;
        ref port link : ThermalLinkPort;
        attribute last_peak_temp : Integer := 0;

        entry; then Standby;
        state Standby;
        state ActiveCooling;

        transition t_engage_fan
            first Standby
            accept alert : EvOverTemp via link
            do action EngageMaxCooling {
                assign last_peak_temp := alert.peak_temp;
                assign fan_pwm := 100;
            }
            then ActiveCooling;
    }
}
```

!!! note "Parameterless & Named-Argument Constructor Sends"
    In addition to local `item` instantiation with `:>>` redefinition, `fsmc` also supports SysML v2.0 `ConstructorExpression` syntax for parameterless signals (`send new EvStart() via link;`) and named-argument constructors (`send new EvOverTemp(peak_temp = sensor_temp_c) via link;`).

---

## 2. State Invariants (`assert constraint`, `DurationOf`) & Metadata (`metadata def`)

Synchronous invariants and state residence-time bounds are expressed directly with OMG SysML v2.0 `assert constraint` and `Time::DurationOf`, requirements with `satisfy requirement`, and domain extensions (multi-state LTL/CTL temporal logic, transition evaluation priority, and static event deferral) with first-class SysML v2.0 `metadata def` / `@MetadataUsage` blocks:

```sysml
package MissionSafety {
    private import ScalarValues::*;
    private import SI::*;
    private import Time::DurationOf;

    metadata def Priority {
        attribute value : Natural;
    }

    metadata def Deferred {
        attribute events : String[*];
    }

    metadata def TemporalProperty {
        attribute kind : String;
        attribute formula : String;
        attribute req : String[0..1];
        attribute description : String[0..1];
    }

    requirement def REQ_UAV_001;
    item def EvStreamTelemetry;
    item def EvLowBattery;

    state def UAVControl {
        out motors_armed : Boolean;

        entry; then Landed;

        state Landed {
            satisfy requirement : REQ_UAV_001;

            // Native OMG SysML v2.0 state constraint (proven by fsmc --verify)
            assert constraint SafeDisarmOnGround {
                not motors_armed
            }

            @Deferred { events = "EvStreamTelemetry"; }
        }

        state FlightActive {
            // Native OMG SysML v2.0 state residence-time bound
            assert constraint { DurationOf(that) <= 500 [ms] }
        }

        transition emergency_land
            first FlightActive
            accept EvLowBattery
            then Landed {
                @Priority { value = 1; }
            }
    }

    @ResponseLanding : TemporalProperty about UAVControl {
        kind = "Safety";
        formula = "G (EvLowBattery -> F Landed)";
        req = "REQ_UAV_001";
    }
}
```

---

## 3. End-to-End Example: SysML v2.0 to C++17 & Pure-C99

=== "SysML v2.0 Source (`spacecraft.sysml`)"
    ```sysml
    package Aerospace {
        private import ScalarValues::*;

        metadata def TemporalProperty {
            attribute kind : String;
            attribute formula : String;
        }

        item def EvLaunch;
        item def EvCritBat;

        state def SpacecraftControl {
            @SafeBat : TemporalProperty {
                kind = "Safety";
                formula = "G (EvCritBat -> F SafeMode)";
            }

            in battery_soc : Real {
                assert constraint { battery_soc >= 0.0 and battery_soc <= 100.0 }
            }
            out heater_cmd : Real {
                assert constraint { heater_cmd >= 0.0 and heater_cmd <= 100.0 }
            }
            attribute cycles : Integer := 0;

            entry; then Standby;

            state Standby;
            state OrbitOps;
            state SafeMode;

            transition t_launch
                first Standby
                accept EvLaunch
                if battery_soc > 20.0
                then OrbitOps;

            transition t_crit
                first OrbitOps
                accept EvCritBat
                if battery_soc <= 20.0
                then SafeMode;
        }
    }
    ```

=== "Transpiled Mermaid (`spacecraft.mmd`)"
    ```mermaid
    stateDiagram-v2
        %% @fsm:name SpacecraftControl
        %% @fsm:port name=battery_soc type=Real dir=in min=0.0 max=100.0
        %% @fsm:port name=heater_cmd type=Real dir=out min=0.0 max=100.0
        %% @fsm:property name=SafeBat kind=Safety formula="G (EvCritBat -> F SafeMode)"
        [*] --> Standby
        Standby --> OrbitOps: EvLaunch [battery_soc > 20.0]
        OrbitOps --> SafeMode: EvCritBat [battery_soc <= 20.0]
    ```

=== "Generated C++17 Ports (`spacecraft_ports.hpp`)"
    ```cpp
    #pragma once
    #include <cstdint>

    namespace Aerospace {

    struct SpacecraftControlInPorts {
        float battery_soc{0.0f};
    };

    struct SpacecraftControlOutPorts {
        float heater_cmd{0.0f};
    };

    struct SpacecraftControlRegisters {
        uint32_t cycles{0};
    };

    } // namespace Aerospace
    ```

---

## 4. CLI Invocations

```bash
# Formally verify SysML v2.0 specification (assert constraint + LTL/CTL + Interval Analysis)
fsmc -i spacecraft.sysml --verify

# Generate split C++17 headers (spacecraft_ports.hpp + spacecraft_fsm.hpp)
fsmc -i spacecraft.sysml --namespace Aerospace -o spacecraft_fsm.hpp

# Generate native bare-metal Pure-C99 (spacecraft.h + spacecraft.c, zero C++ runtime)
fsmc -i spacecraft.sysml --target c99 -o spacecraft.h --emit-manifest

# Discover and compile individual machines from a multi-FSM SysML v2.0 workspace
fsmc -i thermal_subsystem.sysml -I . --list-machines=json
fsmc -i thermal_subsystem.sysml -I . --state-def SensorMonitorFsm --target c99 -o sensor_monitor.h

# Export canonical SMV logic for external nuXmv proof
fsmc -i spacecraft.sysml --export smv -o formal_model.smv
```
