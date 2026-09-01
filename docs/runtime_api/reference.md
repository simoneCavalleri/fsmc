# Full Runtime C++ API Reference

This document provides the formal, comprehensive reference for all classes, types, traits, combinators, and containers in the `fsmc` C++ runtime library (`include/fsm/backend/cpp/runtime/`).

---

## 1. Core Engine: `fsm::fsm`

```cpp
#include "fsm/backend/cpp/runtime/fsm.hpp"
```

```cpp
template <
    typename Table,
    typename InPorts = no_ports,
    typename OutPorts = no_ports,
    typename Registers = no_registers,
    typename Services = no_services,
    typename InitialState = typename Table::initial_state,
    typename Observer = dynamic_observer
>
class fsm;
```

### Type Aliases
- `table_type`: The underlying `fsm::transition_table<Rows...>`.
- `in_ports_type`: The read-only input snapshot structure.
- `out_ports_type`: The single-assignment output structure.
- `registers_type`: The internal datapath state structure ($z^{-1}$ memory).
- `services_type`: The abstract injected environment/driver interface.
- `initial_state`: The root initial state type.
- `observer_type`: The transition observer callback type.

### Constructors
- `constexpr fsm() noexcept`: Default constructs state machine in initial state.
- `constexpr explicit fsm(const Registers& reg) noexcept`: Initializes with initial registers.
- `constexpr fsm(const Registers& reg, Services& srv) noexcept`: Initializes with registers and bound services.
- `constexpr fsm(const Registers& reg, Services& srv, Observer obs) noexcept`: Initializes with registers, services, and observer.

### Member Functions

#### Lifecycle & Execution
- `dispatch_result step(const InPorts& in, OutPorts& out, Services& srv)`: Evaluates continuous condition transitions.
- `dispatch_result step(const InPorts& in, OutPorts& out)`: Step overload omitting `Services`.
- `dispatch_result step()`: Step overload for stateless state machines.
- `template <typename Event> dispatch_result dispatch(const Event& ev, const InPorts& in, OutPorts& out, Services& srv)`: Synchronously evaluates transitions matching `Event`.
- `template <typename Event> dispatch_result dispatch(const Event& ev, const InPorts& in, OutPorts& out)`: Dispatch overload omitting `Services`.
- `template <typename Event> dispatch_result dispatch(const Event& ev)`: Dispatch overload for stateless state machines.

#### State Inspection
- `template <typename State> [[nodiscard]] constexpr bool is_in() const noexcept`: Returns `true` if active state matches `State`.
- `template <typename State> [[nodiscard]] constexpr bool is_in_state() const noexcept`: Alias for `is_in<State>()`.
- `[[nodiscard]] constexpr std::size_t state_index() const noexcept`: Returns 0-based variant index of active state.
- `[[nodiscard]] constexpr std::string_view current_state_name() const noexcept`: Returns human-readable name of active state.

#### Internal Memory
- `[[nodiscard]] constexpr Registers& registers() noexcept`: Mutable reference to internal registers.
- `[[nodiscard]] constexpr const Registers& registers() const noexcept`: Read-only reference to internal registers.

#### Telemetry
- `void set_observer(Observer obs) noexcept`: Attaches transition observer.
- `void clear_observer() noexcept`: Detaches active observer.
- `[[nodiscard]] constexpr const Observer& observer() const noexcept`: Returns observer reference.

---

## 2. Transition Table Builders: `fsm::transition_table` & `fsm::row`

### `fsm::row<Source, Event, Target, Guard, Action>`
Declares a single transition edge:

```cpp
// Format: fsm::row<SourceState, Event, TargetState, GuardPredicate, ActionEffect>
using MyRow = fsm::row<Idle, EvStart, Active, BatteryOkGuard, ArmMotorsAction>;
```

- `Source`: Originating state struct.
- `Event`: Triggering event struct (or `fsm::anonymous_event` for continuous transitions).
- `Target`: Target state struct, or `fsm::history<ParentState, FallbackState>`.
- `Guard`: Optional boolean callable (defaults to `fsm::always_true`).
- `Action`: Optional action callable (defaults to `fsm::no_action`).

### `fsm::on` Fluent Builder
```cpp
using MyTable = fsm::transition_table<
    decltype(fsm::on<EvStart>().from<Idle>().to<Active>().guard<BatteryOk>().action<ArmMotors>())
>;
```

### Boolean Guard Combinators
```cpp
#include "fsm/backend/cpp/runtime/traits/combinators.hpp"

using CombinedGuard = fsm::and_<GuardA, fsm::or_<GuardB, fsm::not_<GuardC>>>;
```

---

## 3. Execution Status: `fsm::dispatch_result`

```cpp
#include "fsm/backend/cpp/runtime/dispatch_result.hpp"
```

```cpp
enum class dispatch_status : std::uint8_t {
    success,        // Transition executed successfully
    deferred,       // Event was deferred in current state
    guard_rejected, // Matching transition found, but guard evaluated to false
    unhandled       // No transition defined for event in active state
};

struct dispatch_result {
    dispatch_status status{dispatch_status::unhandled};
    std::string_view source_state{};
    std::string_view target_state{};
    bool is_internal{false};

    [[nodiscard]] constexpr bool is_success() const noexcept;
    [[nodiscard]] constexpr bool is_deferred() const noexcept;
    [[nodiscard]] constexpr bool is_guard_rejected() const noexcept;
    [[nodiscard]] constexpr bool is_unhandled() const noexcept;
    [[nodiscard]] constexpr bool is_ok() const noexcept; // is_success() || is_deferred()
    explicit constexpr operator bool() const noexcept { return is_ok(); }
};
```

---

## 4. Zero-Heap Storage: `fsm::static_vector<T, Capacity>`

```cpp
#include "fsm/backend/cpp/runtime/static_vector.hpp"
```

A fixed-capacity, stack-allocated sequential container with bounded $O(1)$ operations:

```cpp
template <typename T, std::size_t Capacity>
class static_vector {
public:
    constexpr bool push_back(const T& value) noexcept;
    constexpr bool push_back(T&& value) noexcept;
    constexpr void pop_back() noexcept;
    constexpr void erase(std::size_t index) noexcept;
    constexpr void clear() noexcept;

    [[nodiscard]] constexpr T& front() noexcept;
    [[nodiscard]] constexpr const T& front() const noexcept;
    [[nodiscard]] constexpr T& back() noexcept;
    [[nodiscard]] constexpr const T& back() const noexcept;
    [[nodiscard]] constexpr T& operator[](std::size_t index) noexcept;
    [[nodiscard]] constexpr const T& operator[](std::size_t index) const noexcept;

    [[nodiscard]] constexpr std::size_t size() const noexcept;
    [[nodiscard]] constexpr bool empty() const noexcept;
    [[nodiscard]] constexpr bool full() const noexcept;
    [[nodiscard]] static constexpr std::size_t capacity() noexcept;
};
```

---

## 5. Lock-Free SPSC Engine: `fsm::spsc_fsm`

```cpp
#include "fsm/backend/cpp/runtime/spsc_fsm.hpp"
```

```cpp
template <
    typename Table,
    typename InPorts = no_ports,
    typename OutPorts = no_ports,
    typename Registers = no_registers,
    typename Services = no_services,
    std::size_t QueueCapacity = 64,
    typename InitialState = typename Table::initial_state
>
class spsc_fsm;
```

### Member Functions

#### Producer Context (Wait-Free O(1), ISR-Safe)
- `template <typename Event> bool post(Event&& ev) noexcept`: Enqueues event. Returns `false` if queue is full.
- `template <typename Event> bool send(Event&& ev) noexcept`: Fluent alias for `post()`.
- `template <typename Event> bool enqueue(Event&& ev) noexcept`: FIFO queue push.

#### Consumer Context (RTOS Worker Thread)
- `bool process_one(const InPorts& in, OutPorts& out, Services& srv) noexcept`: Pops and executes the single oldest event.
- `std::size_t run_until_empty(const InPorts& in, OutPorts& out, Services& srv) noexcept`: Drains and executes all queued events.
- `dispatch_result step(const InPorts& in, OutPorts& out, Services& srv) noexcept`: Evaluates continuous condition step.

#### Reader Context (Lock-Free Seqlock)
- `Registers snapshot_registers() const noexcept`: Captures consistent register snapshot using atomic sequence lock without blocking worker.
- `std::string_view state_name() const noexcept`: Atomic load of active state name.
- `template <typename State> bool is_in_state() const noexcept`: Atomic state type query.

---

## 6. Thread-Safe MPSC Engine: `fsm::thread_safe_fsm`

```cpp
#include "fsm/backend/cpp/runtime/thread_safe_fsm.hpp"
```

```cpp
template <
    typename Table,
    typename InPorts = no_ports,
    typename OutPorts = no_ports,
    typename Registers = no_registers,
    typename Services = no_services,
    std::size_t MaxQueueSize = 256,
    typename MutexPolicy = std::mutex
>
class thread_safe_fsm;
```

### Member Functions

#### Asynchronous Operations
- `template <typename Event> void post(Event&& ev)`: Asynchronous, non-blocking fire-and-forget push.
- `template <typename Event, typename Callback> void post(Event&& ev, Callback&& cb)`: Push with completion callback.
- `template <typename Event> std::future<dispatch_result> post_async(Event&& ev)`: Push returning `std::future<dispatch_result>`.
- `template <typename Event, typename Rep, typename Period> void post_delayed(Event&& ev, std::chrono::duration<Rep, Period> delay)`: Schedules event to fire after duration.

#### Synchronous & State Queries (Mutex Guarded)
- `std::string_view current_state_name() const`: Returns active state name under mutex lock.
- `template <typename State> bool is_in_state() const`: Checks state type under mutex lock.
- `Registers snapshot_registers() const`: Returns copy of registers under mutex lock.
- `template <typename Func> void with_registers(Func&& fn)`: Thread-safe callable execution with exclusive access to registers.

---

## 7. Action and Hook Channel Indexes: `fsm::channel_index`

```cpp
#include "fsm/backend/cpp/runtime/traits/hook_traits.hpp"
```

Constants identifying argument positions in compile-time multi-channel hook invocations:

```cpp
namespace fsm {

inline constexpr std::size_t channel_index_event      = 0;
inline constexpr std::size_t channel_index_src_state  = 1;
inline constexpr std::size_t channel_index_dst_state  = 2;
inline constexpr std::size_t channel_index_in_ports   = 3;
inline constexpr std::size_t channel_index_out_ports  = 4;
inline constexpr std::size_t channel_index_registers  = 5;
inline constexpr std::size_t channel_index_services   = 6;
inline constexpr std::size_t channel_index_fsm_inst   = 7;

} // namespace fsm
```

---

## 8. C++20 Concepts: `fsm::Guard` and `fsm::Action`

```cpp
#include "fsm/backend/cpp/runtime/traits/concepts.hpp"
```

Under C++20, callable predicates and transition actions are constrained using native concepts:

```cpp
template <typename GuardType, typename EventType, typename StateType, typename InPorts, typename Registers, typename Services>
concept Guard;
```
Matches any functor returning a type convertible to `bool`, accepting any permutation of `(event, state, in, reg, srv)` or parameterless `()`.

```cpp
template <typename ActionType, typename EventType, typename SrcStateType, typename DstStateType, typename InPorts, typename OutPorts, typename Registers, typename Services>
concept Action;
```
Matches any functor callable with any valid permutation of `(event, src_state, dst_state, in, out, reg, srv)`.

---

## 9. Compile-Time Metaprogramming Traits

```cpp
#include "fsm/backend/cpp/runtime/traits/observer_traits.hpp"
#include "fsm/backend/cpp/runtime/traits/reflection.hpp"
```

- `fsm::count_parent_states_v<StateList>`: Computes exact compile-time count of composite states with substates (bounded history capacity).
- `fsm::is_substate_of_v<Child, Parent>`: Returns `true` if `Child` defines `static constexpr auto parent` matching `Parent`.
- `fsm::get_state_name_static<T>()`: Returns compile-time `std::string_view` for state struct `T`.

---

## 10. Decomposed Runtime Detail Modules

For clean separation of concerns and maximum maintainability, internal engine mechanics are decomposed into isolated headers in `fsm/backend/cpp/runtime/detail/`:

- **`detail/history_manager.hpp`**: Zero-overhead UML history storage with bounded capacity.
- **`detail/deferred_manager.hpp`**: Event deferral FIFO queue and cascading replay engine.
- **`detail/transition_executor.hpp`**: Unrolled compile-time transition fold dispatcher and 4-phase lifecycle runner.
- **`detail/reentrancy_tracker.hpp`**: Atomic thread ID tracking and depth recursion management.
- **`detail/notification_dispatcher.hpp`**: Snapshotting and out-of-lock observer callback dispatcher.
