#pragma once

#if __cplusplus >= 202002L
#include <concepts>
#endif

namespace fsm {

// Default empty domain tags
struct no_ports {};
struct no_registers {};
struct no_services {};

#if __cplusplus >= 202002L
template <typename GuardType, typename EventType, typename StateType, typename InPorts, typename Registers,
          typename Services>
concept Guard =
    requires(const GuardType& guard, const EventType& event, const StateType& state, const InPorts& in,
             Registers& reg, Services& srv) {
        { guard(event, state, in, reg, srv) } -> std::convertible_to<bool>;
    } || requires(const GuardType& guard, const EventType& event, const InPorts& in, Registers& reg,
                  Services& srv) {
        { guard(event, in, reg, srv) } -> std::convertible_to<bool>;
    } || requires(const GuardType& guard, const EventType& event, const InPorts& in, Registers& reg) {
        { guard(event, in, reg) } -> std::convertible_to<bool>;
    } || requires(const GuardType& guard, const EventType& event, const StateType& state, Registers& reg) {
        { guard(event, state, reg) } -> std::convertible_to<bool>;
    } || requires(const GuardType& guard, const EventType& event, Registers& reg) {
        { guard(event, reg) } -> std::convertible_to<bool>;
    } || requires(const GuardType& guard, const StateType& state, Registers& reg) {
        { guard(state, reg) } -> std::convertible_to<bool>;
    } || requires(const GuardType& guard, const EventType& event, const InPorts& in) {
        { guard(event, in) } -> std::convertible_to<bool>;
    } || requires(const GuardType& guard, const InPorts& in, Registers& reg) {
        { guard(in, reg) } -> std::convertible_to<bool>;
    } || requires(const GuardType& guard, const InPorts& in) {
        { guard(in) } -> std::convertible_to<bool>;
    } || requires(const GuardType& guard, Registers& reg) {
        { guard(reg) } -> std::convertible_to<bool>;
    } || requires(const GuardType& guard, const EventType& event, const StateType& state) {
        { guard(event, state) } -> std::convertible_to<bool>;
    } || requires(const GuardType& guard, const StateType& state) {
        { guard(state) } -> std::convertible_to<bool>;
    } || requires(const GuardType& guard, const EventType& event) {
        { guard(event) } -> std::convertible_to<bool>;
    } || requires(const GuardType& guard) {
        { guard() } -> std::convertible_to<bool>;
    };

template <typename ActionType, typename EventType, typename SrcStateType, typename DstStateType, typename InPorts,
          typename OutPorts, typename Registers, typename Services>
concept Action =
    requires(const ActionType& action, const EventType& event, SrcStateType& src_state, DstStateType& dst_state,
             const InPorts& in, OutPorts& out, Registers& reg, Services& srv) {
        action(event, src_state, dst_state, in, out, reg, srv);
    } ||
    requires(const ActionType& action, const EventType& event, const SrcStateType& src_state, DstStateType& dst_state,
             Services& srv) {
        action(event, src_state, dst_state, srv);
    } ||
    requires(const ActionType& action, const EventType& event, const SrcStateType& src_state, Services& srv) {
        action(event, src_state, srv);
    } ||
    requires(const ActionType& action, const EventType& event, Services& srv) {
        action(event, srv);
    } ||
    requires(const ActionType& action, const EventType& event, const InPorts& in, OutPorts& out, Registers& reg,
             Services& srv) { action(event, in, out, reg, srv); } ||
    requires(const ActionType& action, const InPorts& in, OutPorts& out, Registers& reg, Services& srv) {
        action(in, out, reg, srv);
    } ||
    requires(const ActionType& action, const EventType& event, const InPorts& in, OutPorts& out, Registers& reg) {
        action(event, in, out, reg);
    } ||
    requires(const ActionType& action, const EventType& event, const InPorts& in, OutPorts& out) {
        action(event, in, out);
    } ||
    requires(const ActionType& action, OutPorts& out, Registers& reg, Services& srv) { action(out, reg, srv); } ||
    requires(const ActionType& action, const EventType& event, OutPorts& out, Registers& reg) {
        action(event, out, reg);
    } ||
    requires(const ActionType& action, const EventType& event, Registers& reg) {
        action(event, reg);
    } ||
    requires(const ActionType& action, const EventType& event, const InPorts& in, Registers& reg) {
        action(event, in, reg);
    } ||
    requires(const ActionType& action, const InPorts& in, Registers& reg) {
        action(in, reg);
    } ||
    requires(const ActionType& action, const InPorts& in) {
        action(in);
    } ||
    requires(const ActionType& action, const InPorts& in, OutPorts& out) { action(in, out); } ||
    requires(const ActionType& action, OutPorts& out, Registers& reg) { action(out, reg); } ||
    requires(const ActionType& action, OutPorts& out) { action(out); } ||
    requires(const ActionType& action, Registers& reg) { action(reg); } ||
    requires(const ActionType& action, Services& srv) { action(srv); } ||
    requires(const ActionType& action, const EventType& event, SrcStateType& src_state, DstStateType& dst_state,
             Registers& reg) { action(event, src_state, dst_state, reg); } ||
    requires(const ActionType& action, const EventType& event, SrcStateType& src_state, Registers& reg) {
        action(event, src_state, reg);
    } ||
    requires(const ActionType& action, const EventType& event, DstStateType& dst_state, Registers& reg) {
        action(event, dst_state, reg);
    } ||
    requires(const ActionType& action, const EventType& event, SrcStateType& src_state, DstStateType& dst_state) {
        action(event, src_state, dst_state);
    } ||
    requires(const ActionType& action, const EventType& event, DstStateType& dst_state) { action(event, dst_state); } ||
    requires(const ActionType& action, const EventType& event, SrcStateType& src_state) { action(event, src_state); } ||
    requires(const ActionType& action, const EventType& event) { action(event); } ||
    requires(const ActionType& action, SrcStateType& src_state) { action(src_state); } ||
    requires(const ActionType& action, DstStateType& dst_state) { action(dst_state); } ||
    requires(const ActionType& action) { action(); };
#endif

}  // namespace fsm
