# Installation and Setup

This document describes how to obtain, build, and integrate `fsmc` into modern C++ projects and build systems.

---

## System Requirements

- **C++ Compiler**: A modern C++ compiler with full C++20 support for the compiler driver (`fsmc` / `fsm-opt`) and at least C++17 or C++20 for the generated state machine runtime.
  - GCC 11 or newer
  - Clang 13 or newer
  - MSVC 19.29 (Visual Studio 2019 version 16.11) or newer
- **Build System**: CMake 3.16 or newer.
- **Operating Systems**: Linux (Ubuntu, Debian, Fedora, Arch), macOS (Apple Silicon and Intel x86_64), Windows 10/11.

---

## Integration Methods

### Method 1: CMake FetchContent (Recommended)

Using CMake's `FetchContent` module is the simplest way to include `fsmc` in automated build workflows. It pulls the repository at configure time and exports both the compiler targets and the runtime interface library.

```cmake
cmake_minimum_required(VERSION 3.16)
project(FlightControlApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    fsmc
    GIT_REPOSITORY https://github.com/simoneCavalleri/fsmc.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(fsmc)

add_executable(flight_control src/main.cpp)

# Link the header-only runtime library
target_link_libraries(flight_control PRIVATE fsmc_runtime)

# Automatically transpile SysML v2 / PlantUML statecharts to C++ at build time
fsmc_target_sources(flight_control
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/models/mission_controller.sysml
    NAME MissionControllerFSM
    STANDARD 20
    NAMESPACE avionics
)
```

---

### Method 2: Conan 2.0 Package Manager

`fsmc` packages are structured for Conan 2.0 environments.

=== "conanfile.txt"
    ```ini
    [requires]
    fsmc/latest

    [generators]
    CMakeDeps
    CMakeToolchain
    ```

=== "conanfile.py"
    ```python
    from conan import ConanFile

    class FlightControlApp(ConanFile):
        name = "flight_control_app"
        version = "1.0.0"
        settings = "os", "compiler", "build_type", "arch"
        requires = "fsmc/latest"
        generators = "CMakeDeps", "CMakeToolchain"
    ```

---

### Method 3: Standalone Single-Header Export

If your target is a bare-metal microcontroller (ARM Cortex-M0+/M3/M4/M7, RISC-V, ESP32) with no external build system dependencies, you can generate a single self-contained runtime header using the `fsmc` binary:

```bash
# Generate standalone C++20 header
fsmc --export-runtime ./include/fsm --std 20

# Or generate standalone C++17 header
fsmc --export-runtime ./include/fsm --std 17
```

This generates `fsm.hpp`, which has zero dependencies outside the C++ standard library (specifically `<type_traits>`, `<variant>`, `<utility>`, `<string_view>`, and `<atomic>`).

---

### Method 4: Building from Source

To build the `fsmc` compiler binary, `fsm-opt` optimizer, examples, and test suite locally:

```bash
# Clone repository
git clone https://github.com/simoneCavalleri/fsmc.git
cd fsmc

# Configure build directory with optimizations
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Compile compiler driver, IR tools, and test suites
cmake --build build -j8

# Run all verification test suites (100% pass rate)
ctest --test-dir build --output-on-failure
```

The resulting binaries will be located in `build/bin/fsmc` and `build/bin/fsm-opt`.
