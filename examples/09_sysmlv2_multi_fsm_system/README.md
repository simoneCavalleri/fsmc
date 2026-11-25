# Showcase 09: Multi-FSM OMG SysML v2.0 Subsystem (`import`, `port def` / `~`, `--state-def`, Static Outbox)

This showcase demonstrates compiling a **multi-machine OMG SysML v2.0 specification** spanning multiple files into a deterministic, zero-heap **Pure-C99** system:

- `shared_protocol.sysml`: Shared `item def`s and `port def ThermalLinkPort`.
- `thermal_subsystem.sysml`: Imports `SharedProtocol::*`, binds conjugated ports (`~ThermalLinkPort` vs `ThermalLinkPort`) via `part def` / `exhibit state`, and defines two cooperating `state def`s (`SensorMonitorFsm` and `CoolingActuatorFsm`).
- `main.c`: Pure ISO C99 scheduler loop (`LINKER_LANGUAGE C`, zero C++ runtime) routing `SensorMonitorFsm`'s static zero-heap `outbox` directly into `CoolingActuatorFsm`.
