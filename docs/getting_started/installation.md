# Installation & Integration

`fsmc` can be integrated into your project in three different ways:

---

## 1. CMake FetchContent (Recommended)

Include `fsmc` directly in your `CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(
    fsmc
    GIT_REPOSITORY https://github.com/simoneCavalleri/fsmc.git
    GIT_TAG        v0.3.0
)
FetchContent_MakeAvailable(fsmc)

# Link the header-only runtime library
target_link_libraries(my_app PRIVATE fsmc_runtime)
```

To automatically compile statechart diagrams into C++ headers at build time, use the provided CMake helper:

```cmake
fsmc_target_sources(my_app
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/models/uav_mission.puml
    NAME UavMissionFSM
    STANDARD 20
    NAMESPACE avionics
)
```

---

## 2. Conan 2.0 Package Manager

Add `fsmc/0.3.0` to your `conanfile.txt` or `conanfile.py`:

=== "conanfile.txt"
    ```ini
    [requires]
    fsmc/0.3.0

    [generators]
    CMakeDeps
    CMakeToolchain
    ```

=== "conanfile.py"
    ```python
    from conan import ConanFile

    class MyProject(ConanFile):
        requires = "fsmc/0.3.0"
        generators = "CMakeDeps", "CMakeToolchain"
    ```

---

## 3. Standalone Single-Header Runtime (Zero Dependencies)

If you only need the runtime engine without the CLI compiler, generate or copy the standalone header:

```bash
# Export the single-header runtime via CLI
fsmc --export-runtime ./my_include --std 20
```

This produces a single self-contained header (`fsm.hpp`) with **zero third-party dependencies**, perfect for bare-metal ARM Cortex-M, RISC-V, or ESP32 microcontrollers.
