#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "fsm/runtime/cpp/traits/concepts.hpp"
#include "fsm/runtime/cpp/traits/dispatch_result.hpp"
#include "fsm/runtime/cpp/traits/hook_traits.hpp"
#include "fsm/runtime/cpp/traits/observer_traits.hpp"
#include "fsm/runtime/cpp/traits/reflection.hpp"
#include "fsm/runtime/cpp/traits/type_list.hpp"
#include "fsm/runtime/cpp/type_traits.hpp"

namespace {

// ============================================================================
// Sample States, Events, and Contexts for Metaprogramming Tests
// ============================================================================

struct TestContext {
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

// 1. State with on_enter(event, ctx) and on_exit(event, ctx)
struct StateWithEventAndCtx {
    static constexpr std::string_view name = "StateWithEventAndCtx";
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void on_enter(const TestEventA& /*unused*/, TestContext& ctx) { ++ctx.enter_count; }
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void on_exit(const TestEventA& /*unused*/, TestContext& ctx) { ++ctx.exit_count; }
};

// 2. State with on_enter(ctx) and on_exit(ctx)
struct StateWithCtxOnly {
    static constexpr std::string_view name = "StateWithCtxOnly";
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void on_enter(TestContext& ctx) { ctx.enter_count += 2; }
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void on_exit(TestContext& ctx) { ctx.exit_count += 2; }
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
    StateWithEventAndCtx s1;
    EXPECT_EQ(fsm::get_state_name(s1), "StateWithEventAndCtx");

    // State name reflection via member function .name()
    StateWithoutHooks s2;
    EXPECT_EQ(fsm::get_state_name(s2), "StateWithoutHooks");

    // State name fallback reflection via type demangling
    AnonymousEvent anon;
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
    EXPECT_EQ(fsm::get_parent_name<StateWithEventAndCtx>(), "");
}

// ============================================================================
// 3. Hook Detection and Safe Invocations Tests
// ============================================================================

/**
 * @brief Test Intent: Verify hook detection and safe dispatch across all valid hook arities.
 *
 * Scenario:
 * - Test state with on_enter(event, ctx) and on_exit(event, ctx).
 * - Test state with on_enter(ctx) and on_exit(ctx).
 * - Test state with on_enter(event) and on_exit(event).
 * - Test state with void on_enter() and void on_exit().
 * - Test state with no hooks (guaranteed safe no-op).
 */
TEST(TraitsAndHooksTest, HookSafeInvocations) {
    TestContext ctx;
    TestEventA ev;

    // 1. on_enter(event, ctx) / on_exit(event, ctx)
    StateWithEventAndCtx s1;
    fsm::call_on_enter(s1, ev, ctx);
    EXPECT_EQ(ctx.enter_count, 1);
    fsm::call_on_exit(s1, ev, ctx);
    EXPECT_EQ(ctx.exit_count, 1);

    // Initial state enter (without event)
    fsm::call_on_enter(s1, ctx);
    EXPECT_EQ(ctx.enter_count, 1);  // doesn't have on_enter(ctx) or void on_enter()

    // 2. on_enter(ctx) / on_exit(ctx)
    StateWithCtxOnly s2;
    fsm::call_on_enter(s2, ev, ctx);
    EXPECT_EQ(ctx.enter_count, 3);
    fsm::call_on_exit(s2, ev, ctx);
    EXPECT_EQ(ctx.exit_count, 3);

    // Initial state enter
    fsm::call_on_enter(s2, ctx);
    EXPECT_EQ(ctx.enter_count, 5);

    // 3. on_enter(event) / on_exit(event)
    StateWithEventOnly s3;
    fsm::call_on_enter(s3, ev, ctx);
    EXPECT_TRUE(s3.entered);
    fsm::call_on_exit(s3, ev, ctx);
    EXPECT_TRUE(s3.exited);

    // 4. on_enter() / on_exit()
    StateWithVoidHooks s4;
    fsm::call_on_enter(s4, ev, ctx);
    EXPECT_TRUE(s4.entered);
    fsm::call_on_exit(s4, ev, ctx);
    EXPECT_TRUE(s4.exited);

    // 5. State without hooks (safe no-op)
    StateWithoutHooks s5;
    fsm::call_on_enter(s5, ev, ctx);
    fsm::call_on_exit(s5, ev, ctx);
    fsm::call_on_enter(s5, ctx);
}

// ============================================================================
// 4. Guard and Action Multi-Arity Invocations Tests
// ============================================================================

/**
 * @brief Test Intent: Verify guard and action dispatch with variable argument signatures.
 *
 * Scenario:
 * - Call guards accepting (event, src, ctx, fsm), (event, src, ctx), (event, ctx), (ctx), and ().
 * - Call actions accepting (event, src, dst, ctx), (event, src, ctx), (ctx), and ().
 */
TEST(TraitsAndHooksTest, GuardAndActionMultiArityInvocations) {
    TestContext ctx;
    TestEventA ev;
    StateWithEventAndCtx src;
    StateWithCtxOnly dst;

    // Guard with 4 arguments: (event, src, ctx, fsm)
    auto guard4 = [](const auto&, const auto&, TestContext& c, const auto&) {
        ++c.guard_checks;
        return true;
    };
    int dummy_fsm = 0;
    EXPECT_TRUE(fsm::call_guard(guard4, ev, src, ctx, dummy_fsm));
    EXPECT_EQ(ctx.guard_checks, 1);

    // Guard with 3 arguments: (event, src, ctx)
    auto guard3 = [](const auto&, const auto&, TestContext& c) {
        ++c.guard_checks;
        return true;
    };
    EXPECT_TRUE(fsm::call_guard(guard3, ev, src, ctx));
    EXPECT_EQ(ctx.guard_checks, 2);

    // Guard with 2 arguments: (event, ctx)
    auto guard2 = [](const auto&, TestContext& c) {
        ++c.guard_checks;
        return true;
    };
    EXPECT_TRUE(fsm::call_guard(guard2, ev, src, ctx));
    EXPECT_EQ(ctx.guard_checks, 3);

    // Guard with 1 argument: (ctx)
    auto guard1 = [](TestContext& c) {
        ++c.guard_checks;
        return false;
    };
    EXPECT_FALSE(fsm::call_guard(guard1, ev, src, ctx));
    EXPECT_EQ(ctx.guard_checks, 4);

    // Guard with 0 arguments: ()
    auto guard0 = []() {
        return true;
    };
    EXPECT_TRUE(fsm::call_guard(guard0, ev, src, ctx));

    // Action with 5 arguments: (event, src, dst, ctx)
    auto action4 = [](const auto&, auto&, auto&, TestContext& c) {
        ++c.actions_run;
    };
    fsm::call_action(action4, ev, src, dst, ctx);
    EXPECT_EQ(ctx.actions_run, 1);

    // Action with (event, src, ctx)
    auto action3 = [](const auto&, auto&, TestContext& c) {
        c.actions_run += 2;
    };
    fsm::call_action(action3, ev, src, dst, ctx);
    EXPECT_EQ(ctx.actions_run, 3);

    // Action with (ctx)
    auto action1 = [](TestContext& c) {
        c.actions_run += 10;
    };
    fsm::call_action(action1, ev, src, dst, ctx);
    EXPECT_EQ(ctx.actions_run, 13);

    // Action with ()
    bool void_action_called = false;
    auto action0 = [&]() {
        void_action_called = true;
    };
    fsm::call_action(action0, ev, src, dst, ctx);
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
    using StateListWithHistory = fsm::type_list<StateWithEventAndCtx, ChildState>;
    using StateListWithoutHistory = fsm::type_list<StateWithEventAndCtx, StateWithCtxOnly>;

    EXPECT_TRUE(fsm::any_state_has_history<StateListWithHistory>::value);
    EXPECT_FALSE(fsm::any_state_has_history<StateListWithoutHistory>::value);

    using StateListWithDeferred = fsm::type_list<StateWithEventAndCtx, StateWithDeferred>;
    EXPECT_TRUE(fsm::any_state_has_deferred<StateListWithDeferred>::value);
    EXPECT_FALSE(fsm::any_state_has_deferred<StateListWithoutHistory>::value);

    EXPECT_TRUE((fsm::is_deferred_event_v<StateWithDeferred, TestEventA>));
    EXPECT_TRUE((fsm::is_deferred_event_v<StateWithDeferred, TestEventB>));
    EXPECT_FALSE((fsm::is_deferred_event_v<StateWithDeferred, AnonymousEvent>));
    EXPECT_FALSE((fsm::is_deferred_event_v<StateWithEventAndCtx, TestEventA>));
}

}  // namespace
