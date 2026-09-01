#include <gtest/gtest.h>

#include <concepts>
#include <cstdint>
#include <string>

#include "fsm/runtime/cpp/fsm.hpp"
#include "fsm/runtime/cpp/thread_safe_fsm.hpp"

namespace {

// Simulated generated Signal struct with typed members and validator
struct EvPacketRecv {
    static constexpr std::string_view name = "EvPacketRecv";
    std::uint32_t len = 0;
    const std::uint8_t* ptr = nullptr;

    constexpr EvPacketRecv(std::uint32_t length, const std::uint8_t* pointer) noexcept : len(length), ptr(pointer) {}

    [[nodiscard]] constexpr bool is_valid() const noexcept { return len > 0 && ptr != nullptr; }
};

// Formal C++20 StateMachineContextContract concept
template <typename T>
concept StateMachineContextContract = requires(T ctx, const EvPacketRecv& ev) {
    { ctx.is_valid(ev) } -> std::convertible_to<bool>;
    { ctx.on_data(ev) } -> std::same_as<void>;
};

struct ValidDeviceContext {
    [[nodiscard]] static bool is_valid(const EvPacketRecv& ev) noexcept { return ev.is_valid(); }
    void on_data(const EvPacketRecv& /*ev*/) noexcept {}
};

struct IncompleteContext {
    void on_data(const EvPacketRecv& /*ev*/) noexcept {}
    // Missing is_valid
};

/**
 * @brief Test Intent: Verify runtime and constexpr validation logic on typed signal structs.
 *
 * Scenario:
 * - Instantiate signal with valid buffer pointer and positive length -> is_valid() is true.
 * - Instantiate signal with null pointer or zero length -> is_valid() is false.
 */
TEST(ContextContractTest, SignalValidatorExecution) {
    std::uint8_t dummy_buf[4] = {1, 2, 3, 4};
    EvPacketRecv valid_packet(4, dummy_buf);
    EXPECT_TRUE(valid_packet.is_valid());

    EvPacketRecv null_packet(4, nullptr);
    EXPECT_FALSE(null_packet.is_valid());

    EvPacketRecv zero_len_packet(0, dummy_buf);
    EXPECT_FALSE(zero_len_packet.is_valid());
}

/**
 * @brief Test Intent: Verify compile-time C++20 concept requirements on user-defined context structs.
 *
 * Scenario:
 * - Verify ValidDeviceContext satisfies StateMachineContextContract concept.
 * - Verify IncompleteContext (missing member method) fails concept constraints at compile time.
 */
TEST(ContextContractTest, Cpp20ConceptsValidation) {
    static_assert(StateMachineContextContract<ValidDeviceContext>);
    static_assert(!StateMachineContextContract<IncompleteContext>);
    static_assert(!StateMachineContextContract<int>);

    ValidDeviceContext ctx;
    std::uint8_t buf[2] = {0xAA, 0x55};
    EvPacketRecv ev(2, buf);
    EXPECT_TRUE(ctx.is_valid(ev));
}

/**
 * @brief Test Intent: Verify compile-time safety preventing uninitialized context default construction.
 *
 * Scenario:
 * - Machines with `no_context` can be default constructed safely.
 * - Machines with user Context structs cannot be default constructed without a reference,
 *   guaranteeing zero null-dereference undefined behavior at runtime.
 */
TEST(ContextContractTest, CompileTimeContextSafety) {
    struct StateA {};
    struct StateB {};
    struct EventX {};
    using Table = fsm::transition_table<fsm::transition<StateA, EventX, StateB>>;

    // Default construction is allowed when Context == no_context
    static_assert(std::is_default_constructible_v<fsm::fsm<Table, fsm::no_context>>);
    static_assert(std::is_default_constructible_v<fsm::thread_safe_fsm<Table, fsm::no_context>>);

    // Default construction is PROHIBITED when Context != no_context (preventing uninitialized/null dereference UB)
    static_assert(!std::is_default_constructible_v<fsm::fsm<Table, ValidDeviceContext>>);
    static_assert(!std::is_default_constructible_v<fsm::thread_safe_fsm<Table, ValidDeviceContext>>);

    // Explicit context instantiation
    ValidDeviceContext ctx;
    fsm::fsm<Table, ValidDeviceContext> machine(ctx);
    EXPECT_EQ(&machine.context(), &ctx);
    EXPECT_EQ(machine.get_context(), &ctx);

    fsm::thread_safe_fsm<Table, ValidDeviceContext> ts_machine(ctx);
    EXPECT_EQ(&ts_machine.context(), &ctx);
}

/**
 * @brief Test Intent: Verify thread_safe_fsm::with_context executes callable under internal lock.
 *
 * Scenario:
 * - Create a thread_safe_fsm with a context holding a mutable counter.
 * - Call with_context() to read and modify the counter.
 * - Verify the modification is visible through the subsequent snapshot_context().
 */
TEST(ContextContractTest, ThreadSafeWithContextMutation) {
    struct CounterCtx {
        int value = 0;
    };
    struct StateA {};
    struct StateB {};
    struct EventX {};
    using Table = fsm::transition_table<fsm::transition<StateA, EventX, StateB>>;

    CounterCtx ctx{42};
    fsm::thread_safe_fsm<Table, CounterCtx> ts_machine(ctx);

    // with_context allows mutating the context through a callable
    ts_machine.with_context([](CounterCtx& c) { c.value += 8; });

    // snapshot_context returns a copy of the current state
    const CounterCtx snap = ts_machine.snapshot_context();
    EXPECT_EQ(snap.value, 50);
}

/**
 * @brief Test Intent: Verify thread_safe_fsm::snapshot_context is independent from subsequent mutations.
 *
 * Scenario:
 * - Take a snapshot before mutation, mutate via with_context, take a second snapshot.
 * - Verify the first snapshot is unaffected (value copy semantics).
 */
TEST(ContextContractTest, SnapshotContextIsolation) {
    struct DeltaCtx {
        int counter = 0;
    };
    struct StateA {};
    struct StateB {};
    struct EventX {};
    using Table = fsm::transition_table<fsm::transition<StateA, EventX, StateB>>;

    DeltaCtx ctx{10};
    fsm::thread_safe_fsm<Table, DeltaCtx> ts_machine(ctx);

    const DeltaCtx before = ts_machine.snapshot_context();
    EXPECT_EQ(before.counter, 10);

    ts_machine.with_context([](DeltaCtx& c) { c.counter = 99; });

    const DeltaCtx after = ts_machine.snapshot_context();
    EXPECT_EQ(after.counter, 99);

    // The first snapshot must not have been affected
    EXPECT_EQ(before.counter, 10);
}

/**
 * @brief Test Intent: Verify const overload of with_context for read-only access.
 *
 * Scenario:
 * - Const-qualify the thread_safe_fsm reference and call with_context() to read the value.
 * - Verify the read value is consistent with the last mutation.
 */
TEST(ContextContractTest, ThreadSafeWithContextConstReadOnly) {
    struct ReadCtx {
        double temperature = 36.6;
    };
    struct StateA {};
    struct StateB {};
    struct EventX {};
    using Table = fsm::transition_table<fsm::transition<StateA, EventX, StateB>>;

    ReadCtx ctx;
    fsm::thread_safe_fsm<Table, ReadCtx> ts_machine(ctx);

    const auto& const_machine = ts_machine;
    double read_value = 0.0;
    const_machine.with_context([&](const ReadCtx& c) { read_value = c.temperature; });

    EXPECT_DOUBLE_EQ(read_value, 36.6);
}

}  // namespace
