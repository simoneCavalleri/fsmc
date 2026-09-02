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
    [STANDARD <17|20>]
    [NAMESPACE <NamespaceName>]
    [OUTPUT_DIR <OutputDirectory>]
    [STANDALONE]
    [MODULAR]
    [NO_THREAD_SAFE]
    [NO_STUBS]
    [FORMAT <FormatName>]
    [TARGET_LANG <Language>]
)
```

### Argument Reference
| Parameter | Type | Description |
| :--- | :--- | :--- |
| `<TargetName>` | Target | The CMake target (executable or library) receiving the generated header. |
| `DIAGRAMS` | Files | List of input statechart files (`.sysml`, `.puml`, `.mmd`, `.xmi`, `.scxml`, `.json`, `.dot`). |
| `NAME` | String | Name of the generated C++ class (e.g. `UavMissionFSM`). |
| `STANDARD` | Integer | C++ standard version (`17` or `20`). Default: `17`. |
| `NAMESPACE` | String | C++ namespace to enclose generated types. Default: `fsm_generated`. |
| `OUTPUT_DIR` | Path | Output directory for generated headers (default: `${CMAKE_CURRENT_BINARY_DIR}/generated_<target>`). |
| `STANDALONE` | Flag | Generate self-contained header with embedded zero-alloc runtime. |
| `MODULAR` | Flag | Generate header that includes `<fsm/backend/cpp/runtime/fsm.hpp>`. |
| `NO_THREAD_SAFE` | Flag | Disable generation of the `thread_safe_fsm` asynchronous wrapper. |
| `NO_STUBS` | Flag | Disable generation of default stubs for custom actions and guards. |
| `FORMAT` | String | Override input model format parser (`sysml2`, `plantuml`, `mermaid`, etc.). |
| `TARGET_LANG` | String | Target code generator language (`cpp`). |

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

# Generate header from SysML v2 model
fsmc_target_sources(avionics_app
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/models/flight_control.sysml
    NAME FlightControlFSM
    STANDARD 20
    NAMESPACE avionics
)
```
