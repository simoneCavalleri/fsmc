# Getting Started with `fsmc`

This section describes how to install `fsmc`, run verification passes, transpile statecharts between modeling formats, and integrate generated code into your build system.

---

## Navigation & Contents

| Guide | Description | Target Audience |
| :--- | :--- | :--- |
| **[Installation & Setup](installation.md)** | System requirements, CMake `FetchContent`, Conan 2.0, vcpkg, and building from source. | All Users |
| **[Quickstart Tutorial](quickstart.md)** | End-to-end walkthrough from authoring a statechart to formal verification and execution. | New Users |
| **[CLI Reference (`fsmc` & `fsm-opt`)](cli_usage.md)** | Comprehensive command-line flag manual for compilation, verification, and standalone export. | Developers / CI |
| **[Build System Integration](integration_guide.md)** | Automated CMake target integration using `fsmc_target_sources()` and package managers. | Build Engineers |

---

## Compiler Workflows

Depending on your engineering workflow, `fsmc` supports three primary operational modes:

### 1. Model-Based Engineering (MBSE & Transpilation)
Author statecharts in high-level engineering formalisms (OMG SysML v2, Cameo / MagicDraw XMI, W3C SCXML). Use `fsmc` to transpile models losslessly between formats, generate visual diagrams, or produce requirement traceability matrices (RTM) for certification audits.

### 2. Formal Verification & Static Analysis
Run compile-time safety and liveness analysis without executing code. `fsmc` translates statecharts into formal transition systems, invoking SMT solvers (Z3) and symbolic model checkers (nuXmv) to prove temporal invariants (LTL/CTL).

### 3. Target Code Generation
Compile verified statecharts into standalone, zero-heap C++17 or C++20 header files. The generated code adheres to the MBSE 4-domain memory architecture (`InPorts`, `OutPorts`, `Registers`, `Services`) with zero dynamic memory allocation and deterministic execution bounds.
