# Satellite Mission Controller & Flight Software Showcase

This showcase demonstrates a complex **Aerospace Satellite Mission Executive** modeled and compiled with **`fsmc`**.

It demonstrates multi-phase mission progression, sensor threshold validation, submachine coordination, and emergency telemetry fail-safe modes.

---

## What This Example Demonstrates

1. **Multi-Phase Aerospace Mission Execution**:
   - Models the sequential lifecycle of an orbital satellite: `Prelaunch` $\to$ `Ascent` $\to$ `Deployment` $\to$ `OrbitalPayloadOps` $\to$ `Decommission`.
2. **Submachine Coordination & Sub-Statecharts**:
   - Demonstrates modular statechart reuse where payload operations and communication passes are partitioned into independent reusable submachines.
3. **Sensor Threshold Guards & Telemetry Validation**:
   - Validates solar array deployment angles, battery depth-of-discharge (DoD), and onboard computer (OBC) watchdog pings before allowing phase progression.
4. **Emergency Safe-Hold Mode**:
   - Autonomous transition into sun-pointing safe-hold configuration upon loss of attitude control or low power bus voltage.
5. **Universal Multi-Format Parity**:
   - Exact same mission state machine provided across all 8 supported frontend formats (`.sysml`, `.puml`, `.mmd`, `.dot`, `.json`, `.scxml`, `.xmi`, `.smv`).

---

## Running the Example

```bash
# Run via CMake build binary
./build/bin/mission_controller_example
```
