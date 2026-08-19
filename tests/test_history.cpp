#include <cassert>
#include <iostream>
#include <string>

#include "codegen/cpp_generator.hpp"
#include "codegen/fsm_validator.hpp"
#include "codegen/mermaid_parser.hpp"
#include "codegen/plantuml_parser.hpp"
#include "fsm/fsm.hpp"

using namespace fsm::codegen;

namespace {

void test_history_target_parsing_plantuml() {
    std::cout << "[TEST] Running test_history_target_parsing_plantuml...\n";

    const std::string puml = R"(
    @startuml
    [*] --> Standby

    state Operating {
        [*] --> Step1
        Step1 --> Step2 : NextStep
    }

    Operating --> Paused : Pause
    Paused --> Operating[H] : Resume
    @enduml
    )";

    PlantUmlParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(puml, model, err);

    assert(is_parsed);
    const auto* op_state = model.find_state("Operating");
    assert(op_state != nullptr);
    assert(op_state->has_history);

    bool found_history_transition = false;
    for (const auto& t : model.transitions) {
        if (t.source == "Paused" && t.target == "Operating" && t.target_is_history) {
            found_history_transition = true;
        }
    }
    assert(found_history_transition);

    std::cout << "[PASS] test_history_target_parsing_plantuml passed.\n";
}

void test_deep_history_target_parsing_mermaid() {
    std::cout << "[TEST] Running test_deep_history_target_parsing_mermaid...\n";

    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Standby
        state Operating {
            [*] --> StageA
        }
        Operating --> Suspended : Interrupt
        Suspended --> Operating[H*] : Recover
    )";

    MermaidParser parser;
    FsmModel model;
    std::string err;
    const bool is_parsed = parser.parse(mmd, model, err);

    assert(is_parsed);
    const auto* op_state = model.find_state("Operating");
    assert(op_state != nullptr);
    assert(op_state->has_deep_history);

    std::cout << "[PASS] test_deep_history_target_parsing_mermaid passed.\n";
}

void test_history_codegen_expansion() {
    std::cout << "[TEST] Running test_history_codegen_expansion...\n";

    const std::string puml = R"(
    @startuml
    [*] --> Standby

    state Operating {
        [*] --> Step1
        Step1 --> Step2 : NextStep
    }

    Standby --> Operating : Start
    Operating --> Paused : Pause
    Paused --> Operating[H] : Resume
    @enduml
    )";

    PlantUmlParser parser;
    FsmModel model;
    std::string err;
    assert(parser.parse(puml, model, err));

    CppGenerator generator;
    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    std::string code = generator.generate_header(model, opts);

    // Verify parent is generated on substates
    assert(code.find("parent = \"Operating\"") != std::string::npos);

    // Verify history rows are generated
    assert(code.find("fsm::history_is<Operating, Step1>") != std::string::npos);
    assert(code.find("fsm::history_is<Operating, Step2>") != std::string::npos);

    std::cout << "[PASS] test_history_codegen_expansion passed.\n";
}

// Runtime History Execution
struct Standby { static constexpr std::string_view name = "Standby"; };
struct Operating { static constexpr std::string_view name = "Operating"; };
struct Step1 {
    static constexpr std::string_view name = "Step1";
    static constexpr std::string_view parent = "Operating";
};
struct Step2 {
    static constexpr std::string_view name = "Step2";
    static constexpr std::string_view parent = "Operating";
};
struct Step3 {
    static constexpr std::string_view name = "Step3";
    static constexpr std::string_view parent = "Operating";
};
struct Paused { static constexpr std::string_view name = "Paused"; };

struct Start {};
struct NextStep {};
struct Pause {};
struct Resume {};

using HistoryRuntimeTable = fsm::transition_table<
    fsm::row<Standby, Start, Step1>,
    fsm::row<Step1, NextStep, Step2>,
    fsm::row<Step2, NextStep, Step3>,
    // Propagated from Operating -> Paused
    fsm::row<Step1, Pause, Paused>,
    fsm::row<Step2, Pause, Paused>,
    fsm::row<Step3, Pause, Paused>,
    // History expansion on Resume
    fsm::row<Paused, Resume, Step1>::when<fsm::history_is<Operating, Step1>>,
    fsm::row<Paused, Resume, Step2>::when<fsm::history_is<Operating, Step2>>,
    fsm::row<Paused, Resume, Step3>::when<fsm::history_is<Operating, Step3>>,
    fsm::row<Paused, Resume, Step1> // Fallback default
>;

void test_history_runtime_execution() {
    std::cout << "[TEST] Running test_history_runtime_execution...\n";

    fsm::fsm<HistoryRuntimeTable, fsm::no_context, Standby> fsm;
    assert(fsm.is_in_state<Standby>());

    // 1. Standby -> Step1
    assert(fsm.dispatch(Start{}));
    assert(fsm.is_in_state<Step1>());

    // 2. Advance to Step2
    assert(fsm.dispatch(NextStep{}));
    assert(fsm.is_in_state<Step2>());

    // 3. Pause while in Step2 -> Paused (fsm records Operating -> Step2)
    assert(fsm.dispatch(Pause{}));
    assert(fsm.is_in_state<Paused>());
    assert(fsm.get_history("Operating") == "Step2");

    // 4. Resume -> Restores Step2!
    assert(fsm.dispatch(Resume{}));
    assert(fsm.is_in_state<Step2>());

    // 5. Advance to Step3
    assert(fsm.dispatch(NextStep{}));
    assert(fsm.is_in_state<Step3>());

    // 6. Pause while in Step3 -> Paused (fsm records Operating -> Step3)
    assert(fsm.dispatch(Pause{}));
    assert(fsm.is_in_state<Paused>());
    assert(fsm.get_history("Operating") == "Step3");

    // 7. Resume -> Restores Step3!
    assert(fsm.dispatch(Resume{}));
    assert(fsm.is_in_state<Step3>());

    std::cout << "[PASS] test_history_runtime_execution passed.\n";
}

}  // namespace

int main() {
    std::cout << "========================================\n"
              << "     RUNNING UML 2.5 HISTORY TESTS      \n"
              << "========================================\n";

    test_history_target_parsing_plantuml();
    test_deep_history_target_parsing_mermaid();
    test_history_codegen_expansion();
    test_history_runtime_execution();

    std::cout << "========================================\n"
              << "     ALL HISTORY TESTS PASSED (4/4)!    \n"
              << "========================================\n";
    return 0;
}
