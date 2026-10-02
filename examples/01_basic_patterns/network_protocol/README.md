# Example 01: High-Throughput Network Protocol Handshake

## Overview
This example models a mission-critical distributed network protocol connection manager. It demonstrates the fundamentals of reactive statechart engineering in `fsmc`: conditional branch transitions guarded by compound boolean logic (`HasNetworkGuard && HasValidCredentialsGuard`), robust De Morgan negative paths, timeout degradation, clean session teardown, and dual synchronous/asynchronous execution semantics without dynamic heap allocations.

---

## 🔗 The Architecture & Binding Contract

How does `fsmc` connect your high-level diagram to your C++ code?

```text
┌──────────────────────────────────────────────┐
│  Model: connection.sysml / connection.puml   │
│                                              │
│  State: Disconnected ──> Connecting          │
│    accept: ConnectCmd                        │
│    guard:  [Net && Creds]                    │
│    action: / OpenSocketAction                │
└──────────────────────┬───────────────────────┘
                       │
                       │ fsmc compilation (CLI or CMake macro)
                       ▼
┌──────────────────────────────────────────────┐
│  Generated Header: connection_fsm.hpp        │
│                                              │
│  - Namespaced FSM class (net::ConnectionFSM) │
│  - Transition table with typed row bindings  │
│  - Event variants and state tags             │
└──────────────────────┬───────────────────────┘
                       │
                       │ #include "connection_fsm.hpp"
                       ▼
┌──────────────────────────────────────────────┐
│  Your Code: main.cpp                         │
│                                              │
│  - struct HasNetworkGuard          { ... };  │
│  - struct HasValidCredentialsGuard { ... };  │
│  - struct OpenSocketAction         { ... };  │
│                                              │
│  net::ConnectionFSM fsm;                     │
│  fsm.dispatch(net::ConnectCmd{});            │
└──────────────────────────────────────────────┘
```

---

## Diagram / Statechart Model

Formal specifications are provided across multiple interoperable formats:
- **SysML v2**: [`connection.sysml`](connection.sysml) (canonical OMG SysML v2 state definition with typed MBSE I/O ports)
- **PlantUML**: [`connection.puml`](connection.puml) (visual diagram with `@fsm:port` range contracts)
- **Mermaid**: [`connection.mmd`](connection.mmd) (lightweight stateDiagram-v2 representation)

```text
       +-----------------------------------------------------------+
       |                                                           |
       v                                                           | ConnectCmd [!Net || !Creds]
+--------------+               ConnectCmd [Net && Creds]       +----------------+
| Disconnected | --------------------------------------------> |   Connecting   |
+--------------+                                               +----------------+
       ^      ^                                                        |
       |      | HandshakeFailed / Timeout                              | HandshakeOk
       |      +--------------------------------------------------------+
       |                                                               |
       | DisconnectCmd                                                 v
       +------------------------------------------------------- +--------------+
       |                                                        |  Connected   |
       | DisconnectCmd / Timeout                                +--------------+
       |                                                          |          ^
+--------------+             NetworkDegradedEvent                 |          | NetworkRestoredEvent
|  Suspended   | <------------------------------------------------+          |
+--------------+ ------------------------------------------------------------+
```

---

## How to Build and Run

### Recommended: Via CMake (`fsmc_target_sources`)

In production projects, you integrate `fsmc` directly into your `CMakeLists.txt`. CMake automatically invokes `fsmc` whenever the diagram changes:

```cmake
# Add your target executable
add_executable(network_protocol_example main.cpp)
target_link_libraries(network_protocol_example PRIVATE fsmc_runtime)

# Transpile diagram to C++ header at build time
fsmc_target_sources(network_protocol_example
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/connection.sysml
    NAME ConnectionFSM
    STANDARD 20
    STANDALONE
    NAMESPACE net
    NO_STUBS
)
```

Build and execute via project CMake:
```bash
# From repository root:
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --target network_protocol_example

# Run the test binary:
./build/bin/network_protocol_example
```

---

### Alternative: Standalone CLI Workflow

You can also run each compiler stage manually from the command line:

```bash
# 1. From examples/01_basic_patterns/network_protocol/:
# Inspect formal model metrics and run optimization passes
fsm-opt -i connection.sysml --passes=canonicalize,guard-simplification,dead-state-pruning --metrics

# 2. Transpile SysML v2 to canonical PlantUML or nuXmv SMV formal model
fsm-opt -i connection.sysml --emit-puml -o connection_canonical.puml
fsm-opt -i connection.sysml --emit-smv -o connection_formal.smv

# 3. Generate production C++20 standalone state machine header
fsmc -i connection.sysml --target cpp --c++20 --standalone -n ConnectionFSM --namespace net -o connection_fsm.hpp

# 4. Build and execute with your C++ compiler (pointing to runtime headers)
g++ -std=c++20 -Wall -Wextra -Werror -I. -I../../../include main.cpp -o network_runner
./network_runner
```

---

## Key Passes Demonstrated

| Pass Name | Optimization / Validation Performed |
| :--- | :--- |
| `guard-simplification` | Reduces compound boolean expressions using algebraic identities (e.g. `!(!A) -> A`, `A && true -> A`). |
| `dead-state-pruning` | Analyzes reachability graphs and physically prunes dead states and unexecutable transitions. |
| `determinism` | Verifies priority ordering and eliminates non-deterministic transition collisions. |
| `timed-deadlock` | Analyzes timeout events to ensure active states have valid escape transitions. |

---

## Expected Output

```text
================================================================================
 FSMC SHOWCASE 01: HIGH-PERFORMANCE NETWORK PROTOCOL HANDSHAKE
================================================================================
  [STEP] Initial State Machine Instantiation    --> Current State: Disconnected

--- Scenario 1: Negative Path (Missing Network Interface) ---
  [NET Action/ERROR] Network interface or credentials MISSING -> Connection rejected (Total errors: 1)
  [STEP] ConnectCmd with has_network=false      --> Current State: Disconnected

--- Scenario 2: Nominal Connection Lifecycle ---
  [NET Action] Physical link UP -> Non-blocking TCP socket opened (fd=42)
  [STEP] ConnectCmd with valid credentials      --> Current State: Connecting
  [NET Action] Handshake ACK received -> Cryptographic session active (session_id=999)
  [STEP] HandshakeOkEvent received              --> Current State: Connected

--- Scenario 3: Transient Network Degradation ---
  [NET Action] Network degraded -> Outgoing TCP transmission buffer paused
  [STEP] NetworkDegradedEvent received          --> Current State: Suspended
  [NET Action] Network restored -> Transmission buffer resumed
  [STEP] NetworkRestoredEvent received          --> Current State: Connected

--- Scenario 4: Clean Session Teardown ---
  [NET Action] Graceful disconnect -> TCP FIN packet sent; socket 42 closed
  [STEP] DisconnectCmd issued                   --> Current State: Disconnected

--- Scenario 5: Asynchronous Worker Execution (thread_safe_fsm) ---
  [NET Action] Physical link UP -> Non-blocking TCP socket opened (fd=42)
  [STEP] Asynchronous ConnectCmd processed      --> Current State: Connecting
  [NET Action] Handshake ACK received -> Cryptographic session active (session_id=999)
  [STEP] Asynchronous HandshakeOkEvent processed --> Current State: Connected

================================================================================
 ALL HIGH-PERFORMANCE NETWORK PROTOCOL TESTS PASSED [100% OK]
================================================================================
```
