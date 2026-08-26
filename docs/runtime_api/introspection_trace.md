# Transition Trace & Telemetry

`fsmc` provides rich, non-intrusive transition trace telemetry and observer callbacks without memory allocation or runtime performance penalties.

---

## 1. `fsm::dispatch_result` & `fsm::transition_trace`

Every call to `dispatch()` returns an `fsm::dispatch_result` carrying status information and an optional `transition_trace`:

```cpp
namespace fsm {

enum class dispatch_status : std::uint8_t {
    success,        // Transition executed successfully
    deferred,       // Event was postponed via deferred_events directive
    guard_rejected, // Matching transition found, but guard evaluated to false
    unhandled       // No matching transition defined for current state
};

enum class transition_kind : std::uint8_t {
    external,
    internal,
    local
};

struct transition_trace {
    std::string_view source{};
    std::string_view target{};
    std::string_view event{};
    std::string_view guard{};
    std::string_view action{};
    transition_kind kind{transition_kind::external};
};

struct dispatch_result {
    dispatch_status status;
    std::optional<transition_trace> trace;

    [[nodiscard]] constexpr bool is_success() const noexcept;
    [[nodiscard]] constexpr bool is_deferred() const noexcept;
    [[nodiscard]] constexpr bool is_guard_rejected() const noexcept;
    [[nodiscard]] constexpr bool is_unhandled() const noexcept;
    [[nodiscard]] constexpr bool is_ok() const noexcept;
    [[nodiscard]] constexpr std::string_view to_string() const noexcept;
};

} // namespace fsm
```

---

## 2. Using `transition_trace` for Black-Box Flight Recording

Because `std::string_view` literals point to statically allocated strings compiled into the binary, inspecting `trace` performs zero string allocations:

```cpp
auto res = fsm.dispatch(TakeoffCmd{});

if (res.trace.has_value()) {
    std::cout << "[FLIGHT RECORDER] Fired:\n"
              << "  Source: " << res.trace->source << "\n"
              << "  Target: " << res.trace->target << "\n"
              << "  Event:  " << res.trace->event  << "\n"
              << "  Guard:  " << res.trace->guard  << "\n"
              << "  Action: " << res.trace->action << "\n";
} else if (res.is_guard_rejected()) {
    std::cerr << "[WARNING] Takeoff rejected by guard: " 
              << (res.trace ? res.trace->guard : "Unknown") << "\n";
}
```

---

## 3. Transition Observers

You can also attach custom observers to monitor transitions globally:

```cpp
// Static or lambda observer
fsm.set_observer([](const fsm::transition_info& info) {
    SendCanBusLog(info.source, info.target, info.event);
});
```
