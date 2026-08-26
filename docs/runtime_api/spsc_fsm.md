# `fsm::spsc_fsm` (Lock-Free SPSC / ISR-Safe)

`fsm::spsc_fsm<TransitionTable, Context, QueueCapacity, InitialState>` is a specialized, zero-allocation, Single-Producer Single-Consumer FSM wrapper designed for Interrupt Service Routines (ISRs), hard real-time tasks, and multi-core embedded systems.

---

## Class Template Synopsis

```cpp
namespace fsm {

template <
    typename Table,
    typename Context = no_context,
    std::size_t QueueCapacity = 64,
    typename InitialState = typename Table::initial_state
>
class spsc_fsm {
public:
    explicit spsc_fsm(Context ctx = Context{}) noexcept;

    // Producer Interface (Wait-Free O(1), ISR-Safe)
    template <typename Event>
    bool enqueue(const Event& ev) noexcept;

    // Consumer Interface (Dedicated Control Thread)
    bool process_one();
    std::size_t run_until_empty();

    // Lock-Free Reader Interface (Any Thread)
    [[nodiscard]] std::size_t state_index() const noexcept;
    [[nodiscard]] std::string_view state_name() const noexcept;

    template <typename State>
    [[nodiscard]] bool is_in_state() const noexcept;

    [[nodiscard]] Context snapshot_context() const noexcept;

    template <typename Func>
    void with_context(Func&& fn) const noexcept;
};

} // namespace fsm
```

---

## Concurrency Guarantees

### 1. Wait-Free $O(1)$ Producer (`enqueue`)
- Never blocks, spins, or acquires mutexes.
- Returns `true` if the event was successfully enqueued into the lock-free circular ring buffer, or `false` if full.
- Safe to invoke inside nested hardware interrupt handlers (ARM NVIC, RISC-V PLIC).

### 2. Dedicated Single Consumer (`process_one`, `run_until_empty`)
- Pops events from the ring buffer and executes `fsm.dispatch()` sequentially.
- Protects context writes using atomic seqlock increments.

### 3. Lock-Free Seqlock Context Inspection (`snapshot_context`)
- External threads (GUI, Telemetry, Watchdogs) can take consistent snapshots of `Context` without locking or delaying the consumer thread.

---

## Bare-Metal Integration Example (STM32 / ARM Cortex-M)

```cpp
#include "uav_mission_fsm.hpp"

// Static instance with 64-element lock-free ring buffer
avionics::UavMissionFSMContext g_ctx;
avionics::SpscUavMissionFSM g_mission_fsm(g_ctx);

// Hardware UART Interrupt Service Routine (Producer)
extern "C" void USART1_IRQHandler(void) {
    if (USART1->SR & USART_SR_RXNE) {
        uint8_t byte = USART1->DR;
        if (byte == 0xAA) {
            // Wait-Free O(1) enqueue inside ISR
            g_mission_fsm.enqueue(avionics::TakeoffCmd{});
        }
    }
}

// RTOS Task / Main Loop (Consumer)
void Task_ControlLoop(void *pvParameters) {
    while (1) {
        // Process any queued events deterministically
        g_mission_fsm.run_until_empty();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Telemetry Task (Reader)
void Task_Telemetry(void *pvParameters) {
    while (1) {
        // Lock-free seqlock snapshot
        auto ctx_snapshot = g_mission_fsm.snapshot_context();
        std::string_view state = g_mission_fsm.state_name();
        
        SendTelemetry(state, ctx_snapshot.batteryLevel);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
```
