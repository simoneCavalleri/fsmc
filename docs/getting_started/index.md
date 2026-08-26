# Getting Started with `fsmc`

Welcome to the **Getting Started** guide. This section provides everything you need to set up `fsmc`, compile your first state machine model into hard real-time C++, and integrate the toolchain into your build system.

---

## Section Contents

| Topic | Description | Link |
| :--- | :--- | :--- |
| **Installation & Requirements** | Build and install `fsmc` from source, Conan package manager, or CMake FetchContent. | [Installation Guide](installation.md) |
| **Quickstart Tutorial** | Step-by-step walkthrough creating an autonomous UAV flight mission statechart. | [Quickstart Tutorial](quickstart.md) |
| **CLI Usage Reference** | Complete command-line manual for `fsmc` (compiler) and `fsm-opt` (optimizer). | [CLI Reference](cli_usage.md) |
| **Build Systems Integration** | Integrating `fsmc_target_sources` with Modern CMake, FetchContent, and Conan. | [Build Integration](integration_guide.md) |

---

## Recommended Learning Path

1. **Install the Compiler**: Follow the [Installation Guide](installation.md) to install `fsmc` and `fsm-opt` on Linux or macOS.
2. **Build Your First Statechart**: Run through the [Quickstart Tutorial](quickstart.md) to author a SysML v2 / PlantUML model and compile it to C++20.
3. **Explore Modeling Formats**: Review the [Modeling Languages](../formal_languages/index.md) section to choose between SysML v2, Cameo XMI, SCXML, or visual diagrams.
