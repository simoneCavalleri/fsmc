# Developer & Contributor Guide

Welcome to the **`fsmc` Developer & Contributor Guide**. This guide details how to develop, test, and contribute to `fsmc`, including the architectural invariants, codebase organization, and step-by-step recipes for adding new frontend parsers, middle-end analysis passes, backend code emitters, and runtime enhancements.

---

## 1. Architectural Foundations

1. **Zero-Heap, Zero-Exception Embedded Runtime:**
   The C++ runtime library ([`include/fsm/backend/cpp/runtime/`](file:///home/simone/dev/github/fsmc/include/fsm/backend/cpp/runtime/)) and emitted standalone state machines operate with **0 bytes dynamic heap allocation** (`malloc`/`new`), no virtual tables, and no C++ exceptions. Stack storage for advanced features (UML History, deferred event queues) is strictly bounded at compile-time.
2. **MBSE 4-Domain Segregated Datapath:**
   State machines do not rely on monolithic context objects. Transitions interact through 4 strictly segregated memory domains:
    - **`InPorts`**: Read-only input snapshot with formal contract ranges.
    - **`OutPorts`**: Single-assignment actuator command buffer.
    - **`Registers`**: Internal persistent memory with $z^{-1}$ delay semantics.
    - **`Services`**: External hardware drivers, OS abstractions, and logging interfaces.
3. **Lossless Canonical Intermediate Representation (`FsmIr`):**
   Every frontend model (SysML v2, Cameo XMI, SCXML, JSON, PlantUML, Mermaid, DOT, SMV) is parsed into the unified, strongly-typed `FsmIr` AST before verification, optimization, or code emission.
4. **Compile-Time Static Verification:**
   Before target code generation, the middle-end pipeline runs abstract interpretation (interval lattice analysis), SMT invariants verification, and temporal model checking.

---

## 2. Repository Structure

```
fsmc/
├── include/fsm/
│   ├── ir/                  # Unified AST (FsmIr, StateNode, TransitionEdge, PortDefinition)
│   ├── diagnostic/          # DiagnosticEngine with ANSI colors, SourceSpan, and carets
│   ├── frontend/            # Parser interfaces & implementations
│   │   ├── formal/          # High-semantics formal parsers (SysML v2, Cameo XMI, SCXML, SMV)
│   │   ├── diagram/         # Diagram sketch parsers (PlantUML, Mermaid, DOT, JSON)
│   │   └── directive/       # Annotation, guard, and LTL/INVAR parsers
│   ├── middleend/           # PassManager, transformation passes, and model checkers
│   │   ├── passes/          # Optimization & dead-state elimination passes
│   │   └── analysis/        # Interval arithmetic & GuardSatisfiabilityPass
│   └── backend/             # Code generators and serializers
│       ├── cpp/             # C++ target generator & standalone bundles
│       │   └── runtime/     # Canonical C++17/C++20 zero-heap runtime engine
│       ├── diagram/         # Diagram format serializers
│       ├── formal/          # Formal model serializers
│       └── rtm/             # Requirement Traceability Matrix exporter
├── tools/
│   ├── fsmc/                # Primary compiler driver CLI
│   └── fsm-opt/             # Standalone formal IR optimizer & linter CLI
├── tests/                   # Modular GoogleTest suites (54 suites, 100% green)
├── scripts/                 # Maintenance tools (generate_standalone_runtime.py)
└── docs/                    # MkDocs documentation source
```

---

## 3. Developer Recipes

### Recipe A: Implementing a New Frontend Parser

To add support for a new modeling format (e.g. `SimulinkStateflowParser` or `AutosarArxmlParser`):

#### 1. Inherit from `IParser`
Create your parser class under `include/fsm/frontend/formal/` (for formal models) or `include/fsm/frontend/diagram/` (for visual notations):

```cpp
#pragma once

#include <string_view>
#include <string>
#include "fsm/frontend/common/parser_interface.hpp"

namespace fsm::codegen {

class CustomModelParser : public IParser {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "custom_format";
    }

    [[nodiscard]] FrontendKind kind() const noexcept override {
        return FrontendKind::Formal;
    }

    bool parse(std::string_view source_code, FsmIr& out_model, std::string& out_error) override {
        out_model.name = "ParsedStateMachine";

        // Step 1: Parse states and transitions
        // out_model.states.push_back(StateNode{"Idle"});
        // out_model.transitions.push_back(TransitionEdge{"Idle", "Active", "EvStart"});

        // Step 2: Extract port and register contracts if present
        // out_model.ports.push_back(PortDefinition("sensor_val", "float", PortDirection::In));

        // Step 3: Always normalize hierarchy before returning
        out_model.normalize_hierarchy();
        return true;
    }
};

} // namespace fsm::codegen
```

#### 2. Register in `ParserFactory`
Register your new parser in [`include/fsm/frontend/common/parser_factory.hpp`](file:///home/simone/dev/github/fsmc/include/fsm/frontend/common/parser_factory.hpp).

#### 3. Add Unit Tests
Add test cases in `tests/frontend/` using the Arrange-Act-Assert (AAA) pattern to verify state hierarchy, event triggers, and error reporting.

---

### Recipe B: Writing a Middle-End Analysis or Optimization Pass

Middle-end passes operate on the canonical `FsmIr` AST and emit structured warnings or errors via `DiagnosticEngine`.

#### 1. Inherit from `IPass`
Create your pass in `include/fsm/middleend/passes/` or `include/fsm/middleend/analysis/`:

```cpp
#pragma once

#include "fsm/middleend/pass.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"

namespace fsm::middleend {

class RedundantTransitionPass : public IPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "RedundantTransitionPass";
    }

    PassResult run(fsm::codegen::FsmIr& model, fsm::codegen::DiagnosticEngine& diag) override {
        bool modified = false;

        for (const auto& trans : model.transitions) {
            if (trans.source == trans.target && !trans.guard.has_value() && trans.event.empty()) {
                diag.report(fsm::codegen::Diagnostic::warning(
                    "W0401",
                    "Spontaneous unguarded self-transition on state '" + trans.source + "' causes infinite livelocks."
                ));
            }
        }

        return modified ? PassResult::Modified : PassResult::Preserved;
    }
};

} // namespace fsm::middleend
```

#### 2. Register in `PassManager`
Register the pass in [`include/fsm/middleend/pass_manager.hpp`](file:///home/simone/dev/github/fsmc/include/fsm/middleend/pass_manager.hpp) within the default or optimizing pipeline.

---

### Recipe C: Writing a Backend Emitter / Serializer

To emit a new programming language target or diagram syntax:

#### 1. Inherit from `IEmitter`
Create your serializer in `include/fsm/backend/`:

```cpp
#pragma once

#include <sstream>
#include "fsm/backend/emitter_interface.hpp"

namespace fsm::codegen {

class CustomGraphEmitter : public IEmitter {
public:
    [[nodiscard]] std::string_view format_name() const noexcept override {
        return "custom_graph";
    }

    [[nodiscard]] std::string emit(const FsmIr& model, const GeneratorOptions& options) const override {
        std::ostringstream ss;
        ss << "# State Machine: " << model.name << "\n";
        for (const auto& t : model.transitions) {
            ss << t.source << " -> " << t.target << " [" << t.event << "]\n";
        }
        return ss.str();
    }
};

} // namespace fsm::codegen
```

#### 2. Register in `EmitterFactory`
Add the emitter to [`include/fsm/backend/emitter_factory.hpp`](file:///home/simone/dev/github/fsmc/include/fsm/backend/emitter_factory.hpp).

---

### Recipe D: Modifying the Zero-Heap C++ Runtime

When enhancing the C++ runtime engine:

1. **Edit Canonical Headers**: Make changes inside [`include/fsm/backend/cpp/runtime/`](file:///home/simone/dev/github/fsmc/include/fsm/backend/cpp/runtime/). Keep concerns separated in `detail/` (`history_manager.hpp`, `deferred_manager.hpp`, `transition_executor.hpp`, `reentrancy_tracker.hpp`, `notification_dispatcher.hpp`).
2. **Synchronize Standalone Bundles**: Run the standalone generation script to update the single-header distribution files:
   ```bash
   python3 scripts/generate_standalone_runtime.py
   python3 scripts/generate_standalone_runtime.py --check
   ```
3. **Ensure Zero Heap Allocations**: Verify that all containers use fixed-capacity static storage (`fsm::static_vector`, `static_ring_buffer`) and that zero `malloc`/`new` calls or dynamic allocations are introduced.

---

## 4. Testing & Verification Guidelines

### Unit Testing Standards (GoogleTest)

All unit tests follow strict conventions:
- **AAA Pattern**: Explicit `Arrange`, `Act`, and `Assert` phases.
- **Doxygen Documentation**: Every test file and test case includes `@file`, `@brief Test Intent`, and `Scenario:` blocks.
- **Clean Assertions**: No fix annotations (`// FIX`), scratch comments, or unhandled warnings.

```cpp
/**
 * @brief Test Intent: Verify parser extracts guarded transitions with range contracts.
 *
 * Scenario:
 * - Ingest model string with input port contract [0, 100].
 * - Verify FsmIr AST structure and port contract values.
 */
TEST(CustomParserTest, IngestGuardedTransition) {
    // Arrange
    const std::string source = "state Idle -> Active on EvStart if in.temp > 50;";
    fsm::codegen::FsmIr model;
    std::string error;
    CustomParser parser;

    // Act
    bool ok = parser.parse(source, model, error);

    // Assert
    ASSERT_TRUE(ok) << error;
    EXPECT_EQ(model.transitions.size(), 1u);
}
```

### Full Validation Suite

Before opening a pull request, run the complete verification pipeline:

```bash
# 1. Verify standalone runtime synchronization
python3 scripts/generate_standalone_runtime.py --check

# 2. Build and run all CTest suites (must pass 100%)
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

# 3. Setup doc environment (if not already done) and verify documentation build
pip install -r requirements-docs.txt
mkdocs build
```

---

## 5. Coding Standards & Conventions

| Rule | Convention | Example |
| :--- | :--- | :--- |
| **Types & Classes** | `PascalCase` | `Sysml2Parser`, `TransitionEdge`, `HistoryManager` |
| **Methods & Functions** | `snake_case` | `dispatch()`, `step()`, `normalize_hierarchy()` |
| **Member Variables** | `snake_case_` (trailing underscore) | `registers_`, `current_state_` |
| **Public Struct Fields** | `snake_case` | `source`, `target`, `min_value` |
| **Constants & Enums** | `snake_case` / `kCamelCase` | `dispatch_status::success`, `channel_index_event` |
| **Compiler Warnings** | Zero Warnings (`-Wall -Wextra -Wpedantic -Werror`) | Must compile cleanly across GCC, Clang, and MSVC |
