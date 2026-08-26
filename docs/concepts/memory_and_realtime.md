# Memory Architecture & Hard Real-Time Guarantees

`fsmc` is designed from the ground up for strict deterministic memory and execution bounds.

---

## 1. Zero Dynamic Allocation (No-Heap)

- **State Representation**: Stored directly inside `std::variant<States...>`.
- **Transitions**: Executed as pure compile-time unrolled template folds on the call stack.
- **No Virtual Tables**: Zero vtable lookups and zero heap indirection.

---

## 2. Lock-Free & Wait-Free SPSC Execution

For ISRs (Interrupt Service Routines) and hard real-time sensor tasks:
- **`fsm::spsc_ring_buffer`**: Power-of-two circular ring buffer utilizing acquire-release atomics with zero lock contention.
- **Wait-Free $O(1)$**: Producers never block, spin, or allocate.
- **Seqlock Context Protection**: Concurrent reader threads take consistent snapshots of `Context` without locking the producer or consumer threads.
