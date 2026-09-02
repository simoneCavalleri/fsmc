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

## 2. Zero-Allocation Flight Recorder Telemetry

Because all `std::string_view` literals in `transition_trace` reference static string data in the compiler's read-only data section (`.rodata`), inspecting and serializing traces performs **zero string heap allocations**:

```cpp
auto res = fsm.dispatch(TakeoffCmd{});

if (res.is_success() && res.trace.has_value()) {
    std::cout << "[FLIGHT RECORDER] Transition Fired:\n"
              << "  Source: " << res.trace->source << "\n"
              << "  Target: " << res.trace->target << "\n"
              << "  Event:  " << res.trace->event  << "\n"
              << "  Guard:  " << res.trace->guard  << "\n"
              << "  Action: " << res.trace->action << "\n"
              << "  Kind:   " << to_string(res.trace->kind) << "\n";
} else if (res.is_guard_rejected()) {
    std::cerr << "[GUARD REJECTED] Takeoff rejected by guard: " 
              << (res.trace ? res.trace->guard : "Unknown") << "\n";
} else if (res.is_unhandled()) {
    std::cerr << "[UNHANDLED] Event not accepted in current state\n";
}
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

Attach an observer to the FSM instance:

```cpp
fsm.set_observer([](const fsm::transition_info& info) {
    if (info.is_success()) {
        CAN_Bus_SendLog(info.source.data(), info.target.data(), info.event.data());
    }
});
```

---

## 4. Deterministic Tick-Based Timer Manager

In hard real-time and safety-critical embedded systems, operating system background timers (`std::thread`, POSIX timers) introduce non-determinism and thread scheduling jitter. 

`fsmc` provides `fsm::deterministic_timer_manager<MaxTimers>`, an entirely synchronous, bounded, stack/BSS-allocated timer manager:

```cpp
#include "fsm/backend/cpp/runtime/deterministic_timer.hpp"

// Allocate fixed 16-timer manager (0 heap allocations)
fsm::deterministic_timer_manager<16> timers;

// 1. Schedule a one-shot or periodic timer
timers.start_timer(1001 /* timer_id */, 500 /* duration_ms */, false /* periodic */);

// 2. Advance time synchronously in your control loop tick
uint64_t delta_ms = 10;
timers.tick(delta_ms, [&](uint32_t expired_timer_id) {
    if (expired_timer_id == 1001) {
        // Dispatch timeout event into FSM
        fsm.dispatch(TimeoutEvent{});
    }
});
```
