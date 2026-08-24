#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/frontend/plantuml_parser.hpp"
#include "fsm/fsm.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::codegen;

namespace {

TEST(DeepHistoryTest, FourLevelDeepHistoryAstAndCodegen) {
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
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err;

    // Check hierarchy depth
    ASSERT_NE(model.find_state("Operating"), nullptr);
    EXPECT_TRUE(model.find_state("Operating")->is_composite);
    ASSERT_NE(model.find_state("SubSystem"), nullptr);
    EXPECT_TRUE(model.find_state("SubSystem")->is_composite);
    EXPECT_EQ(model.find_state("SubSystem")->parent_state, "Operating");
    ASSERT_NE(model.find_state("Module"), nullptr);
    EXPECT_TRUE(model.find_state("Module")->is_composite);
    EXPECT_EQ(model.find_state("Module")->parent_state, "SubSystem");
    ASSERT_NE(model.find_state("Level4Active"), nullptr);
    EXPECT_EQ(model.find_state("Level4Active")->parent_state, "Module");
    ASSERT_NE(model.find_state("Level4Calibrating"), nullptr);
    EXPECT_EQ(model.find_state("Level4Calibrating")->parent_state, "Module");

    // Check deep history flag
    EXPECT_TRUE(model.find_state("Operating")->has_history);
    EXPECT_TRUE(model.find_state("Operating")->has_deep_history);

    // Generate C++ code and verify compilation syntax
    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    std::string code = CppGenerator::generate_header(model, opts);
    EXPECT_FALSE(code.empty());
    EXPECT_NE(code.find("fsm::history_is<Operating, Level4Calibrating>"), std::string::npos);
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

TEST(DeepHistoryTest, RuntimeExecutionRestoresDeepLeafState) {
    fsm::fsm<DeepHistoryTable, fsm::no_context, Standby> sm;
    EXPECT_TRUE(sm.is_in_state<Standby>());

    // Start -> Level4Active
    EXPECT_TRUE(sm.dispatch(StartCmd{}));
    EXPECT_TRUE(sm.is_in_state<Level4Active>());

    // Navigate to Level4Calibrating
    EXPECT_TRUE(sm.dispatch(CalibrateCmd{}));
    EXPECT_TRUE(sm.is_in_state<Level4Calibrating>());

    // Emergency Interrupt (exiting Operating records history)
    EXPECT_TRUE(sm.dispatch(EStopEvent{}));
    EXPECT_TRUE(sm.is_in_state<Emergency>());
    EXPECT_EQ(sm.get_history("Operating"), "Level4Calibrating");

    // Resume with Deep History -> must restore Level4Calibrating!
    EXPECT_TRUE(sm.dispatch(ResumeDeepCmd{}));
    EXPECT_TRUE(sm.is_in_state<Level4Calibrating>());
}

TEST(DeepHistoryTest, InitialEntryWithoutPriorHistoryFallsBackToDefault) {
    // If we transition to Emergency first, then ResumeDeepCmd without having entered Operating,
    // it falls back to the default initial leaf (Level4Active).
    fsm::fsm<DeepHistoryTable, fsm::no_context, Emergency> sm;
    EXPECT_TRUE(sm.is_in_state<Emergency>());
    EXPECT_EQ(sm.get_history("Operating"), "");

    EXPECT_TRUE(sm.dispatch(ResumeDeepCmd{}));
    EXPECT_TRUE(sm.is_in_state<Level4Active>());
}

}  // namespace
