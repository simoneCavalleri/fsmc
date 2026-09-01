# Runtime C++ API Guide

`fsmc` provides a header-only, **100% zero-heap, zero-vtable, and zero-exception** C++17/C++20 runtime library (`include/fsm/backend/cpp/runtime/`).

The runtime can be generated as a single standalone header (`--standalone`) with **zero external dependencies** outside the C++ Standard Library, making it immediately embeddable into bare-metal microcontrollers, RTOS firmware, and high-performance desktop applications.

---

## 5-Minute C++ Quickstart

Here is a complete, self-contained state machine written directly using the `fsmc` C++ runtime:

```cpp
#include <iostream>
#include <cassert>
#include "fsm/backend/cpp/runtime/fsm.hpp"

// 1. Define States (must provide static constexpr std::string_view name)
struct Off { static constexpr std::string_view name = "Off"; };
struct On  { static constexpr std::string_view name = "On";  };

// 2. Define Events
struct ToggleCmd {};

// 3. Define the Transition Table using fsm::row
using LampTable = fsm::transition_table<
    fsm::row<Off, ToggleCmd, On>,
    fsm::row<On,  ToggleCmd, Off>
>;

int main() {
    // 4. Instantiate the State Machine (starts in InitialState = Off)
    fsm::fsm<LampTable> lamp;

    std::cout << "Initial state: " << lamp.current_state_name() << "\n"; // Off
    assert(lamp.is_in<Off>());

    // 5. Dispatch Events
    lamp.dispatch(ToggleCmd{});
    std::cout << "After toggle: " << lamp.current_state_name() << "\n";  // On
    assert(lamp.is_in<On>());

    lamp.dispatch(ToggleCmd{});
    assert(lamp.is_in<Off>());

    return 0;
}
```

---

## Runtime Engine Flavors

`fsmc` provides three specialized execution engines tailored for different concurrency and threading requirements:

```mermaid
flowchart TD
    Start["Select an execution engine"] --> Q1{"Do you need asynchronous event queueing?"}
    
    Q1 -- No --> Sync["1. fsm::fsm (Synchronous Engine)<br/>- Executes directly on caller's stack<br/>- Zero threading overhead<br/>- Deterministic O(1) step & dispatch"]
    
    Q1 -- Yes --> Q2{"Are events pushed from Interrupts (ISR) or a single thread?"}
    
    Q2 -- Yes --> SPSC["2. fsm::spsc_fsm (Lock-Free SPSC)<br/>- Wait-free O(1) ring buffer<br/>- ISR-safe (no mutex locks)<br/>- Read-only seqlock for telemetry"]
    
    Q2 -- No --> MPSC["3. fsm::thread_safe_fsm (Thread-Safe MPSC)<br/>- Multi-producer event queue<br/>- Dedicated worker thread<br/>- Asynchronous std::future results"]
```

| Engine | Concurrency Model | Target Environment | Key Documentation |
| :--- | :--- | :--- | :--- |
| **`fsm::fsm`** | Synchronous (Caller Thread) | Bare-metal, control loops, deterministic sequential logic | [Synchronous Core FSM](synchronous_fsm.md) |
| **`fsm::spsc_fsm`** | Single-Producer Single-Consumer | Hardware ISRs, DMA callbacks, RTOS sensor queues | [Lock-Free SPSC FSM](spsc_fsm.md) |
| **`fsm::thread_safe_fsm`** | Multi-Producer Single-Consumer | Multi-threaded services, network daemons, UI event loops | [Thread-Safe MPSC FSM](thread_safe_fsm.md) |

---

## Method Selection: `dispatch` vs `step` vs `post`

| Method | Target Engine | Execution Mode | When to Use It? | Return Type |
| :--- | :--- | :--- | :--- | :--- |
| **`dispatch(event, ...)`** | `fsm::fsm` | Synchronous (Immediate) | You want to process a discrete event right now on the current thread. | `dispatch_result` |
| **`step(...)`** | `fsm::fsm`, `spsc_fsm` | Synchronous (Cycle Tick) | Periodic control loop tick (e.g. 1 kHz timer) evaluating continuous anonymous transitions against `InPorts`. | `dispatch_result` |
| **`post(event)`** | `spsc_fsm`, `thread_safe_fsm` | Asynchronous (Fire & Forget) | Pushing an event into a queue without waiting for completion (e.g. inside a hardware ISR or from a producer thread). | `bool` (SPSC) / `void` (MPSC) |
| **`post_async(event)`** | `thread_safe_fsm` | Asynchronous (Future Awaitable) | Pushing an event from a thread and waiting (`.get()`) for the worker thread to finish the transition. | `std::future<dispatch_result>` |
| **`post_delayed(event, delay)`** | `thread_safe_fsm` | Asynchronous (Timed Delay) | Scheduling an event to fire automatically after a specified time duration (e.g. 500ms timeout). | `void` |
| **`process_one(...)`** | `spsc_fsm` | Synchronous (Drain Step) | Executing the single oldest queued event on the consumer control task. | `bool` |
| **`run_until_empty(...)`** | `spsc_fsm` | Synchronous (Drain All) | Processing all pending queued events in a batch loop. | `std::size_t` |

---

## Parameter Injection Guide: When to Pass Ports & Services

`fsmc` supports both construction-time binding and call-site parameter injection:

```mermaid
flowchart TD
    subgraph Construction["Construction-Time Binding"]
        Reg["Registers (Internal State)<br/>fsm(initial_registers)"]
        Srv["Services (Hardware/OS Interface)<br/>fsm(reg, concrete_services)"]
    end

    subgraph CallSite["Call-Site Injection (Every Cycle / Event)"]
        In["InPorts (Input Snapshot)<br/>fsm.step(in, out) / fsm.dispatch(ev, in, out)"]
        Out["OutPorts (Output Buffer)<br/>Single-assignment outputs written by actions"]
    end
```

### Injection Rules

| Domain | Injection Point | Mutability & Lifetime | Call-Site Usage Example |
| :--- | :--- | :--- | :--- |
| **`Registers`** | FSM Construction | Persistent internal memory ($z^{-1}$) | `fsm::fsm sm(initial_registers);` |
| **`InPorts`** | Call-Site Snapshot | Read-only input snapshot | `sm.dispatch(ev, in, out);` or `sm.step(in, out);` |
| **`OutPorts`** | Call-Site Buffer | Single-assignment write buffer | `sm.dispatch(ev, in, out);` or `sm.step(in, out);` |
| **`Services`** | Construction or Call-Site | External drivers / environment RPC | Bound: `sm(reg, srv);`<br/>Injected: `sm.dispatch(ev, in, out, srv);` |
| **Stateless** | Omitted | Zero overhead | `sm.dispatch(ev);` or `sm.step();` |

#### Key Rules:
- **Internal `Registers`**: Passed once at construction (`fsm::fsm sm(initial_registers);`). Persists inside the state machine across cycles. Accessible via `sm.registers()`.
- **`InPorts` (Read-Only Sensor Snapshot)**: Passed at the call site (`sm.dispatch(event, in, out)` or `sm.step(in, out)`). Represents the immutable input state for this exact cycle (enforces the Latching pattern).
- **`OutPorts` (Actuator Command Buffer)**: Single-assignment buffer populated by action effects during the transition step and committed to hardware afterwards.
- **`Services` (External Hardware / Driver Interfaces)**:
    - *Option A (Bound at Construction)*: If you construct `sm(reg, srv)`, you can omit `srv` at call sites: `sm.dispatch(event, in, out);`.
    - *Option B (Injected at Call Site)*: Pass explicitly on each call: `sm.dispatch(event, in, out, srv);`.
- **Stateless / No-Ports FSM**: If your model has no ports or services (`fsm::no_ports`, `fsm::no_services`), omit them completely: `sm.dispatch(event);` or `sm.step();`.

---

## Chapter Roadmap

1. **[Universal Runtime Fundamentals](core_concepts.md)**: Universal building blocks shared across all engines (States, Hooks, Events, 4-Domain Datapath, Flexible Guards/Actions, History, and `dispatch_result`).
2. **[1. Synchronous Core Engine (`fsm::fsm`)](synchronous_fsm.md)**: Synchronous execution on the caller's stack, deterministic $O(1)$ dispatch, and periodic sampled `step()` control loops.
3. **[2. Lock-Free SPSC Engine (`fsm::spsc_fsm`)](spsc_fsm.md)**: Wait-free ISR event ingestion, lock-free ring buffer mechanics, and seqlock reader synchronization.
4. **[3. Thread-Safe MPSC Engine (`fsm::thread_safe_fsm`)](thread_safe_fsm.md)**: Active Object pattern, worker threads, async futures, and timed delayed events.
5. **[Memory Layout & Real-Time Guarantees](memory_and_realtime.md)**: Formal memory guarantees, `fsm::static_vector`, zero-vtable overhead, and WCET determinism.
6. **[Transition Tracing & Telemetry](introspection_trace.md)**: Observers, `dispatch_result`, structured trace metadata, and deterministic timers.
7. **[Architectural Design Patterns](design_patterns.md)**: Production cookbooks for embedded sensor pipelines, flight mission controllers, and multi-FSM coordination.
8. **[Full Runtime API Reference](reference.md)**: Complete member function, type trait, and C++20 concept reference.
