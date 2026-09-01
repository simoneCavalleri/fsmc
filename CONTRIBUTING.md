# Developer & Contribution Guidelines for `fsmc`

Welcome to `fsmc`! This document details how to build, test, navigate the architecture, extend the compiler, and submit contributions.

---

## 1. Build & Test Quickstart

### Prerequisites
- C++20 compiler (GCC 10+, Clang 11+, or MSVC 2019+)
- CMake 3.14+
- `clang-format` (for automated code formatting)

### Building the Project
```bash
# Configure with testing and examples enabled
cmake -B build -S . -DFSMC_ENABLE_TESTING=ON -DFSMC_ENABLE_EXAMPLES=ON

# Build everything
cmake --build build -j$(nproc)

# Run the full test suite (53 test suites)
ctest --test-dir build --output-on-failure
```

### Formatting Code
```bash
find tools include tests examples benchmarks -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i
```

---

## 2. Architecture at a Glance

The compiler pipeline follows a clean, single-pass layered architecture:

```
[ Frontend ]   ──>  [ FsmIr AST ]  ──>  [ Middle-End Passes ]  ──>  [ Backend Emitters ]  ──>  [ Zero-Heap Runtime ]
(Parsers)           (Core Metamodel)     (Intervals, SMT, Opt)       (C++17/C++20, Diagram)      (fsm, spsc_fsm)
```

### Subsystems Breakdown

| Subsystem | Location | Description |
| :--- | :--- | :--- |
| **Frontend** | `include/fsm/frontend/` | Ingests modeling formats into `FsmIr` (`formal/` for SysML/SCXML/XMI; `diagram/` for PlantUML/Mermaid/DOT). |
| **IR Metamodel** | `include/fsm/ir/` | `FsmIr` root struct containing states, transitions, typed ports, datapath variables, and LTL properties. |
| **Middle-End** | `include/fsm/middleend/` | Analysis and optimization passes (`PassManager`, `EFSMIntervalAnalyzer`, SMT/Z3, nuXmv model checking). |
| **Backend** | `include/fsm/backend/` | C++17/C++20 code generation (`cpp/`) and diagram/formal serializers (`formal/`, `diagram/`). |
| **Runtime** | `include/fsm/runtime/` | Header-only runtime library: `fsm` (synchronous), `spsc_fsm` (lock-free ISR), `thread_safe_fsm` (MPSC). |
| **Tools** | `tools/` | `fsmc` (main compiler CLI) and `fsm-opt` (middle-end optimizer CLI). |

---

## 3. Extending `fsmc`

### Adding a New Parser
1. Inherit from `IParser` in `include/fsm/frontend/formal/` or `include/fsm/frontend/diagram/`.
2. Implement `parse(source, out_model, out_error)` and populate `out_model` (`FsmIr`).
3. Call `out_model.normalize_hierarchy();` before returning.
4. Register the parser in [`include/fsm/frontend/common/parser_factory.hpp`](include/fsm/frontend/common/parser_factory.hpp).
5. Add unit tests in `tests/frontend/`.

### Adding a Middle-End Pass
1. Inherit from `IPass` in `include/fsm/middleend/passes/`.
2. Implement `run(model, diag)` returning `PassResult::Modified` or `PassResult::Preserved`.
3. Register the pass in [`include/fsm/middleend/pass_manager.hpp`](include/fsm/middleend/pass_manager.hpp).
4. Add unit tests in `tests/middleend/`.

### Adding a Backend Emitter
1. Inherit from `IEmitter` in `include/fsm/backend/`.
2. Implement `emit(model, options)` returning the formatted string.
3. Register the format in [`include/fsm/backend/emitter_factory.hpp`](include/fsm/backend/emitter_factory.hpp).
4. Add roundtrip tests in `tests/backend/`.

---

## 4. Code Conventions & Guidelines

1. **Self-Documenting Code:** Use clear, descriptive names for classes, functions, and template arguments (`InPorts`, `Registers`, `transition_index`).
2. **Doxygen & Comments:** Keep public APIs documented with brief Doxygen headers. Add inline comments explaining "why" for complex template metaprogramming or domain arithmetic.
3. **Runtime Memory Philosophy (True Zero-Heap):**
   - The runtime engine (`fsm::fsm`, `spsc_fsm`, state variants, tables) operates with **0 heap allocations** (`malloc`/`new`).
   - Advanced features like UML 2.5 History and Deferred Events use inline bounded structures (`fsm::static_vector` and `event_variant`) stored directly on the stack within the `fsm` instance.
4. **Testing:** Write clean GoogleTest test cases following the Arrange-Act-Assert (AAA) pattern.
5. **Zero Warnings:** All code should build cleanly without compiler warnings (`-Wall -Wextra -Wpedantic -Wconversion`).

---

## 5. Developer Certificate of Origin (DCO) & Intellectual Property Cleanliness

To ensure that all contributions are legally clean and compatible with the project's MIT License, `fsmc` adopts the **Developer Certificate of Origin (DCO) Version 1.1** (standardized by the Linux Foundation).

By submitting a pull request, patch, or commit to this repository, you certify that:

```text
Developer Certificate of Origin
Version 1.1

Copyright (C) 2004, 2006 The Linux Foundation and its contributors.

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.
```

### Signing Off Commits
To signify compliance with the DCO, add a `Signed-off-by` line to each commit message:

```bash
git commit -s -m "frontend: add support for custom state attribute"
```
