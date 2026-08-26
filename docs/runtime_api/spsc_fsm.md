# `fsm::spsc_fsm` (Lock-Free SPSC / ISR-Safe)

A zero-allocation, Wait-Free $O(1)$ Single-Producer Single-Consumer FSM wrapper designed for Interrupt Service Routines (ISRs) and hard real-time systems.

---

## API Summary

```cpp
#include "fsm/runtime/cpp/spsc_fsm.hpp"

// 64-element power-of-two lock-free ring buffer
fsm::spsc_fsm<TransitionTable, SystemContext, 64> spsc_machine(ctx);

// ISR Thread (Producer): Wait-Free O(1)
void EXTI0_IRQHandler() {
    spsc_machine.enqueue(TickEvent{});
}

// RTOS Task Thread (Consumer): Deterministic execution
void Task_ControlLoop() {
    spsc_machine.run_until_empty();
}

// Any Reader Thread: Atomic lock-free inspection
auto state_name = spsc_machine.state_name();
auto ctx_copy = spsc_machine.snapshot_context();
```
