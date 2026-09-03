#include <gtest/gtest.h>

#include <concepts>
#include <cstdint>
#include <string>

#include "fsm/backend/cpp/runtime/fsm.hpp"
#include "fsm/backend/cpp/runtime/thread_safe_fsm.hpp"

namespace {

// Simulated generated Signal struct with typed members and validator
struct EvPacketRecv {
    static constexpr std::string_view name = "EvPacketRecv";
    std::uint32_t len = 0;
    const std::uint8_t* ptr = nullptr;

    constexpr EvPacketRecv(std::uint32_t length, const std::uint8_t* pointer) noexcept : len(length), ptr(pointer) {}

    [[nodiscard]] constexpr bool is_valid() const noexcept { return len > 0 && ptr != nullptr; }
};

// Formal C++20 StateMachineServiceContract concept
template <typename T>
concept StateMachineServiceContract = requires(T srv, const EvPacketRecv& ev) {
    { srv.is_valid(ev) } -> std::convertible_to<bool>;
    { srv.on_data(ev) } -> std::same_as<void>;
};

struct ValidDeviceServices {
    [[nodiscard]] static bool is_valid(const EvPacketRecv& ev) noexcept { return ev.is_valid(); }
    void on_data(const EvPacketRecv& /*ev*/) noexcept {}
};

struct IncompleteServices {
    void on_data(const EvPacketRecv& /*ev*/) noexcept {}
    // Missing is_valid
};

/**
 * @brief Test Intent: Verify runtime and constexpr validation logic on typed signal structs.
 */
TEST(DomainContractTest, SignalValidatorExecution) {
    std::uint8_t dummy_buf[4] = {1, 2, 3, 4};
    EvPacketRecv valid_packet(4, dummy_buf);
    EXPECT_TRUE(valid_packet.is_valid());

    EvPacketRecv null_packet(4, nullptr);
    EXPECT_FALSE(null_packet.is_valid());

    EvPacketRecv zero_len_packet(0, dummy_buf);
    EXPECT_FALSE(zero_len_packet.is_valid());
}

/**
 * @brief Test Intent: Verify compile-time C++20 concept requirements on user-defined services/ports structs.
 */
TEST(DomainContractTest, Cpp20ConceptsValidation) {
    static_assert(StateMachineServiceContract<ValidDeviceServices>);
    static_assert(!StateMachineServiceContract<IncompleteServices>);
    static_assert(!StateMachineServiceContract<int>);

    ValidDeviceServices srv;
    std::uint8_t buf[2] = {0xAA, 0x55};
    EvPacketRecv ev(2, buf);
    EXPECT_TRUE(srv.is_valid(ev));
}

/**
 * @brief Test Intent: Verify compile-time safety and initialization for Registers.
 */
TEST(DomainContractTest, CompileTimeDomainSafety) {
    struct StateA {};
    struct StateB {};
    struct EventX {};
    using Table = fsm::transition_table<fsm::transition<StateA, EventX, StateB>>;

    // Default construction is allowed when stateless
    static_assert(std::is_default_constructible_v<fsm::fsm<Table>>);
    static_assert(std::is_default_constructible_v<fsm::thread_safe_fsm<Table>>);

    // Domain register instantiation
    struct DeviceRegisters {
        int status_code = 0;
    };
    DeviceRegisters reg{123};
    fsm::fsm<Table, fsm::no_ports, fsm::no_ports, DeviceRegisters> machine(reg);
    EXPECT_EQ(machine.registers().status_code, 123);

    fsm::thread_safe_fsm<Table, fsm::no_ports, fsm::no_ports, DeviceRegisters> ts_machine(reg);
    EXPECT_EQ(ts_machine.snapshot_registers().status_code, 123);
}

/**
 * @brief Test Intent: Verify thread_safe_fsm::with_registers executes callable under internal lock.
 */
TEST(DomainContractTest, ThreadSafeWithRegistersMutation) {
    struct CounterReg {
        int value = 0;
    };
    struct StateA {};
    struct StateB {};
    struct EventX {};
    using Table = fsm::transition_table<fsm::transition<StateA, EventX, StateB>>;

    CounterReg reg{42};
    fsm::thread_safe_fsm<Table, fsm::no_ports, fsm::no_ports, CounterReg> ts_machine(reg);

    // with_registers allows mutating registers through a callable
    ts_machine.with_registers([](CounterReg& c) { c.value += 8; });

    // snapshot_registers returns a copy of current registers
    const CounterReg snap = ts_machine.snapshot_registers();
    EXPECT_EQ(snap.value, 50);
}

/**
 * @brief Test Intent: Verify thread_safe_fsm::snapshot_registers is independent from subsequent mutations.
 */
TEST(DomainContractTest, SnapshotRegistersIsolation) {
    struct DeltaReg {
        int counter = 0;
    };
    struct StateA {};
    struct StateB {};
    struct EventX {};
    using Table = fsm::transition_table<fsm::transition<StateA, EventX, StateB>>;

    DeltaReg reg{10};
    fsm::thread_safe_fsm<Table, fsm::no_ports, fsm::no_ports, DeltaReg> ts_machine(reg);

    const DeltaReg before = ts_machine.snapshot_registers();
    EXPECT_EQ(before.counter, 10);

    ts_machine.with_registers([](DeltaReg& c) { c.counter = 99; });

    const DeltaReg after = ts_machine.snapshot_registers();
    EXPECT_EQ(after.counter, 99);

    // The first snapshot must not have been affected
    EXPECT_EQ(before.counter, 10);
}

/**
 * @brief Test Intent: Verify const overload of with_registers for read-only access.
 */
TEST(DomainContractTest, ThreadSafeWithRegistersConstReadOnly) {
    struct ReadReg {
        double temperature = 36.6;
    };
    struct StateA {};
    struct StateB {};
    struct EventX {};
    using Table = fsm::transition_table<fsm::transition<StateA, EventX, StateB>>;

    ReadReg reg;
    fsm::thread_safe_fsm<Table, fsm::no_ports, fsm::no_ports, ReadReg> ts_machine(reg);

    const auto& const_machine = ts_machine;
    double read_value = 0.0;
    const_machine.with_registers([&](const ReadReg& c) { read_value = c.temperature; });

    EXPECT_DOUBLE_EQ(read_value, 36.6);
}

}  // namespace
