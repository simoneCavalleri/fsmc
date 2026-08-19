#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "codegen/cameo_xmi_parser.hpp"
#include "codegen/cpp_generator.hpp"
#include "codegen/dot_parser.hpp"
#include "codegen/fsm_validator.hpp"
#include "codegen/json_parser.hpp"
#include "codegen/mermaid_parser.hpp"
#include "codegen/plantuml_parser.hpp"
#include "codegen/scxml_parser.hpp"
#include "fsm/fsm.hpp"
#include "fsm/thread_safe_fsm.hpp"

using namespace fsm::codegen;

namespace {

// ============================================================================
// Multi-Format Parsing Tests
// ============================================================================

void test_deferred_events_plantuml() {
    std::cout << "[TEST] Running test_deferred_events_plantuml...\n";

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
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(puml, model, err);

    assert(is_parsed);
    const auto* init_state = model.find_state("Initializing");
    assert(init_state != nullptr);
    assert(init_state->deferred_events.size() == 2);
    assert(init_state->deferred_events[0] == "RequestCmd");
    assert(init_state->deferred_events[1] == "DataPacket");

    std::cout << "[PASS] test_deferred_events_plantuml passed.\n";
}

void test_deferred_events_mermaid() {
    std::cout << "[TEST] Running test_deferred_events_mermaid...\n";

    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Booting
        Booting : defer UserInput
        Booting --> Running : BootComplete
    )";

    MermaidParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(mmd, model, err);

    assert(is_parsed);
    const auto* boot_state = model.find_state("Booting");
    assert(boot_state != nullptr);
    assert(boot_state->deferred_events.size() == 1);
    assert(boot_state->deferred_events[0] == "UserInput");

    std::cout << "[PASS] test_deferred_events_mermaid passed.\n";
}

void test_deferred_events_cameo() {
    std::cout << "[TEST] Running test_deferred_events_cameo...\n";

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
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(xmi, model, err);

    assert(is_parsed);
    const auto* init_state = model.find_state("Initializing");
    assert(init_state != nullptr);
    assert(init_state->deferred_events.size() == 1);
    assert(init_state->deferred_events[0] == "RequestCmd");

    std::cout << "[PASS] test_deferred_events_cameo passed.\n";
}

void test_deferred_events_scxml() {
    std::cout << "[TEST] Running test_deferred_events_scxml...\n";

    const std::string scxml = R"(<?xml version="1.0" encoding="UTF-8"?>
    <scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Initializing" name="DeferSM">
      <state id="Initializing">
        <defer event="RequestCmd"/>
        <transition event="InitDone" target="Ready"/>
      </state>
      <state id="Ready"/>
    </scxml>)";

    ScxmlParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(scxml, model, err);

    assert(is_parsed);
    const auto* init_state = model.find_state("Initializing");
    assert(init_state != nullptr);
    assert(init_state->deferred_events.size() == 1);
    assert(init_state->deferred_events[0] == "RequestCmd");

    std::cout << "[PASS] test_deferred_events_scxml passed.\n";
}

void test_deferred_events_json() {
    std::cout << "[TEST] Running test_deferred_events_json...\n";

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
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(json, model, err);

    assert(is_parsed);
    const auto* init_state = model.find_state("Initializing");
    assert(init_state != nullptr);
    assert(init_state->deferred_events.size() == 2);
    assert(init_state->deferred_events[0] == "RequestCmd");
    assert(init_state->deferred_events[1] == "DataPacket");

    std::cout << "[PASS] test_deferred_events_json passed.\n";
}

void test_deferred_events_dot() {
    std::cout << "[TEST] Running test_deferred_events_dot...\n";

    const std::string dot = R"(
    digraph DeferFSM {
        __start__ [shape=point];
        __start__ -> Initializing;
        Initializing [defer="RequestCmd, DataPacket"];
        Initializing -> Ready [label="InitDone"];
    }
    )";

    DotParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(dot, model, err);

    assert(is_parsed);
    const auto* init_state = model.find_state("Initializing");
    assert(init_state != nullptr);
    assert(init_state->deferred_events.size() == 2);
    assert(init_state->deferred_events[0] == "RequestCmd");
    assert(init_state->deferred_events[1] == "DataPacket");

    std::cout << "[PASS] test_deferred_events_dot passed.\n";
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
using PipelineTable = fsm::transition_table<
    fsm::transition<Initializing, InitDone, Ready, OnInitDoneAction, fsm::no_guard>,
    fsm::transition<Ready, RequestCmd, Processing, OnRequestAction, fsm::no_guard>,
    fsm::transition<Processing, DataPacket, Completed, OnPacketAction, fsm::no_guard>
>;

void test_deferred_events_sync_runtime() {
    std::cout << "[TEST] Running test_deferred_events_sync_runtime...\n";

    PipelineContext ctx;
    fsm::fsm<PipelineTable, PipelineContext> sm(ctx);

    assert(sm.is_in_state<Initializing>());
    assert(sm.deferred_count() == 0);

    // 1. Dispatch RequestCmd during Initializing -> Should be DEFERRED
    bool handled = sm.dispatch(RequestCmd{});
    assert(handled); // Accepted into deferred queue
    assert(sm.is_in_state<Initializing>());
    assert(sm.deferred_count() == 1);
    assert(!ctx.request_handled);

    // 2. Dispatch DataPacket during Initializing -> Should be DEFERRED
    handled = sm.dispatch(DataPacket{42});
    assert(handled);
    assert(sm.is_in_state<Initializing>());
    assert(sm.deferred_count() == 2);
    assert(ctx.received_payload == 0);

    // 3. Dispatch non-deferred and unhandled event -> Should return false
    handled = sm.dispatch(NonDeferredEvent{});
    assert(!handled);
    assert(sm.deferred_count() == 2);

    // 4. Dispatch InitDone -> transitions to Ready
    // Upon entry to Ready: RequestCmd is replayed -> transitions to Processing!
    // Upon entry to Processing: DataPacket is replayed -> transitions to Completed!
    handled = sm.dispatch(InitDone{});
    assert(handled);
    assert(ctx.init_done_called);
    assert(ctx.request_handled);
    assert(ctx.received_payload == 42);
    assert(sm.is_in_state<Completed>());
    assert(sm.deferred_count() == 0);

    std::cout << "[PASS] test_deferred_events_sync_runtime passed (Cascade replay verified).\n";
}

void test_deferred_events_async_runtime() {
    std::cout << "[TEST] Running test_deferred_events_async_runtime...\n";

    PipelineContext ctx;
    fsm::thread_safe_fsm<PipelineTable, PipelineContext> async_sm(ctx);
    async_sm.start_worker();

    assert(async_sm.is_in_state<Initializing>());

    // Post deferred events first from producer thread
    async_sm.post(RequestCmd{});
    async_sm.post(DataPacket{99});

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(async_sm.is_in_state<Initializing>());
    assert(async_sm.deferred_count() == 2);

    // Post trigger event
    async_sm.post(InitDone{});

    // Wait until background worker processes transition cascade
    while (!async_sm.is_in_state<Completed>()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    assert(ctx.init_done_called);
    assert(ctx.request_handled);
    assert(ctx.received_payload == 99);
    assert(async_sm.deferred_count() == 0);

    async_sm.stop_worker();
    std::cout << "[PASS] test_deferred_events_async_runtime passed.\n";
}

}  // namespace

int main() {
    std::cout << "========================================\n"
              << "     RUNNING UML 2.5 DEFERRED TESTS     \n"
              << "========================================\n";

    test_deferred_events_plantuml();
    test_deferred_events_mermaid();
    test_deferred_events_cameo();
    test_deferred_events_scxml();
    test_deferred_events_json();
    test_deferred_events_dot();
    test_deferred_events_sync_runtime();
    test_deferred_events_async_runtime();

    std::cout << "========================================\n"
              << "     ALL DEFERRED TESTS PASSED (8/8)!   \n"
              << "========================================\n";
    return 0;
}
