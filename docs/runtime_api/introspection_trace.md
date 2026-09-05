# Transition Trace, Telemetry & Introspection

`fsmc` provides rich, non-intrusive transition trace telemetry, observer callbacks, and deterministic tick-based timer management with **0 bytes dynamic memory allocation** and zero performance overhead.

---

## 1. `fsm::dispatch_result` & `fsm::step_result`

Every call to `dispatch()` or `post_async()` returns an `fsm::dispatch_result`, while periodic calls to `step()` return an `fsm::step_result`. Both structs carry an optional `fsm::transition_trace`:

```cpp
namespace fsm {

enum class dispatch_status : std::uint8_t {
    success,        // Transition executed successfully
    deferred,       // Event was deferred by the active state
    guard_rejected, // Matching transition found, but guard evaluated to false
    unhandled       // No transition defined for (current_state, event)
};

enum class step_status : std::uint8_t {
    steady,         // Machine remains nominally in active state
    transitioned    // A continuous transition condition fired
};

enum class transition_kind : std::uint8_t {
    external,
    internal
};

struct transition_trace {
    std::string_view source{};
    std::string_view target{};
    std::string_view event{};
    std::string_view guard{};
    std::string_view action{};
    transition_kind kind{transition_kind::external};

    [[nodiscard]] constexpr bool is_internal() const noexcept;
    [[nodiscard]] constexpr bool is_external() const noexcept;
};

struct dispatch_result {
    dispatch_status status = dispatch_status::unhandled;
    std::optional<transition_trace> trace = std::nullopt;

    [[nodiscard]] constexpr bool is_success() const noexcept;
    [[nodiscard]] constexpr bool is_deferred() const noexcept;
    [[nodiscard]] constexpr bool is_guard_rejected() const noexcept;
    [[nodiscard]] constexpr bool is_unhandled() const noexcept;
    [[nodiscard]] constexpr bool is_ok() const noexcept; // success || deferred
    [[nodiscard]] constexpr std::string_view to_string() const noexcept;
};

struct step_result {
    step_status status = step_status::steady;
    std::optional<transition_trace> trace = std::nullopt;

    [[nodiscard]] constexpr bool has_transitioned() const noexcept;
    [[nodiscard]] constexpr bool is_steady() const noexcept;
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_transitioned(); }
    [[nodiscard]] constexpr std::string_view to_string() const noexcept;
};

} // namespace fsm
```

---

---

## 2. Zero-Allocation Flight Recorder Telemetry

For post-mortem auditing, blackbox event logging, and hard real-time diagnostics, `fsmc` provides `fsm::TraceBuffer<Capacity>` and `fsm::flight_recorder_observer<Capacity>` ([`flight_recorder.hpp`](file:///home/simone/dev/github/fsmc/include/fsm/backend/cpp/runtime/flight_recorder.hpp)).

### Architectural Properties:
* **0 Bytes Dynamic Allocation**: Storage resides entirely on the stack, in `.bss`, or within the FSM object.
* **O(1) Push and Query Time**: Constant-time circular ring buffer recording with deterministic wrap-around.
* **Chronological Logical Indexing**: `recorder[0]` always accesses the oldest recorded entry, and `recorder[size - 1]` accesses the newest, irrespective of internal circular wrap-around.

```cpp
#include <fsm/backend/cpp/runtime/fsm.hpp>
#include <fsm/backend/cpp/runtime/flight_recorder.hpp>

// 1. Configure FSM with a 64-entry Blackbox Flight Recorder
using FmsEngine = fsm::make_fsm<
    FlightTable,
    fsm::with_initial_state<PreflightState>,
    fsm::with_trace_buffer<64>,
    fsm::with_timer_capacity<8>
>;

FmsEngine sm;

// 2. Dispatch transitions and advance deterministic ticks
sm.dispatch(EvArmed{});
sm.tick(100); // Advances internal timers and updates flight recorder tick timestamp
sm.dispatch(EvTakeoff{});

// 3. Inspect recent trace entries or dump table
auto& recorder = sm.observer().recorder();

if (auto last = recorder.last_entry(); last.has_value()) {
    std::cout << "Last transition at tick " << last->tick 
              << ": " << last->source_state << " -> " << last->target_state << "\n";
}

// 4. Dump formatted ASCII flight recorder audit table
recorder.dump(std::cout);
```

### Formatted Output Dump Example:
```text
=== FSM Flight Recorder Audit Trace (2/64 entries) ===
TICK      SOURCE                   EVENT                    TARGET                   STATUS
--------------------------------------------------------------------------------------------
0         Preflight                EvArmed                  Armed                    TAKEN
100       Armed                    EvTakeoff                Ascending                TAKEN
```

---

## 3. Transition Observers

You can attach a compile-time or runtime observer callback to monitor transitions globally across the system:

```cpp
struct transition_info {
    std::string_view source;
    std::string_view target;
    std::string_view event;
    dispatch_status status = dispatch_status::success;
    transition_kind kind = transition_kind::external;
};
```

Attach a custom observer to the FSM instance:

```cpp
sm.set_observer([](const fsm::transition_info& info) {
    if (info.is_success()) {
        CAN_Bus_SendLog(info.source.data(), info.target.data(), info.event.data());
    }
});
```

---

## 4. Deterministic Tick-Based Timer Manager

In hard real-time and embedded systems, operating system background timers (`std::thread`, POSIX timers) introduce non-determinism and thread scheduling jitter. 

`fsmc` provides a fully integrated, zero-allocation synchronous timer engine configured via `fsm::with_timer_capacity<MaxTimers>`:

```cpp
using MyFsm = fsm::make_fsm<
    MyTable,
    fsm::with_initial_state<IdleState>,
    fsm::with_timer_capacity<8> // Allocate fixed 8-timer manager (0 heap allocations)
>;

MyFsm sm;

// Advance time synchronously in your periodic control loop
void periodic_task_10ms() {
    uint64_t dt_ms = 10;
    
    // 1. tick(dt) decrements active timers and fires expired timed transitions
    sm.tick(dt_ms);
    
    // 2. step() evaluates continuous conditions or during actions
    sm.step();
}
```

The underlying `fsm::deterministic_timer_manager<N>` can also be used as a standalone timer utility:

```cpp
#include "fsm/backend/cpp/runtime/deterministic_timer_manager.hpp"

fsm::deterministic_timer_manager<16> timers;
timers.start_timer(1001 /* timer_id */, 500 /* duration_ms */, false /* periodic */);

timers.tick(10, [](uint32_t expired_id) {
    std::cout << "Timer expired: " << expired_id << "\n";
});
```
