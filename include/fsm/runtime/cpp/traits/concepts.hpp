#pragma once

#if __cplusplus >= 202002L
#include <concepts>
#endif

namespace fsm {

// Default empty context tag
struct no_context {};

#if __cplusplus >= 202002L
template <typename GuardType, typename EventType, typename StateType, typename ContextType>
concept Guard =
    requires(const GuardType& guard, const EventType& event, const StateType& state, const ContextType& context) {
        { guard(event, state, context) } -> std::convertible_to<bool>;
    } || requires(const GuardType& guard, const EventType& event, const StateType& state) {
        { guard(event, state) } -> std::convertible_to<bool>;
    } || requires(const GuardType& guard, const EventType& event) {
        { guard(event) } -> std::convertible_to<bool>;
    } || requires(const GuardType& guard) {
        { guard() } -> std::convertible_to<bool>;
    };

template <typename ActionType, typename EventType, typename SrcStateType, typename DstStateType, typename ContextType>
concept Action =
    requires(const ActionType& action, const EventType& event, SrcStateType& src_state, DstStateType& dst_state,
             ContextType& context) { action(event, src_state, dst_state, context); } ||
    requires(const ActionType& action, const EventType& event, SrcStateType& src_state, DstStateType& dst_state) {
        action(event, src_state, dst_state);
    } ||
    requires(const ActionType& action, const EventType& event, DstStateType& dst_state) { action(event, dst_state); } ||
    requires(const ActionType& action, const EventType& event) { action(event); } ||
    requires(const ActionType& action) { action(); };
#endif

}  // namespace fsm
