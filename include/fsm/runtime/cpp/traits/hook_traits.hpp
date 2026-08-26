#pragma once

#include <type_traits>
#include <utility>

namespace fsm {

// ============================================================================
// Detection Idiom & Hook Invocations (on_enter, on_exit, guard, action, context)
// ============================================================================

namespace detail {

// on_enter(event, ctx)
template <typename State, typename Event, typename Context, typename = void>
struct has_on_enter_event_ctx : std::false_type {};

template <typename State, typename Event, typename Context>
struct has_on_enter_event_ctx<
    State, Event, Context,
    std::void_t<decltype(std::declval<State&>().on_enter(std::declval<const Event&>(), std::declval<Context&>()))>>
    : std::true_type {};

// on_enter(ctx)
template <typename State, typename Context, typename = void>
struct has_on_enter_ctx : std::false_type {};

template <typename State, typename Context>
struct has_on_enter_ctx<State, Context,
                        std::void_t<decltype(std::declval<State&>().on_enter(std::declval<Context&>()))>>
    : std::true_type {};

// on_enter(event)
template <typename State, typename Event, typename = void>
struct has_on_enter_event : std::false_type {};

template <typename State, typename Event>
struct has_on_enter_event<State, Event,
                          std::void_t<decltype(std::declval<State&>().on_enter(std::declval<const Event&>()))>>
    : std::true_type {};

// on_enter()
template <typename State, typename = void>
struct has_on_enter_void : std::false_type {};

template <typename State>
struct has_on_enter_void<State, std::void_t<decltype(std::declval<State&>().on_enter())>> : std::true_type {};

// on_exit(event, ctx)
template <typename State, typename Event, typename Context, typename = void>
struct has_on_exit_event_ctx : std::false_type {};

template <typename State, typename Event, typename Context>
struct has_on_exit_event_ctx<
    State, Event, Context,
    std::void_t<decltype(std::declval<State&>().on_exit(std::declval<const Event&>(), std::declval<Context&>()))>>
    : std::true_type {};

// on_exit(ctx)
template <typename State, typename Context, typename = void>
struct has_on_exit_ctx : std::false_type {};

template <typename State, typename Context>
struct has_on_exit_ctx<State, Context, std::void_t<decltype(std::declval<State&>().on_exit(std::declval<Context&>()))>>
    : std::true_type {};

// on_exit(event)
template <typename State, typename Event, typename = void>
struct has_on_exit_event : std::false_type {};

template <typename State, typename Event>
struct has_on_exit_event<State, Event,
                         std::void_t<decltype(std::declval<State&>().on_exit(std::declval<const Event&>()))>>
    : std::true_type {};

// on_exit()
template <typename State, typename = void>
struct has_on_exit_void : std::false_type {};

template <typename State>
struct has_on_exit_void<State, std::void_t<decltype(std::declval<State&>().on_exit())>> : std::true_type {};

}  // namespace detail

// ----------------------------------------------------------------------------
// Safe invocation of on_enter hook (with context and event)
// ----------------------------------------------------------------------------
template <typename State, typename Event, typename Context>
constexpr void call_on_enter(State& state, const Event& event, Context& ctx) {
    if constexpr (detail::has_on_enter_event_ctx<State, Event, Context>::value) {
        state.on_enter(event, ctx);
    } else if constexpr (detail::has_on_enter_ctx<State, Context>::value) {
        state.on_enter(ctx);
    } else if constexpr (detail::has_on_enter_event<State, Event>::value) {
        state.on_enter(event);
    } else if constexpr (detail::has_on_enter_void<State>::value) {
        state.on_enter();
    }
}

// Overload for on_enter without event (initial state enter)
template <typename State, typename Context>
constexpr void call_on_enter(State& state, Context& ctx) {
    if constexpr (detail::has_on_enter_ctx<State, Context>::value) {
        state.on_enter(ctx);
    } else if constexpr (detail::has_on_enter_void<State>::value) {
        state.on_enter();
    }
}

// ----------------------------------------------------------------------------
// Safe invocation of on_exit hook
// ----------------------------------------------------------------------------
template <typename State, typename Event, typename Context>
constexpr void call_on_exit(State& state, const Event& event, Context& ctx) {
    if constexpr (detail::has_on_exit_event_ctx<State, Event, Context>::value) {
        state.on_exit(event, ctx);
    } else if constexpr (detail::has_on_exit_ctx<State, Context>::value) {
        state.on_exit(ctx);
    } else if constexpr (detail::has_on_exit_event<State, Event>::value) {
        state.on_exit(event);
    } else if constexpr (detail::has_on_exit_void<State>::value) {
        state.on_exit();
    }
}

// ----------------------------------------------------------------------------
// Safe invocation of guard
// ----------------------------------------------------------------------------
template <typename Guard, typename Event, typename SrcState, typename Context>
constexpr bool call_guard(const Guard& guard, const Event& event, const SrcState& src, Context& ctx) {
    if constexpr (std::is_invocable_r_v<bool, Guard, const Event&, const SrcState&, Context&>) {
        return guard(event, src, ctx);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, const Event&, Context&>) {
        return guard(event, ctx);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, const SrcState&, Context&>) {
        return guard(src, ctx);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, Context&>) {
        return guard(ctx);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, const Event&, const SrcState&>) {
        return guard(event, src);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, const Event&>) {
        return guard(event);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, const SrcState&>) {
        return guard(src);
    } else if constexpr (std::is_invocable_r_v<bool, Guard>) {
        return guard();
    } else {
        return true;
    }
}

template <typename Guard, typename Event, typename SrcState, typename Context, typename Fsm>
constexpr bool call_guard(const Guard& guard, const Event& event, const SrcState& src, Context& ctx, const Fsm& fsm) {
    if constexpr (std::is_invocable_r_v<bool, Guard, const Event&, const SrcState&, Context&, const Fsm&>) {
        return guard(event, src, ctx, fsm);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, const Event&, const SrcState&, Context&>) {
        return guard(event, src, ctx);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, const Event&, Context&>) {
        return guard(event, ctx);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, const SrcState&, Context&>) {
        return guard(src, ctx);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, Context&>) {
        return guard(ctx);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, const Event&, const SrcState&, const Fsm&>) {
        return guard(event, src, fsm);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, const Fsm&>) {
        return guard(fsm);
    } else {
        return call_guard(guard, event, src, ctx);
    }
}

// ----------------------------------------------------------------------------
// Safe invocation of action
// ----------------------------------------------------------------------------
template <typename Action, typename Event, typename SrcState, typename DstState, typename Context>
constexpr void call_action(Action& action, const Event& event, SrcState& src, DstState& dst, Context& ctx) {
    if constexpr (std::is_invocable_v<Action, const Event&, SrcState&, DstState&, Context&>) {
        action(event, src, dst, ctx);
    } else if constexpr (std::is_invocable_v<Action, const Event&, SrcState&, Context&>) {
        action(event, src, ctx);
    } else if constexpr (std::is_invocable_v<Action, const Event&, Context&>) {
        action(event, ctx);
    } else if constexpr (std::is_invocable_v<Action, Context&>) {
        action(ctx);
    } else if constexpr (std::is_invocable_v<Action, const Event&, SrcState&, DstState&>) {
        action(event, src, dst);
    } else if constexpr (std::is_invocable_v<Action, const Event&, SrcState&>) {
        action(event, src);
    } else if constexpr (std::is_invocable_v<Action, const Event&>) {
        action(event);
    } else if constexpr (std::is_invocable_v<Action>) {
        action();
    }
}

}  // namespace fsm
