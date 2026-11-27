/**
 * @file test_plantuml_parser.cpp
 * @brief Unit test suite for the PlantUML state diagram parser and frontend dialect.
 */

#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "fsm/backend/diagram/plantuml_serializer.hpp"
#include "fsm/frontend/diagram/plantuml/plantuml_parser.hpp"
#include "fsm/middleend/pass_manager.hpp"

using namespace fsm::backend::diagram;
using namespace fsm::frontend::diagram;
using namespace fsm::frontend;
using namespace fsm::middleend;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify basic PlantUML syntax parsing and model validation.
 * @scenario Parse PlantUML with transitions, guards, actions, and initial state pointer.
 * @expected FsmIr elements accurately populated and FsmValidator passes without errors.
 */
TEST(PlantUmlParser, BasicDiagram_ParsedIntoValidFsmIr) {
    const std::string puml = R"(
    @startuml
    [*] --> Standby
    Standby -> Processing : StartTask [HasWork] / InitTask
    Processing -> Standby : StopTask / Cleanup
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err.render_to_string();

    EXPECT_EQ(model.initial_state, "Standby");
    EXPECT_EQ(model.states.size(), 2u);
    EXPECT_EQ(model.signals.size(), 2u);
    EXPECT_EQ(model.guards.size(), 1u);
    EXPECT_EQ(model.actions.size(), 2u);
    EXPECT_EQ(model.transitions.size(), 2u);

    fsm::diagnostic::DiagnosticEngine diag;
    EXPECT_TRUE(fsm::middleend::PassManager::create_validation_pipeline().run(model, diag));
}

/**
 * @brief Verify PlantUML single-line and multi-line comment stripping and composite states.
 * @scenario Parse PlantUML containing single-line (' ...) and block (/ ... /') comments with nested states.
 * @expected Comments discarded, composite hierarchy parsed, and substate parent links established.
 */
TEST(PlantUmlParser, CommentsAndCompositeHierarchy_ParsedCorrectly) {
    const std::string puml = R"(
    @startuml
    ' Single line comment
    /' Multi-line
       block comment '/
    [*] --> Operational
    
    state Operational {
        [*] --> SelfCheck
        SelfCheck : Progress / Report
        SelfCheck --> Armed : CheckOk
    }
    
    Operational --> Standby : Disarm
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err.render_to_string();

    EXPECT_EQ(model.initial_state, "Operational");
    ASSERT_NE(model.find_state("Operational"), nullptr);
    EXPECT_TRUE(model.find_state("Operational")->is_composite);
    EXPECT_EQ(model.find_state("SelfCheck")->parent_state, "Operational");
}

/**
 * @brief Verify FsmValidator detects undefined transition target states in PlantUML models.
 * @scenario Construct FsmIr with transition to a non-existent state `UnknownTarget`.
 * @expected FsmValidator reports error diagnostics and marks model invalid.
 */
TEST(PlantUmlParser, UndefinedTargetState_DetectedByValidator) {
    FsmIr model;
    model.initial_state = "Idle";
    model.add_state("Idle");
    model.transitions.emplace_back("Idle", "UnknownTarget", "MyEvent", std::nullopt, std::nullopt, "");

    fsm::diagnostic::DiagnosticEngine diag;
    EXPECT_FALSE(fsm::middleend::PassManager::create_validation_pipeline().run(model, diag));
    EXPECT_TRUE(diag.has_errors());
}

/**
 * @brief Verify PlantUML parser gracefully rejects empty input.
 * @scenario Feed empty string to PlantUmlParser.
 * @expected Parser returns false with an informative error message.
 */
TEST(PlantUmlParser, EmptyInput_GracefullyRejected) {
    PlantUmlParser puml_parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;

    EXPECT_FALSE(puml_parser.parse("", model, err));
}

/**
 * @brief Verify PlantUML parsing of entryPoint, exitPoint, time invariants, and transition priorities.
 * @scenario Parse PlantUML containing entryPoint and exitPoint pseudostates, time invariant stay limit, and transition
 * priorities.
 * @expected IR captures StateKind::EntryPoint, StateKind::ExitPoint, time_invariant, and explicit priorities.
 */
TEST(PlantUmlParser, EntryExitPointPriorityAndInvariant_CapturedInIr) {
    const std::string puml = R"(
    @startuml
    [*] --> Idle
    state ep <<entryPoint>>
    state xp <<exitPoint>>
    state Active {
        Active : invariant stay <= 100ms
    }
    Idle --> Active : (prio=5) EvGo [GuardOk] / ActGo
    Active --> xp : (priority=2) EvDone
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << "Error: " << err.render_to_string();

    const auto* ep = model.find_state("ep");
    ASSERT_NE(ep, nullptr);
    EXPECT_EQ(ep->kind, StateKind::EntryPoint);

    const auto* xp = model.find_state("xp");
    ASSERT_NE(xp, nullptr);
    EXPECT_EQ(xp->kind, StateKind::ExitPoint);

    const auto* active = model.find_state("Active");
    ASSERT_NE(active, nullptr);
    ASSERT_TRUE(active->time_invariant.has_value());
    EXPECT_EQ(*active->time_invariant, "stay <= 100ms");

    ASSERT_EQ(model.transitions.size(), 2u);
    EXPECT_EQ(model.transitions[0].priority, 5u);
    EXPECT_EQ(model.transitions[1].priority, 2u);
}

/**
 * @brief Verify PlantUML parsing of @fsm:port inline directives into FsmIr ports.
 * @scenario Parse PlantUML containing @fsm:port comments with range constraints and physical units.
 * @expected Inbound and outbound ports added to FsmIr with bounds and units populated.
 */
TEST(PlantUmlParser, PortDirectives_ParsedWithAttributesAndConstraints) {
    const std::string puml = R"(
    @startuml
    ' @fsm:port name=sensor_temp type=float dir=in min=-40.0 max=125.0 constraint="self >= -40.0 and self <= 125.0" unit="[degC]"
    ' @fsm:port name=valve_cmd type=float dir=out min=0.0 max=100.0 constraint="self >= 0.0 and self <= 100.0"
    [*] --> Off
    Off --> On : EvStart
    @enduml
    )";

    PlantUmlParser puml_parser;
    FsmIr puml_model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(puml_parser.parse(puml, puml_model, err)) << err.render_to_string();

    ASSERT_EQ(puml_model.ports.size(), 2u);
    const auto* in_p = puml_model.find_port("sensor_temp");
    ASSERT_NE(in_p, nullptr);
    EXPECT_TRUE(in_p->is_in());
    EXPECT_DOUBLE_EQ(in_p->min_value.value_or(0.0), -40.0);
    EXPECT_DOUBLE_EQ(in_p->max_value.value_or(0.0), 125.0);

    const auto* out_p = puml_model.find_port("valve_cmd");
    ASSERT_NE(out_p, nullptr);
    EXPECT_TRUE(out_p->is_out());
    EXPECT_DOUBLE_EQ(out_p->min_value.value_or(0.0), 0.0);
    EXPECT_DOUBLE_EQ(out_p->max_value.value_or(0.0), 100.0);
}

/**
 * @brief Verify that division operators '/' inside guards are not mistaken for action delimiters.
 */
TEST(PlantUmlParser, GuardWithDivisionOperator_PreservedAndNotTreatedAsAction) {
    const std::string puml = R"(
    @startuml
    [*] --> Standby
    Standby --> Running : Start [speed / 2 > 10] / do_work
    Running --> Standby : Stop [count / 5 == 0]
    @enduml
    )";

    PlantUmlParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << err.render_to_string();

    ASSERT_EQ(model.transitions.size(), 2u);

    // Transition 1: Start [speed / 2 > 10] / do_work
    const auto& t1 = model.transitions[0];
    EXPECT_EQ(t1.source, "Standby");
    EXPECT_EQ(t1.target, "Running");
    EXPECT_EQ(t1.event, "Start");
    ASSERT_TRUE(t1.guard.has_value());
    EXPECT_NE(t1.guard->find("speed"), std::string::npos);
    ASSERT_TRUE(t1.transition_action.has_value());
    EXPECT_EQ(t1.transition_action->name, "do_work");

    // Transition 2: Stop [count / 5 == 0] (no action)
    const auto& t2 = model.transitions[1];
    EXPECT_EQ(t2.source, "Running");
    EXPECT_EQ(t2.target, "Standby");
    EXPECT_EQ(t2.event, "Stop");
    ASSERT_TRUE(t2.guard.has_value());
    EXPECT_NE(t2.guard->find("count"), std::string::npos);
    EXPECT_FALSE(t2.transition_action.has_value());
}

/**
 * @brief Verify PlantUML '--' orthogonal region separator populates StateKind::Parallel and orthogonal_regions.
 * @scenario Composite state Active with two child regions separated by '--'.
 * @expected Active has StateKind::Parallel and 2 orthogonal regions with correct initial states.
 */
TEST(PlantUmlParserTest, OrthogonalSeparatorCreatesParallelRegions) {
    const std::string puml = R"puml(
@startuml
[*] --> Active
state Active {
  state RegA {
    [*] --> A1
    A1 --> A2 : EvA
  }
  --
  state RegB {
    [*] --> B1
    B1 --> B2 : EvB
  }
}
@enduml
)puml";

    PlantUmlParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << err.render_to_string();

    const StateNode* active = model.find_state("Active");
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->kind, StateKind::Parallel);
    ASSERT_EQ(active->orthogonal_regions.size(), 2u);
    EXPECT_EQ(active->orthogonal_regions[0].id, "RegA");
    EXPECT_EQ(active->orthogonal_regions[0].initial_state_id, "A1");
    EXPECT_EQ(active->orthogonal_regions[1].id, "RegB");
    EXPECT_EQ(active->orthogonal_regions[1].initial_state_id, "B1");
}

/**
 * @brief Verify PlantUML states inside regions keep pinned_parent when referenced by root-level Fork/Join transitions.
 * @scenario Parallel state Par with R1 { A1 -> A2 } -- R2 { B1 -> B2 } and external A2 -> J / B2 -> J transitions.
 * @expected A2 stays inside R1 and B2 stays inside R2 after normalize_hierarchy().
 */
TEST(PlantUmlParserTest, ForkJoinWithExternalTransitionsPreservesRegionHierarchy) {
    const std::string puml = R"puml(
@startuml
[*] --> Init
state F <<fork>>
state J <<join>>
state Par {
  state R1 {
    [*] --> A1
    A1 --> A2 : EvA
  }
  --
  state R2 {
    [*] --> B1
    B1 --> B2 : EvB
  }
}
Init --> F : EvStart
F --> A1
F --> B1
A2 --> J : EvDone
B2 --> J : EvDone
J --> Finished
@enduml
)puml";

    PlantUmlParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << err.render_to_string();

    const StateNode* a2 = model.find_state("A2");
    const StateNode* b2 = model.find_state("B2");
    ASSERT_NE(a2, nullptr);
    ASSERT_NE(b2, nullptr);
    EXPECT_EQ(a2->parent_state, "R1");
    EXPECT_EQ(b2->parent_state, "R2");
}

/**
 * @brief Verify synthesized assignment action names do not collide across entry, exit, and transitions.
 * @scenario Parse PlantUML where Ready entry, Ready exit, and two transitions all assign to variable x.
 * @expected All four ActionSignature entries have distinct names and retain their own assignment expressions.
 */
TEST(PlantUmlParser, DistinctAssignmentActionNames_AcrossLifecycleAndTransitions) {
    const std::string puml = R"puml(
@startuml
[*] --> Ready
Ready : entry / x = 10
Ready : exit / x += 1
Ready --> Running : START [x >= 10] / x = x * 2
Ready --> Running : FAST / x = 99
@enduml
)puml";

    PlantUmlParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(puml, model, err)) << err.render_to_string();

    const StateNode* ready = model.find_state("Ready");
    ASSERT_NE(ready, nullptr);
    ASSERT_EQ(ready->entry_actions.size(), 1u);
    ASSERT_EQ(ready->exit_actions.size(), 1u);
    ASSERT_EQ(model.transitions.size(), 2u);
    ASSERT_TRUE(model.transitions[0].transition_action.has_value());
    ASSERT_TRUE(model.transitions[1].transition_action.has_value());

    const std::string n_entry = ready->entry_actions[0].name;
    const std::string n_exit = ready->exit_actions[0].name;
    const std::string n_t0 = model.transitions[0].transition_action->name;
    const std::string n_t1 = model.transitions[1].transition_action->name;

    EXPECT_NE(n_entry, n_exit);
    EXPECT_NE(n_entry, n_t0);
    EXPECT_NE(n_exit, n_t0);
    EXPECT_NE(n_t0, n_t1);

    EXPECT_EQ(ready->entry_actions[0].assignments[0].expression, "10");
    EXPECT_EQ(ready->exit_actions[0].assignments[0].expression, "1");
    EXPECT_EQ(model.transitions[0].transition_action->assignments[0].expression, "x * 2");
    EXPECT_EQ(model.transitions[1].transition_action->assignments[0].expression, "99");
}

/**
 * @brief Verify PlantUmlSerializer omits `[*] --> initial_sub_state` inside StateKind::Parallel blocks.
 * @scenario Parallel state 'Par' has 3 orthogonal regions (Reg1, Reg2, Reg3) and a non-empty initial_sub_state.
 * @expected Serialized PlantUML omits `[*] --> Reg3` inside Par and roundtrips all 3 orthogonal regions.
 */
TEST(PlantUmlSerializerTest, ParallelStateOmitsInitialSubstateAndRoundtripsThreeRegions) {
    FsmIr ir;
    ir.name = "Par3";
    ir.initial_state = "Par";

    auto& par = ir.add_state("Par");
    par.is_composite = true;
    par.kind = StateKind::Parallel;
    par.initial_sub_state = "Reg3";
    par.orthogonal_regions.emplace_back("Reg1", "Reg1", "A1");
    par.orthogonal_regions.emplace_back("Reg2", "Reg2", "B1");
    par.orthogonal_regions.emplace_back("Reg3", "Reg3", "C1");

    auto& r1 = ir.add_state("Reg1", "Par");
    r1.is_composite = true;
    r1.initial_sub_state = "A1";
    ir.add_state("A1", "Reg1");
    ir.add_state("A2", "Reg1");

    auto& r2 = ir.add_state("Reg2", "Par");
    r2.is_composite = true;
    r2.initial_sub_state = "B1";
    ir.add_state("B1", "Reg2");
    ir.add_state("B2", "Reg2");

    auto& r3 = ir.add_state("Reg3", "Par");
    r3.is_composite = true;
    r3.initial_sub_state = "C1";
    ir.add_state("C1", "Reg3");
    ir.add_state("C2", "Reg3");

    const std::string puml = PlantUmlSerializer::serialize(ir);
    EXPECT_EQ(puml.find("[*] --> Reg3"), std::string::npos);

    PlantUmlParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(puml, parsed, err)) << err.render_to_string();
    const auto* par_parsed = parsed.find_state("Par");
    ASSERT_NE(par_parsed, nullptr);
    EXPECT_EQ(par_parsed->kind, StateKind::Parallel);
    EXPECT_EQ(par_parsed->orthogonal_regions.size(), 3u);
}

/**
 * @brief Regression: PlantUML composite state headers with quoted display alias (`state "Label" as Comp {`)
 * preserve hierarchy and initial substate without overwriting root `initial_state`.
 * @scenario Parse `[*] --> Comp` with `state "Composite Label" as Comp { [*] --> Sub1 }`.
 * @expected `ir.initial_state == "Comp"`, `Comp->is_composite == true`, `Comp->initial_sub_state == "Sub1"`,
 * `Sub1->parent_state == "Comp"`.
 */
TEST(PlantUmlParser, CompositeStateWithAlias_PreservesHierarchyAndInitialStates) {
    const std::string puml = R"puml(@startuml
[*] --> Comp
state "Composite Label" as Comp {
    [*] --> Sub1
    Sub1 --> Sub2 : Step
}
Comp --> Done : Finish
@enduml
)puml";

    PlantUmlParser parser;
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(puml, ir, err)) << err.render_to_string();
    EXPECT_EQ(ir.initial_state, "Comp");
    const auto* comp = ir.find_state("Comp");
    ASSERT_NE(comp, nullptr);
    EXPECT_TRUE(comp->is_composite);
    EXPECT_EQ(comp->initial_sub_state, "Sub1");
    const auto* sub1 = ir.find_state("Sub1");
    ASSERT_NE(sub1, nullptr);
    EXPECT_EQ(sub1->parent_state, "Comp");
}

/**
 * @brief Regression: PlantUML standalone `[H]` / `[H*]` history target inside a composite state
 * resolves to the enclosing composite state instead of failing with "invalid source or target".
 * @scenario Parse PlantUML composite state `Comp` with `Sub2 --> [H] : Back`.
 * @expected Transition targets `Comp` with `TransitionEdgeKind::History` and `Comp->has_history == true`.
 */
TEST(PlantUmlParser, StandaloneHistoryTargetInsideComposite_ResolvesToParent) {
    const std::string puml = R"puml(@startuml
[*] --> Comp
state Comp {
    [*] --> Sub1
    Sub1 --> Sub2 : Next
    Sub2 --> [H] : Back
}
@enduml
)puml";

    PlantUmlParser parser;
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(puml, ir, err)) << err.render_to_string();
    const auto* comp = ir.find_state("Comp");
    ASSERT_NE(comp, nullptr);
    EXPECT_TRUE(comp->has_history);
    ASSERT_EQ(ir.transitions.size(), 2u);
    EXPECT_EQ(ir.transitions[1].target, "Comp");
    EXPECT_EQ(ir.transitions[1].event, "Back");
}

/**
 * @brief Regression: `PlantUmlParser` reconstructs `TimeTrigger` when parsing `after_<N>ms` /
 * `every_<N>ms` transition labels emitted by `PlantUmlSerializer`.
 * @scenario Parse PlantUML with `Busy --> Idle : after_100ms`.
 * @expected Transition has a `TimeTrigger` with `duration_in_ms() == 100`.
 */
TEST(PlantUmlParser, TimeTriggerEventLabel_PopulatesTimeTrigger) {
    const std::string puml = R"puml(@startuml
[*] --> Busy
Busy --> Idle : after_100ms
@enduml
)puml";

    PlantUmlParser parser;
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(puml, ir, err)) << err.render_to_string();
    ASSERT_EQ(ir.transitions.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(ir.transitions[0].trigger));
    EXPECT_EQ(std::get<TimeTrigger>(ir.transitions[0].trigger).duration_in_ms(), 100u);
}

/**
 * @brief Regression: `PlantUmlSerializer` emits `State : Event / Action` for
 * `TransitionEdgeKind::Internal` transitions so `PlantUmlParser` preserves `Internal` kind across roundtrips.
 * @scenario Serialize an internal transition `Busy --Ping--> Busy` to PlantUML and re-parse.
 * @expected Re-parsed transition has `kind == TransitionEdgeKind::Internal`.
 */
TEST(PlantUmlParser, InternalTransition_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "PumlIntRt";
    ir.initial_state = "Busy";
    ir.add_state("Busy");
    TransitionEdge t("Busy", "Busy", "Ping");
    t.kind = TransitionEdgeKind::Internal;
    ir.add_transition(t);

    const std::string puml = PlantUmlSerializer::serialize(ir);
    EXPECT_NE(puml.find("Busy : Ping"), std::string::npos);

    PlantUmlParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(puml, parsed, err)) << err.render_to_string();
    ASSERT_EQ(parsed.transitions.size(), 1u);
    EXPECT_EQ(parsed.transitions[0].kind, TransitionEdgeKind::Internal);
}

/**
 * @brief Regression: `PlantUmlSerializer` emits `when(<expr>)` for `ChangeTrigger` transitions
 * so `PlantUmlParser` preserves `ChangeTrigger` across roundtrips.
 * @scenario Serialize a transition with `ChangeTrigger("count > 5")` to PlantUML and re-parse.
 * @expected Serialized PlantUML contains `when(count > 5)` and re-parsed transition holds `ChangeTrigger`.
 */
TEST(PlantUmlParser, ChangeTrigger_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "PumlChangeRt";
    ir.initial_state = "Wait";
    ir.add_state("Wait");
    ir.add_state("Done");
    ir.add_variable(VariableDefinition("count", DataType::int32(), "0"));
    TransitionEdge t("Wait", "Done", "");
    t.trigger = ChangeTrigger("count > 5", true);
    ir.add_transition(t);

    const std::string puml = PlantUmlSerializer::serialize(ir);
    EXPECT_NE(puml.find("Wait --> Done : when(count > 5)"), std::string::npos) << puml;

    PlantUmlParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(puml, parsed, err)) << err.render_to_string();
    ASSERT_EQ(parsed.transitions.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<ChangeTrigger>(parsed.transitions[0].trigger));
    EXPECT_EQ(std::get<ChangeTrigger>(parsed.transitions[0].trigger).raw_expression, "count > 5");
}

/**
 * @brief Regression: `PlantUmlSerializer` and `PlantUmlParser` preserve `TransitionEdgeKind::Local`
 * via `' @fsm:local` directives.
 * @scenario Serialize a composite FSM with a local transition and an external transition to PlantUML and re-parse.
 * @expected Re-parsed local transition has `kind == TransitionEdgeKind::Local`; external remains `External`.
 */
TEST(PlantUmlParser, LocalTransition_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "PumlLocalRt";
    ir.initial_state = "Active";
    StateNode active("Active");
    active.is_composite = true;
    active.initial_sub_state = "SubA";
    ir.add_state(active);
    StateNode sub_a("SubA");
    sub_a.parent_state = "Active";
    ir.add_state(sub_a);
    StateNode sub_b("SubB");
    sub_b.parent_state = "Active";
    ir.add_state(sub_b);

    TransitionEdge loc("Active", "SubB", "Reset");
    loc.kind = TransitionEdgeKind::Local;
    ir.add_transition(loc);

    TransitionEdge ext("SubA", "SubB", "Step");
    ext.kind = TransitionEdgeKind::External;
    ir.add_transition(ext);

    const std::string puml = PlantUmlSerializer::serialize(ir);
    EXPECT_NE(puml.find("' @fsm:local"), std::string::npos) << puml;

    PlantUmlParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(puml, parsed, err)) << err.render_to_string();
    ASSERT_EQ(parsed.transitions.size(), 2u);

    bool found_local = false;
    bool found_external = false;
    for (const auto& t : parsed.transitions) {
        if (t.event == "Reset") {
            EXPECT_EQ(t.kind, TransitionEdgeKind::Local);
            found_local = true;
        } else if (t.event == "Step") {
            EXPECT_EQ(t.kind, TransitionEdgeKind::External);
            found_external = true;
        }
    }
    EXPECT_TRUE(found_local);
    EXPECT_TRUE(found_external);
}

/**
 * @brief Test Intent: Verify explicitly declared composite substates retain pinned parent_state across outer
 * cross-boundary transitions after hierarchy normalization.
 */
TEST(PlantUmlParserTest, CompositeHierarchy_PreservesPinnedParentOnOuterTransitions) {
    const std::string puml = R"(
@startuml
[*] --> Operational
state Operational {
    [*] --> Machining
    state Machining {
        [*] --> Roughing
        Roughing --> Finishing : Next
    }
    Machining --> Standby : Done
}
Operational --> EStop : EStopCmd
@enduml
)";
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine diag;
    PlantUmlParser parser;
    ASSERT_TRUE(parser.parse(puml, ir, diag)) << diag.render_to_string();
    ir.normalize_hierarchy();

    const auto* mach = ir.find_state("Machining");
    ASSERT_NE(mach, nullptr);
    EXPECT_EQ(mach->parent_state, "Operational");
    const auto* rough = ir.find_state("Roughing");
    ASSERT_NE(rough, nullptr);
    EXPECT_EQ(rough->parent_state, "Machining");
}

/**
 * @brief Test Intent: Verify PlantUML supports both OMG UML 2.5 `State : Event / defer` and `State : defer / Event`
 * without corrupting event names with leading underscores.
 */
TEST(PlantUmlParserTest, DeferredEvent_UmlStandardSlashDeferAndDeferSlashSyntax) {
    const std::string puml = R"(
@startuml
[*] --> S1
S1 : EvUml / defer
S1 : defer / EvSlash
S1 --> S2 : Go
@enduml
)";
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine diag;
    PlantUmlParser parser;
    ASSERT_TRUE(parser.parse(puml, ir, diag)) << diag.render_to_string();
    const auto* s1 = ir.find_state("S1");
    ASSERT_NE(s1, nullptr);
    EXPECT_EQ(s1->deferred_events, (std::vector<std::string>{"EvUml", "EvSlash"}));
}

/**
 * @brief Regression (BUG-36 & BUG-41): PlantUML parses `after(500ms)` / `every(100ms)` as `TimeTrigger`
 * and records `parse_warnings` for unrecognized `@fsm:...` directives.
 */
TEST(PlantUmlParserTest, TimeTriggerCallSyntaxAndUnknownDirectiveWarning) {
    const std::string puml = R"(
@startuml TimedWarn
' @fsm:typo_directive foo=bar
[*] --> Wait
Wait --> Done : after(500ms)
Done --> Done : every(100ms)
@enduml
)";
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine diag;
    PlantUmlParser parser;
    ASSERT_TRUE(parser.parse(puml, ir, diag)) << diag.render_to_string();
    ASSERT_EQ(ir.transitions.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(ir.transitions[0].trigger));
    EXPECT_EQ(std::get<TimeTrigger>(ir.transitions[0].trigger).duration_in_ms(), 500U);
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(ir.transitions[1].trigger));
    EXPECT_EQ(std::get<TimeTrigger>(ir.transitions[1].trigger).kind, TimeTriggerKind::Every);
    EXPECT_EQ(std::get<TimeTrigger>(ir.transitions[1].trigger).duration_in_ms(), 100U);
    ASSERT_FALSE(ir.parse_warnings.empty());
    EXPECT_NE(ir.parse_warnings[0].find("typo_directive"), std::string::npos);
}

/**
 * @brief Regression (BUG-47): PlantUML parses `state Login <<submachine:AuthSub>>` into `StateNode::submachine`.
 */
TEST(PlantUmlParserTest, SubmachineStereotype_PopulatesSubmachineRef) {
    const std::string puml = R"(
@startuml HostApp
[*] --> Idle
state Login <<submachine:AuthSub>>
Idle --> Login : EvStart
@enduml
)";
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine diag;
    PlantUmlParser parser;
    ASSERT_TRUE(parser.parse(puml, ir, diag)) << diag.render_to_string();
    const StateNode* login = ir.find_state("Login");
    ASSERT_NE(login, nullptr);
    ASSERT_TRUE(login->submachine.has_value());
    EXPECT_EQ(login->submachine->fsm_name, "AuthSub");
}

}  // namespace
