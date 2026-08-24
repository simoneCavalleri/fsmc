# Runtime C++ API Reference

This document describes the core C++ classes, lifecycle hooks, thread-safe asynchronous wrappers, error handlers, and real-time utilities provided by **`fsmc`**.

---

## Table of Contents
1. [`fsm::fsm<Table, Context, InitialState>`](#1-fsmfsmtable-context-initialstate-synchronous-engine)
2. [`fsm::dispatch_result` & `fsm::dispatch_status`](#2-fsmdispatch_result--fsmdispatch_status)
3. [`fsm::thread_safe_fsm<Table, Context, InitialState>`](#3-fsmthread_safe_fsmtable-context-initialstate-thread-safe-engine)
4. [Composite Boolean Guards (`and_`, `or_`, `not_`)](#4-composite-boolean-guards-and_-or_-not_)
5. [Deferred Events & History Resolution](#5-deferred-events--history-resolution)
6. [`fsm::spsc_ring_buffer<T, Capacity>` (Wait-Free & ISR-Safe)](#6-fsmspsc_ring_buffert-capacity-wait-free--isr-safe)
7. [`fsm::static_ring_buffer<T, Capacity>` (Embedded Zero-Alloc)](#7-fsmstatic_ring_buffert-capacity-embedded-zero-alloc)
8. [Compile-Time Reflection & Type Traits](#8-compile-time-reflection--type-traits)

---

## 1. `fsm::fsm<Table, Context, InitialState>` (Synchronous Engine)

The synchronous, zero-overhead compile-time finite state machine engine.

### Header
```cpp
#include "fsm/fsm.hpp" // or your generated standalone header
```

### Key Characteristics
- **Zero Heap Allocations**: All state storage uses `std::variant<States...>`. Transitions operate entirely on the stack.
- **Sub-Nanosecond Latency**: Transitions compile down to unrolled template folds with zero virtual functions.
- **Context Injection**: Optional hardware/software context struct passed by reference to state machine constructors, guards, and actions.

### Member Functions

#### `dispatch(const Event& event) -> dispatch_result`
Dispatches an event synchronously on the calling thread.

- **Returns**: A `dispatch_result` representing whether the transition fired (`success`), was deferred (`deferred`), rejected by a guard (`guard_rejected`), or had no matching transition (`unhandled`).
- **Complexity**: $O(1)$ to $O(N)$ compile-time unrolling where $N$ is the number of transitions matching the active state.

```cpp
auto res = fsm.dispatch(StartMissionCmd{});
if (res.is_success()) {
    std::cout << "Transition executed successfully.\n";
}
```

#### `is_in_state<State>() const -> bool`
Checks at compile time whether the machine is currently in the specified state type.

```cpp
if (fsm.is_in_state<Cruising>()) {
    std::cout << "Spacecraft is currently in Cruising state.\n";
}
```

#### `current_state_name() const -> std::string_view`
Returns the human-readable string name of the currently active state.

```cpp
std::cout << "Active state: " << fsm.current_state_name() << "\n";
```

#### `context() -> Context&` / `context() const -> const Context&`
Direct access to the underlying context instance.

> [!WARNING]
> Direct `context()` access is non-synchronized. When using `thread_safe_fsm`, prefer `with_context()` for thread-safe access under lock.

#### `set_observer(observer_type observer)` / `clear_observer()`
Attaches a transition observer callback invoked immediately whenever a transition occurs.

```cpp
fsm.set_observer([](const fsm::transition_info& info) {
    std::cout << "[TRANSITION] " << info.source 
              << " --(" << info.event << ")--> " 
              << info.target
              << (info.is_internal ? " [INTERNAL]" : "") << "\n";
});
```

The `fsm::transition_info` struct contains:
- `source`: `std::string_view` name of the source state.
- `target`: `std::string_view` name of the target state.
- `event`: `std::string_view` name of the triggering event.
- `is_internal`: `bool` indicating if the transition was internal.

---

## 2. `fsm::dispatch_result` & `fsm::dispatch_status`

Every event dispatch returns a `fsm::dispatch_result` carrying rich status information:

```cpp
enum class dispatch_status : std::uint8_t {
    success,        // Transition executed successfully
    deferred,       // Event was postponed via deferred_events directive
    guard_rejected, // Matching transition found, but guard evaluated to false
    unhandled       // No matching transition defined for current state
};
```

### Methods
- `is_success() const noexcept -> bool`: Returns `true` if `status == dispatch_status::success`.
- `is_deferred() const noexcept -> bool`: Returns `true` if `status == dispatch_status::deferred`.
- `is_guard_rejected() const noexcept -> bool`: Returns `true` if `status == dispatch_status::guard_rejected`.
- `is_unhandled() const noexcept -> bool`: Returns `true` if `status == dispatch_status::unhandled`.
- `is_ok() const noexcept -> bool`: Returns `true` if `is_success() || is_deferred()`.
- `explicit operator bool() const noexcept`: Implicitly convertible to `bool` (`is_ok()`).
- `to_string() const noexcept -> std::string_view`: Returns `"success"`, `"deferred"`, `"guard_rejected"`, or `"unhandled"`.

---

## 3. `fsm::thread_safe_fsm<Table, Context, InitialState>` (Thread-Safe Engine)

An asynchronous, thread-safe wrapper around `fsm::fsm` providing queue-based execution, background worker threads, synchronized context access, and fine-grained error handlers.

### Header
```cpp
#include "fsm/thread_safe_fsm.hpp" // or your generated standalone header
```

### Core Execution Modes

`thread_safe_fsm` supports two complementary execution models:
1. **Background Worker Mode**: Events are queued and processed asynchronously by a dedicated worker thread (`post`, `post_async`).
2. **Manual Polling Mode**: Events are enqueued without spawning threads (`enqueue`) and drained deterministically by the main/game loop (`process_one`, `process_all`).

---

### Synchronous Thread-Safe Dispatch

#### `send(const Event& event) -> dispatch_result`
Executes a thread-safe synchronous transition on the calling thread:
- Acquires `dispatch_mutex_` exclusively during state evaluation and action execution.
- Captures transition info and observer notifications in an isolated snapshot.
- Invokes observers outside the lock to minimize contention.
- If an action throws, the exception is recorded in `last_exception()`, forwarded to the registered `exception_handler`, and rethrown to the caller.

```cpp
auto res = async_fsm.send(EmergencyStopCmd{});
```

---

### Asynchronous Worker Mode

#### `post(Event event)`
Asynchronous fire-and-forget event injection. Automatically ensures the background worker is running.
If an action throws, the exception is caught, recorded in `last_exception()`, and passed to `exception_handler` without crashing the worker thread.

```cpp
async_fsm.post(SensorTelemetryEvent{temp, pressure});
```

#### `post(Event event, Callback&& on_complete)`
Enqueues an event and executes `on_complete(const dispatch_result&)` outside the lock upon transition completion.

```cpp
async_fsm.post(ConnectCmd{}, [](const fsm::dispatch_result& res) {
    std::cout << "Connect finished: " << res.to_string() << "\n";
});
```

#### `post_async(Event event) -> std::future<dispatch_result>`
Enqueues an event and returns a `std::future<dispatch_result>`. Automatically starts the worker so `future.get()` never deadlocks.

```cpp
auto fut = async_fsm.post_async(CalibrateSensorsCmd{});
auto result = fut.get(); // Blocks until worker processes the event
```

#### `post_delayed(Event event, Duration delay)`
Schedules an event to be dispatched after `delay` has elapsed (e.g. `std::chrono::milliseconds(500)`).

```cpp
async_fsm.post_delayed(HeartbeatTimeoutEvent{}, std::chrono::seconds(2));
```

---

### Manual Polling Mode (Single-Consumer)

#### `enqueue(Event event)`
Thread-safely pushes an event into the internal queue **without** auto-starting the worker thread. Rejects new external events if shutdown is in progress.

```cpp
manual_fsm.enqueue(UserInputEvent{key});
```

#### `process_one() -> bool`
Processes a single pending event from the front of the queue in $O(1)$ constant time (backed by `std::deque`).

- **Contract**: Single-Consumer Polling Contract (called from the main loop).
- **Return**: `true` if an event was processed; `false` if queue was empty, background worker was running, or polling was contested.

```cpp
while (manual_fsm.process_one()) {
    // Process step-by-step
}
```

#### `process_all() -> std::size_t`
Drains all pending events in the queue, including any cascading self-posted events queued by transition actions.

- **Return**: Total number of events processed.

```cpp
std::size_t processed = manual_fsm.process_all();
```

---

### Thread-Safe Context Access

#### `with_context(Callable&& callable)`
Executes `callable(Context&)` under the protection of `dispatch_mutex_`, ensuring serialized, race-free context mutations.

```cpp
async_fsm.with_context([](NetworkContext& ctx) {
    ctx.retry_count = 0;
    ctx.auth_token = "Bearer XYZ";
});
```

---

### Lifecycle & Shutdown Contract

```cpp
// 1. Explicitly start background worker
async_fsm.start_worker();

// 2. Non-blocking asynchronous shutdown request (safe from any thread, including worker actions)
async_fsm.request_stop();

// 3. Synchronous join and complete event drain (called from owning managing thread)
async_fsm.stop_worker();
```

> [!NOTE]
> **Destruction Ownership Policy**: `thread_safe_fsm` must be owned and destroyed by an external managing thread. If an action running on the worker wishes to terminate the FSM, it calls `request_stop()`. Upon destruction (`~thread_safe_fsm()`), `stop_worker()` automatically joins the thread and drains all remaining events safely before purging queues.

---

### Configurable Error & Dispatch Handlers

All handlers are updated atomically under lock. Any in-flight dispatch retains its pre-dispatch handler snapshot; new configurations take effect for subsequent dispatches:

```cpp
// 1. Unhandled event notification
async_fsm.set_unhandled_handler([](std::string_view event, std::string_view state) {
    std::cerr << "[UNHANDLED] Event '" << event << "' ignored in state '" << state << "'\n";
});

// 2. Guard rejection notification
async_fsm.set_guard_rejected_handler([](std::string_view event, std::string_view state) {
    std::cerr << "[GUARD REJECTED] Event '" << event << "' blocked in state '" << state << "'\n";
});

// 3. Deferred event notification
async_fsm.set_deferred_handler([](std::string_view event, std::string_view state) {
    std::cout << "[DEFERRED] Event '" << event << "' postponed in state '" << state << "'\n";
});

// 4. General dispatch failure hook
async_fsm.set_dispatch_failure_handler([](std::string_view evt, std::string_view state, fsm::dispatch_status status) {
    std::cerr << "[FAILURE] Event '" << evt << "' failed with status: " << fsm::to_string(status) << "\n";
});

// 5. Exception handling & last exception retrieval
async_fsm.set_exception_handler([](std::exception_ptr ex) {
    try {
        if (ex) std::rethrow_exception(ex);
    } catch (const std::exception& e) {
        std::cerr << "[EXCEPTION] Action error: " << e.what() << "\n";
    }
});
```

---

## 4. Composite Boolean Guards (`and_`, `or_`, `not_`)

`fsmc` supports composable compile-time boolean predicate combinators with recursive short-circuit evaluation:

```cpp
#include "fsm/composite_guards.hpp"

// Conjunction: evaluates G1 && G2 && ...
using GuardA = fsm::and_<PowerOkGuard, NetworkAvailableGuard>;

// Disjunction: evaluates G1 || G2 || ...
using GuardB = fsm::or_<ManualOverrideGuard, SafetyClearanceGuard>;

// Inversion: evaluates !G
using GuardC = fsm::not_<FaultActiveGuard>;

// Nested composite expression: [PowerOk && (!Fault || Override)]
using ComplexGuard = fsm::and_<
    PowerOkGuard,
    fsm::or_<fsm::not_<FaultActiveGuard>, ManualOverrideGuard>
>;
```

---

## 5. Deferred Events & History Resolution

### Deferred Events
States configured with `deferred_events: [EventA, EventB]` postpone matching events instead of dropping them. When transitioning to a new active state, deferred events are systematically replayed in FIFO order:

```cpp
std::size_t count = fsm.deferred_count();
fsm.clear_deferred_events();
```

### History Pseudostates
- **Shallow History (`[H]`)**: Restores the most recently active direct sub-state of a composite state.
- **Deep History (`[H*]`)**: Recursively restores the entire active sub-state hierarchy down to leaf states.

---

## 6. `fsm::spsc_ring_buffer<T, Capacity>` (Wait-Free & ISR-Safe)

A lock-free, wait-free Single-Producer Single-Consumer circular queue designed for hard real-time systems and hardware **Interrupt Service Routines (ISR)**:

### Guarantees
- **Wait-Free Operations**: Both `push` and `pop` execute in $O(1)$ constant time with zero locks and zero system calls.
- **Cacheline Aligned**: Head and Tail atomic indices reside on distinct 64-byte cache lines (`alignas(64)`) to completely eliminate false sharing.
- **Zero Dynamic Allocation**: Fixed contiguous ring storage.

```cpp
#include "fsm/spsc_ring_buffer.hpp"

// Capacity must be a power of 2
fsm::spsc_ring_buffer<SensorReadingEvent, 1024> isr_event_queue;

// Producer (Hardware ISR / Interrupt context):
extern "C" void USART1_IRQHandler() {
    SensorReadingEvent event{read_uart_register()};
    isr_event_queue.push(event); // Wait-free, never blocks
}

// Consumer (Main Thread / Task):
void update_loop() {
    SensorReadingEvent event;
    while (isr_event_queue.pop(event)) {
        fsm.dispatch(event);
    }
}
```

---

## 7. `fsm::static_ring_buffer<T, Capacity>` (Embedded Zero-Alloc)

A deterministic circular buffer for microcontrollers requiring a static memory footprint without atomic synchronization overhead:

```cpp
#include "fsm/static_ring_buffer.hpp"

fsm::static_ring_buffer<EventVariant, 32> static_queue;
static_queue.push(TickEvent{});
auto event = static_queue.pop();
```

---

## 8. Compile-Time Reflection & Type Traits

```cpp
#include "fsm/type_traits.hpp"

// Extract compile-time demangled event name
std::string_view name = fsm::event_name<StartCmd>(); // "StartCmd"

// C++20 Context Concept Verification
template <typename T>
concept ContextConcept = !std::is_reference_v<T>;
```
