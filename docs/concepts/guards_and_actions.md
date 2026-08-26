# Guards and Action Effects

Guards and actions form the computational layer of Extended Finite State Machines (EFSM). Guards evaluate predicates over events and context variables, while actions execute side effects and mutate context state.

---

## 1. Pure Guard Predicates

Guards are side-effect-free boolean functions. In `fsmc`, guards receive references to the triggering event, the active state, and the user context struct:

```cpp
struct BatterySufficientGuard {
    template <typename Event, typename State, typename Context>
    [[nodiscard]] constexpr bool operator()(const Event& /*evt*/, const State& /*state*/, const Context& ctx) const noexcept {
        return ctx.batterySoC >= 25.0f;
    }
};
```

---

## 2. Compile-Time Boolean Algebra & Composite Guards

Guards can be logically combined at compile time using `fsm::and_`, `fsm::or_`, and `fsm::not_`:

```cpp
using SafeToLaunch = fsm::and_<
    HasGpsLockGuard,
    fsm::and_<BatterySufficientGuard, fsm::not_<CriticalErrorGuard>>
>;
```

During compilation, the `GuardSimplificationPass` analyzes and reduces composite boolean guard trees:

- Double negation elimination: `not(not A) => A`
- Neutral elements: `A and true => A`, `A or false => A`
- Dominant elements: `A and false => false`, `A or true => true`

---

## 3. Automatic Resolved EFSM Expressions

In formal models (SysML v2, Cameo, SCXML), guard expressions such as `[batteryLevel >= 20.0 and isGpsLocked]` and actions such as `do waypointIndex += 1;` are automatically parsed, type-checked, and emitted directly in C++ without requiring manual stub implementations:

```cpp
// Automatically emitted resolved guard in generated C++ header
struct Guard_batteryLevel_gte_20 {
    template <typename Event, typename State, typename Context>
    [[nodiscard]] constexpr bool operator()(const Event& /*evt*/, const State& /*state*/, const Context& ctx) const noexcept {
        return ctx.batteryLevel >= 20.0 && ctx.isGpsLocked;
    }
};

// Automatically emitted resolved action in generated C++ header
struct Action_increment_waypoint {
    template <typename Event, typename SrcState, typename DstState, typename Context>
    constexpr void operator()(const Event& /*evt*/, SrcState& /*src*/, DstState& /*dst*/, Context& ctx) const noexcept {
        ctx.waypointIndex += 1;
    }
};
```

---

## 4. Choice and Junction Inlining (`ChoiceInliningPass`)

When models contain intermediate decision points (`<<choice>>` or `<<junction>>`), the middle-end optimizer inlines the decision trees directly into flat composite transitions.

For example, a transition path `StateA -> ChoiceNode -> StateB` with incoming guard `G1` and outgoing guard `G2` is transformed into a direct edge `StateA -> StateB` guarded by `fsm::and_<G1, G2>` and executing the combined actions `A1` and `A2`. This eliminates intermediate state allocations and enables direct branch resolution.

