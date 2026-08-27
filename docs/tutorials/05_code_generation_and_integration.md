# Tutorial 5: Code Generation & Build Integration

In this final tutorial, you will learn how to turn your verified state machine models into production-ready software:

- How `fsmc` uses the **Generation Gap Pattern** to ensure safe, non-destructive builds.
- Generating standalone and modular C++ code (reference backend).
- Integrating automated compilation into **CMake** (`fsmc_target_sources`).
- The universal multi-backend architecture of `fsmc`.

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
│   └── connection_context.hpp  <-- User Business Logic (Untouched)
└── build/generated/
    └── connection_fsm.hpp      <-- Generated Artifact (Regenerated automatically)
```

1. **The generated file (`connection_fsm.hpp`) is never edited manually**. It lives in your `build/` directory.
2. **Your custom logic lives in your context struct** (`connection_context.hpp`).
3. Whenever you update `connection.sysml`, `fsmc` regenerates the header in milliseconds. Your application code simply links against the updated transition table with **zero lost work**.

---

## 2. Generating C++ Code from the Command Line

```bash
# Generate a self-contained C++20 header with zero external dependencies
fsmc -i connection.sysml -o connection_fsm.hpp --standard 20 --standalone

# Generate a C++17 header
fsmc -i connection.sysml -o connection_fsm.hpp --standard 17 --standalone
```

---

## 3. Using the Generated State Machine in C++

In your `main.cpp`:

```cpp
#include <iostream>
#include "connection_fsm.hpp"

struct ConnectionContext {
    int retry_count{0};

    // Automatically invoked by fsmc runtime when entering Connected state
    void on_entry(const conn::Connected&) {
        std::cout << "[LOG] Connected to remote host.\n";
    }

    // Automatically invoked when exiting Connected state
    void on_exit(const conn::Connected&) {
        std::cout << "[LOG] Connection closed. Flushing socket.\n";
    }
};

int main() {
    ConnectionContext ctx;
    conn::ConnectionManagerFSM fsm(ctx);

    std::cout << "Initial state: " << fsm.current_state_name() << "\n";

    // Dispatch typed events
    fsm.dispatch(conn::ConnectCmd{});
    std::cout << "State after ConnectCmd: " << fsm.current_state_name() << "\n";

    fsm.dispatch(conn::HandshakeOk{});
    std::cout << "State after HandshakeOk: " << fsm.current_state_name() << "\n";

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
    src/connection_context.cpp
)

# Automatically compile .sysml / .puml into C++ headers on every build
fsmc_target_sources(my_app
    DIAGRAMS models/connection.sysml
    NAME ConnectionManagerFSM
    STANDARD 20
    NAMESPACE conn
    STANDALONE
)
```

When you edit `models/connection.sysml` and run `cmake --build build`:

1. CMake recognizes that `connection.sysml` changed.
2. CMake invokes `fsmc` to update `build/generated_my_app/connection_fsm.hpp`.
3. The C++ compiler compiles your application against the new types.
4. If a state or event was renamed in the model, the C++ compiler reports a type-safe compile error pointing directly to the line in `main.cpp` that needs updating.

---

## 5. Summary: The Universal Vision of `fsmc`

You have now completed the entire `fsmc` workflow:

1. **Model** visually (Mermaid, PlantUML) or formally (SysML v2, SCXML).
2. **Optimize & Verify** mathematically (LTL/CTL, interval analysis).
3. **Deploy** with zero overhead into production software.

While C++17 and C++20 serve as the high-performance reference runtime backend implemented today, `fsmc`'s Intermediate Representation (`FsmIr`) is completely target-agnostic and designed to support additional language backends in the future.
