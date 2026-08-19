#include <cassert>
#include <iostream>
#include <string>

#include "codegen/cpp_generator.hpp"
#include "codegen/fsm_model.hpp"
#include "codegen/plantuml_parser.hpp"
#include "fsm/fsm.hpp"

using namespace fsm::codegen;

namespace {

void test_4_level_deep_history_ast() {
    std::cout << "[TEST] Running test_4_level_deep_history_ast...\n";

    const std::string puml = R"(@startuml
[*] --> Standby

state Operating {
    [*] --> SubSystem
    state SubSystem {
        [*] --> Module
        state Module {
            [*] --> Level4Active
            Level4Active --> Level4Calibrating : CalibrateCmd
        }
    }
}

Standby --> Operating : StartCmd
Operating --> Emergency : EStopEvent
Emergency --> Operating[H*] : ResumeDeepCmd
@enduml)";

    PlantUmlParser parser;
    FsmModel model;
    std::string err;
    assert(parser.parse(puml, model, err));

    // Check hierarchy depth
    assert(model.find_state("Operating")->is_composite);
    assert(model.find_state("SubSystem")->is_composite);
    assert(model.find_state("SubSystem")->parent_state == "Operating");
    assert(model.find_state("Module")->is_composite);
    assert(model.find_state("Module")->parent_state == "SubSystem");
    assert(model.find_state("Level4Active")->parent_state == "Module");
    assert(model.find_state("Level4Calibrating")->parent_state == "Module");

    // Check deep history flag
    assert(model.find_state("Operating")->has_history);
    assert(model.find_state("Operating")->has_deep_history);

    // Generate C++ code and verify compilation syntax
    CppGenerator gen;
    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    std::string code = gen.generate_header(model, opts);
    assert(!code.empty());
    assert(code.find("fsm::history_is<Operating, Level4Calibrating>") != std::string::npos);

    std::cout << "[PASS] test_4_level_deep_history_ast passed.\n";
}

// Runtime Execution of Deep History State Machine
struct Standby {
    static constexpr std::string_view name = "Standby";
};
struct Emergency {
    static constexpr std::string_view name = "Emergency";
};

struct Operating {
    static constexpr std::string_view name = "Operating";
};
struct Level4Active {
    static constexpr std::string_view name = "Level4Active";
    static constexpr std::string_view parent = "Operating";
};
struct Level4Calibrating {
    static constexpr std::string_view name = "Level4Calibrating";
    static constexpr std::string_view parent = "Operating";
};

struct StartCmd {};
struct CalibrateCmd {};
struct EStopEvent {};
struct ResumeDeepCmd {};

using DeepHistoryTable = fsm::transition_table<
    fsm::row<Standby, StartCmd, Level4Active>, fsm::row<Level4Active, CalibrateCmd, Level4Calibrating>,
    // EStop from anywhere inside Operating hierarchy to Emergency
    fsm::row<Level4Active, EStopEvent, Emergency>, fsm::row<Level4Calibrating, EStopEvent, Emergency>,
    // Deep history restore
    fsm::row<Emergency, ResumeDeepCmd, Level4Calibrating>::when<fsm::history_is<Operating, Level4Calibrating>>,
    fsm::row<Emergency, ResumeDeepCmd, Level4Active>::when<fsm::history_is<Operating, Level4Active>>,
    fsm::row<Emergency, ResumeDeepCmd, Level4Active>>;

void test_4_level_deep_history_runtime() {
    std::cout << "[TEST] Running test_4_level_deep_history_runtime...\n";

    fsm::fsm<DeepHistoryTable, fsm::no_context, Standby> sm;
    assert(sm.is_in_state<Standby>());

    // Start -> Level4Active
    assert(sm.dispatch(StartCmd{}));
    assert(sm.is_in_state<Level4Active>());

    // Navigate to Level4Calibrating
    assert(sm.dispatch(CalibrateCmd{}));
    assert(sm.is_in_state<Level4Calibrating>());

    // Emergency Interrupt (exiting Operating records history)
    assert(sm.dispatch(EStopEvent{}));
    assert(sm.is_in_state<Emergency>());
    assert(sm.get_history("Operating") == "Level4Calibrating");

    // Resume with Deep History -> must restore Level4Calibrating!
    assert(sm.dispatch(ResumeDeepCmd{}));
    assert(sm.is_in_state<Level4Calibrating>());

    std::cout << "[PASS] test_4_level_deep_history_runtime successfully restored 4th level substate.\n";
}

}  // namespace

int main() {
    std::cout << "========================================\n"
              << "   RUNNING MULTI-LEVEL HISTORY TESTS    \n"
              << "========================================\n";

    test_4_level_deep_history_ast();
    test_4_level_deep_history_runtime();

    std::cout << "========================================\n"
              << "   MULTI-LEVEL HISTORY TESTS PASSED!    \n"
              << "========================================\n";
    return 0;
}
