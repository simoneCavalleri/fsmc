# Guards & Action Effects

Guards determine whether a transition may fire, while actions perform side-effects on hardware or mutate the EFSM shared context.

---

## 1. Pure Guards & Boolean Algebra

Guards in `fsmc` can be combined with boolean operators:

```cpp
// Compile-time composite boolean guard
using SafetyCheck = fsm::and_<HasGpsLock, fsm::not_<BatteryLow>>;
```

During middle-end optimization, the `GuardSimplificationPass` applies bottom-up boolean algebra reduction:
- $\neg(\neg A) \equiv A$
- $A \land \text{true} \equiv A$
- $A \land \text{false} \equiv \text{false}$

---

## 2. Automatic Resolved EFSM Guards

When referencing variables in SysML v2 / formal models (e.g. `batterySoC > 30.0 and not criticalError`), the C++ code generator automatically emits the resolved return expression:

```cpp
struct batterySoC_gt_30 {
    [[nodiscard]] constexpr bool operator()(const auto& /*evt*/, const auto& /*state*/, const auto& ctx) const noexcept {
        return ctx.batterySoC > 30.0f && !ctx.criticalError;
    }
};
```
