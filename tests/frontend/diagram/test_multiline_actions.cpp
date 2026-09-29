/**
 * @file test_multiline_actions.cpp
 * @brief Unit test suite verifying multiline and braced composite action block parsing
 *        in diagram frontends (PlantUML, Mermaid).
 */

#include <gtest/gtest.h>

#include <string>

#include "fsm/frontend/diagram/mermaid_parser.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::frontend::diagram;
using namespace fsm::ir;

namespace {

TEST(MultilineActions, PlantUml_MultilineEntryAndExitActionBracedAssignments) {
    const std::string puml = R"(
    @startuml
    state Active {
      entry / {
        out.alarm = true;
        reg.count = 0;
      }
      exit / {
        out.alarm = false;
      }
    }
    [*] --> Active
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err;

    auto* st = model.find_state("Active");
    ASSERT_NE(st, nullptr);

    // Verify entry actions
    ASSERT_EQ(st->entry_actions.size(), 1u);
    const auto& entry_act = st->entry_actions[0];
    ASSERT_EQ(entry_act.assignments.size(), 2u);

    EXPECT_EQ(entry_act.assignments[0].target.name, "alarm");
    EXPECT_EQ(entry_act.assignments[0].target.scope, LValueScope::OutPort);
    EXPECT_EQ(entry_act.assignments[0].expression, "true");

    EXPECT_EQ(entry_act.assignments[1].target.name, "count");
    EXPECT_EQ(entry_act.assignments[1].target.scope, LValueScope::Register);
    EXPECT_EQ(entry_act.assignments[1].expression, "0");

    // Verify exit actions
    ASSERT_EQ(st->exit_actions.size(), 1u);
    const auto& exit_act = st->exit_actions[0];
    ASSERT_EQ(exit_act.assignments.size(), 1u);
    EXPECT_EQ(exit_act.assignments[0].target.name, "alarm");
    EXPECT_EQ(exit_act.assignments[0].target.scope, LValueScope::OutPort);
    EXPECT_EQ(exit_act.assignments[0].expression, "false");
}

TEST(MultilineActions, PlantUml_TransitionWithCompositeActions) {
    const std::string puml = R"(
    @startuml
    [*] --> Idle
    Idle --> Working : StartTask / { reg.counter += 1; act1(); }
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err;

    ASSERT_EQ(model.transitions.size(), 1u);
    auto it = std::find_if(model.transitions.begin(), model.transitions.end(),
                           [](const auto& t) { return t.source == "Idle" && t.target == "Working"; });
    ASSERT_NE(it, model.transitions.end());
    const auto& tx = *it;

    ASSERT_TRUE(tx.transition_action.has_value());
    const auto& act = *tx.transition_action;

    ASSERT_EQ(act.assignments.size(), 1u);
    EXPECT_EQ(act.assignments[0].target.name, "counter");
    EXPECT_EQ(act.assignments[0].target.scope, LValueScope::Register);
    EXPECT_EQ(act.assignments[0].expression, "1");
    EXPECT_EQ(act.assignments[0].op, AssignmentOp::AddAssign);

    ASSERT_EQ(act.instructions.size(), 1u);
    EXPECT_EQ(act.instructions[0].kind, ActionOpKind::ActionCall);
    const auto* call = std::get_if<ActionCallOp>(&act.instructions[0].op);
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->function_name, "act1");
}

TEST(MultilineActions, PlantUml_InlineSingleLineCompositeAction) {
    const std::string puml = R"(
    @startuml
    Active : entry / { out.status = 1; reg.mode = 2; }
    [*] --> Active
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err;

    auto* st = model.find_state("Active");
    ASSERT_NE(st, nullptr);

    ASSERT_EQ(st->entry_actions.size(), 1u);
    EXPECT_EQ(st->entry_actions[0].assignments.size(), 2u);
    EXPECT_EQ(st->entry_actions[0].assignments[0].target.name, "status");
    EXPECT_EQ(st->entry_actions[0].assignments[1].target.name, "mode");
}

TEST(MultilineActions, Mermaid_MultilineEntryAction) {
    const std::string mermaid_diagram = R"(
    stateDiagram-v2
    [*] --> Active
    state Active {
      Active : entry / {
        out.alarm = true;
        reg.count = 0;
      }
    }
    )";

    MermaidParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(mermaid_diagram, model, err)) << "Error: " << err;

    auto* st = model.find_state("Active");
    ASSERT_NE(st, nullptr);

    ASSERT_EQ(st->entry_actions.size(), 1u);
    const auto& act = st->entry_actions[0];
    ASSERT_EQ(act.assignments.size(), 2u);
    EXPECT_EQ(act.assignments[0].target.name, "alarm");
    EXPECT_EQ(act.assignments[0].target.scope, LValueScope::OutPort);
    EXPECT_EQ(act.assignments[1].target.name, "count");
    EXPECT_EQ(act.assignments[1].target.scope, LValueScope::Register);
}

TEST(MultilineActions, Mermaid_TransitionWithCompositeAction) {
    const std::string mermaid_diagram = R"(
    stateDiagram-v2
    [*] --> Idle
    Idle --> Working : StartTask / { reg.counter += 1; act1(); }
    )";

    MermaidParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(mermaid_diagram, model, err)) << "Error: " << err;

    auto it = std::find_if(model.transitions.begin(), model.transitions.end(),
                           [](const auto& t) { return t.source == "Idle" && t.target == "Working"; });
    ASSERT_NE(it, model.transitions.end());
    const auto& tx = *it;

    ASSERT_TRUE(tx.transition_action.has_value());
    const auto& act = *tx.transition_action;

    ASSERT_EQ(act.assignments.size(), 1u);
    EXPECT_EQ(act.assignments[0].target.name, "counter");
    EXPECT_EQ(act.assignments[0].op, AssignmentOp::AddAssign);

    ASSERT_EQ(act.instructions.size(), 1u);
    const auto* call = std::get_if<ActionCallOp>(&act.instructions[0].op);
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->function_name, "act1");
}

TEST(MultilineActions, Mermaid_StateDescriptionBracedAction) {
    const std::string mermaid_diagram = R"(
    stateDiagram-v2
    state "Active State<br/>entry / { out.alarm = true; reg.count = 0; }" as Active
    [*] --> Active
    )";

    MermaidParser parser;
    FsmIr model;
    std::string err;
    ASSERT_TRUE(parser.parse(mermaid_diagram, model, err)) << "Error: " << err;

    auto* st = model.find_state("Active");
    ASSERT_NE(st, nullptr);

    ASSERT_EQ(st->entry_actions.size(), 1u);
    EXPECT_EQ(st->entry_actions[0].assignments.size(), 2u);
    EXPECT_EQ(st->entry_actions[0].assignments[0].target.name, "alarm");
    EXPECT_EQ(st->entry_actions[0].assignments[1].target.name, "count");
}

} // namespace
