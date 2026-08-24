#include <gtest/gtest.h>

#include <concepts>
#include <cstdint>
#include <string>

#include "fsm/fsm.hpp"
#include "fsm/thread_safe_fsm.hpp"

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

TEST(ContextContractTest, SignalValidatorExecution) {
    std::uint8_t dummy_buf[4] = {1, 2, 3, 4};
    EvPacketRecv valid_packet(4, dummy_buf);
    EXPECT_TRUE(valid_packet.is_valid());

    EvPacketRecv null_packet(4, nullptr);
    EXPECT_FALSE(null_packet.is_valid());

    EvPacketRecv zero_len_packet(0, dummy_buf);
    EXPECT_FALSE(zero_len_packet.is_valid());
}

TEST(ContextContractTest, Cpp20ConceptsValidation) {
    static_assert(StateMachineContextContract<ValidDeviceContext>);
    static_assert(!StateMachineContextContract<IncompleteContext>);
    static_assert(!StateMachineContextContract<int>);

    ValidDeviceContext ctx;
    std::uint8_t buf[2] = {0xAA, 0x55};
    EvPacketRecv ev(2, buf);
    EXPECT_TRUE(ctx.is_valid(ev));
}

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

}  // namespace
