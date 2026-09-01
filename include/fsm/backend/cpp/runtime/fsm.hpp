#pragma once

#include <string_view>
#include <type_traits>
#include <utility>

#include "fsm/backend/cpp/runtime/detail/deferred_manager.hpp"
#include "fsm/backend/cpp/runtime/detail/history_manager.hpp"
#include "fsm/backend/cpp/runtime/detail/transition_executor.hpp"
#include "fsm/backend/cpp/runtime/static_vector.hpp"
#include "fsm/backend/cpp/runtime/transition_table.hpp"

namespace fsm {

/**
 * @brief Core Compile-Time Finite State Machine Engine (Model-Based Systems Engineering Partitioned Domain Architecture).
 *
 * `fsm::fsm` executes statecharts defined via a compile-time static transition table `Table`.
 * It provides strict separation of memory concerns across 4 orthogonal domain interfaces:
 * - `InPorts`: Read-only inputs representing continuous sensor or bus data (with formal min/max range contracts).
 * - `OutPorts`: Actuator and signal output ports updated during state execution.
 * - `Registers`: Internal discrete datapath state variables with single-cycle delay ($z^{-1}$) semantics.
 * - `Services`: External OS/driver service interfaces (e.g. timers, logging, network sockets).
 *
 * ### Execution Paradigms:
 * 1. **Sampled Periodic Step (`step()`):** Executes sampled/continuous transitions evaluated on cyclic clock ticks (PLC/IEC 61131-3 / SCADE style).
 * 2. **Reactive Event Dispatch (`dispatch(event)`):** Evaluates event-triggered transitions in response to external signals or messages.
 *
 * ### Exception safety
 * This class is NOT thread-safe by itself (see thread_safe_fsm / spsc_fsm for
 * concurrent use). Regarding exceptions thrown by user-supplied hooks during
 * an *external* transition, the exact sequence executed is:
 *
 *   1. on_exit(src_state)         — src_state is still the active state
 *   2. action(...)                — dst_state exists only as a local; if this
 *                                    throws, `current_state_` has NOT been
 *                                    reassigned yet, so the FSM is still
 *                                    reported as being in `src_state`, even
 *                                    though on_exit already ran. Only the
 *                                    "basic" exception guarantee is offered:
 *                                    the FSM remains in a valid-but-possibly-
 *                                    inconsistent state (on_exit ran without
 *                                    a matching on_enter) and must not be
 *                                    used again without inspection/reset by
 *                                    the caller.
 *   3. current_state_ = dst_state — reassignment (noexcept as long as the
 *                                    state types' move-assignment is)
 *   4. on_enter(dst_state)        — if this throws, `current_state_` HAS
 *                                    already been reassigned to the new
 *                                    state, so the FSM reports being in
 *                                    `dst_state` even though entry did not
 *                                    finish. Again: basic guarantee only.
 *
 * If your State types can throw from on_enter/on_exit or your Action can
 * throw, wrap the call to dispatch()/step() in a try/catch and treat any
 * caught exception as "FSM state is now suspect" — do not keep dispatching
 * without explicit recovery logic.
 *
 * @tparam Table The static transition table type (`::fsm::transition_table<Rows...>`).
 * @tparam InPorts Struct defining input ports (defaults to `fsm::no_ports`).
 * @tparam OutPorts Struct defining output ports (defaults to `fsm::no_ports`).
 * @tparam Registers Struct defining internal discrete state variables (defaults to `fsm::no_registers`).
 * @tparam Services Class or struct defining external hardware/OS service hooks (defaults to `fsm::no_services`).
 * @tparam InitialState The initial state type to enter on instantiation (defaults to `Table::initial_state`).
 * @tparam Observer Diagnostic telemetry callback observer (defaults to `fsm::no_observer`).
 * @tparam DeferredCapacity Maximum number of simultaneously deferred events
 *         (configurable parameter; only relevant if at least one state declares `deferred_events`).
 *
 * @note Zero-Heap: Operates 100% without dynamic memory allocations (0 bytes heap) across all features, including History states and Deferred Event queues.
 * @note Zero-VTable: Employs static C++ template metaprogramming for deterministic execution times.
 */
template <typename Table, typename InPorts = no_ports, typename OutPorts = no_ports, typename Registers = no_registers,
          typename Services = no_services, typename InitialState = typename Table::initial_state,
          typename Observer = no_observer, std::size_t DeferredCapacity = 16>
class fsm {
  public:
    using table_type = Table;
    using in_ports_type = InPorts;
    using out_ports_type = OutPorts;
    using registers_type = Registers;
    using services_type = Services;
    using initial_state_type = InitialState;
    using observer_type = Observer;
    using state_variant = typename Table::state_variant;

    static constexpr bool has_history = any_state_has_history<typename Table::states>::value;
    static constexpr bool has_deferred = any_state_has_deferred<typename Table::states>::value;
    static constexpr bool has_observer = !std::is_same_v<observer_type, no_observer>;

    using history_entry = ::fsm::history_entry;

    // Static compile-time introspection
    static constexpr std::size_t state_count = Table::state_count;
    static constexpr std::size_t transition_count = Table::transition_count;
    static constexpr std::size_t event_count = Table::event_count;

    static constexpr std::size_t max_history_capacity = detail::history_manager<Table, has_history>::max_history_capacity;
    static constexpr std::size_t max_deferred_capacity = DeferredCapacity;

    template <typename State>
    static constexpr bool has_state = Table::template has_state<State>;

    template <typename Event>
    static constexpr bool has_event = Table::template has_event<Event>;

    // ========================================================================
    // Constructors
    // ========================================================================

    constexpr fsm() : current_state_(initial_state_type{}), table_(), registers_{}, services_(nullptr) {
        enter_initial_state();
    }

    constexpr explicit fsm(services_type& srv, Table table = Table{})
        : current_state_(initial_state_type{}), table_(std::move(table)), registers_{}, services_(&srv) {
        enter_initial_state();
    }

    constexpr explicit fsm(registers_type reg, Table table = Table{})
        : current_state_(initial_state_type{}), table_(std::move(table)), registers_(std::move(reg)), services_(nullptr) {
        enter_initial_state();
    }

    constexpr explicit fsm(registers_type reg, services_type& srv, Table table = Table{})
        : current_state_(initial_state_type{}), table_(std::move(table)), registers_(std::move(reg)), services_(&srv) {
        enter_initial_state();
    }

    template <typename InitState,
              typename = std::enable_if_t<Table::template has_state<std::decay_t<InitState>> &&
                                          !std::is_same_v<std::decay_t<InitState>, registers_type>>>
    constexpr explicit fsm(InitState&& initial, Table table = Table{})
        : current_state_(std::forward<InitState>(initial)), table_(std::move(table)), registers_{}, services_(nullptr) {
        enter_initial_state();
    }

    template <typename InitState,
              typename = std::enable_if_t<Table::template has_state<std::decay_t<InitState>>>>
    constexpr explicit fsm(InitState&& initial, services_type& srv, Table table = Table{})
        : current_state_(std::forward<InitState>(initial)), table_(std::move(table)), registers_{}, services_(&srv) {
        enter_initial_state();
    }

    // ========================================================================
    // Dual-Mode Execution APIs (Sampled Control Loop & Event-Driven Reactive)
    // ========================================================================

    dispatch_result step(const in_ports_type& in, out_ports_type& out, services_type& srv) {
        auto res = dispatch_direct_ports(anonymous_event{}, in, out, srv);
        if constexpr (has_deferred) {
            if (res.is_success()) {
                process_deferred_queue_ports(in, out, srv);
            }
        }
        return res;
    }

    dispatch_result step(const in_ports_type& in, out_ports_type& out) {
        services_type dummy_srv{};
        services_type& srv = (services_ != nullptr) ? *services_ : dummy_srv;
        return step(in, out, srv);
    }

    dispatch_result step() {
        in_ports_type dummy_in{};
        out_ports_type dummy_out{};
        services_type dummy_srv{};
        services_type& srv = (services_ != nullptr) ? *services_ : dummy_srv;
        return step(dummy_in, dummy_out, srv);
    }

    template <typename Event>
    dispatch_result dispatch(const Event& event, const in_ports_type& in, out_ports_type& out, services_type& srv) {
        auto res = dispatch_direct_ports(event, in, out, srv);
        if (res.is_unhandled()) {
            bool was_deferred = false;
            if constexpr (has_deferred) {
                was_deferred = std::visit(
                    [this, &event](const auto& st) -> bool {
                        using StateType = std::decay_t<decltype(st)>;
                        if constexpr (is_deferred_event_v<StateType, Event>) {
                            this->defer_event(event);
                            return true;
                        }
                        return false;
                    },
                    current_state_);
            }
            if (was_deferred) {
                if constexpr (has_observer) {
                    observer_(transition_info{current_state_name(), {}, get_event_name(event), dispatch_status::deferred,
                                              transition_kind::external});
                }
                return dispatch_result{dispatch_status::deferred, res.trace};
            }
            if constexpr (has_observer) {
                observer_(transition_info{current_state_name(), {}, get_event_name(event), dispatch_status::unhandled,
                                              transition_kind::external});
            }
            std::visit([this, &event](const auto& st) { this->on_unhandled_event(event, st); }, current_state_);
        } else if (res.is_guard_rejected()) {
            if constexpr (has_observer) {
                observer_(transition_info{current_state_name(), res.trace ? res.trace->target : std::string_view{},
                                          get_event_name(event), dispatch_status::guard_rejected,
                                          res.trace ? res.trace->kind : transition_kind::external});
            }
        } else if (res.is_success()) {
            if constexpr (has_deferred) {
                process_deferred_queue_ports(in, out, srv);
            }
        }
        return res;
    }

    template <typename Event>
    dispatch_result dispatch(const Event& event, const in_ports_type& in, out_ports_type& out) {
        services_type dummy_srv{};
        services_type& srv = (services_ != nullptr) ? *services_ : dummy_srv;
        return dispatch(event, in, out, srv);
    }

    template <typename Event>
    dispatch_result dispatch(const Event& event) {
        in_ports_type dummy_in{};
        out_ports_type dummy_out{};
        services_type dummy_srv{};
        services_type& srv = (services_ != nullptr) ? *services_ : dummy_srv;
        return dispatch(event, dummy_in, dummy_out, srv);
    }

    template <typename Event>
    dispatch_result dispatch_direct_ports(const Event& event, const in_ports_type& in, out_ports_type& out,
                                          services_type& srv) {
        return std::visit(
            [this, &event, &in, &out, &srv](auto& src_state) -> dispatch_result {
                using CurrentSrc = std::decay_t<decltype(src_state)>;
                auto record_fn = [this](std::string_view p, std::string_view s) {
                    this->history_mgr_.record_history(p, s);
                };
                return detail::execute_transition_from_ports<Table, CurrentSrc>(
                    src_state, event, in, out, this->registers_, &srv, *this, this->observer_,
                    record_fn, std::make_index_sequence<std::tuple_size_v<decltype(this->table_.rows)>>{});
            },
            current_state_);
    }

    template <typename Event>
    dispatch_result dispatch_direct(const Event& event) {
        in_ports_type dummy_in{};
        out_ports_type dummy_out{};
        services_type dummy_srv{};
        services_type& srv = (services_ != nullptr) ? *services_ : dummy_srv;
        return dispatch_direct_ports(event, dummy_in, dummy_out, srv);
    }

    // Deferred events management
    template <typename Event>
    void defer_event(const Event& event) {
        deferred_mgr_.defer_event(event);
    }

    template <bool D = has_deferred>
    void process_deferred_queue() {
        in_ports_type dummy_in{};
        out_ports_type dummy_out{};
        services_type dummy_srv{};
        services_type& srv = (services_ != nullptr) ? *services_ : dummy_srv;
        process_deferred_queue_ports(dummy_in, dummy_out, srv);
    }

    template <bool D = has_deferred>
    void process_deferred_queue_ports(const in_ports_type& in, out_ports_type& out, services_type& srv) {
        if constexpr (D) {
            deferred_mgr_.process_deferred_queue([this, &in, &out, &srv](const auto& ev) {
                return this->dispatch_direct_ports(ev, in, out, srv);
            });
        }
    }

    template <bool D = has_deferred>
    [[nodiscard]] std::size_t deferred_count() const noexcept {
        return deferred_mgr_.deferred_count();
    }

    template <bool D = has_deferred>
    void clear_deferred_events() noexcept {
        deferred_mgr_.clear_deferred_events();
    }

    // ========================================================================
    // Domain State & Register Accessors
    // ========================================================================

    [[nodiscard]] registers_type& registers() noexcept { return registers_; }
    [[nodiscard]] const registers_type& registers() const noexcept { return registers_; }
    [[nodiscard]] registers_type get_registers() const noexcept { return registers_; }
    void set_registers(registers_type reg) noexcept { registers_ = std::move(reg); }

    void set_services(services_type& srv) noexcept { services_ = &srv; }
    [[nodiscard]] services_type* get_services() noexcept { return services_; }
    [[nodiscard]] const services_type* get_services() const noexcept { return services_; }

    // Observer management
    template <typename Callback>
    void set_observer(Callback observer) {
        if constexpr (std::is_same_v<observer_type, dynamic_observer>) {
            observer_.callback = std::move(observer);
        } else if constexpr (!std::is_same_v<observer_type, no_observer>) {
            observer_ = std::move(observer);
        }
    }

    void clear_observer() noexcept {
        if constexpr (std::is_same_v<observer_type, dynamic_observer>) {
            observer_.callback = nullptr;
        }
    }

    // History state management
    template <bool H = has_history>
    void record_history(std::string_view parent, std::string_view substate) {
        history_mgr_.record_history(parent, substate);
    }

    template <bool H = has_history>
    [[nodiscard]] std::string_view get_history(std::string_view parent) const noexcept {
        return history_mgr_.get_history(parent);
    }

    // State Inspection & Mutation
    template <typename State>
    [[nodiscard]] bool is_in_state() const noexcept {
        return std::holds_alternative<State>(current_state_);
    }

    template <typename State>
    [[nodiscard]] bool is_in() const noexcept {
        return is_in_state<State>();
    }

    template <typename State>
    [[nodiscard]] const State* get_state() const noexcept {
        return std::get_if<State>(&current_state_);
    }

    template <typename State>
    [[nodiscard]] State* get_state() noexcept {
        return std::get_if<State>(&current_state_);
    }

    template <typename Callable>
    auto with_state(Callable&& fn) const {
        return std::visit([&fn](const auto& s) { return fn(s); }, current_state_);
    }

    template <typename Callable>
    auto with_state(Callable&& fn) {
        return std::visit([&fn](auto& s) { return fn(s); }, current_state_);
    }

    [[nodiscard]] const state_variant& get_current_state_variant() const noexcept { return current_state_; }
    [[nodiscard]] state_variant& get_current_state_variant() noexcept { return current_state_; }

    template <typename StateType>
    void set_current_state_variant(StateType&& new_state) {
        current_state_ = std::forward<StateType>(new_state);
    }

    [[nodiscard]] std::string_view current_state_name() const {
        return std::visit([](const auto& state) -> std::string_view { return ::fsm::get_state_name(state); },
                          current_state_);
    }

  protected:
    template <typename Event, typename State>
    void on_unhandled_event(const Event& /*event*/, const State& /*src*/) {
        // Default: no-op
    }

  private:
    void enter_initial_state() {
        if (auto* state = std::get_if<initial_state_type>(&current_state_)) {
            in_ports_type dummy_in{};
            out_ports_type dummy_out{};
            services_type dummy_srv{};
            services_type& srv = (services_ != nullptr) ? *services_ : dummy_srv;
            call_on_enter(*state, dummy_in, dummy_out, registers_, srv);
        }
    }

    state_variant current_state_;
    FSMC_NO_UNIQUE_ADDRESS Table table_{};
    FSMC_NO_UNIQUE_ADDRESS registers_type registers_{};
    services_type* services_{nullptr};
    FSMC_NO_UNIQUE_ADDRESS observer_type observer_{};
    FSMC_NO_UNIQUE_ADDRESS detail::history_manager<Table, has_history> history_mgr_{};
    FSMC_NO_UNIQUE_ADDRESS detail::deferred_manager<Table, DeferredCapacity, has_deferred> deferred_mgr_{};
};

template <typename Table, typename InPorts = no_ports, typename OutPorts = no_ports, typename Registers = no_registers,
          typename Services = no_services, typename InitialState = typename Table::initial_state,
          std::size_t DeferredCapacity = 16>
using dynamic_fsm = fsm<Table, InPorts, OutPorts, Registers, Services, InitialState, dynamic_observer, DeferredCapacity>;

}  // namespace fsm
