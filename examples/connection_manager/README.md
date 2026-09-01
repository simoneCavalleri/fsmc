# Resilient Network Connection Manager Showcase

This showcase demonstrates an enterprise-grade, fault-tolerant networking protocol connection manager modeled and compiled with **`fsmc`**.

It highlights **MBSE 4-domain memory segregation**, compile-time boolean guard reduction (De Morgan laws), and the transition between synchronous caller execution and background worker threads.

---

## What This Example Demonstrates

1. **MBSE 4-Domain Segregated Memory Architecture**:
   - `NetworkInPorts`: Read-only snapshot of hardware network interface availability and valid cryptographic credentials.
   - `NetworkRegisters`: Internal datapath variables ($z^{-1}$ delay) tracking socket file descriptor (`socket_fd`), session IDs, error counters, and transmitted byte totals.
2. **De Morgan Composite Boolean Guards**:
   - Uses `fsm::and_<HasNetworkGuard, HasValidCredentialsGuard>` to atomically prevent socket connection attempts when link conditions are unmet.
   - Automatically executes fallback error logging actions (`LogErrorAction`) when conditions fail.
3. **QoS Packet Loss Detection & Flow Control Suspension**:
   - Detects packet loss bursts (>15%) and transitions into `Suspended`, safely pausing outbound transmission queues.
   - Automatically resumes normal transmission (`Connected`) when link latency stabilizes (<20ms).
4. **Dual Execution Engine Demonstration**:
   - **Synchronous Execution (`fsm::fsm`)**: Direct, deterministic dispatch on the calling thread.
   - **Asynchronous Execution (`fsm::thread_safe_fsm`)**: Active Object running on a background worker thread with thread-safe event queuing.
5. **Universal Multi-Format Parity**:
   - Identical model provided across all 8 frontend formats (`.sysml`, `.puml`, `.mmd`, `.dot`, `.json`, `.scxml`, `.xmi`, `.smv`).

---

## Execution Phases in `main.cpp`

```
[Phase 1] Initial State & Network Interface Verification (Disconnected)
    │
[Phase 2] TLS 1.3 Connection Handshake (ConnectCmd -> Connecting -> HandshakeOk -> Connected)
    │
[Phase 3] QoS Flow Control & Packet Queue Suspension (PacketLoss -> Suspended -> LinkStable -> Connected)
    │
[Phase 4] Graceful Session Termination (DisconnectCmd -> Disconnected)
    │
[Phase 5] De Morgan Composite Boolean Guard Enforcement (Invalid credentials / Missing interface rejection)
    │
[Phase 6] TCP / TLS Handshake Timeout Recovery (Connecting -> HandshakeTimeout -> Disconnected)
    │
[Phase 7] Asynchronous Background Thread-Safe Worker Execution (thread_safe_fsm dispatch)
```

---

## Running the Example

```bash
# Run via CMake build binary
./build/bin/connection_manager_example
```
