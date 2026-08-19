#pragma once

#include <functional>
#include <string_view>
#include <utility>
#include <vector>

#include "transition_table.hpp"

namespace fsm {

// Information about an executed transition passed to transition observers
struct transition_info {
    std::string_view source;
    std::string_view target;
    std::string_view event;
    bool is_internal = false;
};

// Core synchronous compile-time Finite State Machine with Context Injection
template <typename Table, typename Context = no_context, typename InitialState = typename Table::initial_state>
class fsm {
  public:
    using table_type = Table;
    using context_type = Context;
    using state_variant = typename Table::state_variant;
    using initial_state_type = InitialState;
    using observer_type = std::function<void(const transition_info&)>;

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

    // Default constructor (with no context or default-constructible context)
    constexpr fsm() : current_state_(InitialState{}), table_(), context_(nullptr) { enter_initial_state(); }

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

    // Constructor with custom transition table only
    constexpr explicit fsm(Table table) : current_state_(InitialState{}), table_(std::move(table)), context_(nullptr) {
        enter_initial_state();
    }

    // Dispatches an event directly (without deferred queue processing)
    template <typename Event>
    bool dispatch_direct(const Event& event) {
        return std::visit(
            [this, &event](auto& src_state) -> bool {
                using CurrentSrc = std::decay_t<decltype(src_state)>;
                return this->try_transition_from<CurrentSrc>(
                    src_state, event, std::make_index_sequence<std::tuple_size_v<decltype(this->table_.rows)>>{});
            },
            current_state_);
    }

    // Dispatches an event to the FSM. Returns true if transition occurred or if event was deferred.
    template <typename Event>
    bool dispatch(const Event& event) {
        bool handled = dispatch_direct(event);

        if (handled) {
            // Process/replay any deferred events that can now fire in the new state
            process_deferred_queue();
        } else {
            // Check if the current state defers this event
            bool deferred = std::visit(
                [this, &event](const auto& src_state) -> bool {
                    using CurrentSrc = std::decay_t<decltype(src_state)>;
                    if constexpr (is_deferred_event_v<CurrentSrc, Event>) {
                        this->deferred_queue_.push_back([event](fsm& self) -> bool {
                            return self.dispatch_direct(event);
                        });
                        return true;
                    } else {
                        return false;
                    }
                },
                current_state_);

            if (deferred) {
                return true;
            }

            std::visit([this, &event](const auto& src_state) { this->on_unhandled_event(event, src_state); },
                       current_state_);
        }

        return handled;
    }

    // Process all deferred events in queue until no more can fire
    void process_deferred_queue() {
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

    // Deferred events introspection
    [[nodiscard]] std::size_t deferred_count() const noexcept { return deferred_queue_.size(); }
    void clear_deferred_events() noexcept { deferred_queue_.clear(); }

    // Observer management
    void set_observer(observer_type observer) { observer_ = std::move(observer); }
    void clear_observer() noexcept { observer_ = nullptr; }

    // History state management
    void record_history(std::string_view parent, std::string_view substate) {
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

    [[nodiscard]] std::string_view get_history(std::string_view parent) const noexcept {
        for (const auto& entry : history_records_) {
            if (entry.parent == parent) {
                return entry.substate;
            }
        }
        return "";
    }

    // Context management
    void set_context(Context& ctx) noexcept { context_ = &ctx; }

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
    bool try_transition_from(CurrentSrc& src_state, const Event& event, std::index_sequence<Indices...> /*indices*/) {
        return (... || try_single_transition<Indices, CurrentSrc>(src_state, event));
    }

    template <std::size_t Index, typename CurrentSrc, typename Event>
    bool try_single_transition(CurrentSrc& src_state, const Event& event) {
        using RowType = std::tuple_element_t<Index, decltype(table_.rows)>;
        using TransSrc = typename RowType::source;
        using TransEvt = typename RowType::event;
        using TransDst = typename RowType::target;

        if constexpr (std::is_same_v<CurrentSrc, TransSrc> && std::is_same_v<std::decay_t<Event>, TransEvt>) {
            auto& row = std::get<Index>(table_.rows);
            auto& ctx = get_ctx();

            // 1. Guard check
            if (!call_guard(row.guard_fn, event, src_state, ctx, *this)) {
                return false;
            }

            if constexpr (RowType::is_internal) {
                // Internal transition: executes action only without calling on_exit or on_enter
                call_action(row.action_fn, event, src_state, src_state, ctx);
                if (observer_) {
                    observer_(transition_info{get_state_name(src_state), get_state_name(src_state),
                                              get_type_name<Event>(), true});
                }
                return true;
            } else {
                // Record history if leaving a composite state parent
                constexpr std::string_view src_parent = get_parent_name<CurrentSrc>();
                constexpr std::string_view dst_parent = get_parent_name<TransDst>();
                if constexpr (!src_parent.empty()) {
                    if constexpr (src_parent != dst_parent) {
                        record_history(src_parent, get_state_name(src_state));
                    }
                }

                // 2. Prepare destination state
                TransDst dst_state{};

                // 3. on_exit on source state
                call_on_exit(src_state, event, ctx);

                // 4. Action on transition
                call_action(row.action_fn, event, src_state, dst_state, ctx);

                // 5. on_enter on target state
                call_on_enter(dst_state, event, ctx);

                if (observer_) {
                    observer_(transition_info{get_state_name(src_state), get_state_name(dst_state),
                                              get_type_name<Event>(), false});
                }

                // 6. Update current active state
                current_state_ = std::move(dst_state);
                return true;
            }
        } else {
            return false;
        }
    }

    state_variant current_state_;
    Table table_;
    Context* context_{nullptr};
    no_context dummy_ctx_{};
    observer_type observer_;
    std::vector<history_entry> history_records_{};
    std::vector<std::function<bool(fsm&)>> deferred_queue_{};
    bool is_replaying_deferred_{false};
};

}  // namespace fsm
