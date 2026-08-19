#include <cassert>
#include <iostream>
#include <string>

#include "codegen/cpp_generator.hpp"
#include "codegen/fsm_model.hpp"
#include "codegen/fsm_validator.hpp"
#include "codegen/sysml2_parser.hpp"

using namespace fsm::codegen;

void test_sysml2_multiline_transition_parsing() {
    std::cout << "[TEST] Running test_sysml2_multiline_transition_parsing...\n";

    const std::string sysml_text = R"(
    state def MissionBehavior {
        entry; then Standby;

        state Standby;
        state InFlight;

        transition authorize_mission
            first Standby
            accept AuthorizeCmd
            if ValidClearanceGuard
            do ArmEnginesAction
            then InFlight;

        transition abort_mission
            first Standby
            accept AuthorizeCmd
            if NoClearanceGuard
            do TriggerAlarmAction
            then Aborted;
    }
    )";

    Sysml2Parser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(sysml_text, model, err);

    assert(is_parsed);
    assert(model.name == "MissionBehavior");
    assert(model.initial_state == "Standby");
    assert(model.find_state("Standby") != nullptr);
    assert(model.find_state("InFlight") != nullptr);
    assert(model.find_state("Aborted") != nullptr);
    assert(model.events.size() == 1);
    assert(model.guards.size() == 2);
    assert(model.actions.size() == 2);
    assert(model.transitions.size() == 2);

    std::cout << "[PASS] test_sysml2_multiline_transition_parsing passed.\n";
}

void test_sysml2_compact_transition_parsing() {
    std::cout << "[TEST] Running test_sysml2_compact_transition_parsing...\n";

    const std::string sysml_text = R"(
    state def DeviceProtocol {
        entry; then Disconnected;

        state Disconnected;
        state Connecting;
        state Connected;

        transition from Disconnected accept ConnectCmd then Connecting;
        transition from Connecting accept SuccessEvent do OnConnectedAction then Connected;
        transition from Connecting accept FailEvent then Disconnected;
        transition from Connected accept DisconnectCmd then Disconnected;
    }
    )";

    Sysml2Parser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(sysml_text, model, err);

    assert(is_parsed);
    assert(model.initial_state == "Disconnected");
    assert(model.states.size() == 3);
    assert(model.transitions.size() == 4);
    assert(model.actions.size() == 1);

    std::cout << "[PASS] test_sysml2_compact_transition_parsing passed.\n";
}

void test_sysml2_composite_states_and_codegen() {
    std::cout << "[TEST] Running test_sysml2_composite_states_and_codegen...\n";

    const std::string sysml_text = R"(
    state def Spacecraft {
        entry; then Standby;

        state Standby {
            entry; then Diagnostics;
            state Diagnostics;
            state Calibrated;
        }

        state InFlight {
            entry; then Ascending;
            state Ascending;
            state Cruising;
        }

        transition from Calibrated accept AuthorizeCmd then Ascending;
        transition from Ascending accept AltitudeReachedEvent do DeployPanelsAction then Cruising;
    }
    )";

    Sysml2Parser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(sysml_text, model, err);

    assert(is_parsed);
    const auto* standby = model.find_state("Standby");
    assert(standby != nullptr);
    assert(standby->is_composite);
    assert(standby->initial_sub_state == "Diagnostics");

    const auto* diag = model.find_state("Diagnostics");
    assert(diag != nullptr);
    assert(diag->parent_state == "Standby");

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    opts.standalone = true;

    const std::string code = CppGenerator::generate_header(model, opts);
    assert(!code.empty());
    assert(code.find("struct Diagnostics") != std::string::npos);
    assert(code.find("struct DeployPanelsAction") != std::string::npos);

    std::cout << "[PASS] test_sysml2_composite_states_and_codegen passed.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "     RUNNING SYSML V2 PARSER TESTS      \n";
    std::cout << "========================================\n";

    test_sysml2_multiline_transition_parsing();
    test_sysml2_compact_transition_parsing();
    test_sysml2_composite_states_and_codegen();

    std::cout << "========================================\n";
    std::cout << "     ALL SYSML V2 TESTS PASSED (3/3)!   \n";
    std::cout << "========================================\n";
    return 0;
}
