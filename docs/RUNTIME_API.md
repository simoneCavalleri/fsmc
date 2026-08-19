# Runtime C++ API Reference

This document describes the core C++ classes, methods, and lifecycle hooks provided by **`fsmc`**.

---

## 1. `fsm::fsm<Table, Context, InitialState>`

The synchronous, zero-overhead compile-time finite state machine engine.

### Header
```cpp
#include "fsm/fsm.hpp" // or your generated standalone header
```

### Member Functions

#### `dispatch(const Event& event) -> bool`
Dispatches an event synchronously on the calling thread.

- **Returns**: `true` if a matching transition was found and executed; `false` if no transition fired (event ignored or guard returned `false`).
- **Complexity**: $O(N)$ compile-time template unrolling where $N$ is the number of transitions matching the event/state pair (compiles to direct jump table or inlined branch).
- **Allocations**: Zero heap allocations.

```cpp
bool handled = fsm.dispatch(StartMissionCmd{});
```

#### `is_in_state<State>() const -> bool`
Checks if the state machine is currently in the specified state type at compile time.

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
Accesses the injected user context instance.

```cpp
fsm.context().battery_percentage = 98;
```

#### `set_observer(observer_type observer)` / `clear_observer()`
Attaches a callback / functor hook that is notified immediately whenever a state transition occurs (both external and internal).

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
- `is_internal`: `bool` indicating if the transition was an internal transition.

---

## 2. `fsm::thread_safe_fsm<Table, Context, InitialState>`

An asynchronous, thread-safe wrapper around `fsm::fsm` backed by an internal condition-variable queue and a background worker thread.

### Header
```cpp
#include "fsm/thread_safe_fsm.hpp" // or your generated standalone header
```

### Member Functions

#### `start_worker()`
Spawns the background worker thread (using `std::jthread` in C++20 with cooperative `std::stop_token` cancellation, or `std::thread` in C++17) and begins processing events from the queue.

```cpp
async_fsm.start_worker();
```

#### `stop_worker()`
Signals the background worker thread to stop, waits for pending queue events to flush, and joins the thread.

```cpp
async_fsm.stop_worker();
```

#### `post(Event&& event)` / `post(const Event& event)`
Thread-safely pushes an event into the queue and notifies the worker thread.

```cpp
async_fsm.post(TelemetryPingEvent{});
```

#### `set_observer(observer_type observer)` / `clear_observer()`
Thread-safely attaches a transition observer hook to the asynchronous state machine under mutex lock.

```cpp
async_fsm.set_observer([](const fsm::transition_info& info) {
    std::cout << "[ASYNC TRACE] " << info.source << " -> " << info.target << "\n";
});
```

#### `with_fsm(Callable&& fn)`
Thread-safely executes a lambda or functor with exclusive access to the underlying `fsm::fsm` instance under mutex lock.

```cpp
async_fsm.with_fsm([](auto& inner_fsm) {
    std::cout << "Locked state inspect: " << inner_fsm.current_state_name() << "\n";
});
```

---

## 3. Composite Guards (`fsm::not_`, `fsm::and_`, `fsm::or_`)

`fsmc` provides compile-time combinators to compose atomic guard predicates with boolean logic:

```cpp
#include "fsm/transition.hpp"

// Inversion: !Guard
using NotEmergency = fsm::not_<IsEmergencyStop>;

// Short-circuit Conjunction: G1 && G2 && G3
using ReadyGuard = fsm::and_<IsPowerOk, IsDoorClosed, fsm::not_<IsEmergencyStop>>;

// Short-circuit Disjunction: G1 || G2
using AlertGuard = fsm::or_<IsEmergencyStop, fsm::not_<IsTempSafe>>;
```

All combinators evaluate arguments with C++ short-circuit semantics and forward arguments matching the arity of the underlying guards.

---

## 4. State Lifecycle Hooks

States can optionally define `on_enter` and `on_exit` methods. They are automatically discovered using C++20 Concepts or C++17 SFINAE:

```cpp
struct Orbiting {
    static constexpr std::string_view name = "Orbiting";

    // Called on entering Orbiting (External transitions only)
    void on_enter(const auto& evt, MissionContext& ctx) {
        std::cout << "Entering Orbiting state.\n";
    }

    // Called on leaving Orbiting (External transitions only)
    void on_exit(const auto& evt, MissionContext& ctx) {
        std::cout << "Leaving Orbiting state.\n";
    }
};
```

---

## 5. Fluent DSL Transition Tables

When constructing transition tables programmatically:

```cpp
using MyTable = fsm::transition_table<
    // External Transition with Composite Guard
    fsm::row<Idle, StartCmd, Running>::when<fsm::and_<HasPowerGuard, InverterReadyGuard>>,

    // Internal Transition (Zero exit/entry overhead)
    fsm::internal_row<Running, PingEvent, fsm::no_guard, LogTelemetryAction>,

    // Guarded Branch with Action
    fsm::row<Running, StopCmd, Idle, fsm::no_guard, StopMotorsAction>
>;
```
