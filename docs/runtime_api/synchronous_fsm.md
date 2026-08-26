# `fsm::fsm` (Synchronous Core Engine)

`fsm::fsm<TransitionTable, Context, InitialState, Observer>` is the core synchronous execution engine of `fsmc`. It provides deterministic, zero-heap, compile-time unrolled finite state machine execution.

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
    using table_type    = Table;
    using context_type  = Context;
    using state_type    = InitialState;
    using observer_type = Observer;

    // Constructors
    constexpr fsm() noexcept;
    constexpr explicit fsm(Context& ctx) noexcept;
    constexpr fsm(Context& ctx, Observer obs) noexcept;

    // Event Dispatch
    template <typename Event>
    constexpr dispatch_result dispatch(const Event& event);


    template <typename Event>
    constexpr dispatch_result dispatch(Event&& event);

    // State Inspection
    template <typename State>
    [[nodiscard]] constexpr bool is_in_state() const noexcept;

    [[nodiscard]] constexpr std::size_t state_index() const noexcept;
    [[nodiscard]] constexpr std::string_view current_state_name() const noexcept;

    // Context Accessors
    [[nodiscard]] constexpr Context& context() noexcept;
    [[nodiscard]] constexpr const Context& context() const noexcept;

    // Observer Attachment
    void set_observer(Observer observer) noexcept;
    [[nodiscard]] constexpr const Observer& observer() const noexcept;
};

} // namespace fsm
```

---

## Lifecycle Hooks & Hook Detection

`fsmc` uses compile-time SFINAE duck-typing (`hook_traits.hpp`) to invoke state lifecycle callbacks only when they are implemented. You never need to inherit from base classes or implement dummy methods.

### 1. State Entry Hooks (`on_entry`)
Invoked immediately upon entering a state.
```cpp
struct SystemReady {
    // Signature option 1: Context + Event
    template <typename Event>
    void on_entry(MyContext& ctx, const Event& ev) {
        ctx.log_entry("SystemReady");
    }

    // Signature option 2: Context only
    void on_entry(MyContext& ctx) {
        ctx.power_on_indicators();
    }

    // Signature option 3: No parameters
    void on_entry() {
        hardware_gpio_high(PIN_LED_GREEN);
    }
};
```

### 2. State Exit Hooks (`on_exit`)
Invoked immediately prior to leaving a state.
```cpp
struct ArmingMotors {
    void on_exit(MyContext& ctx) {
        ctx.reset_arming_timer();
    }
};
```

### 3. Transition Guards (`guard`)
Guards are boolean predicates that determine if a candidate transition is legally allowed to execute. If a guard returns `false`, the engine evaluates subsequent matching rows or marks the event as `guard_rejected`.
```cpp
// Struct Functor Guard
struct HasSufficientBattery {
    bool operator()(const MyContext& ctx, const TakeoffCmd& cmd) const noexcept {
        return ctx.battery_percent >= 30 && cmd.minimum_alt > 0;
    }
};

// Or free function / lambda
constexpr auto is_safe_altitude = [](const MyContext& ctx, const auto&) noexcept {
    return ctx.altitude_m >= 10;
};
```

### 4. Transition Actions (`action`)
Side effects executed strictly when the transition fires, after the source state's `on_exit` and before the target state's `on_entry`.
```cpp
struct LogAndArmActuators {
    void operator()(MyContext& ctx, const TakeoffCmd& cmd) const noexcept {
        ctx.arming_timestamp = get_hardware_microseconds();
        actuator_driver_enable();
    }
};
```

---

## Hierarchical HFSM & Advanced Features

### 1. Nested Hierarchical States & Exit/Entry Cascades
When transitioning between states in different hierarchy levels, `fsm::fsm` executes the Least Common Ancestor (LCA) exit/entry sequence:

1. Exit source sub-state (`on_exit`).
2. Exit source parent state (`on_exit`).
3. Execute transition `action`.
4. Enter target parent state (`on_entry`).
5. Enter target sub-state (`on_entry`).

### 2. Shallow History (`[H]`) and Deep History (`[H*]`)
- **Shallow History `[H]`**: Remembers the most recently active direct sub-state of a composite state.
- **Deep History `[H*]`**: Recursively restores the entire nested leaf state configuration across multi-level hierarchies.

### 3. Internal Transitions (`transition_kind::internal`)
Executes an action without exiting or re-entering the current state (`on_exit` and `on_entry` are bypassed).
```cpp
// Internal transition on telemetry ping: keeps state active, updates heartbeat counter
fsm::internal_row<Navigating, TelemetryPing>::then<UpdateHeartbeatAction>
```

### 4. Choice Pseudo-States
Enforces conditional dynamic branching where multiple exit transitions from a choice point are evaluated in strict priority order against resolved guards.

---

## Zero-Overhead & Memory Model Proof

### Memory Layout
For an FSM without observers, the memory footprint of `fsm::fsm` is strictly:
```
sizeof(fsm) = sizeof(Context*) + sizeof(state_id_t)
```
On a 32-bit microcontroller (ARM Cortex-M), this is only **5 to 8 bytes**.


### Assembly Optimization
Because transition matching is performed via compile-time fold expressions over typed rows, modern compilers (GCC 11+, Clang 13+, MSVC 19.29+) eliminate all intermediate templates and optimize the dispatch into a single direct jump table:

```nasm
; GCC -O3 disassembly for fsm.dispatch(TakeoffCmd{})
dispatch_takeoff:
    movzx   eax, BYTE PTR [rdi+8]   ; Load active state index
    cmp     al, 2                   ; Check if in Preflight state
    jne     .L_unhandled            ; Direct branch if not matching
    mov     rax, QWORD PTR [rdi]    ; Load Context pointer
    cmp     DWORD PTR [rax+4], 30   ; Guard check: ctx.battery_percent >= 30
    jl      .L_guard_rejected
    ; Execute Action inline
    mov     DWORD PTR [rax+12], 1   ; ctx.motors_armed = true
    ; Transition to InFlight (State ID = 3)
    mov     BYTE PTR [rdi+8], 3     ; Update active state index
    mov     eax, 1                  ; return dispatch_status::success
    ret
```

---

## Complete Working Example

```cpp
#include "uav_fsm.hpp"
#include <iostream>

struct UavContext {
    int battery_percent{100};
    int altitude_m{0};
    bool armed{false};
};

int main() {
    UavContext ctx;
    fsm::fsm<AutonomousUavMissionTable, UavContext, Preflight> uav(ctx);

    // Initial state check
    std::cout << "Initial: " << uav.current_state_name() << "\n";

    // Attach non-intrusive trace observer
    uav.set_observer([](const fsm::transition_info& info) {
        std::cout << "[TRACE] " << info.source << " -> " << info.target 
                  << " (Event: " << info.event << ", Status: " 
                  << to_string(info.status) << ")\n";
    });

    // 1. Dispatch calibration event
    auto r1 = uav.dispatch(CalibrationOk{});
    if (r1.is_success()) {
        std::cout << "Calibrated. Active state: " << uav.current_state_name() << "\n";
    }

    // 2. Dispatch takeoff command
    auto r2 = uav.dispatch(TakeoffCmd{});
    if (r2.is_success()) {
        std::cout << "In Flight. Motors Armed: " << std::boolalpha << ctx.armed << "\n";
    }

    // 3. Inspect transition trace metadata
    if (r2.trace.has_value()) {
        std::cout << "Fired transition from " << r2.trace->source 
                  << " to " << r2.trace->target << "\n";
    }

    return 0;
}
```
