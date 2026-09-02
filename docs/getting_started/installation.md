# Installation and Setup

This document describes how to obtain, build, install, and integrate `fsmc` into modern C++ projects and build systems.

---

## 1. System Requirements

- **C++ Compiler**: A modern C++ compiler with full C++20 support for the compiler driver (`fsmc` / `fsm-opt`) and at least C++17 or C++20 for the generated state machine runtime.
  - GCC 11 or newer
  - Clang 13 or newer
  - MSVC 19.29 (Visual Studio 2019 version 16.11) or newer
- **Build System**: CMake 3.16 or newer.
- **Operating Systems**: Linux (Ubuntu, Debian, Fedora, Arch), macOS (Apple Silicon and Intel x86_64), Windows 10/11.

---

## 2. Installation & Integration Methods

### Method 1: Native Build & System Installation (`cmake --install`)

To build and install the `fsmc` CLI compiler, `fsm-opt` optimizer, runtime headers, CMake package modules, and shell completions directly onto your system:

```bash
# 1. Clone repository
git clone https://github.com/simoneCavalleri/fsmc.git
cd fsmc

# 2. Configure Release build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# 3. Build compiler binaries and toolchain
cmake --build build -j$(nproc)

# 4. (Optional) Run verification test suite
ctest --test-dir build --output-on-failure

# 5. Install system-wide (requires sudo)
sudo cmake --install build

# Or install locally without root to ~/.local:
cmake --install build --prefix ~/.local
```

#### What gets installed:
- **CLI Binaries**: `fsmc` and `fsm-opt` into `bin/` (e.g. `/usr/local/bin/fsmc`).
- **C++ Headers & Runtimes**: `include/fsm/` into `include/` (e.g. `/usr/local/include/fsm/`).
- **CMake Package Config**: `fsmcConfig.cmake` and `FsmcTools.cmake` into `lib/cmake/fsmc/`.
- **Shell Completions**: Automated Bash completion in `share/bash-completion/` and Zsh completion in `share/zsh/site-functions/`.

After installation, downstream CMake projects can consume `fsmc` directly:

```cmake
find_package(fsmc CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE fsmc::fsmc_runtime)
```

---

### Method 2: CMake `FetchContent` (Project Integration)

Using CMake's `FetchContent` module is the simplest way to include `fsmc` directly into your project repository without requiring pre-installation on the host machine:

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

### Method 3: Package Managers (vcpkg & Conan 2.0)

=== "vcpkg (Manifest Mode & Git Registry)"
    The repository root includes a [`vcpkg.json`](file:///home/simone/dev/github/fsmc/vcpkg.json) manifest. To consume `fsmc` via vcpkg in your downstream project, declare it as a Git registry in your project's `vcpkg-configuration.json`:

    ```json
    {
      "registries": [
        {
          "kind": "git",
          "repository": "https://github.com/simoneCavalleri/fsmc.git",
          "baseline": "2147e60",
          "packages": [ "fsmc" ]
        }
      ]
    }
    ```

    Or pass `--overlay-ports=/path/to/fsmc`, then declare the dependency in your project's `vcpkg.json`:

    ```json
    {
      "name": "my-flight-app",
      "version-string": "1.0.0",
      "dependencies": [
        "fsmc"
      ]
    }
    ```

=== "Conan 2.0 (Local Package Recipe)"
    `fsmc` includes a root `conanfile.py` recipe to build and export packages to your local Conan 2.0 cache:

    ```bash
    # 1. Clone repository and create local Conan package
    git clone https://github.com/simoneCavalleri/fsmc.git
    cd fsmc
    conan create . --version 0.5.0 -s build_type=Release
    ```

    Once created in your local cache, consume it in your project:

    ```ini
    # conanfile.txt
    [requires]
    fsmc/0.5.0

    [generators]
    CMakeDeps
    CMakeToolchain
    ```

---

### Method 4: Standalone Single-Header Export (Bare-Metal & Zero Deps)

If your target is a bare-metal microcontroller (ARM Cortex-M, RISC-V, ESP32) with no external build system dependencies, you can copy the standalone zero-dependency runtime header directly from the repository:

- C++20 Standalone Runtime: [`include/fsm/backend/cpp/cpp20_standalone_runtime.hpp`](file:///home/simone/dev/github/fsmc/include/fsm/backend/cpp/cpp20_standalone_runtime.hpp)
- C++17 Standalone Runtime: [`include/fsm/backend/cpp/cpp17_standalone_runtime.hpp`](file:///home/simone/dev/github/fsmc/include/fsm/backend/cpp/cpp17_standalone_runtime.hpp)

These single-header files require zero external dependencies beyond standard C++ type traits and atomic headers.
