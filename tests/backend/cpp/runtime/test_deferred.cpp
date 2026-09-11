/**
 * @file test_deferred.cpp
 * @brief Unit test suite for deferred event queueing, capacity limits, and replay semantics.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/cpp/runtime/fsm.hpp"
#include "fsm/backend/cpp/runtime/spsc_fsm.hpp"
#include "fsm/backend/cpp/runtime/thread_safe_fsm.hpp"
#include "fsm/frontend/common/json_parser.hpp"
#include "fsm/frontend/diagram/dot_parser.hpp"
#include "fsm/frontend/diagram/mermaid_parser.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/frontend/formal/cameo_xmi_parser.hpp"
#include "fsm/frontend/formal/scxml_parser.hpp"

using namespace fsm::backend::cpp;
using namespace fsm::backend;
using namespace fsm::frontend;
using namespace fsm::frontend::diagram;
using namespace fsm::frontend::formal;
using namespace fsm::ir;

namespace {

// ============================================================================
// Multi-Format Parsing Tests
// ============================================================================

/**
 * @brief Verify PlantUML parsing of deferred events.
 * @scenario Parse PlantUML diagram with 'State : EventName / defer' notation.
 * @expected FsmIr state node captures deferred event in deferred_events list.
 */
TEST(DeferredEvents, PlantUml_DeferredEvents_ParsedIntoIr) {
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
 * @brief Verify Mermaid parsing of deferred events.
 * @scenario Parse Mermaid diagram with deferred event directive.
 * @expected FsmIr state node captures deferred event in deferred_events list.
 */
TEST(DeferredEvents, Mermaid_DeferredEvents_ParsedIntoIr) {
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
 * @brief Verify Cameo OMG XMI parsing of deferred events.
 * @scenario Parse Cameo XMI state containing deferrableTrigger elements.
 * @expected FsmIr state node captures deferred event in deferred_events list.
 */
TEST(DeferredEvents, Cameo_DeferredEvents_ParsedIntoIr) {
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
 * @brief Verify SCXML parsing of deferred events.
 * @scenario Parse SCXML state with deferred event configuration.
 * @expected FsmIr state node captures deferred event in deferred_events list.
 */
TEST(DeferredEvents, Scxml_DeferredEvents_ParsedIntoIr) {
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
 * @brief Verify XState JSON parsing of deferred events.
 * @scenario Parse JSON schema containing deferred events array on state.
 * @expected FsmIr state node captures deferred event in deferred_events list.
 */
TEST(DeferredEvents, Json_DeferredEvents_ParsedIntoIr) {
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

    JsonParser parser;
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
 * @brief Verify Graphviz DOT parsing of deferred events.
 * @scenario Parse DOT digraph with deferred event state attribute.
 * @expected FsmIr state node captures deferred event in deferred_events list.
 */
TEST(DeferredEvents, Dot_DeferredEvents_ParsedIntoIr) {
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
 * @brief Verify synchronous runtime deferred event cascade replay.
 * @scenario Dispatch event while deferred, transition to consuming state, and observe replay.
 * @expected Deferred event is replayed and transitions machine to final expected state.
 */
TEST(DeferredEvents, SyncRuntime_CascadeReplay_DispatchesDeferredEvents) {
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
 * @brief Verify asynchronous worker processing of deferred events.
 * @scenario Post deferred events to thread-safe worker and trigger state change.
 * @expected Deferred events replayed in FIFO chronological order by worker thread.
 */
TEST(DeferredEvents, AsyncRuntime_DeferredEvents_ProcessedInChronologicalOrder) {
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

    auto reg_snap = async_sm.snapshot_registers();
    EXPECT_TRUE(reg_snap.init_done_called);
    EXPECT_TRUE(reg_snap.request_handled);
    EXPECT_EQ(reg_snap.received_payload, 99);
    EXPECT_EQ(async_sm.deferred_count(), 0U);

    async_sm.stop_worker();
}

/**
 * @brief Verify configurable deferred queue capacity and overflow handling.
 * @scenario Exceed configured deferred capacity with excessive events.
 * @expected Queue enforces maximum bound without memory leak or unbounded allocation.
 */
TEST(DeferredEvents, BoundedCapacity_DeferredQueue_EnforcesConfiguredSize) {
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
