#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/frontend/mermaid_parser.hpp"
#include "fsm/frontend/plantuml_parser.hpp"
#include "fsm/fsm.hpp"
#include "fsm/middleend/fsm_validator.hpp"

using namespace fsm::codegen;

namespace {

TEST(HistoryTest, PlantUmlHistoryTargetParsing) {
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
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err;

    const auto* op_state = model.find_state("Operating");
    ASSERT_NE(op_state, nullptr);
    EXPECT_TRUE(op_state->has_history);

    bool found_history_transition = false;
    for (const auto& t : model.transitions) {
        if (t.source == "Paused" && t.target == "Operating" && t.target_is_history) {
            found_history_transition = true;
        }
    }
    EXPECT_TRUE(found_history_transition);
}

TEST(HistoryTest, MermaidDeepHistoryTargetParsing) {
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
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err;

    const auto* op_state = model.find_state("Operating");
    ASSERT_NE(op_state, nullptr);
    EXPECT_TRUE(op_state->has_deep_history);
}

TEST(HistoryTest, HistoryCodegenExpansion) {
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
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err;

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    std::string code = CppGenerator::generate_header(model, opts);

    // Verify parent is generated on substates
    EXPECT_NE(code.find("parent = \"Operating\""), std::string::npos);

    // Verify history rows are generated
    EXPECT_NE(code.find("fsm::history_is<Operating, Step1>"), std::string::npos);
    EXPECT_NE(code.find("fsm::history_is<Operating, Step2>"), std::string::npos);
}

// Runtime History Execution
struct Standby {
    static constexpr std::string_view name = "Standby";
};
struct Operating {
    static constexpr std::string_view name = "Operating";
};
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
struct Paused {
    static constexpr std::string_view name = "Paused";
};

struct Start {};
struct NextStep {};
struct Pause {};
struct Resume {};

using HistoryRuntimeTable = fsm::transition_table<
    fsm::row<Standby, Start, Step1>, fsm::row<Step1, NextStep, Step2>, fsm::row<Step2, NextStep, Step3>,
    // Propagated from Operating -> Paused
    fsm::row<Step1, Pause, Paused>, fsm::row<Step2, Pause, Paused>, fsm::row<Step3, Pause, Paused>,
    // History expansion on Resume
    fsm::row<Paused, Resume, Step1>::when<fsm::history_is<Operating, Step1>>,
    fsm::row<Paused, Resume, Step2>::when<fsm::history_is<Operating, Step2>>,
    fsm::row<Paused, Resume, Step3>::when<fsm::history_is<Operating, Step3>>,
    fsm::row<Paused, Resume, Step1>  // Fallback default
    >;

TEST(HistoryTest, RuntimeHistoryRestoresLastVisitedSubstate) {
    fsm::fsm<HistoryRuntimeTable, fsm::no_context, Standby> fsm;
    EXPECT_TRUE(fsm.is_in_state<Standby>());

    // 1. Standby -> Step1
    EXPECT_TRUE(fsm.dispatch(Start{}));
    EXPECT_TRUE(fsm.is_in_state<Step1>());

    // 2. Advance to Step2
    EXPECT_TRUE(fsm.dispatch(NextStep{}));
    EXPECT_TRUE(fsm.is_in_state<Step2>());

    // 3. Pause while in Step2 -> Paused (fsm records Operating -> Step2)
    EXPECT_TRUE(fsm.dispatch(Pause{}));
    EXPECT_TRUE(fsm.is_in_state<Paused>());
    EXPECT_EQ(fsm.get_history("Operating"), "Step2");

    // 4. Resume -> Restores Step2!
    EXPECT_TRUE(fsm.dispatch(Resume{}));
    EXPECT_TRUE(fsm.is_in_state<Step2>());

    // 5. Advance to Step3
    EXPECT_TRUE(fsm.dispatch(NextStep{}));
    EXPECT_TRUE(fsm.is_in_state<Step3>());

    // 6. Pause while in Step3 -> Paused (fsm records Operating -> Step3)
    EXPECT_TRUE(fsm.dispatch(Pause{}));
    EXPECT_TRUE(fsm.is_in_state<Paused>());
    EXPECT_EQ(fsm.get_history("Operating"), "Step3");

    // 7. Resume -> Restores Step3!
    EXPECT_TRUE(fsm.dispatch(Resume{}));
    EXPECT_TRUE(fsm.is_in_state<Step3>());
}

}  // namespace
