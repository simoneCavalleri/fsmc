# `fsmc` Showcase Examples Catalog

This directory contains production-grade showcase applications demonstrating the end-to-end capabilities of **`fsmc` (Finite State Machine Compiler)** across diverse engineering domains: aerospace flight control, high-performance networking, industrial motor control, and satellite mission executives.

Each example is compiled and verified automatically as part of the repository test suite (`ctest`).

---

## Showcase Directory

| Directory | Domain | Architecture Highlights | Source Formats | Test Suite Target |
| :--- | :--- | :--- | :--- | :--- |
| **[`autonomous_uav_mission/`](autonomous_uav_mission/README.md)** | Aerospace & Avionics | Hierarchical HFSM, Shallow History (`[H]`), Deferred Events (`@fsm:deferred`), Strongly-typed signals with payload parameters, LTL formal safety properties. | SysML v2, PlantUML, Mermaid, DOT, JSON, SCXML, Cameo XMI, nuXmv SMV | `autonomous_uav_mission_example` |
| **[`connection_manager/`](connection_manager/README.md)** | Networking & Protocols | MBSE 4-domain memory partitioning (`InPorts`, `Registers`), De Morgan composite boolean guards (`and_`, `or_`, `not_`), synchronous & asynchronous dispatch. | SysML v2, PlantUML, Mermaid, DOT, JSON, SCXML, Cameo XMI, nuXmv SMV | `connection_manager_example` |
| **[`async_motor_controller/`](async_motor_controller/README.md)** | Industrial Robotics | Thread-safe Active Object (`thread_safe_fsm`), asynchronous futures (`post_async`), timed ramp-up transitions (`post_delayed`), hardware safety interlocks. | Mermaid (`.mmd`) | `async_motor_controller_example` |
| **[`mission_controller/`](mission_controller/README.md)** | Satellite Systems | Complex multi-stage mission executive, submachine coordination, sensor threshold guards, automatic failover modes. | SysML v2, PlantUML, Mermaid, DOT, JSON, SCXML, Cameo XMI, nuXmv SMV | `mission_controller_example` |

---

## Building and Running the Examples

All examples are built automatically when configuring CMake with tests enabled:

```bash
# Configure and build all example executables
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run all examples via CTest
ctest --test-dir build -R "example" --output-on-failure

# Or run individual binaries directly:
./build/bin/autonomous_uav_mission_example
./build/bin/connection_manager_example
./build/bin/async_motor_controller_example
./build/bin/mission_controller_example
```
