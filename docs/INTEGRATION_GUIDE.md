# Integration & Build System Guide

This guide details how to integrate **`fsmc`** into modern C++ projects using **CMake**, **vcpkg**, **Conan**, or direct single-header standalone inclusion.

---

## 1. CMake Integration (`fsmc_target_sources`)

The recommended way to integrate `fsmc` into your build workflow is via the `fsmc_target_sources` CMake function.

### Basic Setup

```cmake
cmake_minimum_required(VERSION 3.14)
project(MyApplication LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Include fsmc (via find_package or add_subdirectory)
find_package(fsmc REQUIRED) # or include(cmake/FsmGenTools.cmake)

# 2. Define your application target
add_executable(my_app src/main.cpp)

# 3. Automatically compile state machine diagrams
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

### `fsmc_target_sources` Parameter Reference

| Parameter | Type | Description |
| :--- | :--- | :--- |
| `DIAGRAMS` | `list` (**Required**) | One or more paths to `.xmi`, `.scxml`, `.json`, `.dot`, `.sysml`, `.puml`, or `.mmd` model files. |
| `NAME` | `string` | FSM class name (default: inferred from diagram file stem). |
| `STANDARD` | `17` or `20` | Target C++ standard (default: `17`). |
| `NAMESPACE` | `string` | C++ namespace wrapping states, events, and FSM aliases (default: `fsm_generated`). |
| `CONTEXT` | `string` | Custom Context struct/class name (default: `no_context`). |
| `OUTPUT_DIR` | `path` | Output directory for generated headers (default: `${CMAKE_CURRENT_BINARY_DIR}/generated_fsm`). |
| `STANDALONE` | `flag` | Embeds the zero-overhead engine into the generated header (zero external dependencies). |
| `MODULAR` | `flag` | Generates a header that includes external `fsm/fsm.hpp`. |
| `NO_THREAD_SAFE` | `flag` | Disables generation of the `thread_safe_fsm` wrapper alias. |
| `NO_STUBS` | `flag` | Emits forward declarations for custom user-defined guard and action structs. |

---

## 2. Package Managers

### vcpkg Integration

Add `fsmc` as a dependency in your `vcpkg.json`:

```json
{
  "name": "my-project",
  "version-string": "1.0.0",
  "dependencies": [
    "fsmc"
  ]
}
```

In your `CMakeLists.txt`:
```cmake
find_package(fsmc REQUIRED)
```

### Conan 2.0 Integration

Add `fsmc/1.0.0` in your `conanfile.txt` or `conanfile.py`:

```ini
[requires]
fsmc/1.0.0

[generators]
CMakeDeps
CMakeToolchain
```

---

## 3. Standalone Single-Header Usage (Zero Dependencies)

If you do not use CMake or prefer manual code generation:

1. Build or download the `fsmc` binary:
   ```bash
   ./build/bin/fsmc -i model.sysml -o model_fsm.hpp --std 20 --standalone
   ```
2. Copy `model_fsm.hpp` directly into your project's include path.
3. `#include "model_fsm.hpp"` in your C++ code.

**No libraries to link, no external headers needed.**

---

## 4. Exporting the Runtime Library (`--export-runtime`)

If you want to use the core template library without code generation (writing transition tables by hand with Fluent DSL):

```bash
fsmc --export-runtime ./my_project/include/fsm --std 20
```

This exports:
- `include/fsm/fsm.hpp`
- `include/fsm/transition.hpp`
- `include/fsm/transition_table.hpp`
- `include/fsm/thread_safe_fsm.hpp`
- `include/fsm/type_traits.hpp`
