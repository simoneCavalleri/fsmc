/**
 * @file test_mermaid_parser.cpp
 * @brief Unit test suite for the Mermaid stateDiagram-v2 parser and frontend dialect.
 */

#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "fsm/backend/diagram/mermaid_serializer.hpp"
#include "fsm/frontend/diagram/mermaid/mermaid_parser.hpp"
#include "fsm/middleend/pass_manager.hpp"

using namespace fsm::backend::diagram;
using namespace fsm::frontend::diagram;
using namespace fsm::frontend;
using namespace fsm::middleend;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify Mermaid syntax parsing, state aliases, guard/action extraction, and validation.
 * @scenario Parse Mermaid stateDiagram-v2 with aliases, transitions, guards [Guard], and actions / Action.
 * @expected FsmIr element counts and validation pass without errors.
 */
TEST(MermaidParser, BasicDiagram_ParsedIntoValidFsmIr) {
    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Idle
        state "Waiting for CAN" as WaitingForCan
        Idle --> WaitingForCan : CmdStart [CanStartGuard] / OnStartAction
        Idle --> Idle : CmdStop / OnStopAction
        WaitingForCan --> Running : CanOk [IsReady]
        Running --> Idle : CmdStop
    )";

    MermaidParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err.render_to_string();

    EXPECT_EQ(model.initial_state, "Idle");
    EXPECT_EQ(model.states.size(), 3u);
    EXPECT_EQ(model.signals.size(), 3u);
    EXPECT_EQ(model.guards.size(), 2u);
    EXPECT_EQ(model.actions.size(), 2u);
    EXPECT_EQ(model.transitions.size(), 4u);

    fsm::diagnostic::DiagnosticEngine diag;
    EXPECT_TRUE(fsm::middleend::PassManager::create_validation_pipeline().run(model, diag));
    EXPECT_FALSE(diag.has_errors());
}

/**
 * @brief Verify Mermaid comment stripping, note stripping, and composite state hierarchy.
 * @scenario Parse Mermaid diagram containing %% comments, notes, and nested composite states.
 * @expected Comments and notes removed, parent-child links formed, and initial substate assigned.
 */
TEST(MermaidParser, CommentsNotesAndHierarchy_ParsedCorrectly) {
    const std::string mmd = R"(
    stateDiagram-v2
        %% This is a top-level mermaid comment
        [*] --> SuperState
        
        state SuperState {
            [*] --> SubA
            SubA --> SubB : NextEvt / StepAction
            note right of SubB: This is a note
        }
        SuperState --> Finished : CompleteEvt
    )";

    MermaidParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err.render_to_string();

    EXPECT_EQ(model.initial_state, "SuperState");
    ASSERT_NE(model.find_state("SuperState"), nullptr);
    EXPECT_TRUE(model.find_state("SuperState")->is_composite);
    EXPECT_EQ(model.find_state("SuperState")->initial_sub_state, "SubA");

    ASSERT_NE(model.find_state("SubA"), nullptr);
    EXPECT_EQ(model.find_state("SubA")->parent_state, "SuperState");

    ASSERT_NE(model.find_state("SubB"), nullptr);
    EXPECT_EQ(model.find_state("SubB")->parent_state, "SuperState");
}

/**
 * @brief Verify Mermaid parser gracefully rejects empty and whitespace-only inputs.
 * @scenario Feed whitespace-only string to MermaidParser.
 * @expected Parser returns false with an informative error message.
 */
TEST(MermaidParser, EmptyInput_GracefullyRejected) {
    MermaidParser mmd_parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;

    EXPECT_FALSE(mmd_parser.parse("   \n\t ", model, err));
}

/**
 * @brief Verify Mermaid parsing of entryPoint, exitPoint, and transition priority.
 * @scenario Parse Mermaid diagram with state ep <<entryPoint>>, state xp <<exitPoint>>, and transition priority tags.
 * @expected IR captures StateKind::EntryPoint, StateKind::ExitPoint, and transition priorities.
 */
TEST(MermaidParser, EntryExitPointAndPriority_CapturedInIr) {
    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Idle
        state ep <<entryPoint>>
        state xp <<exitPoint>>
        Idle --> Active : (prio=4) EvStart [CanStart] / OnStart
        Active --> xp : (prio=1) EvFinish
    )";

    MermaidParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err.render_to_string();

    const auto* ep = model.find_state("ep");
    ASSERT_NE(ep, nullptr);
    EXPECT_EQ(ep->kind, StateKind::EntryPoint);

    const auto* xp = model.find_state("xp");
    ASSERT_NE(xp, nullptr);
    EXPECT_EQ(xp->kind, StateKind::ExitPoint);

    ASSERT_EQ(model.transitions.size(), 2u);
    EXPECT_EQ(model.transitions[0].priority, 4u);
    EXPECT_EQ(model.transitions[1].priority, 1u);
}

/**
 * @brief Verify Mermaid parsing of @fsm:port directives into FsmIr.
 * @scenario Parse Mermaid diagram containing %% @fsm:port directive.
 * @expected Inbound port created with declared numerical bounds.
 */
TEST(MermaidParser, PortDirectives_ParsedWithAttributesAndConstraints) {
    const std::string mmd = R"(
    stateDiagram-v2
        %% @fsm:port name=voltage_in type=float dir=in min=18.0 max=36.0
        [*] --> Standby
        Standby --> Active : EvPowerOn
    )";

    MermaidParser mmd_parser;
    FsmIr mmd_model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(mmd_parser.parse(mmd, mmd_model, err)) << err.render_to_string();

    ASSERT_EQ(mmd_model.ports.size(), 1u);
    const auto* v_in = mmd_model.find_port("voltage_in");
    ASSERT_NE(v_in, nullptr);
    EXPECT_TRUE(v_in->is_in());
    EXPECT_DOUBLE_EQ(v_in->min_value.value_or(0.0), 18.0);
    EXPECT_DOUBLE_EQ(v_in->max_value.value_or(0.0), 36.0);
}

/**
 * @brief Verify Mermaid parsing of nested square brackets in transition guards and atomic_guard_details preservation.
 * @scenario Parse Mermaid transition with array subscript in guard [buf[0] > 10 && ready].
 * @expected Full bracketed guard is extracted without truncation and atomic GuardModel entries preserve raw_expression.
 */
TEST(MermaidParser, NestedSquareBracketsInGuard_PreservesRawExpressionDetails) {
    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> StateA
        StateA --> StateB : EvGo [buf[0] > 10 && ready] / OnGo
    )";

    MermaidParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err.render_to_string();

    ASSERT_EQ(model.transitions.size(), 1u);
    EXPECT_EQ(model.transitions[0].event, "EvGo");
    ASSERT_TRUE(model.transitions[0].guard.has_value());
    EXPECT_NE(model.transitions[0].guard->find(" && "), std::string::npos);

    ASSERT_EQ(model.guards.size(), 2u);
    bool found_array_expr = false;
    bool found_ready_expr = false;
    for (const auto& g : model.guards) {
        if (g.raw_expression.has_value() && *g.raw_expression == "buf[0] > 10") {
            found_array_expr = true;
        }
        if (g.raw_expression.has_value() && *g.raw_expression == "ready") {
            found_ready_expr = true;
        }
    }
    EXPECT_TRUE(found_array_expr);
    EXPECT_TRUE(found_ready_expr);
}

/**
 * @brief Verify MermaidParser recognizes '--' orthogonal region separator inside composite states.
 * @scenario Mermaid stateDiagram-v2 with two regions inside Active separated by '--'.
 * @expected Active has StateKind::Parallel and 2 orthogonal_regions with correct initial states.
 */
TEST(MermaidParserTest, OrthogonalSeparatorCreatesParallelRegions) {
    const std::string mmd = R"mmd(
stateDiagram-v2
    [*] --> Active
    state Active {
        state Reg1 {
            [*] --> S1
            S1 --> S2 : EvA
        }
        --
        state Reg2 {
            [*] --> T1
            T1 --> T2 : EvB
        }
    }
)mmd";

    MermaidParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << err.render_to_string();

    const StateNode* active = model.find_state("Active");
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->kind, StateKind::Parallel);
    ASSERT_EQ(active->orthogonal_regions.size(), 2u);
    EXPECT_EQ(active->orthogonal_regions[0].id, "Reg1");
    EXPECT_EQ(active->orthogonal_regions[0].initial_state_id, "S1");
    EXPECT_EQ(active->orthogonal_regions[1].id, "Reg2");
    EXPECT_EQ(active->orthogonal_regions[1].initial_state_id, "T1");
}

/**
 * @brief Verify Mermaid states inside orthogonal regions keep pinned_parent when referenced by root Fork/Join
 * transitions.
 * @scenario Mermaid stateDiagram-v2 with Par { R1 { A1 -> A2 } -- R2 { B1 -> B2 } } and root A2 -> J / B2 -> J.
 * @expected A2 stays inside R1 and B2 stays inside R2 after normalize_hierarchy().
 */
TEST(MermaidParserTest, ForkJoinWithExternalTransitionsPreservesRegionHierarchy) {
    const std::string mmd = R"mmd(
stateDiagram-v2
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
)mmd";

    MermaidParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << err.render_to_string();

    const StateNode* a2 = model.find_state("A2");
    const StateNode* b2 = model.find_state("B2");
    ASSERT_NE(a2, nullptr);
    ASSERT_NE(b2, nullptr);
    EXPECT_EQ(a2->parent_state, "R1");
    EXPECT_EQ(b2->parent_state, "R2");
}

/**
 * @brief Verify synthesized assignment action names do not collide across entry, exit, and transitions.
 * @scenario Parse Mermaid where Ready entry, Ready exit, and two transitions all assign to variable x.
 * @expected All four ActionSignature entries have distinct names and retain their own assignment expressions.
 */
TEST(MermaidParser, DistinctAssignmentActionNames_AcrossLifecycleAndTransitions) {
    const std::string mmd = R"mmd(
stateDiagram-v2
    [*] --> Ready
    Ready : entry / x = 10
    Ready : exit / x += 1
    Ready --> Running : START [x >= 10] / x = x * 2
    Ready --> Running : FAST / x = 99
)mmd";

    MermaidParser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << err.render_to_string();

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
 * @brief Verify MermaidSerializer omits `[*] --> initial_sub_state` inside StateKind::Parallel blocks.
 * @scenario Parallel state 'Par' has 3 orthogonal regions (Reg1, Reg2, Reg3) and a non-empty initial_sub_state.
 * @expected Serialized Mermaid omits `[*] --> Reg3` inside Par and roundtrips all 3 orthogonal regions.
 */
TEST(MermaidSerializerTest, ParallelStateOmitsInitialSubstateAndRoundtripsThreeRegions) {
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

    const std::string mmd = MermaidSerializer::serialize(ir);
    EXPECT_EQ(mmd.find("[*] --> Reg3"), std::string::npos);

    MermaidParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, parsed, err)) << err.render_to_string();
    const auto* par_parsed = parsed.find_state("Par");
    ASSERT_NE(par_parsed, nullptr);
    EXPECT_EQ(par_parsed->kind, StateKind::Parallel);
    EXPECT_EQ(par_parsed->orthogonal_regions.size(), 3u);
}

/**
 * @brief Regression: Mermaid composite state headers with quoted display alias (`state "Label" as Comp {`)
 * preserve hierarchy and initial substate without overwriting root `initial_state`.
 * @scenario Parse `[*] --> Comp` with `state "Composite Label" as Comp { [*] --> Sub1 }`.
 * @expected `ir.initial_state == "Comp"`, `Comp->is_composite == true`, `Comp->initial_sub_state == "Sub1"`,
 * `Sub1->parent_state == "Comp"`.
 */
TEST(MermaidParser, CompositeStateWithAlias_PreservesHierarchyAndInitialStates) {
    const std::string mmd = R"mmd(stateDiagram-v2
[*] --> Comp
state "Composite Label" as Comp {
    [*] --> Sub1
    Sub1 --> Sub2 : Step
}
Comp --> Done : Finish
)mmd";

    MermaidParser parser;
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, ir, err)) << err.render_to_string();
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
 * @brief Regression: `MermaidSerializer` emits `%% @fsm:port` directives so `in`/`out` ports survive
 * Mermaid roundtrips.
 * @scenario Serialize an `FsmIr` with `in` and `out` ports to Mermaid, then re-parse with `MermaidParser`.
 * @expected Both ports (`temp_in`, `alarm_out`) are preserved with their types and directions.
 */
TEST(MermaidParser, Ports_RoundtripThroughDirectives) {
    FsmIr ir;
    ir.name = "PortMmd";
    ir.initial_state = "Idle";
    ir.add_state("Idle");
    ir.add_port(PortDefinition("temp_in", DataType::int32(), PortDirection::In));
    ir.add_port(PortDefinition("alarm_out", DataType::boolean(), PortDirection::Out));

    const std::string mmd = MermaidSerializer::serialize(ir);
    EXPECT_NE(mmd.find("%% @fsm:port name=temp_in"), std::string::npos);
    EXPECT_NE(mmd.find("%% @fsm:port name=alarm_out"), std::string::npos);

    MermaidParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, parsed, err)) << err.render_to_string();
    const auto* p_in = parsed.find_port("temp_in");
    const auto* p_out = parsed.find_port("alarm_out");
    ASSERT_NE(p_in, nullptr);
    ASSERT_NE(p_out, nullptr);
    EXPECT_TRUE(p_in->is_in());
    EXPECT_TRUE(p_out->is_out());
}

/**
 * @brief Regression: Mermaid standalone `[H]` / `[H*]` history target inside a composite state
 * resolves to the enclosing composite state instead of failing with "invalid source or target state".
 * @scenario Parse Mermaid composite state `Comp` with `Sub2 --> [H*] : Back`.
 * @expected Transition targets `Comp`, `Comp->has_history == true`, and `Comp->has_deep_history == true`.
 */
TEST(MermaidParser, StandaloneHistoryTargetInsideComposite_ResolvesToParent) {
    const std::string mmd = R"mmd(stateDiagram-v2
    [*] --> Comp
    state Comp {
        [*] --> Sub1
        Sub1 --> Sub2 : Next
        Sub2 --> [H*] : Back
    }
)mmd";

    MermaidParser parser;
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, ir, err)) << err.render_to_string();
    const auto* comp = ir.find_state("Comp");
    ASSERT_NE(comp, nullptr);
    EXPECT_TRUE(comp->has_history);
    EXPECT_TRUE(comp->has_deep_history);
    ASSERT_EQ(ir.transitions.size(), 2u);
    EXPECT_EQ(ir.transitions[1].target, "Comp");
    EXPECT_EQ(ir.transitions[1].event, "Back");
}

/**
 * @brief Regression: `MermaidParser` reconstructs `TimeTrigger` when parsing `after_<N>ms` /
 * `every_<N>ms` transition labels emitted by `MermaidSerializer`.
 * @scenario Parse Mermaid with `Busy --> Idle : after_100ms`.
 * @expected Transition has a `TimeTrigger` with `duration_in_ms() == 100`.
 */
TEST(MermaidParser, TimeTriggerEventLabel_PopulatesTimeTrigger) {
    const std::string mmd = R"mmd(stateDiagram-v2
    [*] --> Busy
    Busy --> Idle : after_100ms
)mmd";

    MermaidParser parser;
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, ir, err)) << err.render_to_string();
    ASSERT_EQ(ir.transitions.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(ir.transitions[0].trigger));
    EXPECT_EQ(std::get<TimeTrigger>(ir.transitions[0].trigger).duration_in_ms(), 100u);
}

/**
 * @brief Regression: `MermaidSerializer` emits `State : Event / Action` for
 * `TransitionEdgeKind::Internal` transitions so `MermaidParser` preserves `Internal` kind across roundtrips.
 * @scenario Serialize an internal transition `Busy --Ping--> Busy` to Mermaid and re-parse.
 * @expected Re-parsed transition has `kind == TransitionEdgeKind::Internal`.
 */
TEST(MermaidParser, InternalTransition_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "MmdIntRt";
    ir.initial_state = "Busy";
    ir.add_state("Busy");
    TransitionEdge t("Busy", "Busy", "Ping");
    t.kind = TransitionEdgeKind::Internal;
    ir.add_transition(t);

    const std::string mmd = MermaidSerializer::serialize(ir);
    EXPECT_NE(mmd.find("Busy : Ping"), std::string::npos);

    MermaidParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, parsed, err)) << err.render_to_string();
    ASSERT_EQ(parsed.transitions.size(), 1u);
    EXPECT_EQ(parsed.transitions[0].kind, TransitionEdgeKind::Internal);
}

/**
 * @brief Regression: `MermaidSerializer` emits `%% @fsm:state` directives for `traceability_reqs`
 * and `time_invariant` on both leaf and composite states so `MermaidParser` preserves them across roundtrips.
 * @scenario Serialize an FSM with leaf state `Idle` (`REQ_001`) and composite `Active` (`500[ms]`), then re-parse.
 * @expected Re-parsed `Idle` has `traceability_reqs == {"REQ_001"}` and `Active` has `time_invariant == "500[ms]"`.
 */
TEST(MermaidParser, StateReqsAndTimeInvariant_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "MmdMetaRt";
    ir.initial_state = "Idle";
    auto& idle = ir.add_state("Idle");
    idle.traceability_reqs.push_back("REQ_001");

    auto& active = ir.add_state("Active");
    active.is_composite = true;
    active.initial_sub_state = "Sub1";
    active.time_invariant = "500[ms]";
    ir.add_state("Sub1", "Active");

    const std::string mmd = MermaidSerializer::serialize(ir);
    MermaidParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, parsed, err)) << err.render_to_string();

    const auto* p_idle = parsed.find_state("Idle");
    ASSERT_NE(p_idle, nullptr);
    ASSERT_EQ(p_idle->traceability_reqs.size(), 1u);
    EXPECT_EQ(p_idle->traceability_reqs[0], "REQ_001");

    const auto* p_active = parsed.find_state("Active");
    ASSERT_NE(p_active, nullptr);
    ASSERT_TRUE(p_active->time_invariant.has_value());
    EXPECT_EQ(*p_active->time_invariant, "500[ms]");
}

/**
 * @brief Regression: `MermaidSerializer` emits `when(<expr>)` for `ChangeTrigger` transitions
 * so `MermaidParser` preserves `ChangeTrigger` across roundtrips.
 * @scenario Serialize a transition with `ChangeTrigger("count > 5")` to Mermaid and re-parse.
 * @expected Serialized Mermaid contains `when(count > 5)` and re-parsed transition holds `ChangeTrigger`.
 */
TEST(MermaidParser, ChangeTrigger_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "MmdChangeRt";
    ir.initial_state = "Wait";
    ir.add_state("Wait");
    ir.add_state("Done");
    ir.add_variable(VariableDefinition("count", DataType::int32(), "0"));
    TransitionEdge t("Wait", "Done", "");
    t.trigger = ChangeTrigger("count > 5", true);
    ir.add_transition(t);

    const std::string mmd = MermaidSerializer::serialize(ir);
    EXPECT_NE(mmd.find("Wait --> Done : when(count > 5)"), std::string::npos) << mmd;

    MermaidParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, parsed, err)) << err.render_to_string();
    ASSERT_EQ(parsed.transitions.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<ChangeTrigger>(parsed.transitions[0].trigger));
    EXPECT_EQ(std::get<ChangeTrigger>(parsed.transitions[0].trigger).raw_expression, "count > 5");
}

/**
 * @brief Regression: `MermaidSerializer` and `MermaidParser` preserve `TransitionEdgeKind::Local`
 * via `%% @fsm:local` directives.
 * @scenario Serialize a composite FSM with a local transition and an external transition to Mermaid and re-parse.
 * @expected Re-parsed local transition has `kind == TransitionEdgeKind::Local`; external remains `External`.
 */
TEST(MermaidParser, LocalTransition_PreservedAcrossSerializerRoundtrip) {
    FsmIr ir;
    ir.name = "MmdLocalRt";
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

    const std::string mmd = MermaidSerializer::serialize(ir);
    EXPECT_NE(mmd.find("%% @fsm:local"), std::string::npos) << mmd;

    MermaidParser parser;
    FsmIr parsed;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(mmd, parsed, err)) << err.render_to_string();
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
TEST(MermaidParserTest, CompositeHierarchy_PreservesPinnedParentOnOuterTransitions) {
    const std::string mmd = R"(
stateDiagram-v2
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
)";
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine diag;
    MermaidParser parser;
    ASSERT_TRUE(parser.parse(mmd, ir, diag)) << diag.render_to_string();
    ir.normalize_hierarchy();

    const auto* mach = ir.find_state("Machining");
    ASSERT_NE(mach, nullptr);
    EXPECT_EQ(mach->parent_state, "Operational");
}

/**
 * @brief Regression (BUG-36 & BUG-41): Mermaid parses `after(500ms)` / `every(100ms)` as `TimeTrigger`
 * and records `parse_warnings` for unrecognized `@fsm:...` directives.
 */
TEST(MermaidParserTest, TimeTriggerCallSyntaxAndUnknownDirectiveWarning) {
    const std::string mmd = R"(
stateDiagram-v2
    %% @fsm:bogus_dir x=1
    [*] --> Wait
    Wait --> Done : after(500ms)
    Done --> Done : every(100ms)
)";
    FsmIr ir;
    fsm::diagnostic::DiagnosticEngine diag;
    MermaidParser parser;
    ASSERT_TRUE(parser.parse(mmd, ir, diag)) << diag.render_to_string();
    ASSERT_EQ(ir.transitions.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(ir.transitions[0].trigger));
    EXPECT_EQ(std::get<TimeTrigger>(ir.transitions[0].trigger).duration_in_ms(), 500U);
    ASSERT_TRUE(std::holds_alternative<TimeTrigger>(ir.transitions[1].trigger));
    EXPECT_EQ(std::get<TimeTrigger>(ir.transitions[1].trigger).kind, TimeTriggerKind::Every);
    EXPECT_EQ(std::get<TimeTrigger>(ir.transitions[1].trigger).duration_in_ms(), 100U);
    ASSERT_FALSE(ir.parse_warnings.empty());
    EXPECT_NE(ir.parse_warnings[0].find("bogus_dir"), std::string::npos);
}

}  // namespace
