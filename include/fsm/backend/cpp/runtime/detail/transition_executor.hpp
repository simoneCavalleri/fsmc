#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "fsm/backend/cpp/runtime/traits/dispatch_result.hpp"
#include "fsm/backend/cpp/runtime/traits/hook_traits.hpp"
#include "fsm/backend/cpp/runtime/traits/observer_traits.hpp"
#include "fsm/backend/cpp/runtime/traits/reflection.hpp"
#include "fsm/backend/cpp/runtime/transition.hpp"

namespace fsm::detail {

template <typename Current, typename Target, typename State, typename Event, typename In, typename Out,
          typename Registers, typename Services>
constexpr void call_hierarchical_on_exit(State& current_state, const Event& event, const In& in, Out& out,
                                         Registers& reg, Services& srv) {
    call_on_exit(current_state, event, in, out, reg, srv);
    if constexpr (has_parent_type<State>::value) {
        using Parent = typename State::parent_type;
        if constexpr (!is_substate_of_v<Target, Parent>) {
            if constexpr (std::is_default_constructible_v<Parent>) {
                Parent parent_inst{};
                call_hierarchical_on_exit<Current, Target, Parent>(parent_inst, event, in, out, reg, srv);
            }
        }
    }
}

template <typename Current, typename Target, typename Ancestor, typename Event, typename In, typename Out,
          typename Registers, typename Services>
constexpr void call_ancestor_on_enter_helper(const Event& event, const In& in, Out& out, Registers& reg,
                                             Services& srv) {
    if constexpr (has_parent_type<Ancestor>::value) {
        using SuperParent = typename Ancestor::parent_type;
        if constexpr (!is_substate_of_v<Current, SuperParent>) {
            call_ancestor_on_enter_helper<Current, Target, SuperParent>(event, in, out, reg, srv);
        }
    }
    if constexpr (!std::is_same_v<Ancestor, Target>) {
        if constexpr (std::is_default_constructible_v<Ancestor>) {
            Ancestor ancestor_inst{};
            call_on_enter(ancestor_inst, event, in, out, reg, srv);
        }
    }
}

template <typename Current, typename Target, typename State, typename Event, typename In, typename Out,
          typename Registers, typename Services>
constexpr void call_hierarchical_on_enter(State& target_state, const Event& event, const In& in, Out& out,
                                          Registers& reg, Services& srv) {
    if constexpr (has_parent_type<Target>::value) {
        using Parent = typename Target::parent_type;
        if constexpr (!is_substate_of_v<Current, Parent>) {
            call_ancestor_on_enter_helper<Current, Target, Parent>(event, in, out, reg, srv);
        }
    }
    call_on_enter(target_state, event, in, out, reg, srv);
}

template <typename Ancestor, typename Event, typename In, typename Out, typename Registers, typename Services>
constexpr void call_initial_ancestor_on_enter(const Event& event, const In& in, Out& out, Registers& reg,
                                              Services& srv) {
    if constexpr (has_parent_type<Ancestor>::value) {
        using SuperParent = typename Ancestor::parent_type;
        call_initial_ancestor_on_enter<SuperParent>(event, in, out, reg, srv);
    }
    if constexpr (std::is_default_constructible_v<Ancestor>) {
        Ancestor ancestor_inst{};
        call_on_enter(ancestor_inst, event, in, out, reg, srv);
    }
}

template <typename Target, typename Event, typename In, typename Out, typename Registers, typename Services>
constexpr void call_initial_hierarchical_on_enter(Target& target_state, const Event& event, const In& in, Out& out,
                                                  Registers& reg, Services& srv) {
    if constexpr (has_parent_type<Target>::value) {
        using Parent = typename Target::parent_type;
        call_initial_ancestor_on_enter<Parent>(event, in, out, reg, srv);
    }
    call_on_enter(target_state, event, in, out, reg, srv);
}

template <typename Ancestor, typename In, typename Out, typename Registers, typename Services>
constexpr void call_initial_ancestor_on_enter(const In& in, Out& out, Registers& reg, Services& srv) {
    if constexpr (has_parent_type<Ancestor>::value) {
        using SuperParent = typename Ancestor::parent_type;
        call_initial_ancestor_on_enter<SuperParent>(in, out, reg, srv);
    }
    if constexpr (std::is_default_constructible_v<Ancestor>) {
        Ancestor ancestor_inst{};
        call_on_enter(ancestor_inst, anonymous_event{}, in, out, reg, srv);
    }
}

template <typename Target, typename In, typename Out, typename Registers, typename Services>
constexpr void call_initial_hierarchical_on_enter(Target& target_state, const In& in, Out& out, Registers& reg,
                                                  Services& srv) {
    if constexpr (has_parent_type<Target>::value) {
        using Parent = typename Target::parent_type;
        call_initial_ancestor_on_enter<Parent>(in, out, reg, srv);
    }
    call_on_enter(target_state, anonymous_event{}, in, out, reg, srv);
}

template <typename Table, typename CurrentSrc, typename Event, typename In, typename Out, typename Registers,
          typename Services, typename FsmInstance, typename ObserverCallback, typename RecordHistoryFn,
          std::size_t... Indices>
dispatch_result execute_transition_from_ports(CurrentSrc& src_state, const Event& event, const In& in, Out& out,
                                              Registers& registers_, Services* services_, FsmInstance& fsm_inst,
                                              const ObserverCallback& observer_, RecordHistoryFn&& record_history_fn,
                                              std::index_sequence<Indices...> /*indices*/) {
    constexpr bool has_observer = !std::is_same_v<std::decay_t<ObserverCallback>, no_observer>;

    auto resolve_srv = [&]() -> Services& {
        if (services_ != nullptr) {
            return *services_;
        }
        if constexpr (std::is_default_constructible_v<Services>) {
            static Services dummy{};
            return dummy;
        } else {
            std::terminate();
        }
    };
    Services& srv = resolve_srv();

    bool any_guard_rejected = false;
    std::optional<transition_trace> executed_trace = std::nullopt;
    std::optional<transition_trace> last_rejected_trace = std::nullopt;

    auto try_index = [&](auto idx_constant) -> bool {
        constexpr std::size_t Index = decltype(idx_constant)::value;
        using RowType = std::tuple_element_t<Index, decltype(Table::rows)>;
        using TransSrc = typename RowType::source;
        using TransEvt = typename RowType::event;

        if constexpr ((std::is_same_v<CurrentSrc, TransSrc> || is_substate_of_v<CurrentSrc, TransSrc>) &&
                      (std::is_same_v<std::decay_t<Event>, TransEvt> ||
                       (std::is_same_v<std::decay_t<Event>, anonymous_event> &&
                        std::is_same_v<TransEvt, anonymous_event>))) {
            using Guard = typename RowType::guard_type;
            using Action = typename RowType::action_type;
            using TransDst = typename RowType::target;

#if __cplusplus >= 202002L
            using ExpectedDst = std::conditional_t<RowType::is_internal, CurrentSrc, TransDst>;
            static_assert(::fsm::Guard<Guard, Event, CurrentSrc, In, Registers, Services> ||
                              std::is_same_v<Guard, no_guard> ||
                              std::is_invocable_v<Guard, const Event&, const CurrentSrc&, const In&, const Registers&,
                                                  Services&, const FsmInstance&>,
                          "Guard functor does not satisfy fsm::Guard concept for the specified state machine domain.");
            static_assert(
                ::fsm::Action<Action, Event, CurrentSrc, ExpectedDst, In, Out, Registers, Services> ||
                    std::is_same_v<Action, no_action>,
                "Action functor does not satisfy fsm::Action concept for the specified state machine domain.");
#endif

            const auto src_name = get_state_name(src_state);
            const auto evt_name = get_event_name(event);

            bool guard_passed = call_guard(Guard{}, event, src_state, in, registers_, srv, fsm_inst);

            if (!guard_passed) {
                any_guard_rejected = true;
                using TargetState = typename RowType::target;
                last_rejected_trace =
                    transition_trace{src_name,
                                     RowType::is_internal ? src_name : get_state_name_static<TargetState>(),
                                     evt_name,
                                     get_type_name<Guard>(),
                                     get_type_name<Action>(),
                                     RowType::is_internal ? transition_kind::internal : transition_kind::external};
                return false;
            }

            if constexpr (RowType::is_internal) {
                Action act{};
                call_action(act, event, src_state, src_state, in, out, registers_, srv);
                executed_trace = transition_trace{src_name,
                                                  src_name,
                                                  evt_name,
                                                  get_type_name<Guard>(),
                                                  get_type_name<Action>(),
                                                  transition_kind::internal};
                if constexpr (has_observer) {
                    observer_(transition_info{src_name, src_name, evt_name, dispatch_status::success,
                                              transition_kind::internal});
                }
                return true;
            } else {
                constexpr std::string_view src_parent = get_parent_name<CurrentSrc>();
                if constexpr (!src_parent.empty() || detail::has_parent_type<CurrentSrc>::value) {
                    record_ancestor_history<CurrentSrc>(src_name, record_history_fn);
                }

                // 4-Phase Transition Lifecycle:
                // 1. on_exit(src_state)
                // 2. action(event, src, dst, in, out, reg, srv)
                // 3. state reassignment
                // 4. on_enter(dst_state)
                TransDst dst_state{};
                call_hierarchical_on_exit<CurrentSrc, TransDst>(src_state, event, in, out, registers_, srv);
                Action act{};
                call_action(act, event, src_state, dst_state, in, out, registers_, srv);

                fsm_inst.set_current_state_variant(std::move(dst_state));
                call_hierarchical_on_enter<CurrentSrc, TransDst>(
                    std::get<TransDst>(fsm_inst.get_current_state_variant()), event, in, out, registers_, srv);

                const auto dst_name = get_state_name(std::get<TransDst>(fsm_inst.get_current_state_variant()));
                executed_trace = transition_trace{src_name,
                                                  dst_name,
                                                  evt_name,
                                                  get_type_name<Guard>(),
                                                  get_type_name<Action>(),
                                                  transition_kind::external};
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
        return dispatch_result{dispatch_status::success, executed_trace};
    }
    if (any_guard_rejected) {
        return dispatch_result{dispatch_status::guard_rejected, last_rejected_trace};
    }
    return dispatch_result{
        dispatch_status::unhandled,
        transition_trace{get_state_name(src_state), {}, get_event_name(event), {}, {}, transition_kind::external}};
}

}  // namespace fsm::detail
