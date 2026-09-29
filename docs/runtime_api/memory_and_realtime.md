# C++ Runtime Memory Architecture & Real-Time Guarantees

In safety-critical embedded systems, automotive firmware, and aerospace controllers, dynamic memory allocations (`malloc`, `new`, `std::vector`, `std::function`) and dynamic virtual tables are strictly prohibited due to heap fragmentation and non-deterministic timing risks.

The `fsmc` C++17/C++20 reference backend is engineered to provide **100% zero-heap, zero-vtable, and zero-exception** guarantees.

---

## 1. Zero-Heap and Zero-VTable Guarantees

Every `fsm::fsm` instance is stored contiguously on the stack or in static/BSS storage with compile-time known bounds:

```mermaid
flowchart TD
    subgraph StackSpace["Stack Memory Space: sizeof(fsm::fsm) — Contiguous Inline Allocation"]
        direction LR
        subgraph FSM["fsm::fsm Object on Stack"]
            direction TB
            subgraph F1["1. Active State Configuration"]
                V["std::variant&lt;States...&gt;<br/>Type Tag Index + Max State Payload<br/><b>0 Bytes Heap — Stack Inline</b>"]
            end
            subgraph F2["2. Datapath Registers"]
                R["Registers (z^-1 State)<br/>Persistent Internal Fields<br/><b>0 Bytes Heap — Stack Inline</b>"]
            end
            subgraph F3["3. History Storage"]
                H["static_vector&lt;history_entry, N&gt;<br/>Bounded BSS / Stack Buffer<br/><b>0 Bytes Heap — Stack Inline</b>"]
            end
            subgraph F4["4. Deferred Event Queue"]
                D["static_vector&lt;event_variant, M&gt;<br/>Bounded Inline Event Storage<br/><b>0 Bytes Heap — Stack Inline</b>"]
            end
            subgraph F5["5. Service Injected Binding"]
                P["Services* (Non-Owning Reference)<br/>External Hardware / OS Driver<br/><b>0 Bytes Overhead — Direct Pointer</b>"]
            end
            subgraph F6["6. Deterministic Timers"]
                T["deterministic_timer_manager&lt;K&gt;<br/>Static Timer Slot Array<br/><b>0 Bytes Heap — Stack Inline</b>"]
            end
            subgraph F7["7. Flight Recorder Buffer"]
                TR["TraceBuffer&lt;Cap&gt; (Circular Ring)<br/>O(1) Chronological History<br/><b>0 Bytes Heap — Stack Inline</b>"]
            end
        end
    end

    subgraph Boundaries["Compile-Time Formal Invariants"]
        Heap["Heap Allocation Space: 0 Bytes<br/>(malloc / new / heap closures strictly eliminated)"]
        VTable["Virtual Method Tables (vtable): 0 Bytes<br/>(!std::is_polymorphic_v / Zero vptr overhead)"]
    end

    FSM -.->|No Heap Pointers| Heap
    FSM -.->|Static Template Dispatch| VTable
```

### Memory & Execution Metrics

| Metric | Architectural Guarantee | Implementation Mechanism |
| :--- | :--- | :--- |
| **Heap Allocations** | **0 Bytes** | Stack-allocated objects, no dynamic memory (`malloc`/`new`) |
| **Virtual Table Overhead** | **0 Bytes** | Compile-time template dispatch (`!std::is_polymorphic_v`) |
| **State Storage** | Contiguous inline memory | `std::variant<States...>` |
| **History & Deferred Events** | Fixed-capacity stack storage | Inline `fsm::static_vector` and `event_variant` |
| **Deterministic Timers** | Zero-allocation countdown slots | Fixed `deterministic_timer_manager<K>` |
| **Flight Recorder Telemetry** | Bounded circular audit trail | Static `TraceBuffer<Capacity>` with $O(1)$ push |
| **Transition Dispatch** | Deterministic $O(1)$ execution time | Compile-time unrolled fold expressions |
| **Event Queues** | Wait-free $O(1)$ insertion | Static power-of-two circular ring buffer |

---

## 2. Inline Static Vector Storage (`fsm::static_vector`)

To support UML 2.5 History states and Deferred Event queues without dynamic heap allocations, `fsmc` provides `fsm::static_vector<T, Capacity>` in `include/fsm/backend/cpp/runtime/static_vector.hpp`:

- **Stack-Allocated Buffer**: Stores up to `Capacity` elements directly in inline storage within the `fsm` struct.
- **Deterministic $O(1)$ Operations**: `push_back`, `pop_back`, `erase`, `front`, and `back` execute in bounded constant time.
- **History Records**: Capacity is bounded at compile time to the number of composite states in the transition table (`Table::state_count`).
- **Deferred Queue**: Stores typed event variants (`Table::event_variant`) inline without type-erasure heap closures.

---

## 3. Lock-Free SPSC Ring Buffer (`fsm::spsc_ring_buffer`)

For embedded systems where discrete events are produced inside hardware Interrupt Service Routines (ISRs) and consumed by a worker control task, mutex locking is unsafe.

`fsmc` provides `fsm::spsc_ring_buffer<T, Capacity>`:

- **Power-of-Two Ring Buffer**: Array index wrapping is evaluated via bitwise masking (`index & (Capacity - 1)`), avoiding hardware division instructions.
- **Acquire-Release Memory Ordering**: Uses `std::atomic<std::size_t>` with `memory_order_acquire` and `memory_order_release` to synchronize producer and consumer without mutex locks.
- **Wait-Free O(1) Enqueue**: The producer thread (ISR) executes in a bounded number of CPU cycles without retries or spinning.

---

## 4. Seqlock Synchronization for Reader Threads

`spsc_fsm::snapshot_registers()` implements a sequence lock (seqlock) protocol allowing reader threads (e.g. telemetry or logging) to capture consistent snapshots of internal `Registers` without blocking the control task:

1. The consumer increments `seq_` to an odd number before mutating registers, and to an even number after.
2. The reader thread reads `seq_` before and after copying the registers.
3. If `seq_` was even and unchanged across the copy, the snapshot is guaranteed free of torn-reads.

---

## 5. Zero-Heap Binary State Snapshot Serialization & Deserialization

Starting in `v0.7.0`, `fsmc` provides built-in binary snapshot serialization and restoration designed for high-reliability embedded checkpoints, NVRAM retention across reboot cycles, and hot-standby dual-redundant synchronization.

### Key Guarantees
- **100% Zero-Heap**: Operates directly into caller-provided stack buffers, fixed arrays, or non-volatile memory slots using `std::span<std::uint8_t>` or raw buffer pointers.
- **Full Statechart Capture**: Serializes active state variant index, history states, deterministic timers, residence duration (time invariants), and datapath registers.
- **Defensive Integrity Validation**: Packed header with magic word (`0x46534D43`), schema revision, registers size check, and 32-bit FNV-1a checksum. Deserialization strictly rejects truncated, outdated, or corrupted snapshots.

### Usage Example

```cpp
#include "fsm/backend/cpp/runtime/fsm.hpp"
#include "fsm/backend/cpp/runtime/serialization.hpp"

// Allocate fixed-size stack buffer
std::array<std::uint8_t, 256> snapshot_buf{};
std::size_t bytes_written = 0;

// Serialize active state machine snapshot
bool ok = fsm::serialize_state(controller, snapshot_buf, bytes_written);
if (ok) {
    // Write buffer to NVRAM, EEPROM, or network socket
}

// In a fresh instance or after reboot recovery:
ControllerFSM restored_controller;
std::size_t bytes_read = 0;
bool restored = fsm::deserialize_state(restored_controller, 
                                      std::span<const std::uint8_t>(snapshot_buf.data(), bytes_written), 
                                      bytes_read);
if (restored) {
    // System resumes execution seamlessly from the exact checkpointed state
}
```
