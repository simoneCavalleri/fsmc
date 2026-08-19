#pragma once

#include <string_view>
#include <utility>

#include "transition_table.hpp"

namespace fsm {

// Core synchronous compile-time Finite State Machine with Context Injection
template <typename Table, typename Context = no_context,
          typename InitialState = typename Table::initial_state>
class fsm {
 public:
  using table_type = Table;
  using context_type = Context;
  using state_variant = typename Table::state_variant;
  using initial_state_type = InitialState;

  // Default constructor (with no context or default-constructible context)
  constexpr fsm()
      : current_state_(InitialState{}), table_(), context_(nullptr) {
    enter_initial_state();
  }

  // Constructor with Context reference
  constexpr explicit fsm(Context& ctx, Table table = Table{})
      : current_state_(InitialState{}),
        table_(std::move(table)),
        context_(&ctx) {
    enter_initial_state();
  }

  // Constructor with Context reference and specific initial state instance
  constexpr explicit fsm(Context& ctx, InitialState initial,
                         Table table = Table{})
      : current_state_(std::move(initial)),
        table_(std::move(table)),
        context_(&ctx) {
    enter_initial_state();
  }

  // Constructor with custom transition table only
  constexpr explicit fsm(Table table)
      : current_state_(InitialState{}),
        table_(std::move(table)),
        context_(nullptr) {
    enter_initial_state();
  }

  // Dispatches an event to the FSM. Returns true if transition occurred, false
  // otherwise.
  template <typename Event>
  bool dispatch(const Event& event) {
    bool handled = std::visit(
        [this, &event](auto& src_state) -> bool {
          using CurrentSrc = std::decay_t<decltype(src_state)>;
          return this->try_transition_from<CurrentSrc>(
              src_state, event,
              std::make_index_sequence<
                  std::tuple_size_v<decltype(this->table_.rows)>>{});
        },
        current_state_);

    if (!handled) {
      std::visit(
          [this, &event](const auto& src_state) {
            this->on_unhandled_event(event, src_state);
          },
          current_state_);
    }

    return handled;
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
  [[nodiscard]] const state_variant& get_current_state_variant()
      const noexcept {
    return current_state_;
  }

  // Query current state name as string_view
  [[nodiscard]] std::string_view current_state_name() const {
    return std::visit(
        [](const auto& state) -> std::string_view {
          return ::fsm::get_state_name(state);
        },
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

  const Context& get_ctx() const noexcept {
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
  bool try_transition_from(CurrentSrc& src_state, const Event& event,
                           std::index_sequence<Indices...> /*indices*/) {
    return (... ||
            try_single_transition<Indices, CurrentSrc>(src_state, event));
  }

  template <std::size_t Index, typename CurrentSrc, typename Event>
  bool try_single_transition(CurrentSrc& src_state, const Event& event) {
    using RowType = std::tuple_element_t<Index, decltype(table_.rows)>;
    using TransSrc = typename RowType::source;
    using TransEvt = typename RowType::event;
    using TransDst = typename RowType::target;

    if constexpr (std::is_same_v<CurrentSrc, TransSrc> &&
                  std::is_same_v<std::decay_t<Event>, TransEvt>) {
      auto& row = std::get<Index>(table_.rows);
      auto& ctx = get_ctx();

      // 1. Guard check
      if (!call_guard(row.guard_fn, event, src_state, ctx)) {
        return false;
      }

      if constexpr (RowType::is_internal) {
        // Internal transition: executes action only without calling on_exit or on_enter
        call_action(row.action_fn, event, src_state, src_state, ctx);
        return true;
      } else {
        // 2. Prepare destination state
        TransDst dst_state{};

        // 3. on_exit on source state
        call_on_exit(src_state, event, ctx);

        // 4. Action on transition
        call_action(row.action_fn, event, src_state, dst_state, ctx);

        // 5. on_enter on target state
        call_on_enter(dst_state, event, ctx);

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
};

}  // namespace fsm
