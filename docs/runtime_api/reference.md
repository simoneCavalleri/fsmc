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
    typename Observer = no_observer,
    std::size_t DeferredCapacity = 16
>
class fsm;
```

### Type Aliases
- `table_type`: The underlying `fsm::transition_table<Rows...>`.
- `in_ports_type`: The read-only input snapshot structure.
- `out_ports_type`: The single-assignment output structure.
- `registers_type`: The internal datapath state structure ($z^{-1}$ memory).
- `services_type`: The abstract injected environment/driver interface.
- `initial_state_type`: The root initial state type.
- `observer_type`: The transition observer callback type.

### Constructors
- `constexpr fsm()`: Default constructs state machine in initial state.
- `constexpr explicit fsm(services_type& srv, Table table = Table{})`: Initializes with bound external services.
- `constexpr explicit fsm(registers_type reg, Table table = Table{})`: Initializes with initial registers.
- `constexpr explicit fsm(registers_type reg, services_type& srv, Table table = Table{})`: Initializes with registers and bound services.
- `template <typename InitState> constexpr explicit fsm(InitState&& initial, Table table = Table{})`: Initializes with custom initial state.
- `template <typename InitState> constexpr explicit fsm(InitState&& initial, services_type& srv, Table table = Table{})`: Initializes with custom initial state and bound services.

### Member Functions

> [!TIP]
> **Which Overload to Call?**
> - **Standard (Recommended for 90% of apps)**: If services were passed at construction (`fsm(reg, srv)`), call `fsm.dispatch(ev, in, out)` and `fsm.step(in, out)`.
> - **Stateless / No-Ports (Events Only)**: If your model defines no ports, call `fsm.dispatch(ev)` (`step()` is only needed if using anonymous transitions).
> - **Dynamic / Call-Site Injection (Production & Testing)**: If services were not bound at construction (e.g. multi-channel dispatch, hardware failover, or unit testing with mocks), pass `srv` explicitly: `fsm.dispatch(ev, in, out, srv)`.

#### Lifecycle & Execution
- `step_result step(const in_ports_type& in, out_ports_type& out, services_type& srv)`: Evaluates continuous condition transitions.
- `template <typename DurationRep> step_result step(DurationRep dt, const in_ports_type& in, out_ports_type& out, services_type& srv)`: Evaluates continuous transitions with explicit $\Delta t$.
- `step_result step(const in_ports_type& in, out_ports_type& out)`: Step overload omitting `Services` (uses constructor-bound services or default).
- `step_result step()`: Step overload for stateless state machines.
- `template <typename Event> dispatch_result dispatch(const Event& ev, const in_ports_type& in, out_ports_type& out, services_type& srv)`: Synchronously evaluates transitions matching `Event`.
- `template <typename Event> dispatch_result dispatch(const Event& ev, const in_ports_type& in, out_ports_type& out)`: Dispatch overload omitting `Services` (uses constructor-bound services or default).
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
- `template <typename Callback> void set_observer(Callback observer)`: Attaches transition observer callback.
- `void clear_observer() noexcept`: Detaches active observer.

---

## 2. Policy-Based Configuration: `fsm::config` (v0.5.0+)

```cpp
#include "fsm/backend/cpp/runtime/config.hpp"
```

The policy configuration metaprogramming structure extracts and binds state machine domains in arbitrary order:

```cpp
template <typename Table, typename... Policies>
struct config;
```

### Semantic Policy Modifier Tags
- `fsm::with_registers<T>`: Specifies internal datapath state type (default: `no_registers`).
- `fsm::with_ports<In, Out>`: Specifies input and output port structures (default: `no_ports, no_ports`).
- `fsm::with_services<Srv>`: Specifies external injected service interface (default: `no_services`).
- `fsm::with_observer<Obs>`: Specifies compile-time observer type (default: `no_observer`).
- `fsm::with_initial_state<State>`: Overrides root initial state type (default: `Table::initial_state`).
- `fsm::with_deferred_capacity<N>`: Configures static capacity of deferred event queue (default: `16`).
- `fsm::with_queue_capacity<N>`: Configures ring buffer capacity in SPSC/async wrappers (default: `64`).

### Modern Factory Type Aliases
- `template <typename Table, typename... Policies> using make_fsm`: Instantiates synchronous core engine with extracted policies.
- `template <typename Table, typename... Policies> using make_spsc_fsm`: Instantiates lock-free SPSC engine with extracted policies.
- `template <typename Table, typename... Policies> using make_thread_safe_fsm`: Instantiates active object engine with extracted policies.

---

## 3. Transition Table Builders: `fsm::transition_table` & `fsm::row`

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

## 4. Execution Status: `fsm::dispatch_result` & `fsm::step_result`

```cpp
#include "fsm/backend/cpp/runtime/traits/dispatch_result.hpp"
#include "fsm/backend/cpp/runtime/traits/step_result.hpp"
```

### `fsm::transition_trace`
```cpp
enum class transition_kind : std::uint8_t { external, internal };

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
```

### `fsm::dispatch_result`
```cpp
enum class dispatch_status : std::uint8_t {
    success,        // Transition executed successfully
    deferred,       // Event was deferred in current state
    guard_rejected, // Matching transition found, but guard evaluated to false
    unhandled       // No transition defined for event in active state
};

struct dispatch_result {
    dispatch_status status{dispatch_status::unhandled};
    std::optional<transition_trace> trace{std::nullopt};

    [[nodiscard]] constexpr bool is_success() const noexcept;
    [[nodiscard]] constexpr bool is_deferred() const noexcept;
    [[nodiscard]] constexpr bool is_guard_rejected() const noexcept;
    [[nodiscard]] constexpr bool is_unhandled() const noexcept;
    [[nodiscard]] constexpr bool is_ok() const noexcept; // is_success() || is_deferred()
    explicit constexpr operator bool() const noexcept { return is_ok(); }
    [[nodiscard]] constexpr std::string_view to_string() const noexcept;
};
```

### `fsm::step_result`
```cpp
enum class step_status : std::uint8_t {
    steady,        // Machine remains nominally in active state
    transitioned   // A continuous transition condition fired
};

struct step_result {
    step_status status{step_status::steady};
    std::optional<transition_trace> trace{std::nullopt};

    [[nodiscard]] constexpr bool has_transitioned() const noexcept;
    [[nodiscard]] constexpr bool is_transitioned() const noexcept;
    [[nodiscard]] constexpr bool is_steady() const noexcept;
    explicit constexpr operator bool() const noexcept { return has_transitioned(); }
    [[nodiscard]] constexpr std::string_view to_string() const noexcept;
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
- `template <typename Event> bool post(Event&& ev) noexcept`: Enqueues event into the ring buffer. Returns `false` if the queue is full.

#### Consumer Context (RTOS Worker Thread)
- `bool process_one()`: Pops and executes the single oldest event.
- `bool process_one(const in_ports_type& in, out_ports_type& out)`: Pops and executes oldest event with I/O snapshot.
- `std::size_t run_until_empty()`: Drains and executes all queued events.
- `std::size_t run_until_empty(const in_ports_type& in, out_ports_type& out)`: Drains and executes all queued events with I/O snapshot.
- `step_result step()`: Evaluates continuous condition step.
- `step_result step(const in_ports_type& in, out_ports_type& out)`: Evaluates continuous condition step with I/O snapshot.
- `template <typename DurationRep> step_result step(DurationRep dt, const in_ports_type& in, out_ports_type& out)`: Evaluates continuous step with $\Delta t$.

#### Reader Context (Lock-Free Seqlock)
- `Registers snapshot_registers() const noexcept`: Captures consistent register snapshot using atomic sequence lock without blocking worker.
- `std::string_view state_name() const noexcept`: Atomic load of active state name.
- `template <typename State> bool is_in_state() const noexcept`: Atomic state type query.

---

## 6. SPSC Policy-Based Alias: `fsm::make_spsc_fsm`

```cpp
template <typename Table, typename... Policies>
using make_spsc_fsm = spsc_fsm<config<Table, Policies...>>;
```

Equivalent to `spsc_fsm` but uses the policy modifier system (`with_registers<T>`, `with_queue_capacity<N>`, etc.) instead of raw positional template arguments:

```cpp
// Modern policy-based (preferred):
fsm::make_spsc_fsm<SensorTable,
    fsm::with_registers<SensorRegisters>,
    fsm::with_queue_capacity<256>
> spsc_sm(regs);
```

---

## 7. Thread-Safe MPSC Engine: `fsm::thread_safe_fsm`

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
    typename InitialState = typename Table::initial_state,
    std::size_t MaxQueueSize = 256
>
class thread_safe_fsm;
```

### Member Functions

#### Asynchronous Operations
- `template <typename Event> void post(Event&& ev)`: Asynchronous, non-blocking fire-and-forget push.
- `template <typename Event, typename Callback> void post(Event&& ev, Callback&& cb)`: Push with completion callback.
- `template <typename Event> std::future<dispatch_result> post_async(Event&& ev)`: Push returning `std::future<dispatch_result>`.
- `template <typename Event, typename Rep, typename Period> void post_delayed(Event&& ev, std::chrono::duration<Rep, Period> delay, bool cancel_if_state_changes = false)`: Schedules event to fire after duration.
- `template <typename Event, typename Rep, typename Period> void post_state_timeout(Event&& ev, std::chrono::duration<Rep, Period> delay)`: Schedules state-bound timeout automatically invalidated on state change.

#### Synchronous & State Queries (Mutex Guarded)
- `step_result step(const InPorts& in, OutPorts& out, Services& srv)`: Thread-safe continuous step evaluation under lock.
- `step_result step(const InPorts& in, OutPorts& out)`: Step overload using constructor-bound services.
- `step_result step()`: Step overload for stateless state machines.
- `template <typename DurationRep> step_result step(DurationRep dt, const InPorts& in, OutPorts& out, Services& srv)`: Step with $\Delta t$.
- `template <typename DurationRep> step_result step(DurationRep dt, const InPorts& in, OutPorts& out)`: Step with $\Delta t$ and constructor-bound services.
- `std::string_view current_state_name() const`: Returns active state name under mutex lock.
- `template <typename State> bool is_in_state() const`: Checks state type under mutex lock.
- `template <typename State> bool is_in() const`: Alias for `is_in_state<State>()`.

#### Safe-by-Design Datapath Access (v0.5.0+)
- `registers_type snapshot_registers() const`: Safely returns an isolated, consistent copy of internal registers under mutex lock.
- `void update_registers(registers_type reg)`: Atomically updates internal registers under mutex lock.
- `template <typename Func> decltype(auto) with_registers(Func&& fn)`: Executes callable `fn(registers)` inside exclusive mutex lock.

> [!IMPORTANT]
> **Data Race Prevention**: Direct naked references via `registers()` and `unsafe_registers()` were **permanently removed in `v0.5.0`** to eliminate data races and torn reads. Always use `with_registers()`, `update_registers()`, or `snapshot_registers()`.

---

## 8. Action and Hook Channel Indexes: `fsm::channel_index`

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

## 9. C++20 Concepts: `fsm::Guard` and `fsm::Action`

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

## 10. Compile-Time Metaprogramming Traits

```cpp
#include "fsm/backend/cpp/runtime/traits/observer_traits.hpp"
#include "fsm/backend/cpp/runtime/traits/reflection.hpp"
```

- `fsm::count_parent_states_v<StateList>`: Computes exact compile-time count of composite states with substates (bounded history capacity).
- `fsm::is_substate_of_v<Child, Parent>`: Returns `true` if `Child` defines `static constexpr auto parent` matching `Parent`.
- `fsm::get_state_name_static<T>()`: Returns compile-time `std::string_view` for state struct `T`.

---

## 11. Decomposed Runtime Detail Modules

For clean separation of concerns and maximum maintainability, internal engine mechanics are decomposed into isolated headers in `fsm/backend/cpp/runtime/detail/`:

- **`detail/history_manager.hpp`**: Zero-overhead UML history storage with bounded capacity.
- **`detail/deferred_manager.hpp`**: Event deferral FIFO queue and cascading replay engine.
- **`detail/transition_executor.hpp`**: Unrolled compile-time transition fold dispatcher and 4-phase lifecycle runner.
- **`detail/reentrancy_tracker.hpp`**: Atomic thread ID tracking and depth recursion management.
- **`detail/notification_dispatcher.hpp`**: Snapshotting and out-of-lock observer callback dispatcher.
