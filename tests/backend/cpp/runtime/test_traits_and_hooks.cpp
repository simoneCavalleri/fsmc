#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "fsm/backend/cpp/runtime/fsm.hpp"
#include "fsm/backend/cpp/runtime/traits/concepts.hpp"
#include "fsm/backend/cpp/runtime/traits/dispatch_result.hpp"
#include "fsm/backend/cpp/runtime/traits/hook_traits.hpp"
#include "fsm/backend/cpp/runtime/traits/observer_traits.hpp"
#include "fsm/backend/cpp/runtime/traits/reflection.hpp"
#include "fsm/backend/cpp/runtime/traits/type_list.hpp"
#include "fsm/backend/cpp/runtime/transition.hpp"
#include "fsm/backend/cpp/runtime/type_traits.hpp"

namespace {

// ============================================================================
// Sample States, Events, and Contexts for Metaprogramming Tests
// ============================================================================

struct TestRegisters {
    int enter_count = 0;
    int exit_count = 0;
    int guard_checks = 0;
    int actions_run = 0;
};

struct TestEventA {
    static constexpr std::string_view name = "TestEventA";
};

struct TestEventB {
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    [[nodiscard]] std::string_view name() const noexcept { return "DynamicEventB"; }
};

struct AnonymousEvent {
    int value = 42;
};

// 1. State with on_enter(event, in, out, reg, srv) and on_exit(event, in, out, reg, srv)
struct StateWithEventAndReg {
    static constexpr std::string_view name = "StateWithEventAndReg";
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void on_enter(const TestEventA& /*unused*/, const fsm::no_ports&, fsm::no_ports&, TestRegisters& reg,
                  fsm::no_services&) {
        ++reg.enter_count;
    }
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void on_exit(const TestEventA& /*unused*/, const fsm::no_ports&, fsm::no_ports&, TestRegisters& reg,
                 fsm::no_services&) {
        ++reg.exit_count;
    }
};

// 2. State with on_enter(in, out, reg, srv) and on_exit(in, out, reg, srv)
struct StateWithRegOnly {
    static constexpr std::string_view name = "StateWithRegOnly";
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void on_enter(const fsm::no_ports&, fsm::no_ports&, TestRegisters& reg, fsm::no_services&) { reg.enter_count += 2; }
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void on_exit(const fsm::no_ports&, fsm::no_ports&, TestRegisters& reg, fsm::no_services&) { reg.exit_count += 2; }
};

// 3. State with on_enter(event) and on_exit(event)
struct StateWithEventOnly {
    static constexpr std::string_view name = "StateWithEventOnly";
    bool entered = false;
    bool exited = false;
    void on_enter(const TestEventA& /*unused*/) { entered = true; }
    void on_exit(const TestEventA& /*unused*/) { exited = true; }
};

// 4. State with void on_enter() and void on_exit()
struct StateWithVoidHooks {
    static constexpr std::string_view name = "StateWithVoidHooks";
    bool entered = false;
    bool exited = false;
    void on_enter() { entered = true; }
    void on_exit() { exited = true; }
};

// 5. State without any hooks
struct StateWithoutHooks {
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    [[nodiscard]] std::string_view name() const noexcept { return "StateWithoutHooks"; }
};

// 6. Hierarchical child state
struct ChildState {
    static constexpr std::string_view name = "ChildState";
    static constexpr std::string_view parent = "ParentCompositeState";
};

// 7. State with deferred events
struct StateWithDeferred {
    using deferred_events = fsm::type_list<TestEventA, TestEventB>;
};

// ============================================================================
// 1. Type List Metaprogramming Tests
// ============================================================================

/**
 * @brief Test Intent: Verify compile-time type list algorithms and transformations.
 *
 * Scenario:
 * - Validate size, front element extraction, list concatenation, and element presence (contains).
 * - Validate order-preserving deduplication (type_list_unique_t).
 * - Validate conversion to std::variant and std::tuple.
 */
TEST(TraitsAndHooksTest, TypeListAlgorithms) {
    using L1 = fsm::type_list<int, double, char>;
    using L2 = fsm::type_list<char, float, double, int, long>;

    // Size
    EXPECT_EQ(L1::size, 3u);
    EXPECT_EQ(L2::size, 5u);

    // Front
    EXPECT_TRUE((std::is_same_v<fsm::type_list_front_t<L1>, int>));
    EXPECT_TRUE((std::is_same_v<fsm::type_list_front_t<L2>, char>));

    // Concatenation
    using Cat = fsm::type_list_cat_t<L1, L2>;
    EXPECT_EQ(Cat::size, 8u);

    // Contains
    EXPECT_TRUE((fsm::type_list_contains_v<int, L1>));
    EXPECT_TRUE((fsm::type_list_contains_v<double, L1>));
    EXPECT_TRUE((fsm::type_list_contains_v<char, L1>));
    EXPECT_FALSE((fsm::type_list_contains_v<float, L1>));
    EXPECT_FALSE((fsm::type_list_contains_v<long, L1>));

    // Deduplication (preserving first occurrence order: int, double, char, float, long)
    using Unique = fsm::type_list_unique_t<Cat>;
    EXPECT_EQ(Unique::size, 5u);
    EXPECT_TRUE((fsm::type_list_contains_v<int, Unique>));
    EXPECT_TRUE((fsm::type_list_contains_v<double, Unique>));
    EXPECT_TRUE((fsm::type_list_contains_v<char, Unique>));
    EXPECT_TRUE((fsm::type_list_contains_v<float, Unique>));
    EXPECT_TRUE((fsm::type_list_contains_v<long, Unique>));

    // Conversion to std::variant and std::tuple
    using VariantType = fsm::to_variant_t<Unique>;
    EXPECT_TRUE((std::is_same_v<VariantType, std::variant<int, double, char, float, long>>));

    using TupleType = fsm::to_tuple_t<Unique>;
    EXPECT_TRUE((std::is_same_v<TupleType, std::tuple<int, double, char, float, long>>));
}

// ============================================================================
// 2. Reflection and Demangling Tests
// ============================================================================

/**
 * @brief Test Intent: Verify compile-time name reflection, parent hierarchy querying, and type demangling.
 *
 * Scenario:
 * - Extract names from static member `::name`, member function `.name()`, and fallback type demangling.
 * - Extract event names and verify parent hierarchy relationship for nested composite states.
 */
TEST(TraitsAndHooksTest, ReflectionAndDemangling) {
    // State name reflection via static member
    StateWithEventAndReg s1;
    EXPECT_EQ(fsm::get_state_name(s1), "StateWithEventAndReg");

    // State name reflection via member function .name()
    StateWithoutHooks s2;
    EXPECT_EQ(fsm::get_state_name(s2), "StateWithoutHooks");

    // State name fallback reflection via type demangling
    AnonymousEvent anon;
    EXPECT_EQ(fsm::get_event_name(anon), "AnonymousEvent");
    EXPECT_EQ(fsm::get_type_name<AnonymousEvent>(), "AnonymousEvent");

    // Event name reflection
    TestEventA ev_a;
    EXPECT_EQ(fsm::get_event_name(ev_a), "TestEventA");

    TestEventB ev_b;
    EXPECT_EQ(fsm::get_event_name(ev_b), "DynamicEventB");

    EXPECT_EQ(fsm::event_name<TestEventA>(), "TestEventA");
    EXPECT_EQ(fsm::event_name<AnonymousEvent>(), "AnonymousEvent");

    // Parent hierarchy reflection
    EXPECT_EQ(fsm::get_parent_name<ChildState>(), "ParentCompositeState");
    EXPECT_EQ(fsm::get_parent_name<StateWithEventAndReg>(), "");
}

// ============================================================================
// 3. Hook Detection and Safe Invocations Tests
// ============================================================================

/**
 * @brief Test Intent: Verify hook detection and safe dispatch across all valid hook arities.
 */
TEST(TraitsAndHooksTest, HookSafeInvocations) {
    TestRegisters reg;
    fsm::no_ports in;
    fsm::no_ports out;
    fsm::no_services srv;
    TestEventA ev;

    // 1. on_enter(event, in, out, reg, srv) / on_exit(event, in, out, reg, srv)
    StateWithEventAndReg s1;
    fsm::call_on_enter(s1, ev, in, out, reg, srv);
    EXPECT_EQ(reg.enter_count, 1);
    fsm::call_on_exit(s1, ev, in, out, reg, srv);
    EXPECT_EQ(reg.exit_count, 1);

    // Initial state enter (without event)
    fsm::call_on_enter(s1, in, out, reg, srv);
    EXPECT_EQ(reg.enter_count, 1);  // doesn't have on_enter(in, out, reg, srv) without event

    // 2. on_enter(in, out, reg, srv) / on_exit(in, out, reg, srv)
    StateWithRegOnly s2;
    fsm::call_on_enter(s2, ev, in, out, reg, srv);
    EXPECT_EQ(reg.enter_count, 1);
    fsm::call_on_exit(s2, ev, in, out, reg, srv);
    EXPECT_EQ(reg.exit_count, 1);

    // Initial state enter
    fsm::call_on_enter(s2, in, out, reg, srv);
    EXPECT_EQ(reg.enter_count, 3);

    // 3. on_enter(event) / on_exit(event)
    StateWithEventOnly s3;
    fsm::call_on_enter(s3, ev, in, out, reg, srv);
    EXPECT_TRUE(s3.entered);
    fsm::call_on_exit(s3, ev, in, out, reg, srv);
    EXPECT_TRUE(s3.exited);

    // 4. on_enter() / on_exit()
    StateWithVoidHooks s4;
    fsm::call_on_enter(s4, ev, in, out, reg, srv);
    EXPECT_TRUE(s4.entered);
    fsm::call_on_exit(s4, ev, in, out, reg, srv);
    EXPECT_TRUE(s4.exited);

    // 5. State without hooks (safe no-op)
    StateWithoutHooks s5;
    fsm::call_on_enter(s5, ev, in, out, reg, srv);
    fsm::call_on_exit(s5, ev, in, out, reg, srv);
    fsm::call_on_enter(s5, in, out, reg, srv);
    fsm::call_on_enter(s5);
}

// ============================================================================
// 4. Guard and Action Multi-Arity Invocations Tests
// ============================================================================

/**
 * @brief Test Intent: Verify guard and action dispatch with variable argument signatures.
 */
TEST(TraitsAndHooksTest, GuardAndActionMultiArityInvocations) {
    TestRegisters reg;
    TestEventA ev;
    StateWithEventAndReg src;
    StateWithRegOnly dst;

    // Guard with variadic arguments
    auto guard4 = [](const auto&, const auto&, TestRegisters& r, const auto&) {
        ++r.guard_checks;
        return true;
    };
    int dummy_fsm = 0;
    EXPECT_TRUE(fsm::call_guard(guard4, ev, src, reg, dummy_fsm));
    EXPECT_EQ(reg.guard_checks, 1);

    auto guard3 = [](const auto&, const auto&, TestRegisters& r) {
        ++r.guard_checks;
        return true;
    };
    EXPECT_TRUE(fsm::call_guard(guard3, ev, src, reg));
    EXPECT_EQ(reg.guard_checks, 2);

    auto guard2 = [](const auto&, TestRegisters& r) {
        ++r.guard_checks;
        return true;
    };
    EXPECT_TRUE(fsm::call_guard(guard2, ev, src, reg));
    EXPECT_EQ(reg.guard_checks, 3);

    auto guard1 = [](TestRegisters& r) {
        ++r.guard_checks;
        return false;
    };
    EXPECT_FALSE(fsm::call_guard(guard1, ev, src, reg));
    EXPECT_EQ(reg.guard_checks, 4);

    auto guard0 = []() {
        return true;
    };
    EXPECT_TRUE(fsm::call_guard(guard0, ev, src, reg));

    // Action with variadic arguments
    auto action4 = [](const auto&, auto&, auto&, TestRegisters& r) {
        ++r.actions_run;
    };
    fsm::call_action(action4, ev, src, dst, reg);
    EXPECT_EQ(reg.actions_run, 1);

    auto action3 = [](const auto&, auto&, TestRegisters& r) {
        r.actions_run += 2;
    };
    fsm::call_action(action3, ev, src, dst, reg);
    EXPECT_EQ(reg.actions_run, 3);

    auto action1 = [](TestRegisters& r) {
        r.actions_run += 10;
    };
    fsm::call_action(action1, ev, src, dst, reg);
    EXPECT_EQ(reg.actions_run, 13);

    bool void_action_called = false;
    auto action0 = [&]() {
        void_action_called = true;
    };
    fsm::call_action(action0, ev, src, dst, reg);
    EXPECT_TRUE(void_action_called);
}

// ============================================================================
// 5. Dispatch Result, Status, and Observer Policy Traits
// ============================================================================

/**
 * @brief Test Intent: Verify dispatch_result statuses, boolean cast semantics, and observer detection traits.
 *
 * Scenario:
 * - Verify is_success(), is_deferred(), is_guard_rejected(), is_unhandled() statuses.
 * - Verify detection of dynamic vs no-op static observers.
 * - Verify compile-time detection of history pseudostates and deferred events across type_list.
 */
TEST(TraitsAndHooksTest, DispatchResultAndObserverPolicies) {
    fsm::dispatch_result ok_res(fsm::dispatch_status::success);
    EXPECT_TRUE(ok_res.is_success());
    EXPECT_TRUE(ok_res.is_ok());
    EXPECT_TRUE(static_cast<bool>(ok_res));
    EXPECT_EQ(ok_res.to_string(), "success");

    fsm::dispatch_result def_res(fsm::dispatch_status::deferred);
    EXPECT_TRUE(def_res.is_deferred());
    EXPECT_TRUE(def_res.is_ok());
    EXPECT_EQ(def_res.to_string(), "deferred");

    fsm::dispatch_result rej_res(fsm::dispatch_status::guard_rejected);
    EXPECT_TRUE(rej_res.is_guard_rejected());
    EXPECT_FALSE(rej_res.is_ok());
    EXPECT_EQ(rej_res.to_string(), "guard_rejected");

    fsm::dispatch_result unh_res(fsm::dispatch_status::unhandled);
    EXPECT_TRUE(unh_res.is_unhandled());
    EXPECT_FALSE(unh_res.is_ok());
    EXPECT_EQ(unh_res.to_string(), "unhandled");

    EXPECT_EQ(ok_res, fsm::dispatch_status::success);
    EXPECT_NE(ok_res, fsm::dispatch_status::unhandled);

    // Observer traits
    EXPECT_FALSE(fsm::is_dynamic_observer_v<fsm::no_observer>);
    EXPECT_TRUE(fsm::is_dynamic_observer_v<fsm::dynamic_observer>);
    EXPECT_TRUE((fsm::is_dynamic_observer_v<std::function<void(const fsm::transition_info&)>>));

    // History and deferred state detection across type_list
    using StateListWithHistory = fsm::type_list<StateWithEventAndReg, ChildState>;
    using StateListWithoutHistory = fsm::type_list<StateWithEventAndReg, StateWithRegOnly>;

    EXPECT_TRUE(fsm::any_state_has_history<StateListWithHistory>::value);
    EXPECT_FALSE(fsm::any_state_has_history<StateListWithoutHistory>::value);

    using StateListWithDeferred = fsm::type_list<StateWithEventAndReg, StateWithDeferred>;
    EXPECT_TRUE(fsm::any_state_has_deferred<StateListWithDeferred>::value);
    EXPECT_FALSE(fsm::any_state_has_deferred<StateListWithoutHistory>::value);

    EXPECT_TRUE((fsm::is_deferred_event_v<StateWithDeferred, TestEventA>));
    EXPECT_TRUE((fsm::is_deferred_event_v<StateWithDeferred, TestEventB>));
    EXPECT_FALSE((fsm::is_deferred_event_v<StateWithDeferred, AnonymousEvent>));
    EXPECT_FALSE((fsm::is_deferred_event_v<StateWithEventAndReg, TestEventA>));
}

/**
 * @brief Test Intent: Verify transition_trace struct and trace introspection on dispatch_result.
 *
 * Scenario:
 * - Construct dispatch_result with explicit transition_trace.
 * - Verify access to source, target, event, guard, action, and transition_kind.
 * - Verify is_internal() and is_external() query helpers.
 */
TEST(TraitsAndHooksTest, DispatchResultTransitionTraceInspection) {
    fsm::transition_trace trace{"StateIdle",       "StateRunning",   "StartEvent",
                                "AllowStartGuard", "LogStartAction", fsm::transition_kind::external};

    EXPECT_EQ(trace.source, "StateIdle");
    EXPECT_EQ(trace.target, "StateRunning");
    EXPECT_EQ(trace.event, "StartEvent");
    EXPECT_EQ(trace.guard, "AllowStartGuard");
    EXPECT_EQ(trace.action, "LogStartAction");
    EXPECT_TRUE(trace.is_external());
    EXPECT_FALSE(trace.is_internal());

    fsm::dispatch_result res(fsm::dispatch_status::success, trace);
    EXPECT_TRUE(res.is_success());
    ASSERT_TRUE(res.trace.has_value());
    EXPECT_EQ(res.trace->source, "StateIdle");
    EXPECT_EQ(res.trace->target, "StateRunning");
    EXPECT_EQ(res.trace->guard, "AllowStartGuard");
    EXPECT_EQ(res.trace->action, "LogStartAction");
}

// ============================================================================
// Legacy Poison Checks (Monolithic Context Rejection)
// ============================================================================

struct MonolithicContext {
    int val = 0;
};

struct LegacyContextGuard {
    bool operator()(const MonolithicContext& ctx) const { return ctx.val > 0; }
};

struct LegacyContextAction {
    void operator()(MonolithicContext& ctx) const { ctx.val++; }
};

struct ModernInPorts {
    double value{0.0};
};

struct ModernOutPorts {
    double cmd{0.0};
};

struct ModernRegisters {
    int count{0};
};

struct ModernServices {
    void Log() {}
};

/**
 * @brief Test Intent: Certify at compile-time that legacy monolithic context signatures (guard(Context&),
 * action(Context&)) are rejected.
 */
TEST(TraitsAndHooksTest, LegacyContextPoisonCheck) {
    // 1. Verify Legacy Guard is NOT invocable with modern domain references
    static_assert(!std::is_invocable_v<LegacyContextGuard, TestEventA, ModernInPorts, ModernRegisters>,
                  "POISON TEST: Legacy Context guard must be rejected at compile-time");
    static_assert(!std::is_invocable_v<LegacyContextGuard, ModernInPorts, ModernRegisters>,
                  "POISON TEST: Legacy Context guard must be rejected at compile-time");
    static_assert(!std::is_invocable_v<LegacyContextGuard, ModernInPorts>,
                  "POISON TEST: Legacy Context guard must be rejected at compile-time");

    // 2. Verify Legacy Action is NOT invocable with modern domain references
    static_assert(!std::is_invocable_v<LegacyContextAction, TestEventA, ModernOutPorts, ModernRegisters>,
                  "POISON TEST: Legacy Context action must be rejected at compile-time");
    static_assert(!std::is_invocable_v<LegacyContextAction, ModernOutPorts, ModernRegisters>,
                  "POISON TEST: Legacy Context action must be rejected at compile-time");
    static_assert(!std::is_invocable_v<LegacyContextAction, ModernServices&>,
                  "POISON TEST: Legacy Context action must be rejected at compile-time");
}

// ============================================================================
// Duplicate Row Detection & State Name Static Resolution
// ============================================================================

struct DRowS1 {
    static constexpr std::string_view name = "DRowS1";
};
struct DRowS2 {
    static constexpr std::string_view name = "DRowS2";
};
struct DRowS3 {
    static constexpr std::string_view name = "DRowS3";
};
struct DRowE1 {
    static constexpr std::string_view name = "DRowE1";
};
struct DRowG1 {
    constexpr bool operator()() const { return true; }
};
struct DRowG2 {
    constexpr bool operator()() const { return true; }
};

using ValidTable =
    fsm::transition_table<fsm::row<DRowS1, DRowE1, DRowS2>::when<DRowG1>,
                          fsm::row<DRowS1, DRowE1, DRowS3>::when<DRowG2>, fsm::row<DRowS2, DRowE1, DRowS1>>;

TEST(TraitsAndHooksTest, DuplicateRowDetectionTraits) {
    using RowA = fsm::row<DRowS1, DRowE1, DRowS2>::when<DRowG1>;
    using RowB = fsm::row<DRowS1, DRowE1, DRowS3>::when<DRowG1>;  // Same (source, event, guard) -> duplicate!
    using RowC = fsm::row<DRowS1, DRowE1, DRowS2>::when<DRowG2>;  // Different guard -> not duplicate

    static_assert(fsm::detail::is_duplicate_row<RowA, RowB>::value, "Exact (source, event, guard) must match");
    static_assert(!fsm::detail::is_duplicate_row<RowA, RowC>::value, "Different guard must not match");

    static_assert(!fsm::detail::has_any_duplicate_row<RowA, RowC>::value, "Valid table must have no duplicates");
    static_assert(fsm::detail::has_any_duplicate_row<RowA, RowB, RowC>::value,
                  "Table with RowA and RowB must report duplicate");

    EXPECT_EQ(ValidTable::transition_count, 3);
    EXPECT_EQ(ValidTable::state_count, 3);
}

struct StateWithStaticName {
    static constexpr std::string_view name = "CustomStaticStateName";
};

struct StateWithoutCustomName {};

struct RejectGuardStaticTrace {
    template <typename... Args>
    constexpr bool operator()(const Args&...) const noexcept {
        return false;
    }
};

using StaticTraceTable =
    fsm::transition_table<fsm::row<DRowS1, DRowE1, StateWithStaticName>::when<RejectGuardStaticTrace>>;

/**
 * @brief Test Intent: Verify static state name resolution and compile-time string reflection.
 *
 * Scenario:
 * - Query get_state_name_static for struct with static constexpr std::string_view name.
 * - Verify fallback demangled name for struct without explicit name member.
 * - Verify target state name is populated in rejected guard dispatch trace.
 */
TEST(TraitsAndHooksTest, StateNameStaticResolutionConsistency) {
    EXPECT_EQ(fsm::get_state_name_static<StateWithStaticName>(), "CustomStaticStateName");
    EXPECT_NE(fsm::get_state_name_static<StateWithoutCustomName>(), "");

    fsm::fsm<StaticTraceTable, fsm::no_ports, fsm::no_ports, fsm::no_registers, fsm::no_services, DRowS1> machine;
    auto res = machine.dispatch(DRowE1{});

    EXPECT_TRUE(res.is_guard_rejected());
    ASSERT_TRUE(res.trace.has_value());
    EXPECT_EQ(res.trace->target, "CustomStaticStateName");
}

struct MockParentState {
    static constexpr std::string_view name = "Parent";
};
struct MockSubState {
    static constexpr std::string_view name = "Sub";
};
struct MockFsmInstance {
    [[nodiscard]] std::string_view get_history(std::string_view) const { return "Sub"; }
};

/**
 * @brief Test Intent: Verify history_is guard helper signature and type safety.
 *
 * Scenario:
 * - Instantiate fsm::history_is<Parent, Sub> guard.
 * - Invoke with multi-channel domain parameters and mock FSM instance.
 * - Verify history matches expected active substate.
 */
TEST(TraitsAndHooksTest, HistoryIsOverloadAndTypeSafety) {
    fsm::history_is<MockParentState, MockSubState> hist_guard;
    MockFsmInstance mock_fsm;
    fsm::no_ports in;
    fsm::no_registers reg;
    fsm::no_services srv;
    DRowS1 state;
    DRowE1 evt;

    bool matches = hist_guard(evt, state, in, reg, srv, mock_fsm);
    EXPECT_TRUE(matches);
}

#if __cplusplus >= 202002L
struct ValidCustomGuard {
    bool operator()(const DRowE1&, const DRowS1&) const { return true; }
};

struct ValidCustomAction {
    void operator()(const DRowE1&, DRowS1&, StateWithStaticName&) const {}
};

/**
 * @brief Test Intent: Verify C++20 Concept constraints (fsm::Guard and fsm::Action) and scalar type rejection.
 *
 * Scenario:
 * - Prove valid callable functors satisfy fsm::Guard and fsm::Action concepts.
 * - Prove default no_guard and no_action sentinel types satisfy concepts.
 * - Prove primitive scalar types (int, double) are rejected at compile time.
 */
TEST(TraitsAndHooksTest, ConceptAndScalarSanityCompliance) {
    // Guard concept
    static_assert(fsm::Guard<ValidCustomGuard, DRowE1, DRowS1, fsm::no_ports, fsm::no_registers, fsm::no_services>);
    static_assert(fsm::Guard<fsm::no_guard, DRowE1, DRowS1, fsm::no_ports, fsm::no_registers, fsm::no_services>);
    static_assert(!fsm::Guard<int, DRowE1, DRowS1, fsm::no_ports, fsm::no_registers, fsm::no_services>);

    // Action concept
    static_assert(fsm::Action<ValidCustomAction, DRowE1, DRowS1, StateWithStaticName, fsm::no_ports, fsm::no_ports,
                              fsm::no_registers, fsm::no_services>);
    static_assert(fsm::Action<fsm::no_action, DRowE1, DRowS1, StateWithStaticName, fsm::no_ports, fsm::no_ports,
                              fsm::no_registers, fsm::no_services>);
    static_assert(!fsm::Action<double, DRowE1, DRowS1, StateWithStaticName, fsm::no_ports, fsm::no_ports,
                               fsm::no_registers, fsm::no_services>);
}
#endif

}  // namespace
