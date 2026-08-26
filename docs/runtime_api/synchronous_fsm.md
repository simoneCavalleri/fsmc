# `fsm::fsm` (Synchronous Core Engine)

`fsm::fsm<TransitionTable, Context, InitialState, Observer>` is the foundation of `fsmc`'s C++ runtime. It is a synchronous, zero-heap, compile-time unrolled state machine engine.

---

## Class Template Synopsis

```cpp
namespace fsm {

template <
    typename Table,
    typename Context = no_context,
    typename InitialState = typename Table::initial_state,
    typename Observer = null_observer
>
class fsm {
public:
    // Constructors
    constexpr fsm() noexcept;
    constexpr explicit fsm(Context& ctx) noexcept;

    // Event Dispatch
    template <typename Event>
    constexpr dispatch_result dispatch(const Event& event);

    // State Queries
    template <typename State>
    [[nodiscard]] constexpr bool is_in_state() const noexcept;

    [[nodiscard]] constexpr std::size_t state_index() const noexcept;
    [[nodiscard]] constexpr std::string_view current_state_name() const noexcept;

    // Context Access
    [[nodiscard]] constexpr Context& context() noexcept;
    [[nodiscard]] constexpr const Context& context() const noexcept;

    // Observer Attachment
    void set_observer(Observer observer);
};

} // namespace fsm
```

---

## Template Metaprogramming Architecture

Instead of dynamic polymorphic dispatch or nested runtime switch-case tables:
1. The transition table is represented as a compile-time type list:
   ```cpp
   using Table = fsm::transition_table<
       fsm::row<Idle, StartCmd, Running>::when<HasGpsLock>::then<ArmMotors>,
       fsm::row<Running, StopCmd, Idle>::then<DisarmMotors>
   >;
   ```
2. When `dispatch(event)` is called, the compiler expands a C++17 fold expression matching the active state index in `std::variant<States...>` against valid rows in the table.
3. Inactive rows are eliminated at compile time by the optimizer, generating direct inlined branch instructions.

---

## Example Usage

```cpp
#include "uav_mission_fsm.hpp"
#include <iostream>

int main() {
    avionics::UavMissionFSMContext ctx;
    ctx.batteryLevel = 100.0;

    avionics::UavMissionFSM fsm(ctx);

    // Register transition observer hook
    fsm.set_observer([](const fsm::transition_info& info) {
        std::cout << "[OBSERVER] Fired: " << info.source 
                  << " --(" << info.event << ")--> " 
                  << info.target << "\n";
    });

    auto res = fsm.dispatch(avionics::CalibrationOk{});
    if (res.is_success()) {
        std::cout << "Successfully entered: " << fsm.current_state_name() << "\n";
    }

    return 0;
}
```
