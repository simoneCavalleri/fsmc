#pragma once

#include <functional>
#include <string_view>
#include <utility>
#include <vector>

#include "transition_table.hpp"

namespace fsm {

// Core synchronous compile-time Finite State Machine with Context Injection
template <typename Table, typename Context = no_context, typename InitialState = typename Table::initial_state,
          typename Observer = no_observer>
class fsm {
  public:
    using table_type = Table;
    using context_type = Context;
    using state_variant = typename Table::state_variant;
    using initial_state_type = InitialState;
    using observer_type = Observer;

    static constexpr bool has_history = any_state_has_history<typename Table::states>::value;
    static constexpr bool has_deferred = any_state_has_deferred<typename Table::states>::value;
    static constexpr bool has_observer = !std::is_same_v<Observer, no_observer>;

    struct history_entry {
        std::string_view parent;
        std::string_view substate;
    };

    // Static compile-time introspection
    static constexpr std::size_t state_count = Table::state_count;
    static constexpr std::size_t transition_count = Table::transition_count;
    static constexpr std::size_t event_count = Table::event_count;

    template <typename State>
    static constexpr bool has_state = Table::template has_state<State>;

    template <typename Event>
    static constexpr bool has_event = Table::template has_event<Event>;

    // Default constructor (enabled only when Context is no_context)
    template <typename C = Context, typename = std::enable_if_t<std::is_same_v<C, no_context>>>
    constexpr fsm() : current_state_(InitialState{}), table_(), context_(nullptr) {
        enter_initial_state();
    }

    // Constructor with Context reference
    constexpr explicit fsm(Context& ctx, Table table = Table{})
        : current_state_(InitialState{}), table_(std::move(table)), context_(&ctx) {
        enter_initial_state();
    }

    // Constructor with Context reference and specific initial state instance
    constexpr explicit fsm(Context& ctx, InitialState initial, Table table = Table{})
        : current_state_(std::move(initial)), table_(std::move(table)), context_(&ctx) {
        enter_initial_state();
    }

    // Dispatches an event directly (without deferred queue processing)
    template <typename Event>
    dispatch_result dispatch_direct(const Event& event) {
        return std::visit(
            [this, &event](auto& src_state) -> dispatch_result {
                using CurrentSrc = std::decay_t<decltype(src_state)>;
                return this->try_transition_from<CurrentSrc>(
                    src_state, event, std::make_index_sequence<std::tuple_size_v<decltype(this->table_.rows)>>{});
            },
            current_state_);
    }

    // Dispatches an event to the FSM. Returns dispatch_result (evaluates to true if executed or deferred).
    template <typename Event>
    dispatch_result dispatch(const Event& event) {
        dispatch_result res = dispatch_direct(event);

        if (res.is_success()) {
            // Process/replay any deferred events that can now fire in the new state
            process_deferred_queue();
        } else {
            const auto src_name = current_state_name();
            const auto evt_name = get_event_name(event);

            if constexpr (has_deferred) {
                // Check if the current state defers this event
                bool deferred = std::visit(
                    [this, &event](const auto& src_state) -> bool {
                        using CurrentSrc = std::decay_t<decltype(src_state)>;
                        if constexpr (is_deferred_event_v<CurrentSrc, Event>) {
                            this->deferred_queue_.push_back(
                                [event](fsm& self) -> bool { return self.dispatch_direct(event).is_success(); });
                            return true;
                        } else {
                            return false;
                        }
                    },
                    current_state_);

                if (deferred) {
                    if constexpr (has_observer) {
                        observer_(transition_info{src_name, src_name, evt_name, dispatch_status::deferred,
                                                  transition_kind::external});
                    }
                    return dispatch_result{dispatch_status::deferred};
                }
            }

            if (res.is_guard_rejected()) {
                if constexpr (has_observer) {
                    observer_(transition_info{src_name, src_name, evt_name, dispatch_status::guard_rejected,
                                              transition_kind::external});
                }
            } else if (res.is_unhandled()) {
                if constexpr (has_observer) {
                    observer_(transition_info{src_name, src_name, evt_name, dispatch_status::unhandled,
                                              transition_kind::external});
                }
                std::visit([this, &event](const auto& src_state) { this->on_unhandled_event(event, src_state); },
                           current_state_);
            }
        }

        return res;
    }

    // Process all deferred events in queue until no more can fire
    void process_deferred_queue() {
        if constexpr (has_deferred) {
            if (deferred_queue_.empty() || is_replaying_deferred_) {
                return;
            }
            is_replaying_deferred_ = true;

            bool any_handled = true;
            while (any_handled && !deferred_queue_.empty()) {
                any_handled = false;
                for (auto it = deferred_queue_.begin(); it != deferred_queue_.end();) {
                    if ((*it)(*this)) {
                        it = deferred_queue_.erase(it);
                        any_handled = true;
                        // State may have transitioned, re-scan queue
                        break;
                    }
                    ++it;
                }
            }

            is_replaying_deferred_ = false;
        }
    }

    // Deferred events introspection
    [[nodiscard]] std::size_t deferred_count() const noexcept {
        if constexpr (has_deferred) {
            return deferred_queue_.size();
        } else {
            return 0;
        }
    }

    void clear_deferred_events() noexcept {
        if constexpr (has_deferred) {
            deferred_queue_.clear();
        }
    }

    // Observer management (available for dynamic_observer or custom functor)
    template <typename Obs = Observer, typename = std::enable_if_t<is_dynamic_observer_v<Obs>>>
    void set_observer(typename Obs::callback_type observer) {
        if constexpr (std::is_same_v<Observer, dynamic_observer>) {
            observer_.callback = std::move(observer);
        } else {
            observer_ = std::move(observer);
        }
    }

    template <typename Obs = Observer, typename = std::enable_if_t<is_dynamic_observer_v<Obs>>>
    void clear_observer() noexcept {
        if constexpr (std::is_same_v<Observer, dynamic_observer>) {
            observer_.callback = nullptr;
        } else {
            observer_ = nullptr;
        }
    }

    // History state management
    void record_history(std::string_view parent, std::string_view substate) {
        if constexpr (has_history) {
            if (parent.empty() || substate.empty()) {
                return;
            }
            for (auto& entry : history_records_) {
                if (entry.parent == parent) {
                    entry.substate = substate;
                    return;
                }
            }
            history_records_.push_back({parent, substate});
        }
    }

    [[nodiscard]] std::string_view get_history(std::string_view parent) const noexcept {
        if constexpr (has_history) {
            for (const auto& entry : history_records_) {
                if (entry.parent == parent) {
                    return entry.substate;
                }
            }
        }
        return "";
    }

    // Context management
    void set_context(Context& ctx) noexcept { context_ = &ctx; }

    [[nodiscard]] Context& context() noexcept { return get_ctx(); }

    [[nodiscard]] const Context& context() const noexcept { return get_ctx(); }

    [[nodiscard]] Context* get_context() noexcept { return context_; }

    [[nodiscard]] const Context* get_context() const noexcept { return context_; }

    // Query if the current state is of type State
    template <typename State>
    [[nodiscard]] bool is_in_state() const noexcept {
        return std::holds_alternative<State>(current_state_);
    }

    // Get pointer to current state if it matches State, nullptr otherwise
    template <typename State>
    [[nodiscard]] const State* get_state() const noexcept {
        return std::get_if<State>(&current_state_);
    }

    template <typename State>
    [[nodiscard]] State* get_state() noexcept {
        return std::get_if<State>(&current_state_);
    }

    // Access the raw variant holding current state
    [[nodiscard]] const state_variant& get_current_state_variant() const noexcept { return current_state_; }

    // Query current state name as string_view
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
    Context& get_ctx() noexcept {
        if constexpr (std::is_same_v<Context, no_context>) {
            return dummy_ctx_;
        } else {
            return *context_;
        }
    }

    [[nodiscard]] const Context& get_ctx() const noexcept {
        if constexpr (std::is_same_v<Context, no_context>) {
            return dummy_ctx_;
        } else {
            return *context_;
        }
    }

    void enter_initial_state() {
        if (auto* state = std::get_if<InitialState>(&current_state_)) {
            call_on_enter(*state, get_ctx());
        }
    }

    template <typename CurrentSrc, typename Event, std::size_t... Indices>
    dispatch_result try_transition_from(CurrentSrc& src_state, const Event& event,
                                        std::index_sequence<Indices...> /*indices*/) {
        bool any_guard_rejected = false;

        auto try_index = [&](auto idx_constant) -> bool {
            constexpr std::size_t Index = decltype(idx_constant)::value;
            using RowType = std::tuple_element_t<Index, decltype(table_.rows)>;
            using TransSrc = typename RowType::source;
            using TransEvt = typename RowType::event;

            if constexpr (std::is_same_v<CurrentSrc, TransSrc> && std::is_same_v<std::decay_t<Event>, TransEvt>) {
                auto& row = std::get<Index>(table_.rows);
                auto& ctx = get_ctx();

                if (!call_guard(row.guard_fn, event, src_state, ctx, *this)) {
                    any_guard_rejected = true;
                    return false;
                }

                const auto src_name = get_state_name(src_state);
                const auto evt_name = get_event_name(event);

                if constexpr (RowType::is_internal) {
                    call_action(row.action_fn, event, src_state, src_state, ctx);
                    if constexpr (has_observer) {
                        observer_(transition_info{src_name, src_name, evt_name, dispatch_status::success,
                                                  transition_kind::internal});
                    }
                    return true;
                } else {
                    using TransDst = typename RowType::target;
                    constexpr std::string_view src_parent = get_parent_name<CurrentSrc>();
                    constexpr std::string_view dst_parent = get_parent_name<TransDst>();
                    if constexpr (!src_parent.empty()) {
                        if constexpr (src_parent != dst_parent) {
                            record_history(src_parent, src_name);
                        }
                    }

                    TransDst dst_state{};
                    call_on_exit(src_state, event, ctx);
                    call_action(row.action_fn, event, src_state, dst_state, ctx);
                    current_state_ = std::move(dst_state);
                    call_on_enter(std::get<TransDst>(current_state_), event, ctx);

                    const auto dst_name = get_state_name(std::get<TransDst>(current_state_));
                    if constexpr (has_observer) {
                        observer_(transition_info{src_name, dst_name, evt_name, dispatch_status::success,
                                                  transition_kind::external});
                    }
                    return true;
                }
            }
            return false;
        };

        bool executed = (try_index(std::integral_constant<std::size_t, Indices>{}) || ...);
        if (executed) {
            return dispatch_result{dispatch_status::success};
        }
        if (any_guard_rejected) {
            return dispatch_result{dispatch_status::guard_rejected};
        }
        return dispatch_result{dispatch_status::unhandled};
    }

    using history_storage = std::conditional_t<has_history, std::vector<history_entry>, empty_storage>;
    using deferred_storage = std::conditional_t<has_deferred, std::vector<std::function<bool(fsm&)>>, empty_storage>;

    state_variant current_state_;
    [[no_unique_address]] Table table_{};
    Context* context_{nullptr};
    [[no_unique_address]] no_context dummy_ctx_{};
    [[no_unique_address]] Observer observer_{};
    [[no_unique_address]] history_storage history_records_{};
    [[no_unique_address]] deferred_storage deferred_queue_{};
    [[no_unique_address]] std::conditional_t<has_deferred, bool, empty_storage> is_replaying_deferred_{};
};

template <typename Table, typename Context = no_context, typename InitialState = typename Table::initial_state>
using dynamic_fsm = fsm<Table, Context, InitialState, dynamic_observer>;

}  // namespace fsm
