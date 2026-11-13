# Step 5: Code Generation & Build Integration

!!! note "Curriculum Continuity"
    In **[Step 4](04_formal_verification.md)**, we verified safety invariants and generated Requirement Traceability matrices. In this step, we return to the `ConnectionManager` model from Step 2 to compile it into production C++ code, integrate it with CMake, and choose the right concurrency runtime.

In this guide, you will learn how to turn verified state machine models into production software:

- How `fsmc` uses the **Generation Gap Pattern** to ensure safe, non-destructive builds.
- Generating standalone and modular C++ code (C++17 and C++20).
- Integrating automated compilation into **CMake** (`fsmc_target_sources`).
- Deploying with partitioned memory domains (`InPorts`, `OutPorts`, `Registers`, `Services`).

---

## 1. The Generation Gap Pattern: Safe, Non-Destructive Builds

A common pitfall with code generation tools is that modifying a model might overwrite manually written business logic.

`fsmc` prevents this by enforcing a strict **separation between Generated Artifacts and User Code**:

```
my_project/
├── models/
│   └── connection.sysml        <-- Source model (Source of Truth)
├── src/
│   ├── main.cpp                <-- User Application Code (Untouched)
│   └── connection_services.hpp <-- User Service Implementations (Untouched)
└── build/generated/
    └── connection_fsm.hpp      <-- Generated Artifact (Regenerated automatically)
```

1. **The generated file (`connection_fsm.hpp`) is never edited manually**. It lives in your `build/` directory.
2. **Your custom side-effects live in your services implementation** (`connection_services.hpp`).
3. Whenever you update `connection.sysml`, `fsmc` regenerates the header in milliseconds. Your application code simply links against the updated transition table with **zero lost work**.

---

## 2. Generating Code from the Command Line

Use `fsmc` to emit either a standalone single-file header or a modular header paired with an exported runtime library:

=== "Standalone Header (Single-FSM)"
    ```bash
    # Self-contained C++20 header with embedded zero-allocation runtime
    fsmc -i connection.sysml -o connection_fsm.hpp --target cpp --std 20 --standalone --namespace conn --name ConnectionManager
    ```

=== "Modular Headers (Multi-FSM Projects)"
    ```bash
    # 1. Export runtime headers once into your project include path
    fsmc --export-runtime ./include/ --std 20

    # 2. Generate a lightweight FSM header referencing the shared runtime
    fsmc -i connection.sysml -o connection_fsm.hpp --target cpp --std 20 --modular --namespace conn --name ConnectionManager

    # Or specify a custom runtime header include path
    fsmc -i connection.sysml -o connection_fsm.hpp --modular="<fsm/fsm.hpp>" --namespace conn --name ConnectionManager
    ```

!!! info "Target Backends"
    Production code generation targets **C++17** and **C++20** (`--target cpp`). For planned Rust (`no_std`) and ISO C99 backends, see the **[Multi-Target Architecture & Roadmap](../runtime_api/multi_target_roadmap.md)**.

---

## 3. Understanding the Generated Header

The generated `connection_fsm.hpp` exposes three **policy-based type aliases**—one for each concurrency model:

```cpp
// Synchronous FSM (single-threaded, stack-allocated)
using ConnectionManager = ::fsm::make_fsm<
    ConnectionManagerTable,
    ::fsm::with_initial_state<Disconnected>,
    ::fsm::with_ports<ConnectionManagerInPorts, ConnectionManagerOutPorts>,
    ::fsm::with_registers<ConnectionManagerRegisters>,
    ::fsm::with_services<ConnectionManagerServices>,
    ::fsm::with_observer<::fsm::dynamic_observer>
>;

// Thread-safe Active Object FSM (mutex-protected dispatch + event queue)
using ThreadSafeConnectionManager = ::fsm::make_thread_safe_fsm<
    ConnectionManagerTable,
    ::fsm::with_initial_state<Disconnected>,
    ::fsm::with_ports<ConnectionManagerInPorts, ConnectionManagerOutPorts>,
    ::fsm::with_registers<ConnectionManagerRegisters>,
    ::fsm::with_services<ConnectionManagerServices>
>;

// Lock-free SPSC FSM (ISR/producer -> single consumer thread)
using SpscConnectionManager = ::fsm::make_spsc_fsm<
    ConnectionManagerTable,
    ::fsm::with_initial_state<Disconnected>,
    ::fsm::with_ports<ConnectionManagerInPorts, ConnectionManagerOutPorts>,
    ::fsm::with_registers<ConnectionManagerRegisters>,
    ::fsm::with_services<ConnectionManagerServices>,
    ::fsm::with_queue_capacity<64>
>;
```

Each `with_*` policy binds independently. When the model references external action names, `fsmc` generates a virtual `ConnectionManagerServices` interface; otherwise, `ConnectionManagerServices` aliases `::fsm::no_services`.

---

## 4. Using the Generated State Machine

In your `main.cpp`, instantiate `ConnectionManager` with your register state and drive it via `dispatch()` or periodic `step()` calls:

```cpp
#include <cassert>
#include <iostream>
#include "connection_fsm.hpp"

int main() {
    using namespace conn;

    // Bind persistent registers and instantiate stack-allocated FSM
    ConnectionManagerRegisters reg{};
    ConnectionManager fsm(reg);

    ConnectionManagerInPorts in{};
    in.latency_ms = 45.0f;
    in.is_authenticated = true;
    ConnectionManagerOutPorts out{};

    // Validate port range contracts generated from SysML v2 constraints
    assert(in.validate_contracts());
    std::cout << "Initial state: " << fsm.current_state_name() << "\n";

    // 1. Dispatch typed events
    fsm.dispatch(ConnectCmd{}, in, out);
    std::cout << "State after ConnectCmd: " << fsm.current_state_name() << "\n";

    fsm.dispatch(HandshakeOk{}, in, out);
    std::cout << "State after HandshakeOk: " << fsm.current_state_name() << "\n";

    // 2. Sampled continuous step (periodic control-loop tick)
    fsm.step(in, out);

    return 0;
}
```

For interrupt-driven producers or multi-threaded systems, instantiate `SpscConnectionManager` or `ThreadSafeConnectionManager` instead of `ConnectionManager` using the same transition table and domain contracts. See **[Execution Topologies Overview](../runtime_api/index.md)** for full API details.

---

## 5. Seamless CMake Integration (`fsmc_target_sources`)

To automate state machine compilation in your C++ build pipeline, use the CMake helper function provided by `fsmc`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_embedded_app CXX)

find_package(fsmc REQUIRED)

add_executable(my_app
    src/main.cpp
)

target_link_libraries(my_app PRIVATE fsmc::runtime)

# Automatically compile models/connection.sysml into ${CMAKE_CURRENT_BINARY_DIR}/generated_my_app/
fsmc_target_sources(my_app
    DIAGRAMS models/connection.sysml
    NAME ConnectionManager
    STANDARD 20
    STANDALONE
    NAMESPACE conn
)
```

See the **[CMake Integration Reference](../reference/cmake_integration.md)** for full argument documentation (`OUTPUT_DIR`, `RUNTIME_HEADER`, `VERIFY`, `STRICT`, and more).

---

## Next Steps

Now that you can generate and integrate state machine builds cleanly, proceed to **[Step 6: Complete Real-World Case Study — Autonomous UAV Flight Controller](06_real_world_case_study.md)**.


