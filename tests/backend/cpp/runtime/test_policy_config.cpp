#include <gtest/gtest.h>

#include <string>
#include <type_traits>

#include "fsm/backend/cpp/runtime/config.hpp"
#include "fsm/backend/cpp/runtime/fsm.hpp"
#include "fsm/backend/cpp/runtime/spsc_fsm.hpp"
#include "fsm/backend/cpp/runtime/thread_safe_fsm.hpp"

namespace {

// ============================================================================
// Test Fixtures & Types
// ============================================================================

struct StateA {};
struct StateB {};

struct EvNext {};

struct DummyRegisters {
    int counter{0};
    double value{0.0};
};

struct DummyInPorts {
    float sensor_readout{0.0F};
};

struct DummyOutPorts {
    bool valve_open{false};
};

struct DummyServices {
    int service_id{42};
};

using PolicyTestTable = fsm::transition_table<fsm::row<StateA, EvNext, StateB>, fsm::row<StateB, EvNext, StateA>>;

/**
 * @brief Test Intent: Verify default policy extraction in fsm::config.
 *
 * Scenario:
 * - Instantiate fsm::config<PolicyTestTable> with no modifier policies.
 * - Verify all domain interfaces resolve to default no_* types and capacities.
 */
TEST(PolicyConfigTest, DefaultPolicyExtraction) {
    using Cfg = fsm::config<PolicyTestTable>;

    static_assert(std::is_same_v<Cfg::table_type, PolicyTestTable>);
    static_assert(std::is_same_v<Cfg::registers_type, fsm::no_registers>);
    static_assert(std::is_same_v<Cfg::in_ports_type, fsm::no_ports>);
    static_assert(std::is_same_v<Cfg::out_ports_type, fsm::no_ports>);
    static_assert(std::is_same_v<Cfg::services_type, fsm::no_services>);
    static_assert(std::is_same_v<Cfg::initial_state_type, StateA>);
    static_assert(Cfg::deferred_capacity == 16);
    static_assert(Cfg::queue_capacity == 64);
    static_assert(fsm::is_config_v<Cfg>);
}

/**
 * @brief Test Intent: Verify custom policy extraction in arbitrary order.
 *
 * Scenario:
 * - Instantiate fsm::config with with_registers, with_ports, with_services, and with_queue_capacity.
 * - Verify policies are correctly mapped regardless of specification order.
 */
TEST(PolicyConfigTest, ArbitraryOrderPolicyExtraction) {
    using Cfg1 =
        fsm::config<PolicyTestTable, fsm::with_registers<DummyRegisters>, fsm::with_ports<DummyInPorts, DummyOutPorts>,
                    fsm::with_services<DummyServices>, fsm::with_queue_capacity<128>, fsm::with_deferred_capacity<32>>;

    using Cfg2 = fsm::config<PolicyTestTable, fsm::with_deferred_capacity<32>, fsm::with_services<DummyServices>,
                             fsm::with_queue_capacity<128>, fsm::with_ports<DummyInPorts, DummyOutPorts>,
                             fsm::with_registers<DummyRegisters>>;

    static_assert(std::is_same_v<typename Cfg1::registers_type, DummyRegisters>);
    static_assert(std::is_same_v<typename Cfg1::in_ports_type, DummyInPorts>);
    static_assert(std::is_same_v<typename Cfg1::out_ports_type, DummyOutPorts>);
    static_assert(std::is_same_v<typename Cfg1::services_type, DummyServices>);
    static_assert(Cfg1::deferred_capacity == 32);
    static_assert(Cfg1::queue_capacity == 128);

    // Order invariance
    static_assert(std::is_same_v<typename Cfg1::registers_type, typename Cfg2::registers_type>);
    static_assert(std::is_same_v<typename Cfg1::in_ports_type, typename Cfg2::in_ports_type>);
    static_assert(std::is_same_v<typename Cfg1::out_ports_type, typename Cfg2::out_ports_type>);
    static_assert(std::is_same_v<typename Cfg1::services_type, typename Cfg2::services_type>);
    static_assert(Cfg1::deferred_capacity == Cfg2::deferred_capacity);
    static_assert(Cfg1::queue_capacity == Cfg2::queue_capacity);
}

/**
 * @brief Test Intent: Verify instantiation and execution of fsm::make_fsm.
 *
 * Scenario:
 * - Instantiate synchronous FSM via fsm::make_fsm<PolicyTestTable, with_registers<DummyRegisters>>.
 * - Verify state transitions and register manipulation.
 */
TEST(PolicyConfigTest, MakeFsmExecution) {
    DummyRegisters initial_regs{10, 3.14};
    fsm::make_fsm<PolicyTestTable, fsm::with_registers<DummyRegisters>> sm(initial_regs);

    EXPECT_TRUE(sm.is_in<StateA>());
    EXPECT_EQ(sm.registers().counter, 10);

    sm.registers().counter += 5;
    EXPECT_EQ(sm.registers().counter, 15);

    auto res = sm.dispatch(EvNext{});
    EXPECT_TRUE(res);
    EXPECT_TRUE(sm.is_in<StateB>());
}

/**
 * @brief Test Intent: Verify instantiation and lock-free execution of fsm::make_spsc_fsm.
 *
 * Scenario:
 * - Instantiate spsc_fsm via fsm::make_spsc_fsm with with_registers and with_queue_capacity.
 * - Post events, process transitions, and verify seqlock snapshot.
 */
TEST(PolicyConfigTest, MakeSpscFsmExecution) {
    DummyRegisters initial_regs{100, 2.718};
    fsm::make_spsc_fsm<PolicyTestTable, fsm::with_registers<DummyRegisters>, fsm::with_queue_capacity<32>> spsc_machine(
        initial_regs);

    EXPECT_TRUE(spsc_machine.is_in<StateA>());
    EXPECT_TRUE(spsc_machine.post(EvNext{}));

    EXPECT_TRUE(spsc_machine.process_one());
    EXPECT_TRUE(spsc_machine.is_in<StateB>());

    auto snap = spsc_machine.snapshot_registers();
    EXPECT_EQ(snap.counter, 100);
}

/**
 * @brief Test Intent: Verify instantiation and safe-by-design access of fsm::make_thread_safe_fsm.
 *
 * Scenario:
 * - Instantiate thread_safe_fsm via fsm::make_thread_safe_fsm.
 * - Verify with_registers and snapshot_registers without uncoordinated naked references.
 */
TEST(PolicyConfigTest, MakeThreadSafeFsmSafeByDesign) {
    DummyRegisters initial_regs{50, 1.414};
    fsm::make_thread_safe_fsm<PolicyTestTable, fsm::with_registers<DummyRegisters>> async_machine(initial_regs);

    EXPECT_TRUE(async_machine.is_in<StateA>());

    // Safe-by-design modification
    async_machine.with_registers([](DummyRegisters& r) {
        r.counter += 25;
        r.value = 9.99;
    });

    // Safe-by-design read snapshot
    auto snap = async_machine.snapshot_registers();
    EXPECT_EQ(snap.counter, 75);
    EXPECT_DOUBLE_EQ(snap.value, 9.99);

    // Safe transition
    async_machine.send(EvNext{});
    EXPECT_TRUE(async_machine.is_in<StateB>());
}

/**
 * @brief Test Intent: Verify with_timer_capacity and with_trace_buffer extraction and execution.
 *
 * Scenario:
 * - Instantiate config with with_trace_buffer<16> and with_timer_capacity<8>.
 * - Verify policy types and compile-time capacity extraction.
 * - Execute timer ticking and observer transition logging.
 */
TEST(PolicyConfigTest, TimerCapacityAndTraceBufferPolicies) {
    using Cfg = fsm::config<PolicyTestTable, fsm::with_trace_buffer<16>, fsm::with_timer_capacity<8>>;

    static_assert(Cfg::timer_capacity == 8);
    static_assert(std::is_same_v<typename Cfg::observer_type, fsm::flight_recorder_observer<16>>);

    fsm::make_fsm<PolicyTestTable, fsm::with_trace_buffer<16>, fsm::with_timer_capacity<8>> sm;
    EXPECT_TRUE(sm.is_in<StateA>());

    // Start a timer and step time
    sm.timer_manager().start_timer(1, 100);
    EXPECT_TRUE(sm.timer_manager().is_timer_active(1));
    std::size_t expired = sm.tick(150);
    EXPECT_EQ(expired, 1u);

    // Dispatch transition and check observer trace
    sm.dispatch(EvNext{});
    EXPECT_TRUE(sm.is_in<StateB>());
    EXPECT_EQ(sm.observer().recorder().size(), 1u);
    EXPECT_EQ(sm.observer().recorder()[0].target_state, "StateB");
}

/**
 * @brief Test Intent: Verify fluent on transition table syntax (both type-level and decltype expressions).
 */
TEST(PolicyConfigTest, FluentOnTransitionBuilder) {
    // 1. Single-argument fluent on<Event>::from<Source>::to<Target>
    using FluentTable1 = fsm::transition_table<
        fsm::on<EvNext>::from<StateA>::to<StateB>,
        fsm::on<EvNext>::from<StateB>::to<StateA>
    >;
    fsm::make_fsm<FluentTable1> sm1;
    EXPECT_TRUE(sm1.is_in<StateA>());
    sm1.dispatch(EvNext{});
    EXPECT_TRUE(sm1.is_in<StateB>());

    // 2. Direct on<Event, Source>::to<Target>
    using FluentTable2 = fsm::transition_table<
        fsm::on<EvNext, StateA>::to<StateB>,
        fsm::on<EvNext, StateB>::to<StateA>
    >;
    fsm::make_fsm<FluentTable2> sm2;
    EXPECT_TRUE(sm2.is_in<StateA>());
    sm2.dispatch(EvNext{});
    EXPECT_TRUE(sm2.is_in<StateB>());
}

}  // namespace
