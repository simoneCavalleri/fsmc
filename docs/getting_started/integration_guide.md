# Integration & Build System Guide

This guide details how to integrate **`fsmc`** into modern C++ projects using **Modern CMake (Target-Based)**, **FetchContent**, **CPM.cmake**, **vcpkg**, **Conan**, or direct single-header standalone inclusion.

---

## 1. Modern CMake Integration (`fsmc_target_sources` & `fsmc::fsmc_runtime`)

`fsmc` provides target-based CMake packages exporting granular interface libraries:

| CMake Target | Alias | Description |
| :--- | :--- | :--- |
| `fsmc_runtime` | `fsmc::runtime`, `fsmc::fsmc_runtime` | Embedded zero-allocation header-only runtime (`fsm`, `spsc_ring_buffer`, `static_ring_buffer`). |
| `fsmc_ir` | `fsmc::ir` | Strongly-typed AST, semantic graph model, and JSON serializer. |
| `fsmc_middleend`| `fsmc::middleend` | `PassManager`, hierarchy canonicalizer, safety verifiers. |
| `fsmc_frontend` | `fsmc::frontend` | 7 Ingestion parsers (SysML v2, XMI, SCXML, JSON, DOT, PUML, MMD). |
| `fsmc_backend`  | `fsmc::backend` | C++17/20 generators and graphical emitters. |
| `fsmc_compiler` | `fsmc::compiler` | Full compiler pipeline aggregating all modular libraries. |

### Basic Setup

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyApplication LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Find fsmc package
find_package(fsmc CONFIG REQUIRED)

# 2. Define application target and link runtime
add_executable(my_app src/main.cpp)
target_link_libraries(my_app PRIVATE fsmc::runtime)

# 3. Automatically compile state machine diagrams
# Note: fsmc_target_sources automatically handles both formal specifications
# (SysML v2, SCXML, Cameo XMI, nuXmv SMV) and visual diagrams (PlantUML, Mermaid, DOT, JSON).
fsmc_target_sources(my_app
    DIAGRAMS
        models/mission.puml
        models/protocol.mmd
        models/spacecraft.sysml
    NAME MissionFSM
    STANDARD 20
    STANDALONE
    NAMESPACE space
    CONTEXT MissionContext
    NO_STUBS
)
```

---

## 2. FetchContent Integration (Zero-Install Setup)

To use `fsmc` without installing it system-wide:

```cmake
include(FetchContent)
FetchContent_Declare(
    fsmc
    GIT_REPOSITORY https://github.com/simoneCavalleri/fsmc.git
    GIT_TAG main
)
# Disable test/example builds for downstream consumers
set(FSMC_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(FSMC_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FSMC_ENABLE_BENCHMARKS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(fsmc)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE fsmc::fsmc_runtime)

fsmc_target_sources(my_app
    DIAGRAMS models/connection.mmd
    STANDARD 20
    STANDALONE
)
```

---

## 3. CMake Configuration Options

When compiling or embedding `fsmc` in your workspace:

| Option | Default | Description |
| :--- | :--- | :--- |
| `FSMC_ENABLE_TESTING` | `ON` | Builds the 48 GoogleTest test suites. |
| `FSMC_ENABLE_EXAMPLES` | `ON` | Builds showcase example targets. |
| `FSMC_ENABLE_BENCHMARKS` | `ON` | Builds dispatch micro-benchmarks. |
| `FSMC_ENABLE_SANITIZERS` | `OFF` | Enables Address & Undefined Sanitizers in Debug builds. |
| `FSMC_BUILD_WASM` | `OFF` | Builds WebAssembly playground targets (Emscripten). |

---

## 4. `fsmc_target_sources` Parameter Reference

| Parameter | Type | Description |
| :--- | :--- | :--- |
| `DIAGRAMS` | `list` (**Required**) | Paths to `.sysml`, `.puml`, `.mmd`, `.xmi`, `.scxml`, `.json`, or `.dot` files (automatically passes `--allow-diagram-codegen` for visual formats). |
| `NAME` | `string` | FSM class name (default: inferred from diagram file stem). |
| `STANDARD` | `17` or `20` | Target C++ standard (default: `17`). |
| `NAMESPACE` | `string` | C++ namespace wrapping states, events, and FSM aliases (default: `fsm_generated`). |
| `CONTEXT` | `string` | Custom Context struct/class name (default: `no_context`). |
| `OUTPUT_DIR` | `path` | Output directory for generated headers (default: `${CMAKE_CURRENT_BINARY_DIR}/generated_fsm`). |
| `STANDALONE` | `flag` | Embeds the zero-overhead engine into the generated header (zero external dependencies). |
| `MODULAR` | `flag` | Generates a header that includes external `<fsm/runtime/cpp/fsm.hpp>`. |
| `NO_THREAD_SAFE` | `flag` | Disables generation of the `thread_safe_fsm` wrapper alias. |
| `NO_STUBS` | `flag` | Emits forward declarations for custom user-defined guard and action structs. |

---

## 5. Package Managers

### vcpkg Integration
Add `fsmc` in your `vcpkg.json`:
```json
{
  "name": "my-project",
  "version-string": "0.2.0",
  "dependencies": [
    "fsmc"
  ]
}
```

### Conan 2.0 Integration
Add `fsmc/0.2.0` in your `conanfile.txt`:
```ini
[requires]
fsmc/0.2.0

[generators]
CMakeDeps
CMakeToolchain
```

---

## 6. Standalone Single-Header Usage (Zero Dependencies)

Generate headers manually via CLI:
```bash
./build/bin/fsmc -i model.sysml -o model_fsm.hpp --std 20 --standalone
```
Drop `model_fsm.hpp` directly into your C++ project without any external runtime dependencies.
