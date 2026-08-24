#pragma once

#include <ostream>

#include "fsm/backend/cpp/cpp_options.hpp"

namespace fsm::codegen {

class Cpp20StandaloneRuntime {
  public:
    static void emit(std::ostream& out, const GeneratorOptions& opts) {
        out << "#include <cstdint>\n";
        out << "#include <string>\n";
        out << "#include <string_view>\n";
        out << "#include <variant>\n";
        out << "#include <tuple>\n";
        out << "#include <concepts>\n";
        out << "#include <type_traits>\n";
        out << "#include <utility>\n";
        out << "#include <functional>\n";
        out << "#include <vector>\n";
        out << "#include <deque>\n";
        out << "#include <chrono>\n";
        out << "#include <memory>\n";
        out << "#include <exception>\n";
        if (opts.thread_safe) {
            out << "#include <queue>\n";
            out << "#include <mutex>\n";
            out << "#include <condition_variable>\n";
            out << "#include <thread>\n";
            out << "#include <stop_token>\n";
            out << "#include <atomic>\n";
            out << "#include <future>\n";
        }
        out << "\n";

        out << "namespace fsm {\n\n";
        out << "struct no_context {};\n\n";

        // Dispatch result
        out << "enum class dispatch_status : std::uint8_t {\n";
        out << "    success,\n";
        out << "    deferred,\n";
        out << "    guard_rejected,\n";
        out << "    unhandled\n";
        out << "};\n\n";

        out << "inline constexpr std::string_view to_string(dispatch_status s) noexcept {\n";
        out << "    switch (s) {\n";
        out << "        case dispatch_status::success: return \"success\";\n";
        out << "        case dispatch_status::deferred: return \"deferred\";\n";
        out << "        case dispatch_status::guard_rejected: return \"guard_rejected\";\n";
        out << "        case dispatch_status::unhandled: return \"unhandled\";\n";
        out << "    }\n";
        out << "    return \"unknown\";\n";
        out << "}\n\n";

        out << "struct dispatch_result {\n";
        out << "    dispatch_status status = dispatch_status::unhandled;\n\n";
        out << "    constexpr dispatch_result() noexcept = default;\n";
        out << "    constexpr dispatch_result(dispatch_status s) noexcept : status(s) {}\n\n";
        out << "    [[nodiscard]] constexpr bool is_success() const noexcept { return status == "
               "dispatch_status::success; }\n";
        out << "    [[nodiscard]] constexpr bool is_deferred() const noexcept { return status == "
               "dispatch_status::deferred; }\n";
        out << "    [[nodiscard]] constexpr bool is_guard_rejected() const noexcept { return status == "
               "dispatch_status::guard_rejected; }\n";
        out << "    [[nodiscard]] constexpr bool is_unhandled() const noexcept { return status == "
               "dispatch_status::unhandled; }\n";
        out << "    [[nodiscard]] constexpr bool is_ok() const noexcept { return is_success() || is_deferred(); }\n\n";
        out << "    [[nodiscard]] constexpr explicit operator bool() const noexcept { return is_ok(); }\n";
        out << "    constexpr bool operator==(const dispatch_result& other) const noexcept { return status == "
               "other.status; }\n";
        out << "    constexpr bool operator==(dispatch_status other_status) const noexcept { return status == "
               "other_status; }\n";
        out << "    constexpr bool operator!=(const dispatch_result& other) const noexcept { return status != "
               "other.status; }\n";
        out << "    constexpr bool operator!=(dispatch_status other_status) const noexcept { return status != "
               "other_status; }\n\n";
        out << "    [[nodiscard]] constexpr std::string_view to_string() const noexcept { return "
               "::fsm::to_string(status); }\n";
        out << "};\n\n";

        // Transition kind & info
        out << "enum class transition_kind : std::uint8_t {\n";
        out << "    external,\n";
        out << "    internal\n";
        out << "};\n\n";

        out << "inline constexpr std::string_view to_string(transition_kind k) noexcept {\n";
        out << "    switch (k) {\n";
        out << "        case transition_kind::external: return \"external\";\n";
        out << "        case transition_kind::internal: return \"internal\";\n";
        out << "    }\n";
        out << "    return \"external\";\n";
        out << "}\n\n";

        out << "struct transition_info {\n";
        out << "    std::string_view source;\n";
        out << "    std::string_view target;\n";
        out << "    std::string_view event;\n";
        out << "    dispatch_status status = dispatch_status::success;\n";
        out << "    transition_kind kind = transition_kind::external;\n\n";
        out << "    [[nodiscard]] constexpr bool is_internal() const noexcept { return kind == "
               "transition_kind::internal; }\n";
        out << "    [[nodiscard]] constexpr bool is_external() const noexcept { return kind == "
               "transition_kind::external; }\n";
        out << "    [[nodiscard]] constexpr bool is_success() const noexcept { return status == "
               "dispatch_status::success; }\n";
        out << "    [[nodiscard]] constexpr bool is_deferred() const noexcept { return status == "
               "dispatch_status::deferred; }\n";
        out << "    [[nodiscard]] constexpr bool is_guard_rejected() const noexcept { return status == "
               "dispatch_status::guard_rejected; }\n";
        out << "    [[nodiscard]] constexpr bool is_unhandled() const noexcept { return status == "
               "dispatch_status::unhandled; }\n";
        out << "};\n\n";

        // Type list helpers
        out << "template <typename... Ts> struct type_list {};\n\n";

        out << "namespace detail {\n";
        out << "template <typename T, typename List> struct contains;\n";
        out << "template <typename T> struct contains<T, type_list<>> : std::false_type {};\n";
        out << "template <typename T, typename... Rest> struct contains<T, type_list<T, Rest...>> : std::true_type "
               "{};\n";
        out << "template <typename T, typename Head, typename... Rest> struct contains<T, type_list<Head, Rest...>> : "
               "contains<T, type_list<Rest...>> {};\n\n";

        out << "template <typename List, typename T> struct append_unique;\n";
        out << "template <typename... Ts, typename T> struct append_unique<type_list<Ts...>, T> {\n";
        out << "    using type = std::conditional_t<contains<T, type_list<Ts...>>::value, type_list<Ts...>, "
               "type_list<Ts..., T>>;\n";
        out << "};\n";
        out << "template <typename List, typename Result = type_list<>> struct unique;\n";
        out << "template <typename Result> struct unique<type_list<>, Result> { using type = Result; };\n";
        out << "template <typename Head, typename... Tail, typename Result> struct unique<type_list<Head, Tail...>, "
               "Result> {\n";
        out << "  private:\n";
        out << "    using next_result = typename append_unique<Result, Head>::type;\n";
        out << "  public:\n";
        out << "    using type = typename unique<type_list<Tail...>, next_result>::type;\n";
        out << "};\n";
        out << "template <typename List> using unique_t = typename unique<List>::type;\n\n";

        out << "template <typename List> struct to_variant;\n";
        out << "template <typename... Ts> struct to_variant<type_list<Ts...>> { using type = std::variant<Ts...>; };\n";
        out << "template <typename List> using to_variant_t = typename to_variant<List>::type;\n";
        out << "} // namespace detail\n\n";

        out << "namespace detail {\n";
        out << "template <typename State, typename = void> struct has_deferred_events : std::false_type {};\n";
        out << "template <typename State> struct has_deferred_events<State, std::void_t<typename "
               "State::deferred_events>> : std::true_type {};\n";
        out << "} // namespace detail\n\n";

        out << "template <typename State, typename Event>\n";
        out << "inline constexpr bool is_deferred_event_v = []() constexpr {\n";
        out << "    if constexpr (detail::has_deferred_events<State>::value) {\n";
        out << "        return detail::contains<std::decay_t<Event>, typename State::deferred_events>::value;\n";
        out << "    } else {\n";
        out << "        return false;\n";
        out << "    }\n";
        out << "}();\n\n";

        out << "struct no_guard {\n";
        out << "    [[nodiscard]] constexpr bool operator()(const auto&...) const noexcept { return true; }\n";
        out << "};\n\n";

        out << "struct no_action {\n";
        out << "    constexpr void operator()(auto&...) const noexcept {}\n";
        out << "};\n\n";

        // Combinators
        out << "template <typename Guard, typename Event, typename State, typename Context>\n";
        out << "constexpr bool invoke_guard(const Guard& g, const Event& evt, const State& s, Context* ctx);\n\n";

        out << "template <typename Guard, typename Event, typename State, typename Context, typename Fsm>\n";
        out << "constexpr bool invoke_guard(const Guard& g, const Event& evt, const State& s, Context* ctx, const Fsm& "
               "fsm);\n\n";

        out << "template <typename Guard>\n";
        out << "struct not_ {\n";
        out << "    Guard guard_fn{};\n";
        out << "    constexpr not_() = default;\n";
        out << "    constexpr explicit not_(Guard g) : guard_fn(std::move(g)) {}\n";
        out << "    template <typename Event, typename State, typename Context, typename Fsm>\n";
        out << "    constexpr bool operator()(const Event& evt, const State& s, Context& ctx, const Fsm& fsm) const "
               "{\n";
        out << "        return !invoke_guard(guard_fn, evt, s, &ctx, fsm);\n";
        out << "    }\n";
        out << "    template <typename Event, typename State, typename Context>\n";
        out << "    constexpr bool operator()(const Event& evt, const State& s, Context& ctx) const {\n";
        out << "        return !invoke_guard(guard_fn, evt, s, &ctx);\n";
        out << "    }\n";
        out << "};\n\n";

        out << "template <typename Guard1, typename Guard2, typename... Rest>\n";
        out << "struct and_ {\n";
        out << "    Guard1 g1{};\n";
        out << "    Guard2 g2{};\n";
        out << "    constexpr and_() = default;\n";
        out << "    constexpr and_(Guard1 first, Guard2 second) : g1(std::move(first)), g2(std::move(second)) {}\n";
        out << "    template <typename Event, typename State, typename Context, typename Fsm>\n";
        out << "    constexpr bool operator()(const Event& evt, const State& s, Context& ctx, const Fsm& fsm) const "
               "{\n";
        out << "        if (!invoke_guard(g1, evt, s, &ctx, fsm)) return false;\n";
        out << "        if constexpr (sizeof...(Rest) == 0) {\n";
        out << "            return invoke_guard(g2, evt, s, &ctx, fsm);\n";
        out << "        } else {\n";
        out << "            return and_<Guard2, Rest...>{}(evt, s, ctx, fsm);\n";
        out << "        }\n";
        out << "    }\n";
        out << "    template <typename Event, typename State, typename Context>\n";
        out << "    constexpr bool operator()(const Event& evt, const State& s, Context& ctx) const {\n";
        out << "        if (!invoke_guard(g1, evt, s, &ctx)) return false;\n";
        out << "        if constexpr (sizeof...(Rest) == 0) {\n";
        out << "            return invoke_guard(g2, evt, s, &ctx);\n";
        out << "        } else {\n";
        out << "            return and_<Guard2, Rest...>{}(evt, s, ctx);\n";
        out << "        }\n";
        out << "    }\n";
        out << "};\n\n";

        out << "template <typename Guard1, typename Guard2, typename... Rest>\n";
        out << "struct or_ {\n";
        out << "    Guard1 g1{};\n";
        out << "    Guard2 g2{};\n";
        out << "    constexpr or_() = default;\n";
        out << "    constexpr or_(Guard1 first, Guard2 second) : g1(std::move(first)), g2(std::move(second)) {}\n";
        out << "    template <typename Event, typename State, typename Context, typename Fsm>\n";
        out << "    constexpr bool operator()(const Event& evt, const State& s, Context& ctx, const Fsm& fsm) const "
               "{\n";
        out << "        if (invoke_guard(g1, evt, s, &ctx, fsm)) return true;\n";
        out << "        if constexpr (sizeof...(Rest) == 0) {\n";
        out << "            return invoke_guard(g2, evt, s, &ctx, fsm);\n";
        out << "        } else {\n";
        out << "            return or_<Guard2, Rest...>{}(evt, s, ctx, fsm);\n";
        out << "        }\n";
        out << "    }\n";
        out << "    template <typename Event, typename State, typename Context>\n";
        out << "    constexpr bool operator()(const Event& evt, const State& s, Context& ctx) const {\n";
        out << "        if (invoke_guard(g1, evt, s, &ctx)) return true;\n";
        out << "        if constexpr (sizeof...(Rest) == 0) {\n";
        out << "            return invoke_guard(g2, evt, s, &ctx);\n";
        out << "        } else {\n";
        out << "            return or_<Guard2, Rest...>{}(evt, s, ctx);\n";
        out << "        }\n";
        out << "    }\n";
        out << "};\n\n";

        out << "template <typename ParentState, typename SubState>\n";
        out << "struct history_is {\n";
        out << "    template <typename Event, typename State, typename Context, typename Fsm>\n";
        out << "    constexpr bool operator()(const Event&, const State&, Context&, const Fsm& fsm) const {\n";
        out << "        return fsm.get_history(ParentState::name) == SubState::name;\n";
        out << "    }\n";
        out << "    template <typename Event, typename State, typename Context>\n";
        out << "    constexpr bool operator()(const Event&, const State&, Context&) const {\n";
        out << "        return false;\n";
        out << "    }\n";
        out << "};\n\n";

        // Row DSL
        out << "template <typename Src, typename Evt, typename Dst, typename GuardType = no_guard, typename ActionType "
               "= no_action>\n";
        out << "struct row {\n";
        out << "    using source_state = Src;\n";
        out << "    using event_type = Evt;\n";
        out << "    using target_state = Dst;\n";
        out << "    using guard_type = GuardType;\n";
        out << "    using action_type = ActionType;\n";
        out << "    static constexpr bool is_internal = false;\n\n";
        out << "    template <typename G> using when = row<Src, Evt, Dst, G, ActionType>;\n";
        out << "    template <typename A> using then = row<Src, Evt, Dst, GuardType, A>;\n";
        out << "};\n\n";

        out << "template <typename State, typename Evt, typename GuardType = no_guard, typename ActionType = "
               "no_action>\n";
        out << "struct internal_row {\n";
        out << "    using source_state = State;\n";
        out << "    using event_type = Evt;\n";
        out << "    using target_state = State;\n";
        out << "    using guard_type = GuardType;\n";
        out << "    using action_type = ActionType;\n";
        out << "    static constexpr bool is_internal = true;\n\n";
        out << "    template <typename G> using when = internal_row<State, Evt, G, ActionType>;\n";
        out << "    template <typename A> using then = internal_row<State, Evt, GuardType, A>;\n";
        out << "};\n\n";

        // Table
        out << "template <typename... Rows>\n";
        out << "struct transition_table {\n";
        out << "    using rows = std::tuple<Rows...>;\n";
        out << "    using raw_states = type_list<typename Rows::source_state..., typename Rows::target_state...>;\n";
        out << "    using unique_states = detail::unique_t<raw_states>;\n";
        out << "    using state_variant = detail::to_variant_t<unique_states>;\n";
        out << "};\n\n";

        // State name helper
        out << "template <typename State>\n";
        out << "constexpr std::string_view get_state_name(const State&) noexcept {\n";
        out << "    if constexpr (requires { State::name; }) {\n";
        out << "        return State::name;\n";
        out << "    } else {\n";
        out << "        return \"State\";\n";
        out << "    }\n";
        out << "}\n\n";

        // Event name helper
        out << "template <typename Event>\n";
        out << "constexpr std::string_view get_event_name(const Event&) noexcept {\n";
        out << "    if constexpr (requires { Event::name; }) {\n";
        out << "        return Event::name;\n";
        out << "    } else {\n";
        out << "        return \"Event\";\n";
        out << "    }\n";
        out << "}\n\n";

        // Invocations
        out << "template <typename Guard, typename Event, typename State, typename Context>\n";
        out << "constexpr bool invoke_guard(const Guard& g, const Event& evt, const State& s, Context* ctx) {\n";
        out << "    if constexpr (requires { { g(evt, s, *ctx) } -> std::convertible_to<bool>; }) {\n";
        out << "        return ctx ? g(evt, s, *ctx) : true;\n";
        out << "    } else if constexpr (requires { { g(evt, s) } -> std::convertible_to<bool>; }) {\n";
        out << "        return g(evt, s);\n";
        out << "    } else if constexpr (requires { { g(evt) } -> std::convertible_to<bool>; }) {\n";
        out << "        return g(evt);\n";
        out << "    } else if constexpr (requires { { g() } -> std::convertible_to<bool>; }) {\n";
        out << "        return g();\n";
        out << "    } else { return true; }\n";
        out << "}\n\n";

        out << "template <typename Guard, typename Event, typename State, typename Context, typename Fsm>\n";
        out << "constexpr bool invoke_guard(const Guard& g, const Event& evt, const State& s, Context* ctx, const Fsm& "
               "fsm) {\n";
        out << "    if constexpr (requires { { g(evt, s, *ctx, fsm) } -> std::convertible_to<bool>; }) {\n";
        out << "        return ctx ? g(evt, s, *ctx, fsm) : true;\n";
        out << "    } else if constexpr (requires { { g(evt, s, fsm) } -> std::convertible_to<bool>; }) {\n";
        out << "        return g(evt, s, fsm);\n";
        out << "    } else if constexpr (requires { { g(fsm) } -> std::convertible_to<bool>; }) {\n";
        out << "        return g(fsm);\n";
        out << "    } else {\n";
        out << "        return invoke_guard(g, evt, s, ctx);\n";
        out << "    }\n";
        out << "}\n\n";

        out << "template <typename Action, typename Event, typename SrcState, typename DstState, typename Context>\n";
        out << "constexpr void invoke_action(const Action& a, const Event& evt, SrcState& src, DstState& dst, Context* "
               "ctx) {\n";
        out << "    if constexpr (requires { a(evt, src, dst, *ctx); }) {\n";
        out << "        if (ctx) a(evt, src, dst, *ctx);\n";
        out << "    } else if constexpr (requires { a(evt, src, dst); }) {\n";
        out << "        a(evt, src, dst);\n";
        out << "    } else if constexpr (requires { a(evt, dst); }) {\n";
        out << "        a(evt, dst);\n";
        out << "    } else if constexpr (requires { a(evt); }) {\n";
        out << "        a(evt);\n";
        out << "    } else if constexpr (requires { a(); }) {\n";
        out << "        a();\n";
        out << "    }\n";
        out << "}\n\n";

        out << "template <typename State, typename Context>\n";
        out << "constexpr void invoke_enter_hook(State& s, Context* ctx) {\n";
        out << "    if constexpr (requires(State& st, Context& c) { st.on_enter(c); }) {\n";
        out << "        if (ctx) s.on_enter(*ctx);\n";
        out << "    } else if constexpr (requires(State& st) { st.on_enter(); }) {\n";
        out << "        s.on_enter();\n";
        out << "    }\n";
        out << "}\n\n";

        out << "template <typename State, typename Context>\n";
        out << "constexpr void invoke_exit_hook(State& s, Context* ctx) {\n";
        out << "    if constexpr (requires(State& st, Context& c) { st.on_exit(c); }) {\n";
        out << "        if (ctx) s.on_exit(*ctx);\n";
        out << "    } else if constexpr (requires(State& st) { st.on_exit(); }) {\n";
        out << "        s.on_exit();\n";
        out << "    }\n";
        out << "}\n\n";

        // FSM class
        out << "template <typename Table, typename Context = no_context, typename InitialState = typename "
               "std::tuple_element_t<0, typename Table::rows>::source_state>\n";
        out << "class fsm {\n";
        out << "public:\n";
        out << "    using table_type = Table;\n";
        out << "    using context_type = Context;\n";
        out << "    using state_variant = typename Table::state_variant;\n";
        out << "    using observer_type = std::function<void(const transition_info&)>;\n\n";
        out << "    static constexpr std::size_t state_count = std::variant_size_v<state_variant>;\n";
        out << "    static constexpr std::size_t transition_count = std::tuple_size_v<typename Table::rows>;\n";
        out << "    template <typename State> static constexpr bool has_state = detail::contains<State, typename "
               "Table::unique_states>::value;\n\n";
        out << "    constexpr fsm() requires (std::is_same_v<Context, no_context>) : current_state_(InitialState{}), "
               "context_(nullptr) { enter_initial(); }\n";
        out << "    constexpr explicit fsm(Context& ctx) : current_state_(InitialState{}), context_(&ctx) { "
               "enter_initial(); }\n";
        out << "    constexpr explicit fsm(Context& ctx, InitialState init) : current_state_(std::move(init)), "
               "context_(&ctx) { enter_initial(); }\n\n";

        out << "    template <typename Event>\n";
        out << "    dispatch_result dispatch_direct(const Event& event) {\n";
        out << "        return std::visit([this, &event](auto& src) -> dispatch_result {\n";
        out << "            return this->template process_event<Event, std::decay_t<decltype(src)>>(event, src);\n";
        out << "        }, current_state_);\n";
        out << "    }\n\n";

        out << "    template <typename Event>\n";
        out << "    dispatch_result dispatch(const Event& event) {\n";
        out << "        dispatch_result res = dispatch_direct(event);\n";
        out << "        if (res.is_success()) {\n";
        out << "            process_deferred_queue();\n";
        out << "        } else {\n";
        out << "            const auto src_name = current_state_name();\n";
        out << "            const auto evt_name = get_event_name(event);\n";
        out << "            bool deferred = std::visit([this, &event](const auto& src) -> bool {\n";
        out << "                using CurrentSrc = std::decay_t<decltype(src)>;\n";
        out << "                if constexpr (is_deferred_event_v<CurrentSrc, Event>) {\n";
        out << "                    this->deferred_queue_.push_back([event](fsm& self) -> bool {\n";
        out << "                        return self.dispatch_direct(event).is_success();\n";
        out << "                    });\n";
        out << "                    return true;\n";
        out << "                } else {\n";
        out << "                    return false;\n";
        out << "                }\n";
        out << "            }, current_state_);\n";
        out << "            if (deferred) {\n";
        out << "                if (observer_) observer_(transition_info{src_name, src_name, evt_name, "
               "dispatch_status::deferred, transition_kind::external});\n";
        out << "                return dispatch_result{dispatch_status::deferred};\n";
        out << "            }\n";
        out << "            if (res.is_guard_rejected()) {\n";
        out << "                if (observer_) observer_(transition_info{src_name, src_name, evt_name, "
               "dispatch_status::guard_rejected, transition_kind::external});\n";
        out << "            } else if (res.is_unhandled()) {\n";
        out << "                if (observer_) observer_(transition_info{src_name, src_name, evt_name, "
               "dispatch_status::unhandled, transition_kind::external});\n";
        out << "            }\n";
        out << "        }\n";
        out << "        return res;\n";
        out << "    }\n\n";

        out << "    void process_deferred_queue() {\n";
        out << "        if (deferred_queue_.empty() || is_replaying_deferred_) return;\n";
        out << "        is_replaying_deferred_ = true;\n";
        out << "        bool any_handled = true;\n";
        out << "        while (any_handled && !deferred_queue_.empty()) {\n";
        out << "            any_handled = false;\n";
        out << "            for (auto it = deferred_queue_.begin(); it != deferred_queue_.end();) {\n";
        out << "                if ((*it)(*this)) {\n";
        out << "                    it = deferred_queue_.erase(it);\n";
        out << "                    any_handled = true;\n";
        out << "                    break;\n";
        out << "                }\n";
        out << "                ++it;\n";
        out << "            }\n";
        out << "        }\n";
        out << "        is_replaying_deferred_ = false;\n";
        out << "    }\n\n";

        out << "    [[nodiscard]] std::size_t deferred_count() const noexcept { return deferred_queue_.size(); }\n";
        out << "    void clear_deferred_events() noexcept { deferred_queue_.clear(); }\n\n";

        out << "    void set_observer(observer_type observer) { observer_ = std::move(observer); }\n\n";

        out << "    struct history_entry {\n";
        out << "        std::string_view parent;\n";
        out << "        std::string_view substate;\n";
        out << "    };\n\n";

        out << "    void record_history(std::string_view parent, std::string_view substate) {\n";
        out << "        if (parent.empty() || substate.empty()) return;\n";
        out << "        for (auto& entry : history_records_) {\n";
        out << "            if (entry.parent == parent) { entry.substate = substate; return; }\n";
        out << "        }\n";
        out << "        history_records_.push_back({parent, substate});\n";
        out << "    }\n\n";

        out << "    [[nodiscard]] std::string_view get_history(std::string_view parent) const noexcept {\n";
        out << "        for (const auto& entry : history_records_) {\n";
        out << "            if (entry.parent == parent) return entry.substate;\n";
        out << "        }\n";
        out << "        return \"\";\n";
        out << "    }\n\n";

        out << "    template <typename State> [[nodiscard]] constexpr bool is_in_state() const noexcept {\n";
        out << "        return std::holds_alternative<State>(current_state_);\n";
        out << "    }\n\n";

        out << "    [[nodiscard]] std::string_view current_state_name() const noexcept {\n";
        out << "        return std::visit([](const auto& s) { return ::fsm::get_state_name(s); }, current_state_);\n";
        out << "    }\n\n";

        out << "    [[nodiscard]] const state_variant& get_current_state_variant() const noexcept { return "
               "current_state_; }\n";
        out << "    [[nodiscard]] Context* get_context() noexcept { return context_; }\n";
        out << "    [[nodiscard]] Context& context() noexcept {\n";
        out << "        if constexpr (std::is_same_v<Context, no_context>) return dummy_ctx_;\n";
        out << "        else return *context_;\n";
        out << "    }\n";
        out << "    [[nodiscard]] const Context& context() const noexcept {\n";
        out << "        if constexpr (std::is_same_v<Context, no_context>) return dummy_ctx_;\n";
        out << "        else return *context_;\n";
        out << "    }\n\n";

        out << "private:\n";
        out << "    constexpr void enter_initial() {\n";
        out << "        std::visit([this](auto& s) { ::fsm::invoke_enter_hook(s, context_); }, current_state_);\n";
        out << "    }\n\n";

        out << "    template <typename Event, typename SrcState>\n";
        out << "    dispatch_result process_event(const Event& evt, SrcState& src) {\n";
        out << "        dispatch_result final_res{dispatch_status::unhandled};\n";
        out << "        std::apply([&](auto... row_inst) {\n";
        out << "            (([&](auto row) {\n";
        out << "                if (final_res.is_success()) return;\n";
        out << "                auto r = try_transition<decltype(row)>(evt, src);\n";
        out << "                if (r.is_success()) final_res = r;\n";
        out << "                else if (r.is_guard_rejected() && final_res.is_unhandled()) final_res = r;\n";
        out << "            }(row_inst)), ...);\n";
        out << "        }, typename Table::rows{});\n";
        out << "        return final_res;\n";
        out << "    }\n\n";

        out << "    template <typename Row, typename Event, typename SrcState>\n";
        out << "    dispatch_result try_transition(const Event& evt, SrcState& src) {\n";
        out << "        using Transition = Row;\n";
        out << "        using Src = typename Transition::source_state;\n";
        out << "        using Dst = typename Transition::target_state;\n";
        out << "        using Evt = typename Transition::event_type;\n";
        out << "        using Guard = typename Transition::guard_type;\n";
        out << "        using Action = typename Transition::action_type;\n\n";

        out << "        if constexpr (std::is_same_v<SrcState, Src> && std::is_same_v<Event, Evt>) {\n";
        out << "            if (!invoke_guard(Guard{}, evt, src, context_, *this)) {\n";
        out << "                return dispatch_result{dispatch_status::guard_rejected};\n";
        out << "            }\n";
        out << "            const auto src_name = get_state_name(src);\n";
        out << "            const auto evt_name = get_event_name(evt);\n";
        out << "            if constexpr (Transition::is_internal) {\n";
        out << "                invoke_action(Action{}, evt, src, src, context_);\n";
        out << "                if (observer_) observer_(transition_info{src_name, src_name, evt_name, "
               "dispatch_status::success, transition_kind::internal});\n";
        out << "                return dispatch_result{dispatch_status::success};\n";
        out << "            } else {\n";
        out << "                invoke_exit_hook(src, context_);\n";
        out << "                Dst dst{};\n";
        out << "                invoke_action(Action{}, evt, src, dst, context_);\n";
        out << "                current_state_ = std::move(dst);\n";
        out << "                invoke_enter_hook(std::get<Dst>(current_state_), context_);\n";
        out << "                if constexpr (requires { Dst::parent; }) {\n";
        out << "                    record_history(Dst::parent, Dst::name);\n";
        out << "                }\n";
        out << "                const auto dst_name = get_state_name(std::get<Dst>(current_state_));\n";
        out << "                if (observer_) observer_(transition_info{src_name, dst_name, evt_name, "
               "dispatch_status::success, transition_kind::external});\n";
        out << "                return dispatch_result{dispatch_status::success};\n";
        out << "            }\n";
        out << "        }\n";
        out << "        return dispatch_result{dispatch_status::unhandled};\n";
        out << "    }\n\n";

        out << "    state_variant current_state_;\n";
        out << "    Context* context_{nullptr};\n";
        out << "    no_context dummy_ctx_{};\n";
        out << "    std::vector<history_entry> history_records_;\n";
        out << "    std::vector<std::function<bool(fsm&)>> deferred_queue_;\n";
        out << "    bool is_replaying_deferred_{false};\n";
        out << "    observer_type observer_;\n";
        out << "};\n\n";

        // Thread-Safe wrapper in C++20
        if (opts.thread_safe) {
            out << "template <typename Table, typename Context = no_context, typename InitialState = typename "
                   "std::tuple_element_t<0, typename Table::rows>::source_state>\n";
            out << "class thread_safe_fsm {\n";
            out << "public:\n";
            out << "    using fsm_type = fsm<Table, Context, InitialState>;\n";
            out << "    using event_handler = std::function<void(fsm_type&)>;\n";
            out << "    using unhandled_handler = std::function<void(std::string_view, std::string_view)>;\n";
            out << "    using guard_rejected_handler = std::function<void(std::string_view, std::string_view)>;\n";
            out << "    using deferred_handler = std::function<void(std::string_view, std::string_view)>;\n";
            out << "    using dispatch_failure_handler = std::function<void(std::string_view, std::string_view, "
                   "dispatch_status)>;\n";
            out << "    using exception_handler = std::function<void(std::exception_ptr)>;\n\n";

            out << "    constexpr thread_safe_fsm() requires (std::is_same_v<Context, no_context>) = default;\n";
            out << "    constexpr explicit thread_safe_fsm(Context& ctx) : fsm_(ctx) {}\n";
            out << "    ~thread_safe_fsm() {\n";
            out << "        stop_worker();\n";
            out << "        clear_queue();\n";
            out << "    }\n\n";

            out << "    template <typename Callable>\n";
            out << "    auto with_context(Callable&& callable) {\n";
            out << "        std::scoped_lock lock(mutex_);\n";
            out << "        return std::forward<Callable>(callable)(fsm_.context());\n";
            out << "    }\n\n";

            out << "    template <typename Callable>\n";
            out << "    auto with_context(Callable&& callable) const {\n";
            out << "        std::scoped_lock lock(mutex_);\n";
            out << "        return std::forward<Callable>(callable)(fsm_.context());\n";
            out << "    }\n\n";

            out << "    /** @warning Direct unsynchronized Context access. Prefer with_context() for thread-safety. "
                   "*/\n";
            out << "    [[nodiscard]] Context& context() noexcept { return fsm_.context(); }\n";
            out << "    [[nodiscard]] const Context& context() const noexcept { return fsm_.context(); }\n\n";

            out << "    void set_observer(typename fsm_type::observer_type obs) {\n";
            out << "        std::scoped_lock lock(mutex_);\n";
            out << "        user_observer_ = std::move(obs);\n";
            out << "        if (user_observer_) {\n";
            out << "            fsm_.set_observer([this](const transition_info& info) {\n";
            out << "                notification_buffer_.push_back(info);\n";
            out << "            });\n";
            out << "        } else {\n";
            out << "            fsm_.set_observer(nullptr);\n";
            out << "        }\n";
            out << "    }\n\n";

            out << "    void set_unhandled_handler(unhandled_handler handler) { std::scoped_lock lock(mutex_); "
                   "unhandled_handler_ = std::move(handler); }\n";
            out << "    void set_guard_rejected_handler(guard_rejected_handler handler) { std::scoped_lock "
                   "lock(mutex_); guard_rejected_handler_ = std::move(handler); }\n";
            out << "    void set_deferred_handler(deferred_handler handler) { std::scoped_lock lock(mutex_); "
                   "deferred_handler_ = std::move(handler); }\n";
            out << "    void set_dispatch_failure_handler(dispatch_failure_handler handler) { std::scoped_lock "
                   "lock(mutex_); failure_handler_ = std::move(handler); }\n";
            out << "    void set_exception_handler(exception_handler handler) { std::scoped_lock lock(mutex_); "
                   "exception_handler_ = std::move(handler); }\n\n";

            out << "    [[nodiscard]] std::exception_ptr last_exception() const { std::scoped_lock lock(mutex_); "
                   "return last_exception_; }\n";
            out << "    void clear_last_exception() { std::scoped_lock lock(mutex_); last_exception_ = nullptr; }\n\n";

            out << "    /** Synchronous dispatch: executes state transition under lock, notifications outside lock. "
                   "*/\n";
            out << "    template <typename Event>\n";
            out << "    dispatch_result send(const Event& event) {\n";
            out << "        dispatch_snapshot snap;\n";
            out << "        try {\n";
            out << "            snap = execute_dispatch_under_lock(event);\n";
            out << "        } catch (...) {\n";
            out << "            auto ex = std::current_exception();\n";
            out << "            handle_exception_outside_lock(ex, get_exception_handler_copy());\n";
            out << "            std::rethrow_exception(ex);\n";
            out << "        }\n";
            out << "        invoke_notifications_outside_lock(event, snap);\n";
            out << "        return snap.result;\n";
            out << "    }\n\n";

            out << "    [[nodiscard]] bool is_calling_from_worker_thread() const noexcept {\n";
            out << "        const auto id = worker_thread_id_.load(std::memory_order_acquire);\n";
            out << "        return id != std::thread::id{} && std::this_thread::get_id() == id;\n";
            out << "    }\n\n";

            out << "    [[nodiscard]] bool is_calling_from_stopping_thread() const noexcept {\n";
            out << "        const auto id = stopping_thread_id_.load(std::memory_order_acquire);\n";
            out << "        return id != std::thread::id{} && std::this_thread::get_id() == id;\n";
            out << "    }\n\n";

            out << "    /** Asynchronous post (fire-and-forget). Automatically ensures worker thread is running. */\n";
            out << "    template <typename Event>\n";
            out << "    void post(Event&& event) {\n";
            out << "        if (!running_.load() && !is_calling_from_worker_thread()) {\n";
            out << "            start_worker();\n";
            out << "        }\n";
            out << "        enqueue(std::forward<Event>(event));\n";
            out << "    }\n\n";

            out << "    /** Asynchronous post returning std::future. Automatically ensures worker is running so future "
                   "never hangs. */\n";
            out << "    template <typename Event>\n";
            out << "    std::future<dispatch_result> post_async(Event&& event) {\n";
            out << "        if (!running_.load() && !is_calling_from_worker_thread()) {\n";
            out << "            start_worker();\n";
            out << "        }\n";
            out << "        auto p = std::make_shared<std::promise<dispatch_result>>();\n";
            out << "        auto f = p->get_future();\n";
            out << "        auto task = [this, evt = std::forward<Event>(event), p](fsm_type&) {\n";
            out << "            try {\n";
            out << "                auto snap = execute_dispatch_under_lock(evt);\n";
            out << "                invoke_notifications_outside_lock(evt, snap);\n";
            out << "                p->set_value(snap.result);\n";
            out << "            } catch (...) {\n";
            out << "                auto ex = std::current_exception();\n";
            out << "                handle_exception_outside_lock(ex, get_exception_handler_copy());\n";
            out << "                p->set_exception(ex);\n";
            out << "            }\n";
            out << "        };\n";
            out << "        {\n";
            out << "            std::scoped_lock lock(queue_mutex_);\n";
            out << "            if (is_stopping_.load(std::memory_order_acquire) && !is_calling_from_worker_thread() "
                   "&& !is_calling_from_stopping_thread()) {\n";
            out << "                p->set_exception(std::make_exception_ptr(std::runtime_error(\"FSM is shutting "
                   "down\")));\n";
            out << "                return f;\n";
            out << "            }\n";
            out << "            queue_.push_back(std::move(task));\n";
            out << "        }\n";
            out << "        cv_.notify_one();\n";
            out << "        return f;\n";
            out << "    }\n\n";

            out << "    /** Enqueues an event without auto-starting the worker thread (Manual Polling Mode). */\n";
            out << "    template <typename Event>\n";
            out << "    void enqueue(Event&& event) {\n";
            out << "        auto task = [this, evt = std::forward<Event>(event)](fsm_type&) {\n";
            out << "            try {\n";
            out << "                auto snap = execute_dispatch_under_lock(evt);\n";
            out << "                invoke_notifications_outside_lock(evt, snap);\n";
            out << "            } catch (...) {\n";
            out << "                handle_exception_outside_lock(std::current_exception(), "
                   "get_exception_handler_copy());\n";
            out << "            }\n";
            out << "        };\n";
            out << "        {\n";
            out << "            std::scoped_lock lock(queue_mutex_);\n";
            out << "            if (is_stopping_.load(std::memory_order_acquire) && !is_calling_from_worker_thread() "
                   "&& !is_calling_from_stopping_thread()) return;\n";
            out << "            queue_.push_back(std::move(task));\n";
            out << "        }\n";
            out << "        cv_.notify_one();\n";
            out << "    }\n\n";

            out << "    /** Single-Consumer Polling: processes one event in O(1) time. */\n";
            out << "    bool process_one() {\n";
            out << "        if (running_.load(std::memory_order_acquire)) return false;\n";
            out << "        bool expected = false;\n";
            out << "        if (!is_polling_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) "
                   "return false;\n";
            out << "        struct PollingGuard {\n";
            out << "            std::atomic<bool>& flag;\n";
            out << "            ~PollingGuard() { flag.store(false, std::memory_order_release); }\n";
            out << "        } guard{is_polling_};\n";
            out << "        event_handler task;\n";
            out << "        {\n";
            out << "            std::scoped_lock lock(queue_mutex_);\n";
            out << "            if (queue_.empty()) return false;\n";
            out << "            task = std::move(queue_.front());\n";
            out << "            queue_.pop_front();\n";
            out << "        }\n";
            out << "        if (task) {\n";
            out << "            try { task(fsm_); } catch (...) { "
                   "handle_exception_outside_lock(std::current_exception(), get_exception_handler_copy()); return "
                   "false; }\n";
            out << "        }\n";
            out << "        return true;\n";
            out << "    }\n\n";

            out << "    /** Single-Consumer Polling: processes all pending and cascading events. */\n";
            out << "    std::size_t process_all() {\n";
            out << "        if (running_.load(std::memory_order_acquire)) return 0;\n";
            out << "        bool expected = false;\n";
            out << "        if (!is_polling_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) "
                   "return 0;\n";
            out << "        struct PollingGuard {\n";
            out << "            std::atomic<bool>& flag;\n";
            out << "            ~PollingGuard() { flag.store(false, std::memory_order_release); }\n";
            out << "        } guard{is_polling_};\n";
            out << "        std::size_t total = 0;\n";
            out << "        while (true) {\n";
            out << "            std::deque<event_handler> batch;\n";
            out << "            {\n";
            out << "                std::scoped_lock lock(queue_mutex_);\n";
            out << "                if (queue_.empty()) break;\n";
            out << "                batch.swap(queue_);\n";
            out << "            }\n";
            out << "            for (auto& task : batch) {\n";
            out << "                if (task) { try { task(fsm_); } catch (...) { "
                   "handle_exception_outside_lock(std::current_exception(), get_exception_handler_copy()); } }\n";
            out << "            }\n";
            out << "            total += batch.size();\n";
            out << "        }\n";
            out << "        return total;\n";
            out << "    }\n\n";

            out << "    void start_worker() {\n";
            out << "        if (is_calling_from_worker_thread()) return;\n";
            out << "        std::scoped_lock lifecycle_lock(lifecycle_mutex_);\n";
            out << "        {\n";
            out << "            std::scoped_lock q_lock(queue_mutex_);\n";
            out << "            if (running_.load() || is_stopping_.load()) return;\n";
            out << "            running_ = true;\n";
            out << "        }\n";
            out << "        worker_ = std::jthread([this](std::stop_token st) {\n";
            out << "            worker_thread_id_.store(std::this_thread::get_id(), std::memory_order_release);\n";
            out << "            while (!st.stop_requested()) {\n";
            out << "                std::deque<event_handler> batch;\n";
            out << "                {\n";
            out << "                    std::unique_lock<std::mutex> worker_lock(queue_mutex_);\n";
            out << "                    cv_.wait(worker_lock, [&] { return !queue_.empty() || !running_.load(); });\n";
            out << "                    if (!running_.load() && queue_.empty()) break;\n";
            out << "                    batch.swap(queue_);\n";
            out << "                }\n";
            out << "                for (auto& task : batch) {\n";
            out << "                    if (task) { try { task(fsm_); } catch (...) { "
                   "handle_exception_outside_lock(std::current_exception(), get_exception_handler_copy()); } }\n";
            out << "                }\n";
            out << "            }\n";
            out << "            worker_thread_id_.store(std::thread::id{}, std::memory_order_release);\n";
            out << "        });\n";
            out << "    }\n\n";

            out << "    void request_stop() noexcept {\n";
            out << "        {\n";
            out << "            std::scoped_lock q_lock(queue_mutex_);\n";
            out << "            if (!running_.load()) return;\n";
            out << "            running_ = false;\n";
            out << "        }\n";
            out << "        cv_.notify_all();\n";
            out << "    }\n\n";

            out << "    void stop_worker() {\n";
            out << "        if (is_calling_from_worker_thread()) {\n";
            out << "            request_stop();\n";
            out << "            return;\n";
            out << "        }\n";
            out << "        std::scoped_lock lifecycle_lock(lifecycle_mutex_);\n";
            out << "        stopping_thread_id_.store(std::this_thread::get_id(), std::memory_order_release);\n";
            out << "        {\n";
            out << "            std::scoped_lock q_lock(queue_mutex_);\n";
            out << "            running_ = false;\n";
            out << "            is_stopping_ = true;\n";
            out << "        }\n";
            out << "        cv_.notify_all();\n";
            out << "        if (worker_.joinable()) {\n";
            out << "            worker_.request_stop();\n";
            out << "            worker_.join();\n";
            out << "        }\n";
            out << "        process_all();\n";
            out << "        {\n";
            out << "            std::scoped_lock q_lock(queue_mutex_);\n";
            out << "            is_stopping_ = false;\n";
            out << "            stopping_thread_id_.store(std::thread::id{}, std::memory_order_release);\n";
            out << "        }\n";
            out << "    }\n\n";

            out << "    void clear_queue() {\n";
            out << "        std::scoped_lock lock(queue_mutex_);\n";
            out << "        queue_.clear();\n";
            out << "    }\n\n";

            out << "    template <typename State> [[nodiscard]] bool is_in_state() const {\n";
            out << "        std::scoped_lock lock(mutex_);\n";
            out << "        return fsm_.template is_in_state<State>();\n";
            out << "    }\n\n";

            out << "    [[nodiscard]] std::string_view current_state_name() const {\n";
            out << "        std::scoped_lock lock(mutex_);\n";
            out << "        return fsm_.current_state_name();\n";
            out << "    }\n\n";

            out << "    [[nodiscard]] bool is_queue_empty() const {\n";
            out << "        std::scoped_lock lock(queue_mutex_);\n";
            out << "        return queue_.empty();\n";
            out << "    }\n\n";

            out << "private:\n";
            out << "    struct dispatch_snapshot {\n";
            out << "        dispatch_result result{dispatch_status::unhandled};\n";
            out << "        std::string state_name{};\n";
            out << "        std::vector<transition_info> notifications{};\n";
            out << "        unhandled_handler unhandled_h{};\n";
            out << "        guard_rejected_handler guard_rejected_h{};\n";
            out << "        deferred_handler deferred_h{};\n";
            out << "        dispatch_failure_handler failure_h{};\n";
            out << "        exception_handler exception_h{};\n";
            out << "        typename fsm_type::observer_type observer_h{};\n";
            out << "    };\n\n";

            out << "    template <typename Event>\n";
            out << "    dispatch_snapshot execute_dispatch_under_lock(const Event& evt) {\n";
            out << "        dispatch_snapshot snap;\n";
            out << "        std::scoped_lock lock(mutex_);\n";
            out << "        notification_buffer_.clear();\n";
            out << "        snap.result = fsm_.dispatch(evt);\n";
            out << "        snap.state_name = std::string(fsm_.current_state_name());\n";
            out << "        snap.notifications = std::move(notification_buffer_);\n";
            out << "        snap.unhandled_h = unhandled_handler_;\n";
            out << "        snap.guard_rejected_h = guard_rejected_handler_;\n";
            out << "        snap.deferred_h = deferred_handler_;\n";
            out << "        snap.failure_h = failure_handler_;\n";
            out << "        snap.exception_h = exception_handler_;\n";
            out << "        snap.observer_h = user_observer_;\n";
            out << "        return snap;\n";
            out << "    }\n\n";

            out << "    template <typename Event>\n";
            out << "    void invoke_notifications_outside_lock(const Event& evt, const dispatch_snapshot& snap) {\n";
            out << "        if (snap.observer_h) {\n";
            out << "            for (const auto& info : snap.notifications) {\n";
            out << "                try { snap.observer_h(info); } catch (...) { "
                   "handle_exception_outside_lock(std::current_exception(), snap.exception_h); }\n";
            out << "            }\n";
            out << "        }\n";
            out << "        if (snap.result.is_unhandled() && snap.unhandled_h) {\n";
            out << "            try { snap.unhandled_h(get_event_name(evt), snap.state_name); } catch (...) { "
                   "handle_exception_outside_lock(std::current_exception(), snap.exception_h); }\n";
            out << "        } else if (snap.result.is_guard_rejected() && snap.guard_rejected_h) {\n";
            out << "            try { snap.guard_rejected_h(get_event_name(evt), snap.state_name); } catch (...) { "
                   "handle_exception_outside_lock(std::current_exception(), snap.exception_h); }\n";
            out << "        } else if (snap.result.is_deferred() && snap.deferred_h) {\n";
            out << "            try { snap.deferred_h(get_event_name(evt), snap.state_name); } catch (...) { "
                   "handle_exception_outside_lock(std::current_exception(), snap.exception_h); }\n";
            out << "        }\n";
            out << "        if (!snap.result.is_ok() && snap.failure_h) {\n";
            out << "            try { snap.failure_h(get_event_name(evt), snap.state_name, snap.result.status); } "
                   "catch (...) { handle_exception_outside_lock(std::current_exception(), snap.exception_h); }\n";
            out << "        }\n";
            out << "    }\n\n";

            out << "    void handle_exception_outside_lock(std::exception_ptr ex, const exception_handler& local_h) "
                   "{\n";
            out << "        { std::scoped_lock lock(mutex_); last_exception_ = ex; }\n";
            out << "        if (local_h) { try { local_h(ex); } catch (...) {} }\n";
            out << "    }\n\n";

            out << "    exception_handler get_exception_handler_copy() {\n";
            out << "        std::scoped_lock lock(mutex_);\n";
            out << "        return exception_handler_;\n";
            out << "    }\n\n";

            out << "    fsm_type fsm_;\n";
            out << "    mutable std::recursive_mutex mutex_;\n";
            out << "    mutable std::mutex queue_mutex_;\n";
            out << "    mutable std::recursive_mutex lifecycle_mutex_;\n";
            out << "    std::condition_variable_any cv_;\n";
            out << "    std::deque<event_handler> queue_;\n";
            out << "    std::atomic<bool> running_{false};\n";
            out << "    std::atomic<bool> is_stopping_{false};\n";
            out << "    std::atomic<bool> is_polling_{false};\n";
            out << "    std::atomic<std::thread::id> worker_thread_id_{};\n";
            out << "    std::atomic<std::thread::id> stopping_thread_id_{};\n";
            out << "    std::jthread worker_;\n";
            out << "    typename fsm_type::observer_type user_observer_{};\n";
            out << "    std::vector<transition_info> notification_buffer_{};\n";
            out << "    unhandled_handler unhandled_handler_{};\n";
            out << "    guard_rejected_handler guard_rejected_handler_{};\n";
            out << "    deferred_handler deferred_handler_{};\n";
            out << "    dispatch_failure_handler failure_handler_{};\n";
            out << "    exception_handler exception_handler_{};\n";
            out << "    std::exception_ptr last_exception_{nullptr};\n";
            out << "};\n\n";
        }

        out << "} // namespace fsm\n\n";
    }
};

}  // namespace fsm::codegen
