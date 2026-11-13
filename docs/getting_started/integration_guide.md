# Integration & Build System Guide

This guide details how to integrate **`fsmc`** and its generated C++ state machines into CMake builds, package managers, standalone firmware toolchains, and CI/CD quality gates. For planned Rust and C ecosystems, see the [Multi-Target Roadmap](../runtime_api/multi_target_roadmap.md).

---

## 1. Modern CMake Integration

`fsmc` provides target-based CMake packages exporting granular interface libraries and code generation macros.

### Available Targets

| CMake Target | Alias | Description |
| :--- | :--- | :--- |
| `fsmc_runtime` | `fsmc::runtime`, `fsmc::fsmc_runtime` | Embedded zero-allocation header-only runtime (`fsm`, `spsc_fsm`, `thread_safe_fsm`, `static_ring_buffer`). |
| `fsmc_ir` | `fsmc::ir` | Strongly-typed AST, semantic graph model, and JSON serializer. |
| `fsmc_middleend`| `fsmc::middleend` | `PassManager`, hierarchy canonicalizer, safety verifiers. |
| `fsmc_frontend` | `fsmc::frontend` | 9 Ingestion parsers (SysML v2, Stateflow, Cameo XMI, SCXML, nuXmv SMV, XState JSON, PlantUML, Mermaid, DOT). |
| `fsmc_backend`  | `fsmc::backend` | C++17/20 generators and graphical/formal emitters. |
| `fsmc_compiler` | `fsmc::compiler` | Full compiler pipeline aggregating all modular libraries. |

### Basic CMake Application Setup

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyApplication LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Locate installed fsmc package
find_package(fsmc CONFIG REQUIRED)

# 2. Define application target and link runtime
add_executable(my_app src/main.cpp)
target_link_libraries(my_app PRIVATE fsmc::runtime)

# 3. Automatically transpile models during build
fsmc_target_sources(my_app
    DIAGRAMS
        models/spacecraft.sysml
        models/mission.puml
    NAME MissionFSM
    STANDARD 20
    STANDALONE
    NAMESPACE avionics
    NO_STUBS
)
```

---

## 2. FetchContent Integration (Zero-Install C++ Setup)

To consume `fsmc` directly from Git without system-wide pre-installation:

```cmake
include(FetchContent)
FetchContent_Declare(
    fsmc
    GIT_REPOSITORY https://github.com/simoneCavalleri/fsmc.git
    GIT_TAG main
)

# Disable test and benchmark targets for downstream consumers
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

## 3. `fsmc_target_sources` Macro Overview

| Parameter | Default / Notes |
| :--- | :--- |
| `DIAGRAMS` (**Required**) | One or more `.sysml`, `.slx`, `.sfx`, `.xmi`, `.scxml`, `.smv`, `.json`, `.puml`, `.mmd`, or `.dot` files. |
| `OUTPUT_DIR` | Defaults to `${CMAKE_CURRENT_BINARY_DIR}/generated_${TARGET_NAME}`. |
| `STANDALONE` / `MODULAR` | Self-contained header vs. external runtime include (`RUNTIME_HEADER` defaults to `"fsm.hpp"`). |

For the full signature and every parameter (`NAME`, `STANDARD`, `NAMESPACE`, `NO_THREAD_SAFE`, `NO_STUBS`, `FORMAT`, `TARGET_LANG`), see the **[CMake Integration Reference](../reference/cmake_integration.md)**.

---

## 4. C/C++ Package Managers

=== "Conan 2.0"
    After exporting `fsmc` to your local Conan cache (`conan create . --version 0.10.0`), reference it in `conanfile.txt`:
    ```ini
    [requires]
    fsmc/0.10.0

    [generators]
    CMakeDeps
    CMakeToolchain
    ```

=== "vcpkg"
    Declare `fsmc` in your project's `vcpkg.json`:
    ```json
    {
      "name": "my-project",
      "version-string": "1.0.0",
      "dependencies": [
        "fsmc"
      ]
    }
    ```

---

## 5. Standalone Drop-in & Custom Makefiles (Bare-Metal / Zero Deps)

When building firmware with GNU Make or custom embedded toolchains where integrating CMake is not desirable, invoke `fsmc --standalone` in a pre-build rule:

```makefile
MODEL   = models/uav_mission.sysml
GEN_DIR = src/generated
GEN_HDR = $(GEN_DIR)/uav_mission_fsm.hpp

$(GEN_HDR): $(MODEL)
	@mkdir -p $(GEN_DIR)
	fsmc -i $< -o $(GEN_HDR) --target cpp --std 20 --standalone
```

Drop the generated single-header artifact directly into your firmware sources—it requires no external libraries, dynamic linkers, or third-party dependencies.

---

## 6. CI/CD Quality Gate: Formal Verification in CI

Prevent broken models or unreachable states from reaching your main branch by integrating `fsmc --verify` into CI:

=== "GitHub Actions (`.github/workflows/verify.yml`)"
    ```yaml
    name: Model Formal Verification Quality Gate

    on: [push, pull_request]

    jobs:
      verify-models:
        runs-on: ubuntu-latest
        steps:
          - uses: actions/checkout@v4

          - name: Verify All Statechart Models
            run: |
              fsmc -i models/spacecraft.sysml --verify
              fsmc -i models/protocol.scxml --verify --ltl "G (Request -> F Response)"
    ```

=== "GitLab CI (`.gitlab-ci.yml`)"
    ```yaml
    stages:
      - verify
      - build

    verify-models:
      stage: verify
      image: ubuntu:22.04
      script:
        - for m in models/*.sysml; do fsmc -i "$m" --verify; done
      rules:
        - if: $CI_PIPELINE_SOURCE == "merge_request_event"
    ```
