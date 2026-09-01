# Autonomous UAV Mission & Flight Executive Showcase

This showcase demonstrates a safety-critical aerospace flight executive modeled and compiled with **`fsmc`**.

It illustrates how complex hierarchical behavior, emergency fail-safe sequences, and memory preservation are expressed in formal statecharts and compiled into zero-allocation C++ code.

---

## What This Example Demonstrates

1. **Hierarchical Statecharts (HFSM)**: Nested substates within the `Navigating` composite state (`Ascending`, `WaypointNav`, `SearchPattern`, `TrackingTarget`).
2. **Shallow History Pseudostate (`[H]`)**: When the UAV enters `HoverPause` (e.g. temporary loiter), triggering `ResumeMissionCmd` restores the exact substate that was active before the pause (e.g. `SearchPattern`), bypassing default initial states.
3. **Deferred Event Queuing (`@fsm:deferred`)**: Non-critical events like `ResumeMissionCmd` or telemetry pings received during active transitions are stored in a bounded inline queue and recalled when the target state becomes ready.
4. **Strongly-Typed Signals with Payload Parameters**:
   - `EvWaypointCmd`: Latitude, Longitude, Altitude target coordinates.
   - `EvTelemetry`: Battery millivolts, altitude centimeters, GPS lock status.
5. **Formal LTL Temporal Logic Properties**: Proves safety invariants (e.g. $\mathbf{G}(\text{LowBattery} \implies \mathbf{F}(\text{SafeLanding}))$) across all 8 format representations.
6. **Multi-Format Parity**: Contains the exact same state machine authored across all 8 supported frontend formats:
   - `uav_mission.sysml` (OMG SysML v2)
   - `uav_mission_cameo.xmi` (Cameo / MagicDraw XMI)
   - `uav_mission.scxml` (W3C SCXML)
   - `uav_mission.puml` (PlantUML)
   - `uav_mission.mmd` (Mermaid)
   - `uav_mission.dot` (Graphviz DOT)
   - `uav_mission.json` (XState JSON)
   - `uav_mission.smv` (nuXmv / SMV)

---

## Execution Phases in `main.cpp`

```
[Phase 1] Preflight Initialization & Sensor Calibration (SensorCalib -> SystemReady)
    │
[Phase 2] Takeoff & Climb to Target Altitude (TakeoffCmd -> Ascending -> WaypointNav)
    │
[Phase 3] Autonomous Navigation & Search Pattern Execution (AreaReached -> SearchPattern)
    │
[Phase 4] Deferred Event Handling During Active Search (EvTelemetryPing handled)
    │
[Phase 5] History Restoration [H] After Loiter (PauseCmd -> HoverPause -> ResumeMissionCmd -> SearchPattern)
    │
[Phase 6] Emergency Low-Battery Fail-Safe & Auto-Landing (LowBattery -> ReturnToHome -> Landed -> FinalShutdown)
```

---

## Running the Example

```bash
# Run via CMake build binary
./build/bin/autonomous_uav_mission_example
```
