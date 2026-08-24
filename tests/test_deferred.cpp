#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/frontend/cameo_xmi_parser.hpp"
#include "fsm/frontend/dot_parser.hpp"
#include "fsm/frontend/json_parser.hpp"
#include "fsm/frontend/mermaid_parser.hpp"
#include "fsm/frontend/plantuml_parser.hpp"
#include "fsm/frontend/scxml_parser.hpp"
#include "fsm/fsm.hpp"
#include "fsm/middleend/fsm_validator.hpp"
#include "fsm/thread_safe_fsm.hpp"

using namespace fsm::codegen;

namespace {

// ============================================================================
// Multi-Format Parsing Tests
// ============================================================================

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
    using deferred_events = fsm::type_list<RequestCmd, DataPacket>;
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

// Context for verification
struct PipelineContext {
    bool init_done_called = false;
    bool request_handled = false;
    int received_payload = 0;
};

// Actions
struct OnInitDoneAction {
    void operator()(const InitDone& /*evt*/, Initializing& /*src*/, Ready& /*dst*/, PipelineContext& ctx) const {
        ctx.init_done_called = true;
    }
};

struct OnRequestAction {
    void operator()(const RequestCmd& /*evt*/, Ready& /*src*/, Processing& /*dst*/, PipelineContext& ctx) const {
        ctx.request_handled = true;
    }
};

struct OnPacketAction {
    void operator()(const DataPacket& evt, Processing& /*src*/, Completed& /*dst*/, PipelineContext& ctx) const {
        ctx.received_payload = evt.payload;
    }
};

// Transition Table
using PipelineTable =
    fsm::transition_table<fsm::transition<Initializing, InitDone, Ready, OnInitDoneAction, fsm::no_guard>,
                          fsm::transition<Ready, RequestCmd, Processing, OnRequestAction, fsm::no_guard>,
                          fsm::transition<Processing, DataPacket, Completed, OnPacketAction, fsm::no_guard>>;

TEST(DeferredEventsTest, SyncRuntimeCascadeReplay) {
    PipelineContext ctx;
    fsm::fsm<PipelineTable, PipelineContext> sm(ctx);

    EXPECT_TRUE(sm.is_in_state<Initializing>());
    EXPECT_EQ(sm.deferred_count(), 0U);

    // 1. Dispatch RequestCmd during Initializing -> Should be DEFERRED
    auto res1 = sm.dispatch(RequestCmd{});
    EXPECT_TRUE(res1.is_deferred());  // Accepted into deferred queue
    EXPECT_TRUE(sm.is_in_state<Initializing>());
    EXPECT_EQ(sm.deferred_count(), 1U);
    EXPECT_FALSE(ctx.request_handled);

    // 2. Dispatch DataPacket during Initializing -> Should be DEFERRED
    auto res2 = sm.dispatch(DataPacket{42});
    EXPECT_TRUE(res2.is_deferred());
    EXPECT_TRUE(sm.is_in_state<Initializing>());
    EXPECT_EQ(sm.deferred_count(), 2U);
    EXPECT_EQ(ctx.received_payload, 0);

    // 3. Dispatch non-deferred and unhandled event -> Should return false
    auto res3 = sm.dispatch(NonDeferredEvent{});
    EXPECT_TRUE(res3.is_unhandled());
    EXPECT_EQ(sm.deferred_count(), 2U);

    // 4. Dispatch InitDone -> transitions to Ready
    // Upon entry to Ready: RequestCmd is replayed -> transitions to Processing!
    // Upon entry to Processing: DataPacket is replayed -> transitions to Completed!
    auto res4 = sm.dispatch(InitDone{});
    EXPECT_TRUE(res4.is_success());
    EXPECT_TRUE(ctx.init_done_called);
    EXPECT_TRUE(ctx.request_handled);
    EXPECT_EQ(ctx.received_payload, 42);
    EXPECT_TRUE(sm.is_in_state<Completed>());
    EXPECT_EQ(sm.deferred_count(), 0U);
}

TEST(DeferredEventsTest, AsyncRuntimeExecution) {
    PipelineContext ctx;
    fsm::thread_safe_fsm<PipelineTable, PipelineContext> async_sm(ctx);
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

    EXPECT_TRUE(ctx.init_done_called);
    EXPECT_TRUE(ctx.request_handled);
    EXPECT_EQ(ctx.received_payload, 99);
    EXPECT_EQ(async_sm.deferred_count(), 0U);

    async_sm.stop_worker();
}

}  // namespace
