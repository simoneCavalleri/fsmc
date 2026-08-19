#pragma once

#include "type_traits.hpp"

namespace fsm {

// Default no-op action
struct no_action {
  constexpr void operator()() const noexcept {}
};

// Default always-true guard
struct no_guard {
  constexpr bool operator()() const noexcept { return true; }
};

// Single transition specification in compile-time transition table
template <typename SourceState, typename EventType, typename TargetState,
          typename ActionType = no_action, typename GuardType = no_guard>
struct transition {
  using source = SourceState;
  using event = EventType;
  using target = TargetState;
  using action_type = ActionType;
  using guard_type = GuardType;
  static constexpr bool is_internal = false;

  ActionType action_fn{};
  GuardType guard_fn{};

  constexpr transition() = default;
  constexpr transition(ActionType act, GuardType grd = {})
      : action_fn(std::move(act)), guard_fn(std::move(grd)) {}
};

// Internal transition specification (executes action without exiting/entering state)
template <typename State, typename EventType,
          typename ActionType = no_action, typename GuardType = no_guard>
struct internal_transition : transition<State, EventType, State, ActionType, GuardType> {
  static constexpr bool is_internal = true;
  using transition<State, EventType, State, ActionType, GuardType>::transition;
};

// ============================================================================
// Fluent Transition Builders: row, on, and internal_row
// ============================================================================

// Fluent Transition Builder: row<Src, Event, Dst>::when<Guard>::then<Action>
template <typename SourceState, typename EventType, typename TargetState,
          typename ActionType = no_action, typename GuardType = no_guard>
struct row
    : transition<SourceState, EventType, TargetState, ActionType, GuardType> {
  using base =
      transition<SourceState, EventType, TargetState, ActionType, GuardType>;

  constexpr row() = default;
  constexpr row(ActionType act, GuardType grd = {})
      : base(std::move(act), std::move(grd)) {}

  // Add / override Guard
  template <typename NewGuard>
  using guard = row<SourceState, EventType, TargetState, ActionType, NewGuard>;

  template <typename NewGuard>
  using when = row<SourceState, EventType, TargetState, ActionType, NewGuard>;

  // Add / override Action
  template <typename NewAction>
  using action = row<SourceState, EventType, TargetState, NewAction, GuardType>;

  template <typename NewAction>
  using then = row<SourceState, EventType, TargetState, NewAction, GuardType>;
};

// Fluent Internal Transition Builder: internal_row<State, Event>::when<Guard>::then<Action>
template <typename State, typename EventType,
          typename ActionType = no_action, typename GuardType = no_guard>
struct internal_row : internal_transition<State, EventType, ActionType, GuardType> {
  using base = internal_transition<State, EventType, ActionType, GuardType>;

  constexpr internal_row() = default;
  constexpr internal_row(ActionType act, GuardType grd = {})
      : base(std::move(act), std::move(grd)) {}

  template <typename NewGuard>
  using guard = internal_row<State, EventType, ActionType, NewGuard>;

  template <typename NewGuard>
  using when = internal_row<State, EventType, ActionType, NewGuard>;

  template <typename NewAction>
  using action = internal_row<State, EventType, NewAction, GuardType>;

  template <typename NewAction>
  using then = internal_row<State, EventType, NewAction, GuardType>;
};

// Fluent Event-First Builder: on<Event, Source>::to<Target>::when<Guard>::then<Action>
template <typename EventType, typename SourceState>
struct on {
  template <typename TargetState>
  using to = row<SourceState, EventType, TargetState>;
};

// Helper for fluent creation
template <typename SourceState, typename EventType, typename TargetState,
          typename ActionType = no_action, typename GuardType = no_guard>
constexpr auto make_transition(ActionType action = {}, GuardType guard = {}) {
  return transition<SourceState, EventType, TargetState, ActionType, GuardType>{
      std::move(action), std::move(guard)};
}

}  // namespace fsm
