#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/cpp/runtime/fsm.hpp"
#include "fsm/backend/cpp/runtime/spsc_fsm.hpp"
#include "fsm/backend/cpp/runtime/thread_safe_fsm.hpp"
#include "fsm/frontend/diagram/dot_parser.hpp"
#include "fsm/frontend/diagram/json_parser.hpp"
#include "fsm/frontend/diagram/mermaid_parser.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/frontend/formal/cameo_xmi_parser.hpp"
#include "fsm/frontend/formal/scxml_parser.hpp"

using namespace ::fsm::codegen;

namespace {

// ============================================================================
// Multi-Format Parsing Tests
// ============================================================================

/**
 * @brief Test Intent: Verify PlantUML `defer <Event>` directive parsing into state deferred events.
 *
 * Scenario:
 * - Parse PlantUML with `Initializing : defer RequestCmd` and `Initializing : defer DataPacket`.
 * - Verify IR state contains both deferred event names.
 */
TEST(DeferredEventsTest, PlantUmlParsing) {
    const std::string puml = R"(
    @startuml
    [*] --> Initializing

    Initializing : defer RequestCmd
    Initializing : defer DataPacket

    Initializing --> Ready : InitDone
    Ready --> Processing : RequestCmd
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err;

    const auto* init_state = model.find_state("Initializing");
    ASSERT_NE(init_state, nullptr);
    ASSERT_EQ(init_state->deferred_events.size(), 2U);
    EXPECT_EQ(init_state->deferred_events[0], "RequestCmd");
    EXPECT_EQ(init_state->deferred_events[1], "DataPacket");
}

/**
 * @brief Test Intent: Verify Mermaid `defer <Event>` syntax parsing.
 *
 * Scenario:
 * - Parse Mermaid with `Booting : defer UserInput`.
 * - Verify Booting state records UserInput in deferred_events.
 */
TEST(DeferredEventsTest, MermaidParsing) {
    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Booting
        Booting : defer UserInput
        Booting --> Running : BootComplete
    )";

    MermaidParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err;

    const auto* boot_state = model.find_state("Booting");
    ASSERT_NE(boot_state, nullptr);
    ASSERT_EQ(boot_state->deferred_events.size(), 1U);
    EXPECT_EQ(boot_state->deferred_events[0], "UserInput");
}

/**
 * @brief Test Intent: Verify Cameo / MagicDraw XMI deferrableTrigger element parsing.
 *
 * Scenario:
 * - Parse OMG XMI containing `<deferrableTrigger name="RequestCmd"/>`.
 * - Verify state records RequestCmd in deferred_events list.
 */
TEST(DeferredEventsTest, CameoParsing) {
    const std::string xmi = R"(<?xml version="1.0" encoding="UTF-8"?>
    <xmi:XMI xmi:version="2.1" xmlns:uml="http://www.omg.org/spec/UML/20090901" xmlns:xmi="http://schema.omg.org/spec/XMI/2.1">
      <uml:Model xmi:id="_m1" name="CameoDeferModel">
        <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm1" name="DeferSM">
          <region xmi:id="_r1">
            <subvertex xmi:type="uml:Pseudostate" xmi:id="_ps1" kind="initial"/>
            <subvertex xmi:type="uml:State" xmi:id="_s_init" name="Initializing">
              <deferrableTrigger xmi:type="uml:Trigger" xmi:id="_dt1" name="RequestCmd"/>
            </subvertex>
            <subvertex xmi:type="uml:State" xmi:id="_s_ready" name="Ready"/>
            <transition xmi:id="_t0" source="_ps1" target="_s_init"/>
            <transition xmi:id="_t1" source="_s_init" target="_s_ready" trigger="InitDone"/>
          </region>
        </packagedElement>
      </uml:Model>
    </xmi:XMI>)";

    CameoXmiParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(xmi, model, err)) << "Error: " << err;

    const auto* init_state = model.find_state("Initializing");
    ASSERT_NE(init_state, nullptr);
    ASSERT_EQ(init_state->deferred_events.size(), 1U);
    EXPECT_EQ(init_state->deferred_events[0], "RequestCmd");
}

/**
 * @brief Test Intent: Verify W3C SCXML `<defer event="..."/>` syntax parsing.
 *
 * Scenario:
 * - Parse SCXML with `<defer event="RequestCmd"/>` child element inside `<state>`.
 * - Verify parsed FsmIr captures the deferred event definition.
 */
TEST(DeferredEventsTest, ScxmlParsing) {
    const std::string scxml = R"(<?xml version="1.0" encoding="UTF-8"?>
    <scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Initializing" name="DeferSM">
      <state id="Initializing">
        <defer event="RequestCmd"/>
        <transition event="InitDone" target="Ready"/>
      </state>
      <state id="Ready"/>
    </scxml>)";

    ScxmlParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(scxml, model, err)) << "Error: " << err;

    const auto* init_state = model.find_state("Initializing");
    ASSERT_NE(init_state, nullptr);
    ASSERT_EQ(init_state->deferred_events.size(), 1U);
    EXPECT_EQ(init_state->deferred_events[0], "RequestCmd");
}

/**
 * @brief Test Intent: Verify JSON statechart `"defer": [...]` array parsing.
 *
 * Scenario:
 * - Parse XState JSON with `"defer": ["RequestCmd", "DataPacket"]`.
 * - Verify both deferred events are captured in IR.
 */
TEST(DeferredEventsTest, JsonParsing) {
    const std::string json = R"({
      "id": "DeferSM",
      "initial": "Initializing",
      "states": {
        "Initializing": {
          "defer": ["RequestCmd", "DataPacket"],
          "on": {
            "InitDone": "Ready"
          }
        },
        "Ready": {}
      }
    })";

    JsonStateParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(json, model, err)) << "Error: " << err;

    const auto* init_state = model.find_state("Initializing");
    ASSERT_NE(init_state, nullptr);
    ASSERT_EQ(init_state->deferred_events.size(), 2U);
    EXPECT_EQ(init_state->deferred_events[0], "RequestCmd");
    EXPECT_EQ(init_state->deferred_events[1], "DataPacket");
}

/**
 * @brief Test Intent: Verify Graphviz DOT `defer="A, B"` attribute parsing.
 *
 * Scenario:
 * - Parse DOT graph with `Initializing [defer="RequestCmd, DataPacket"]`.
 * - Verify parsed FsmIr captures both comma-separated deferred events.
 */
TEST(DeferredEventsTest, DotParsing) {
    const std::string dot = R"(
    digraph DeferFSM {
        __start__ [shape=point];
        __start__ -> Initializing;
        Initializing [defer="RequestCmd, DataPacket"];
        Initializing -> Ready [label="InitDone"];
    }
    )";

    DotParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(dot, model, err)) << "Error: " << err;

    const auto* init_state = model.find_state("Initializing");
    ASSERT_NE(init_state, nullptr);
    ASSERT_EQ(init_state->deferred_events.size(), 2U);
    EXPECT_EQ(init_state->deferred_events[0], "RequestCmd");
    EXPECT_EQ(init_state->deferred_events[1], "DataPacket");
}

// ============================================================================
// Runtime Execution Tests (Synchronous & Asynchronous)
// ============================================================================

// Events
struct InitDone {};
struct RequestCmd {};
struct DataPacket {
    int payload = 0;
};
struct NonDeferredEvent {};

// States
struct Initializing {
    static constexpr std::string_view name = "Initializing";
    using deferred_events = ::fsm::type_list<RequestCmd, DataPacket>;
};

struct Ready {
    static constexpr std::string_view name = "Ready";
};

struct Processing {
    static constexpr std::string_view name = "Processing";
};

struct Completed {
    static constexpr std::string_view name = "Completed";
};

// Registers for verification
struct PipelineRegisters {
    bool init_done_called = false;
    bool request_handled = false;
    int received_payload = 0;
};

// Actions
struct OnInitDoneAction {
    void operator()(PipelineRegisters& reg) const { reg.init_done_called = true; }
};

struct OnRequestAction {
    void operator()(PipelineRegisters& reg) const { reg.request_handled = true; }
};

struct OnPacketAction {
    void operator()(const DataPacket& evt, PipelineRegisters& reg) const { reg.received_payload = evt.payload; }
};

// Transition Table
using PipelineTable =
    ::fsm::transition_table<::fsm::transition<Initializing, InitDone, Ready, OnInitDoneAction, ::fsm::no_guard>,
                            ::fsm::transition<Ready, RequestCmd, Processing, OnRequestAction, ::fsm::no_guard>,
                            ::fsm::transition<Processing, DataPacket, Completed, OnPacketAction, ::fsm::no_guard>>;

/**
 * @brief Test Intent: Verify synchronous runtime cascade replay of deferred events upon state transitions.
 *
 * Scenario:
 * - Dispatch RequestCmd and DataPacket while in Initializing state (both must be deferred into queue).
 * - Dispatch InitDone: FSM enters Ready, automatically un-defers and processes RequestCmd (moving to Processing),
 *   and automatically un-defers DataPacket (moving to Completed).
 * - Verify all payload and context modifications occurred in proper FIFO order.
 */
TEST(DeferredEventsTest, SyncRuntimeCascadeReplay) {
    PipelineRegisters reg;
    ::fsm::fsm<PipelineTable, ::fsm::no_ports, ::fsm::no_ports, PipelineRegisters> sm(reg);

    EXPECT_TRUE(sm.is_in_state<Initializing>());
    EXPECT_EQ(sm.deferred_count(), 0U);

    // 1. Dispatch RequestCmd during Initializing -> Should be DEFERRED
    auto res1 = sm.dispatch(RequestCmd{});
    EXPECT_TRUE(res1.is_deferred());  // Accepted into deferred queue
    EXPECT_TRUE(sm.is_in_state<Initializing>());
    EXPECT_EQ(sm.deferred_count(), 1U);
    EXPECT_FALSE(sm.registers().request_handled);

    // 2. Dispatch DataPacket during Initializing -> Should be DEFERRED
    auto res2 = sm.dispatch(DataPacket{42});
    EXPECT_TRUE(res2.is_deferred());
    EXPECT_TRUE(sm.is_in_state<Initializing>());
    EXPECT_EQ(sm.deferred_count(), 2U);
    EXPECT_EQ(sm.registers().received_payload, 0);

    // 3. Dispatch non-deferred and unhandled event -> Should return false
    auto res3 = sm.dispatch(NonDeferredEvent{});
    EXPECT_TRUE(res3.is_unhandled());
    EXPECT_EQ(sm.deferred_count(), 2U);

    // 4. Dispatch InitDone -> transitions to Ready
    // Upon entry to Ready: RequestCmd is replayed -> transitions to Processing!
    // Upon entry to Processing: DataPacket is replayed -> transitions to Completed!
    auto res4 = sm.dispatch(InitDone{});
    EXPECT_TRUE(res4.is_success());
    EXPECT_TRUE(sm.registers().init_done_called);
    EXPECT_TRUE(sm.registers().request_handled);
    EXPECT_EQ(sm.registers().received_payload, 42);
    EXPECT_TRUE(sm.is_in_state<Completed>());
    EXPECT_EQ(sm.deferred_count(), 0U);
}

/**
 * @brief Test Intent: Verify asynchronous multi-threaded deferred event processing.
 *
 * Scenario:
 * - Start thread_safe_fsm worker thread.
 * - Post deferred events from producer thread.
 * - Post trigger event and wait for worker thread to asynchronously cascade replay and reach Completed state.
 */
TEST(DeferredEventsTest, AsyncRuntimeExecution) {
    PipelineRegisters reg;
    ::fsm::thread_safe_fsm<PipelineTable, ::fsm::no_ports, ::fsm::no_ports, PipelineRegisters> async_sm(reg);
    async_sm.start_worker();

    EXPECT_TRUE(async_sm.is_in_state<Initializing>());

    // Post deferred events first from producer thread
    async_sm.post(RequestCmd{});
    async_sm.post(DataPacket{99});

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_TRUE(async_sm.is_in_state<Initializing>());
    EXPECT_EQ(async_sm.deferred_count(), 2U);

    // Post trigger event
    async_sm.post(InitDone{});

    // Wait until background worker processes transition cascade
    while (!async_sm.is_in_state<Completed>()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(async_sm.registers().init_done_called);
    EXPECT_TRUE(async_sm.registers().request_handled);
    EXPECT_EQ(async_sm.registers().received_payload, 99);
    EXPECT_EQ(async_sm.deferred_count(), 0U);

    async_sm.stop_worker();
}

/**
 * @brief Test Intent: Verify configurable DeferredCapacity template parameter across all runtime wrappers.
 */
TEST(DeferredEventsTest, ConfigurableDeferredCapacity) {
    using CustomFsm = ::fsm::fsm<PipelineTable, ::fsm::no_ports, ::fsm::no_ports, PipelineRegisters, ::fsm::no_services,
                                 Initializing, ::fsm::no_observer, 32>;
    using CustomDynamicFsm = ::fsm::dynamic_fsm<PipelineTable, ::fsm::no_ports, ::fsm::no_ports, PipelineRegisters,
                                                ::fsm::no_services, Initializing, 32>;
    using CustomThreadSafeFsm = ::fsm::thread_safe_fsm<PipelineTable, ::fsm::no_ports, ::fsm::no_ports,
                                                       PipelineRegisters, ::fsm::no_services, Initializing, 32>;
    using CustomSpscFsm = ::fsm::spsc_fsm<PipelineTable, ::fsm::no_ports, ::fsm::no_ports, PipelineRegisters,
                                          ::fsm::no_services, 64, Initializing, 32>;

    PipelineRegisters reg;
    CustomFsm m1(reg);
    EXPECT_TRUE(m1.is_in_state<Initializing>());

    CustomDynamicFsm m2(reg);
    EXPECT_TRUE(m2.is_in_state<Initializing>());

    CustomThreadSafeFsm m3(reg);
    EXPECT_TRUE(m3.is_in_state<Initializing>());

    CustomSpscFsm m4(reg);
    EXPECT_TRUE(m4.is_in_state<Initializing>());
}

}  // namespace
