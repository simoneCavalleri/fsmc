# Frequently Asked Questions (FAQ) & Troubleshooting

This reference answers common architectural questions, explains compiler diagnostics, and provides a quick troubleshooting checklist for `fsmc`.

---

## 1. Troubleshooting & Diagnostics Checklist

| Symptom / Diagnostic | Probable Cause | Recommended Fix |
| :--- | :--- | :--- |
| **`no matching function for call to 'operator()'` in Guard** | Guard's `operator()` is not marked `const`. | Add `const noexcept` to the guard struct's `operator()` signature. |
| **`binding reference to type 'InPorts' discards qualifiers`** | Action or Guard attempts to accept `InPorts&` as non-const. | Change parameter to `const InPorts&`. Input ports are strictly immutable. |
| **`step()` returns `step_status::steady` unexpectedly** | No continuous anonymous transitions (`fsm::anonymous_event`) originated from the active state, or guard evaluated to `false`. | Use `sm.dispatch(Event{})` for discrete events, or ensure continuous guards are satisfied. |
| **Warning `W0301: Overlapping Guard Intervals`** | Multiple transitions leave the same state on the same event with overlapping conditions. | Make guard intervals disjoint or assign explicit priorities (e.g. `priority: 1`). |
| **Linker error: missing symbols** | `fsmc` headers not included or generated FSM file not linked. | Ensure `fsmc_target_sources(your_target ...)` is in `CMakeLists.txt` or `#include "fsm/backend/cpp/runtime/fsm.hpp"`. |

---

## 2. Frequently Asked Questions (FAQ)

### Q1: Why does the compiler reject my Guard or Action signature?

**Cause**: The compiler uses template metaprogramming concepts to automatically inject only the parameters your functor requests.

Common mistakes:

1. **Guard `operator()` is non-const**:
   ```cpp
   // ❌ WRONG: Non-const operator
   struct OverTemp { bool operator()(const MotorInPorts& in) { return in.temp > 80.0f; } };

   //  CORRECT: Const operator
   struct OverTemp { bool operator()(const MotorInPorts& in) const noexcept { return in.temp > 80.0f; } };
   ```

2. **Action takes `InPorts` as mutable**:
   ```cpp
   // ❌ WRONG: Mutable input port
   struct BadAction { void operator()(MotorInPorts& in) const {} };

   //  CORRECT: Const input port and mutable output port
   struct GoodAction { void operator()(const MotorInPorts& in, MotorOutPorts& out) const noexcept {} };
   ```

---

### Q2: Is `fsmc` compatible with `-fno-exceptions` and `-fno-rtti`?

**Yes, 100% natively.**

- `fsmc` does not use `throw`, `catch`, or `try`. All transition outcomes are returned deterministically via `dispatch_result` and `step_result`.
- State identification uses `constexpr std::string_view name` and compile-time type indices, eliminating all runtime RTTI (`typeid` / `dynamic_cast`).

---

### Q3: Why does `step()` not process my discrete event?

**Cause**: `step()` and `dispatch()` serve two different execution domains:

- **`dispatch(Event{})`**: Evaluates discrete event triggers on the caller stack.
- **`step([dt], in, out)`**: Evaluates continuous anonymous transitions (clock ticks / sensor thresholds / `in_state_for<N>` dwell).

To fire an event, call `sm.dispatch(Event{})`.

---

### Q4: Can I use C++ Lambdas for Guards and Actions?

`fsm::row` is a compile-time type list requiring named functor types (structs). This guarantees:

1. **Zero runtime overhead**: Inlines 100% into direct branch instructions with 0 indirect function pointers.
2. **SMT / Formal Verifiability**: Enables the compiler to analyze and verify guard satisfiability formally in Z3 and nuXmv.

To define custom logic, write a lightweight 1-line struct:

```cpp
struct IsArmedGuard { bool operator()(const InPorts& in) const noexcept { return in.armed; } };
```

---

### Q5: How do Timers work when state changes before timeout?

In asynchronous execution ([`fsm::thread_safe_fsm`](thread_safe_fsm.md)), **`post_state_timeout(event, duration)`** automatically captures the active state at registration time:

- If an external event causes the state machine to transition before the timer expires, the obsolete timeout is **silently discarded with zero side-effects** when popped from the priority queue.

---

### Q6: In a Pure Logic / Stateless state machine, do I ever need to call `step()`?

**In 99% of cases, NO.**

A pure logic or stateless state machine (e.g. protocol parser, UI navigation, turn-based game loop) is purely event-driven: it only reacts to explicit events via `sm.dispatch(Event{})`.

Calling `step()` is only necessary in the rare case where your model defines **spontaneous / anonymous sequence transitions** (transitions without an `on Event` trigger) or discrete cycle dwell timers (`in_state_for<Ticks>`).
