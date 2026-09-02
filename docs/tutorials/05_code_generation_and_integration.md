# Tutorial 5: Code Generation & Build Integration

In this tutorial, you will learn how to turn your verified state machine models into production-ready software:

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

=== "C++ Target (Production v0.5.0)"
    ```bash
    # Generate a self-contained C++20 header with zero external dependencies
    fsmc -i connection.sysml -o connection_fsm.hpp --target cpp --std 20 --standalone

    # Generate a C++17 header
    fsmc -i connection.sysml -o connection_fsm.hpp --target cpp --std 17 --standalone
    ```

=== "Rust Target (Roadmap Preview)"
    > [!NOTE]
    > **Roadmap Preview**: Rust code generation is an upcoming multi-target release feature. In `v0.5.0`, C++ is the active production runtime.

    ```bash
    # Generate idiomatic `#![no_std]` Rust module
    fsmc -i connection.sysml -o connection_fsm.rs --target rust --namespace conn
    ```

=== "C Target (MISRA-C Roadmap)"
    > [!NOTE]
    > **Roadmap Preview**: ISO C99 / MISRA-C code generation is an upcoming multi-target release feature. In `v0.5.0`, C++ is the active production runtime.

    ```bash
    # Generate MISRA-C compliant header and implementation
    fsmc -i connection.sysml -o connection_fsm.h --target c --prefix conn_
    ```

---

## 3. Using the Generated State Machine

=== "C++ Target (Production v0.5.0)"
    In your `main.cpp`:
    ```cpp
    #include <cassert>
    #include <iostream>
    #include "connection_fsm.hpp"

    struct ConcreteConnectionServices : public conn::ConnectionManagerServices {
        void log(const std::string& msg) override {
            std::cout << "[SERVICE LOG] " << msg << "\n";
        }
    };

    int main() {
        using namespace conn;

        ConnectionManagerRegisters reg{0};
        ConcreteConnectionServices srv;
        ConnectionManagerFSM fsm(reg, srv);

        ConnectionManagerInPorts in;
        in.latency_ms = 45.0;
        in.is_authenticated = true;
        ConnectionManagerOutPorts out;

        assert(in.validate_contracts());
        std::cout << "Initial state: " << fsm.current_state_name() << "\n";

        // 1. Dispatch typed events (srv is automatically bound from constructor)
        fsm.dispatch(ConnectCmd{}, in, out);
        std::cout << "State after ConnectCmd: " << fsm.current_state_name() << "\n";

        fsm.dispatch(HandshakeOk{}, in, out);
        std::cout << "State after HandshakeOk: " << fsm.current_state_name() << "\n";

        // 2. Sampled continuous step
        fsm.step(in, out);

        return 0;
    }
    ```

=== "Rust Target (Roadmap Preview)"
    > [!NOTE]
    > **Roadmap Preview**: Preview of upcoming Rust `#![no_std]` application code.

    In your `main.rs`:
    ```rust
    use conn::connection_fsm::*;

    fn main() {
        let mut fsm = ConnectionManagerFsm::new(ConnectionRegisters::default());
        let mut in_ports = ConnectionInPorts { latency_ms: 45.0, is_authenticated: true };
        let mut out_ports = ConnectionOutPorts::default();

        // 1. Dispatch typed events
        fsm.dispatch(&Event::ConnectCmd, &in_ports, &mut out_ports);
        fsm.dispatch(&Event::HandshakeOk, &in_ports, &mut out_ports);

        // 2. Sampled continuous step
        fsm.step(&in_ports, &mut out_ports);
    }
    ```

=== "C Target (MISRA-C Roadmap)"
    > [!NOTE]
    > **Roadmap Preview**: Preview of upcoming ISO C99 application code.

    In your `main.c`:
    ```c
    #include "connection_fsm.h"

    int main(void) {
        conn_fsm_t fsm;
        conn_registers_t reg = {0};
        conn_in_ports_t in = {.latency_ms = 45.0f, .is_authenticated = true};
        conn_out_ports_t out = {0};

        conn_fsm_init(&fsm, &reg);

        /* 1. Dispatch typed events */
        conn_fsm_dispatch(&fsm, CONN_EV_CONNECT, &in, &out);
        conn_fsm_dispatch(&fsm, CONN_EV_HANDSHAKE_OK, &in, &out);

        /* 2. Sampled continuous step */
        conn_fsm_step(&fsm, &in, &out);

        return 0;
    }
    ```

---

## 4. Seamless CMake Integration (`fsmc_target_sources`)

To automate state machine compilation in your C++ build pipeline, use the CMake helper function provided by `fsmc`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_embedded_app CXX)

find_package(fsmc REQUIRED)

add_executable(my_app
    src/main.cpp
)

target_link_libraries(my_app PRIVATE fsmc::runtime)

# Automatically compile models/connection.sysml into ${CMAKE_CURRENT_BINARY_DIR}/generated_fsm/
fsmc_target_sources(my_app
    DIAGRAMS models/connection.sysml
    NAME ConnectionManagerFSM
    STANDARD 20
    STANDALONE
    NAMESPACE conn
)
```

---

## Next Steps

Now that you have mastered the complete pipeline from modeling to code generation, proceed to the capstone chapter:

**[Tutorial 6: Complete Real-World Case Study (Autonomous UAV Controller)](06_real_world_case_study.md)**.

---

<div style="display: flex; justify-content: space-between; align-items: center; margin-top: 2rem; padding-top: 1rem; border-top: 1px solid var(--fsmc-border);">
    <a href="04_formal_verification.md" style="font-weight: 600; color: var(--fsmc-primary);">← Tutorial 4: Formal Verification</a>
    <a href="06_real_world_case_study.md" style="font-weight: 600; color: var(--fsmc-primary);">Tutorial 6: UAV Flight Controller →</a>
</div>
