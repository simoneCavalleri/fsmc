# CMake Integration Reference

`fsmc` provides CMake functions and imported targets that make integrating formal state machine compilation into CMake builds straightforward and maintainable.

---

## 1. Importing `fsmc` into CMake

When `fsmc` is imported via `FetchContent` or `find_package(fsmc CONFIG REQUIRED)`, it defines:

- **`fsmc_runtime`**: An `INTERFACE` library containing runtime C++ header search paths and compile features (`cxx_std_17` or `cxx_std_20`).
- **`fsmc::fsmc`**: The host executable target for the compiler driver.
- **`fsmc::fsm-opt`**: The host executable target for the formal IR optimizer.

---

## 2. The `fsmc_target_sources` Macro

The `fsmc_target_sources` macro hooks into your target's build graph, creating custom commands that transpile input models into C++ headers before your source files are compiled.

### Signature
```cmake
fsmc_target_sources(
    <TargetName>
    DIAGRAMS <diagram1> [<diagram2>...]
    [NAME <GeneratedFSMClassName>]
    [NAMESPACE <NamespaceName>]
    [OUTPUT_DIR <OutputDirectory>]
    [RUNTIME_HEADER <HeaderPath>]
    [STANDALONE]
    [MODULAR]
    [EMIT_C_ABI]
    [EMIT_MANIFEST]
    [TARGET <cpp|c99>]
    [NO_THREAD_SAFE]
    [FREESTANDING]
    [NO_STUBS]
    [ALLOW_DIAGRAM_CODEGEN]
    [FORMAT <FormatName>]
    [TARGET_LANG <Language>]
)
```

### Argument Reference
| Parameter | Type | Description |
| :--- | :--- | :--- |
| `<TargetName>` | Target | The CMake target (executable or library) receiving the generated headers/sources. |
| `DIAGRAMS` | Files | List of input statechart files (`.ir.json`, `.sysml`, `.slx`, `.sfx`, `.xmi`, `.scxml`, `.smv`, `.json`, `.puml`, `.mmd`, `.dot`). |
| `NAME` | String | Name of the generated C++ class (e.g. `UavMissionFSM`). |
| `NAMESPACE` | String | C++ namespace to enclose generated types. Default: `fsm_generated`. |
| `OUTPUT_DIR` | Path | Output directory for generated artifacts (default: `${CMAKE_CURRENT_BINARY_DIR}/generated_${TARGET_NAME}`). |
| `STANDALONE` | Flag | Emit a single self-contained `<stem>_fsm.hpp` instead of the default split `<stem>_ports.hpp` + `<stem>_fsm.hpp` pair. |
| `MODULAR` | Flag | Generate header that includes external runtime header (default: `"fsm.hpp"`, matching `--export-runtime`). |
| `EMIT_C_ABI` | Flag | Also emit pure C99 zero-allocation ABI wrapper (`<stem>_c.h` + `<stem>_c.cpp`) and add `<stem>_c.cpp` to `<TargetName>`. |
| `EMIT_MANIFEST` | Flag | Also emit machine-readable `<stem>.manifest.json` (`schemas/fsm_manifest.schema.json`) alongside generated artifacts. |
| `TARGET` / `TARGET_LANG` | String | Target backend: `cpp` (default C++17) or `c99` (native Pure-C99 `<stem>.h` + `<stem>.c`, zero C++ runtime). |
| `RUNTIME_HEADER` | String | Custom runtime header path included in modular mode (e.g. `"fsm.hpp"`, `"<fsm/fsm.hpp>"`). |
| `NO_THREAD_SAFE` | Flag | Disable generation of the `thread_safe_fsm` asynchronous wrapper. |
| `FREESTANDING` | Flag | Emit zero-STL freestanding C++17 header (`--freestanding`, `-fno-exceptions -fno-rtti` compatible). |
| `NO_STUBS` | Flag | Disable generation of default stubs for custom actions and guards. |
| `ALLOW_DIAGRAM_CODEGEN` | Flag | Explicitly opt in to C++ codegen from informal diagram formats (`.puml`, `.mmd`, `.dot`). |
| `FORMAT` | String | Override input model format parser (`ir-json`, `sysml2`, `plantuml`, `mermaid`, etc.). |
| `TARGET_LANG` | String | Target code generator language (`cpp`). |

!!! tip "Cross-Compilation (`FSMC_HOST_EXECUTABLE`)"
    When cross-compiling for embedded targets (ARM Cortex-M, RISC-V, Emscripten), pass `-DFSMC_HOST_EXECUTABLE=/path/to/host/fsmc` so `fsmc_target_sources` runs the host compiler binary instead of the cross-compiled target executable.

---

## 3. Complete Integration Example

```cmake
cmake_minimum_required(VERSION 3.16)
project(AvionicsApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    fsmc
    GIT_REPOSITORY https://github.com/simoneCavalleri/fsmc.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(fsmc)

add_executable(avionics_app src/main.cpp src/sensors.cpp)
target_link_libraries(avionics_app PRIVATE fsmc_runtime)

# Generate split C++17 headers (flight_control_ports.hpp + flight_control_fsm.hpp) from SysML v2
fsmc_target_sources(avionics_app
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/models/flight_control.sysml
    NAME FlightControlFSM
    NAMESPACE avionics
)
```
