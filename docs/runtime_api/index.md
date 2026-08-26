# Runtime C++ API

The `fsmc` runtime is an ultra-lightweight, zero-allocation, header-only C++17/C++20 infrastructure engineered for bare-metal microcontrollers, RTOS kernels, and high-frequency real-time execution engines.

---

## Section Contents

| Component | Target Environment | Concurrency & Safety Model | Documentation |
| :--- | :--- | :--- | :--- |
| **`fsm::fsm`** | Single-Threaded Core | Synchronous, zero heap allocations, compile-time dispatch table. | [Synchronous FSM](synchronous_fsm.md) |
| **`fsm::spsc_fsm`** | ISR & Lock-Free Queue | Wait-Free $O(1)$, ISR-safe single-producer single-consumer ring buffer. | [Lock-Free SPSC FSM](spsc_fsm.md) |
| **`fsm::thread_safe_fsm`** | Multi-Threaded Engine | Asynchronous MPSC event loop with dedicated worker thread. | [Thread-Safe FSM](thread_safe_fsm.md) |
| **Transition Trace & Introspection** | Telemetry & Logging | Runtime transition observation and history tracking in `dispatch_result`. | [Introspection Trace](introspection_trace.md) |
| **Full API Reference** | All Engines | Complete member functions, type traits, and lifecycle hooks specification. | [Runtime API Reference](reference.md) |

---

## Standalone Header Deployment

You can deploy the runtime as a single standalone header with zero external dependencies:

```bash
# Generate standalone C++20 header
fsmc -i mission.sysml -o mission_fsm.hpp --standard 20 --standalone
```
