# `fsmc`

<div align="center">

[![CI](https://github.com/simoneCavalleri/fsmc/actions/workflows/ci.yml/badge.svg)](https://github.com/simoneCavalleri/fsmc/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/simoneCavalleri/fsmc?color=blue)](https://github.com/simoneCavalleri/fsmc/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-17%20%7C%2020-blue.svg)](https://isocpp.org/)
[![OMG Standards](https://img.shields.io/badge/OMG-UML%202.5%20%7C%20SysML%20v2-orange.svg)](https://www.omg.org/spec/UML/2.5/)
[![Zero Overhead](https://img.shields.io/badge/Performance-Zero%20Overhead-success.svg)](docs/ARCHITECTURE.md)

**A high-performance OMG UML 2.5 & SysML v2 State Machine Compiler for C++17 and C++20.**  
*Convert Mermaid (`stateDiagram-v2`), PlantUML (`@startuml`), and SysML v2 (`.sysml`) diagrams into zero-overhead C++ code.*

[Quickstart](#-60-second-quickstart) • [Features](#-features) • [UML / SysML Guide](docs/UML_REFERENCE.md) • [CMake Integration](docs/INTEGRATION_GUIDE.md) • [API Docs](docs/RUNTIME_API.md)

</div>

---

## 🚀 60-Second Quickstart

### 1. Define Your Model (`connection.mmd` or `mission.sysml`)

```mermaid
stateDiagram-v2
    [*] --> Disconnected
    Disconnected --> Connecting : ConnectCmd
    Connecting --> Connected : ConnectionSuccessEvent / StartHeartbeat
    Connecting --> Disconnected : ConnectionFailedEvent / LogFailure
    Connected : Ping / ResetWatchdog
    Connected --> Disconnected : DisconnectCmd / CleanupSession
```

*Or in native **OMG SysML v2** (`.sysml`):*
```sysml
state def ConnectionFSM {
    entry; then Disconnected;
    state Disconnected;
    state Connecting;
    state Connected;

    transition from Disconnected accept ConnectCmd then Connecting;
    transition from Connecting accept ConnectionSuccessEvent do StartHeartbeat then Connected;
    transition from Connecting accept ConnectionFailedEvent do LogFailure then Disconnected;
    transition from Connected accept Ping do ResetWatchdog;
    transition from Connected accept DisconnectCmd do CleanupSession then Disconnected;
}
```

### 2. Add to CMake (`CMakeLists.txt`)

```cmake
find_package(fsmc REQUIRED)

add_executable(my_app main.cpp)

# Automatically compile diagram into connection_fsm.hpp
fsmc_target_sources(my_app
    DIAGRAMS
        models/connection.mmd
        models/mission.sysml
    NAME ConnectionFSM
    STANDARD 20
    STANDALONE
)
```

### 3. Dispatch Events in C++ (`main.cpp`)

```cpp
#include "connection_fsm.hpp"
#include <iostream>

int main() {
    ConnectionFSM fsm;

    std::cout << "State: " << fsm.current_state_name() << "\n"; // Disconnected

    fsm.dispatch(ConnectCmd{});
    std::cout << "State: " << fsm.current_state_name() << "\n"; // Connecting

    fsm.dispatch(ConnectionSuccessEvent{});
    std::cout << "State: " << fsm.current_state_name() << "\n"; // Connected

    // Zero-overhead internal transition:
    fsm.dispatch(Ping{});

    return 0;
}
```

---

## ✨ Features

- **Full OMG UML 2.5 & SysML v2 State Machine Compliance**:
  - **Hierarchical / Composite States (HFSM)**: Nested sub-state blocks with parent-child discovery.
  - **Internal Transitions**: `State : Event [Guard] / Action` — zero `on_exit`/`on_enter` overhead.
  - **Choice & Junction Pseudostates**: Dynamic `<<choice>>` / `<<junction>>` runtime branching.
  - **History States**: Shallow (`[H]`) and Deep (`[H*]`) state resumption upon recovery.
  - **Deferred Events**: `State : defer Event` declarations for buffering unhandled signals.
  - **State Lifecycle Hooks**: `on_enter` and `on_exit` member function reflection.
- **Multi-Format Parsing**:
  - **OMG SysML v2** (`.sysml`): Native textual specification format.
  - **Mermaid** (`stateDiagram-v2`): Standard in GitHub, GitLab, and Markdown documentation.
  - **PlantUML** (`@startuml`): Standard in enterprise model-driven engineering.
- **Pure Decoupled Compiler Architecture**:
  - The CLI executable (`fsmc`) is built with C++20 and generates code for target applications targeting **C++17** or **C++20**.
- **Standalone Self-Contained Output (`--standalone`, default)**:
  - Generates a single `.hpp` header containing both the state machine definition and the embedded engine. **Zero external dependencies, zero libraries to link.**
- **First-Class CMake Integration (`fsmc_target_sources`)**:
  - Automatically compiles diagram files during build and binds generated headers to your targets.
- **Thread-Safe Asynchronous Worker**:
  - Optional `thread_safe_fsm` wrapper for lockless background queue dispatching.

---

## 📚 Documentation Index

- 📖 **[OMG UML 2.5 & SysML v2 Reference Guide](docs/UML_REFERENCE.md)**: Syntax reference for SysML v2, PlantUML, and Mermaid.
- 🛠️ **[Integration & Build Guide](docs/INTEGRATION_GUIDE.md)**: CMake, vcpkg, Conan, and standalone usage.
- ⚡ **[Runtime C++ API Reference](docs/RUNTIME_API.md)**: Classes, methods, lifecycle hooks, and asynchronous workers.
- 🏛️ **[Compiler Architecture](docs/ARCHITECTURE.md)**: Pipeline design, choice branch flattening, and template metaprogramming.

---

## 🔨 Building & Testing

### Prerequisites
- CMake 3.14+
- Modern C++20 compiler (GCC 10+, Clang 11+, MSVC 2019+)

```bash
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## 💻 CLI Usage

```bash
fsmc -i <diagram_file> [OPTIONS]
```

| Option | Description | Default |
| :--- | :--- | :--- |
| `-i, --input <file>` | Input diagram file (`.sysml`, `.mmd`, `.mermaid`, `.puml`, `.plantuml`) | **Required** |
| `-o, --output <file>` | Output C++ header file | `stdout` |
| `-n, --name <name>` | FSM class name | Inferred from filename |
| `--namespace <ns>` | C++ namespace | `fsm_generated` |
| `--context <type>` | Custom Context struct/class name | `no_context` |
| `--std <17\|20>` | Target C++ standard (`17` or `20`) | `17` |
| `--c++17` / `--c++20` | Shortcut for `--std 17` or `--std 20` | |
| `--standalone` | Generate a self-contained header with embedded runtime | `true` |
| `--modular` | Generate FSM header only, including external `fsm/fsm.hpp` | |
| `--format <fmt>` | Explicit format: `sysml2`, `mermaid`, `plantuml`, `auto` | `auto` |
| `--no-stubs` | Emits forward declarations for custom user guard/action structs | |
| `-h, --help` | Display help menu and exit | |

---

## 📄 License
MIT License. See [LICENSE](LICENSE) for details.
