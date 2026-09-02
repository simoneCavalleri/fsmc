# Tutorial 5: Code Generation & Build Integration

In this final tutorial, you will learn how to turn your verified state machine models into production-ready software:

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

## 2. Generating C++ Code from the Command Line

```bash
# Generate a self-contained C++20 header with zero external dependencies
fsmc -i connection.sysml -o connection_fsm.hpp --std 20 --standalone

# Generate a C++17 header
fsmc -i connection.sysml -o connection_fsm.hpp --std 17 --standalone
```

---

## 3. Using the Generated State Machine in C++

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

---

## 4. Seamless CMake Integration (`fsmc_target_sources`)

To automate state machine compilation in your build pipeline, use the CMake helper function provided by `fsmc`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_embedded_app CXX)

find_package(fsmc REQUIRED)

add_executable(my_app
    src/main.cpp
)

target_link_libraries(my_app PRIVATE fsmc::fsmc_runtime)

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

## 5. Next Steps

Now that you have mastered the complete pipeline from modeling to code generation, proceed to the capstone chapter:

👉 **[Step 6: Complete Real-World Case Study (Autonomous UAV Controller)](06_real_world_case_study.md)**.
