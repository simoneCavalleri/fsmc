# Getting Started with `fsmc`

This section describes how to install `fsmc`, run verification passes, transpile statecharts between modeling formats, and integrate generated code into your build system.

---

## Navigation & Contents

| Guide | Description | Target Audience |
| :--- | :--- | :--- |
| **[Installation & Setup](installation.md)** | System requirements, CMake `FetchContent`, Conan 2.0, vcpkg, and building from source. | All Users |
| **[Quickstart Tutorial](quickstart.md)** | End-to-end walkthrough from authoring a SysML v2 statechart to formal verification and C++ execution. | New Users |
| **[Developer Cheat Sheet](quick_reference.md)** | Single-page C++ runtime and CLI quick reference for daily engineering work. | Developers |
| **[CLI Reference (`fsmc` & `fsm-opt`)](cli_usage.md)** | Comprehensive command-line flag manual for compilation, verification, and export. | Developers / CI |
| **[Build System Integration](integration_guide.md)** | Automated CMake integration (`fsmc_target_sources`), package managers, and CI verification gates. | Build Engineers |

---

## How `fsmc` Fits Into Your Workflow

1. **Author or Import Models**: Write clean textual SysML v2 or ingest existing MathWorks Stateflow, Cameo XMI, W3C SCXML, XState JSON, PlantUML, Mermaid, or Graphviz DOT models.
2. **Verify and Optimize**: Run middle-end static checks (`--verify`) for deadlocks, interval domain soundness, and LTL/CTL temporal properties, or export to nuXmv SMV and Requirement Traceability Matrices (RTM).
3. **Compile and Integrate**: Emit zero-heap C++17 or C++20 headers structured around segregated `InPorts`, `OutPorts`, `Registers`, and `Services` domains, and run via synchronous (`fsm::fsm`), lock-free SPSC (`fsm::spsc_fsm`), or async MPSC (`fsm::thread_safe_fsm`) engines.
