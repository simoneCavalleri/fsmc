# fsmc: Universal State Machine Compiler for Critical Systems

[![CI Test Matrix](https://github.com/simoneCavalleri/fsmc/actions/workflows/cmake-ci.yml/badge.svg)](https://github.com/simoneCavalleri/fsmc/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![C++17/C++20](https://img.shields.io/badge/Language-C%2B%2B17%20%7C%20C%2B%2B20-orange.svg)](https://en.cppreference.com/)
[![Zero-Heap Allocation](https://img.shields.io/badge/Runtime-Zero--Heap%20Alloc-green.svg)](#)

**`fsmc`** is a production-grade, deterministic compiler, formal verification engine, and zero-overhead C++17/C++20 runtime infrastructure for finite state machines (FSM) and hierarchical statecharts (HFSM).

Built specifically for **mission-critical, hard real-time, aerospace, automotive (ISO 26262), and embedded (DO-178C)** applications.

---

## ⚡ Core Value Proposition

=== "Aerospace & Systems Engineering"
    - **Native OMG SysML v2, Cameo XMI, W3C SCXML Ingestion**: Model state machines in standard MBSE tools.
    - **LTL/CTL Model Checking**: Formally verify temporal safety invariants and response liveness before deployment.
    - **Requirement Traceability (RTM)**: Automatic compliance matrix generation in Markdown and JSON.

=== "Embedded & Automotive (AUTOSAR/RTOS)"
    - **Lock-Free `fsm::spsc_fsm`**: Wait-Free $O(1)$ single-producer ring buffer safe for Interrupt Service Routines (ISRs).
    - **Zero Dynamic Memory Allocation**: No `malloc`, no `new`, zero heap fragmentation.
    - **Compile-Time Folding**: Sub-nanosecond transitions via flat template metaprogramming.

=== "Application & Game Development"
    - **Thread-Safe Asynchronous Wrapper (`fsm::thread_safe_fsm`)**: Thread-safe MPSC event queue with background worker execution.
    - **Visual Transpilation**: Bidirectional transpilation between Mermaid, PlantUML, Graphviz DOT, and XState JSON.
    - **Comprehensive Introspection**: Live transition trace metadata via `dispatch_result`.

---

## 🚀 Quick Navigation

<div class="grid cards" markdown>

-   :material-rocket-launch: **[Getting Started](getting_started/installation.md)**
    ---
    Install `fsmc` via CMake FetchContent, Conan, or pre-built binaries.

-   :material-book-open-page-variant: **[Core Concepts](concepts/states_and_hierarchy.md)**
    ---
    Learn about HFSM hierarchy, history recovery, and data path semantics.

-   :material-shield-check: **[Safety & Formal Verification](verification_and_safety/model_checking.md)**
    ---
    Model check LTL/CTL formulas, run EFSM interval analysis, and generate RTMs.

-   :material-play-circle: **[Interactive Playground](playground/index.md)**
    ---
    Try statecharts, verify models, and generate C++ in your browser via WebAssembly.

</div>
