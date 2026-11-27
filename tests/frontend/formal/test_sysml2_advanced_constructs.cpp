/**
 * @file test_sysml2_advanced_constructs.cpp
 * @brief Unit test suite for SysML v2 advanced constructs (nested/qualified packages,
 *        binding connectors, fork and join composite transitions).
 */

#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/formal/sysml2_serializer.hpp"
#include "fsm/frontend/formal/sysml2/sysml2_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/fsm_ir_deserializer.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"
#include "fsm/middleend/pass_manager.hpp"

using namespace fsm::backend::formal;
using namespace fsm::frontend::formal;
using namespace fsm::ir;

namespace {

TEST(Sysml2AdvancedConstructs, QualifiedPackage_ParsedIntoModelPackage) {
    const std::string sysml_text = R"(
    package Aerospace::Navigation {
        state def Autopilot {
            entry; then Idle;
            state Idle;
        }
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err.render_to_string();

    EXPECT_EQ(model.package, "Aerospace::Navigation");
    EXPECT_EQ(model.name, "Autopilot");
    EXPECT_EQ(model.initial_state, "Idle");
}

TEST(Sysml2AdvancedConstructs, NestedPackage_ConcatenatesHierarchy) {
    const std::string sysml_text = R"(
    package Spacecraft {
        package Propulsion {
            package ThrustController {
                state def MainEngine {
                    entry; then Standby;
                    state Standby;
                }
            }
        }
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err.render_to_string();

    EXPECT_EQ(model.package, "Spacecraft::Propulsion::ThrustController");
    EXPECT_EQ(model.name, "MainEngine");
    EXPECT_EQ(model.initial_state, "Standby");
}

TEST(Sysml2AdvancedConstructs, BindingConnectors_PopulatesBindingsAndPortBoundTo) {
    const std::string sysml_text = R"(
    package Telemetry {
        state def SensorHub {
            in port raw_temp : Real;
            out port filtered_temp : Real;

            bind raw_temp = adc.channel0;
            connect filtered_temp to bus.tx_temp;

            entry; then Running;
            state Running;
        }
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err.render_to_string();

    EXPECT_EQ(model.package, "Telemetry");
    EXPECT_EQ(model.name, "SensorHub");

    // Check bindings on the FsmIr model
    ASSERT_EQ(model.bindings.size(), 2u);
    EXPECT_EQ(model.bindings[0].destination, "raw_temp");
    EXPECT_EQ(model.bindings[0].source, "adc.channel0");

    EXPECT_EQ(model.bindings[1].destination, "filtered_temp");
    EXPECT_EQ(model.bindings[1].source, "bus.tx_temp");

    // Check bound_to property propagated to PortDefinition
    ASSERT_EQ(model.ports.size(), 2u);
    auto it_in =
        std::find_if(model.ports.begin(), model.ports.end(), [](const auto& p) { return p.name == "raw_temp"; });
    ASSERT_NE(it_in, model.ports.end());
    EXPECT_EQ(it_in->bound_to, "adc.channel0");

    auto it_out =
        std::find_if(model.ports.begin(), model.ports.end(), [](const auto& p) { return p.name == "filtered_temp"; });
    ASSERT_NE(it_out, model.ports.end());
    EXPECT_EQ(it_out->bound_to, "bus.tx_temp");
}

TEST(Sysml2AdvancedConstructs, ForkCompositeTransition_ParsesMultipleTargets) {
    const std::string sysml_text = R"(
    state def ParallelExecution {
        entry; then Idle;
        state Idle;
        state TaskA;
        state TaskB;

        transition fork_tasks
            first Idle
            accept StartTrigger
            fork (TaskA, TaskB);
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err.render_to_string();

    EXPECT_EQ(model.name, "ParallelExecution");
    ASSERT_EQ(model.transitions.size(), 1u);

    const auto& tx = model.transitions[0];
    EXPECT_EQ(tx.source, "Idle");
    EXPECT_EQ(tx.event, "StartTrigger");

    std::vector<std::string> expected_targets = {"TaskA", "TaskB"};
    EXPECT_EQ(tx.target_ids, expected_targets);
    ASSERT_EQ(tx.multi_target_ids.size(), 2u);
    EXPECT_EQ(tx.target, "TaskA");
}

TEST(Sysml2AdvancedConstructs, JoinCompositeTransition_ParsesMultipleSources) {
    const std::string sysml_text = R"(
    state def ParallelExecution {
        entry; then TaskA;
        state TaskA;
        state TaskB;
        state Done;

        transition join_tasks
            join (TaskA, TaskB)
            accept CompleteTrigger
            then Done;
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err.render_to_string();

    EXPECT_EQ(model.name, "ParallelExecution");
    ASSERT_EQ(model.transitions.size(), 1u);

    const auto& tx = model.transitions[0];
    EXPECT_EQ(tx.target, "Done");
    EXPECT_EQ(tx.event, "CompleteTrigger");

    std::vector<std::string> expected_sources = {"TaskA", "TaskB"};
    EXPECT_EQ(tx.source_ids, expected_sources);
    ASSERT_EQ(tx.multi_source_ids.size(), 2u);
    EXPECT_EQ(tx.source, "TaskA");
}

/**
 * @brief Verify SysML v2 parallel states and lifecycle slash action syntax (entry / act; exit / act;)
 *        are parsed accurately and serialized losslessly without reachability anomalies.
 */
TEST(Sysml2AdvancedConstructs, ParallelStateAndSlashActions_RoundtrippedLosslessly) {
    const std::string sysml_text = R"(
    state def ConcurrentSystem {
        entry; then Standby;

        state Standby;

        state Shutdown {
            entry / OpenContactorsAction;
            exit / ResetRelaysAction;
        }

        parallel state Operational {
            state ThermalRegion {
                entry; then CoolingOff;
                state CoolingOff;
                state CoolingOn;
                transition from CoolingOff accept TempHigh then CoolingOn;
            }
            state PressureRegion {
                entry; then PressureNominal;
                state PressureNominal;
                state PressureVent;
                transition from PressureNominal accept Overpressure then PressureVent;
            }
        }

        transition from Standby accept StartCmd then Operational;
        transition from Operational accept EStopCmd then Shutdown;
    }
    )";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_text, model, err)) << "Error: " << err.render_to_string();

    // Verify Shutdown actions
    const auto* shutdown_st = model.find_state("Shutdown");
    ASSERT_NE(shutdown_st, nullptr);
    ASSERT_FALSE(shutdown_st->entry_actions.empty());
    EXPECT_EQ(shutdown_st->entry_actions[0].name, "OpenContactorsAction");
    ASSERT_FALSE(shutdown_st->exit_actions.empty());
    EXPECT_EQ(shutdown_st->exit_actions[0].name, "ResetRelaysAction");

    // Verify Operational parallel state
    const auto* op_st = model.find_state("Operational");
    ASSERT_NE(op_st, nullptr);
    EXPECT_EQ(op_st->kind, StateKind::Parallel);

    // Initial validation must pass
    fsm::diagnostic::DiagnosticEngine val1_diag;
    EXPECT_TRUE(fsm::middleend::PassManager::create_validation_pipeline().run(model, val1_diag));
    EXPECT_FALSE(val1_diag.has_errors());

    // Roundtrip serialize to SysML v2
    std::string exported_sysml = Sysml2Serializer::serialize(model);
    EXPECT_NE(exported_sysml.find("state Operational parallel"), std::string::npos);
    EXPECT_NE(exported_sysml.find("entry action OpenContactorsAction;"), std::string::npos);
    EXPECT_NE(exported_sysml.find("exit action ResetRelaysAction;"), std::string::npos);

    // Parse exported model back
    FsmIr roundtrip_model;
    ASSERT_TRUE(parser.parse(exported_sysml, roundtrip_model, err)) << "Parse back error: " << err.render_to_string();

    const auto* rt_op = roundtrip_model.find_state("Operational");
    ASSERT_NE(rt_op, nullptr);
    EXPECT_EQ(rt_op->kind, StateKind::Parallel);

    // Validating the roundtripped model must NOT produce reachability warnings on parallel regions!
    fsm::diagnostic::DiagnosticEngine val2_diag;
    EXPECT_TRUE(fsm::middleend::PassManager::create_validation_pipeline().run(roundtrip_model, val2_diag));
    EXPECT_FALSE(val2_diag.has_errors());

    bool has_reachability_warning = false;
    for (const auto& d : val2_diag.get_diagnostics()) {
        if (d.message.find("unreachable") != std::string::npos) {
            has_reachability_warning = true;
        }
    }
    EXPECT_FALSE(has_reachability_warning);
}

/**
 * @brief Regression (BUG-46): SysML v2 portless `send EvInternal();` lowers to `SignalEmitOp`
 * with `target_port == std::nullopt` for run-to-completion internal self-dispatch.
 */
TEST(Sysml2AdvancedConstructs, PortlessSendStatement_LowersToInternalSignalEmitOp) {
    const std::string sysml_src = R"(
state def InternalChain {
    entry; then S1;
    state S1;
    state S2 {
        entry action { send EvNext(); }
    }
    state S3;

    transition t1 first S1 accept EvStart then S2;
    transition t2 first S2 accept EvNext then S3;
}
)";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_src, model, err)) << "Error: " << err.render_to_string();

    const StateNode* s2 = model.find_state("S2");
    ASSERT_NE(s2, nullptr);
    ASSERT_FALSE(s2->entry_actions.empty());
    bool found_internal_emit = false;
    for (const auto& inst : s2->entry_actions[0].instructions) {
        if (const auto* se = std::get_if<SignalEmitOp>(&inst.op)) {
            if (se->signal_name == "EvNext" && !se->target_port.has_value()) {
                found_internal_emit = true;
            }
        }
    }
    EXPECT_TRUE(found_internal_emit);
}

/**
 * @brief Regression (BUG-56 & BUG-57): SysML v2 preserves imperative source order when `assign` and `send`
 * are interleaved, captures inline `do action { ... }` and `time invariant`, and widens negative `Integer`
 * variables to signed `int32_t`.
 */
TEST(Sysml2AdvancedConstructs, SequentialAssignSendDoActionTimeInvariantAndNegativeInteger) {
    const std::string sysml_src = R"(
package SeqPkg {
    signal def EvOut { attribute v : Integer; }
    state def SeqFsm {
        out port outp : Integer;
        attribute x : Integer = 0;
        attribute balance : Integer = -5;

        entry; then Active;
        state Active {
            time invariant stay <= 100 [ms];
            do action { assign x := x + 1; }
        }
        state Done;

        transition t1 first Active accept EvGo do action {
            assign x := 10;
            send EvOut(x) via outp;
            assign x := 20;
        } then Done;
    }
}
)";

    Sysml2Parser parser;
    FsmIr model;
    fsm::diagnostic::DiagnosticEngine err;
    ASSERT_TRUE(parser.parse(sysml_src, model, err)) << "Error: " << err.render_to_string();

    // Negative Integer widens to signed int32_t
    const auto* bal = model.find_variable("balance");
    ASSERT_NE(bal, nullptr);
    EXPECT_EQ(bal->type, DataType::int32());
    EXPECT_EQ(bal->initial_value, "-5");

    // time invariant & inline do action body captured on Active
    const auto* active = model.find_state("Active");
    ASSERT_NE(active, nullptr);
    ASSERT_TRUE(active->time_invariant.has_value());
    EXPECT_NE(active->time_invariant->raw_expression.find("100"), std::string::npos);
    ASSERT_TRUE(active->do_activity_action.has_value());
    ASSERT_EQ(active->do_activity_action->assignments.size(), 1u);
    EXPECT_EQ(active->do_activity_action->assignments[0].target.name, "x");

    // Interleaved assign -> send -> assign lowered in exact order inside instructions
    ASSERT_EQ(model.transitions.size(), 1u);
    ASSERT_TRUE(model.transitions[0].transition_action.has_value());
    const auto& act = *model.transitions[0].transition_action;
    EXPECT_TRUE(act.assignments.empty());
    ASSERT_EQ(act.instructions.size(), 3u);
    ASSERT_TRUE(std::holds_alternative<StoreOp>(act.instructions[0].op));
    EXPECT_EQ(std::get<StoreOp>(act.instructions[0].op).expression, "10");
    ASSERT_TRUE(std::holds_alternative<SignalEmitOp>(act.instructions[1].op));
    EXPECT_EQ(std::get<SignalEmitOp>(act.instructions[1].op).signal_name, "EvOut");
    ASSERT_TRUE(std::holds_alternative<StoreOp>(act.instructions[2].op));
    EXPECT_EQ(std::get<StoreOp>(act.instructions[2].op).expression, "20");

    // Regression (BUG-72): do_activity_action body survives both SysML v2 and IR JSON roundtrips
    const std::string sysml_rt = Sysml2Serializer::serialize(model);
    EXPECT_NE(sysml_rt.find("do action { assign x := x + 1; }"), std::string::npos) << sysml_rt;
    FsmIr model_from_sysml;
    fsm::diagnostic::DiagnosticEngine err2;
    ASSERT_TRUE(parser.parse(sysml_rt, model_from_sysml, err2)) << err2.render_to_string();
    const auto* active_s = model_from_sysml.find_state("Active");
    ASSERT_NE(active_s, nullptr);
    ASSERT_TRUE(active_s->do_activity_action.has_value());
    ASSERT_EQ(active_s->do_activity_action->assignments.size(), 1u);
    EXPECT_EQ(active_s->do_activity_action->assignments[0].target.name, "x");

    const std::string ir_json = FsmIrSerializer::serialize_json(model);
    FsmIr model_from_json;
    std::string json_err;
    ASSERT_TRUE(FsmIrDeserializer::deserialize_json(ir_json, model_from_json, json_err)) << json_err;
    const auto* active_j = model_from_json.find_state("Active");
    ASSERT_NE(active_j, nullptr);
    ASSERT_TRUE(active_j->do_activity_action.has_value());
    ASSERT_EQ(active_j->do_activity_action->assignments.size(), 1u);
    EXPECT_EQ(active_j->do_activity_action->assignments[0].target.name, "x");
}

}  // namespace
