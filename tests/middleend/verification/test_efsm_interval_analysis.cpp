/**
 * @file test_efsm_interval_analysis.cpp
 * @brief Unit tests for EFSMIntervalAnalyzer abstract interpretation and port interval contracts.
 */

#include <gtest/gtest.h>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/efsm_interval_analysis.hpp"

using namespace fsm::diagnostic;
using namespace fsm::middleend::analysis;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify EFSM interval analysis validates port domain bounds and detects contract violations.
 * @scenario Define InPort 'sensor_val' in [0, 100] and OutPort 'actuator_cmd' in [0, 200].
 * @expected Pass confirms contract adherence without reporting errors on compliant models.
 */
TEST(EfsmIntervalAnalysis, CompliantPortBounds_PassesWithoutErrors) {
    FsmIr model;
    model.name = "BoundedFSM";

    PortDefinition in_p("sensor_val", "float", PortDirection::In);
    in_p.min_value = 0.0;
    in_p.max_value = 100.0;
    model.ports.push_back(in_p);

    PortDefinition out_p("actuator_cmd", "float", PortDirection::Out);
    out_p.min_value = 0.0;
    out_p.max_value = 200.0;
    model.ports.push_back(out_p);

    DiagnosticEngine diag;
    EFSMIntervalAnalyzer interval_pass(model);
    auto findings = interval_pass.analyze(diag);
    EXPECT_FALSE(diag.has_errors());
}

/**
 * @brief Verify EFSM interval analyzer detects out-of-range assignments violating OutPort contracts.
 * @scenario Define OutPort 'heater_power' in range [0.0, 100.0] and assign value 150.0f.
 * @expected Analyzer emits W_PORT_RANGE_VIOLATION diagnostic flagging out-of-range assignment.
 */
TEST(EfsmIntervalAnalysis, OutOfRangePortAssignment_EmitsPortRangeViolation) {
    FsmIr model;
    model.name = "ThermostatFSM";
    model.initial_state = "Idle";
    model.add_state("Idle");
    model.add_state("Heating");

    PortDefinition out_p("heater_power", "float", PortDirection::Out);
    out_p.min_value = 0.0;
    out_p.max_value = 100.0;
    model.ports.push_back(out_p);

    TransitionEdge t("t1", "Idle", "Heating", SignalTrigger("EvStart"));
    ActionSignature act("OverheatAct", "OverheatAct");
    act.assignments.push_back({"heater_power", "150.0f"});
    t.transition_action = act;
    model.add_transition(t);

    DiagnosticEngine diag;
    EFSMIntervalAnalyzer analyzer(model);
    auto findings = analyzer.analyze(diag);

    bool found_port_violation = false;
    for (const auto& f : findings) {
        if (f.variable_name == "heater_power" && f.is_error) {
            found_port_violation = true;
            break;
        }
    }
    EXPECT_TRUE(found_port_violation);
}

/**
 * @brief Verify EFSM interval analyzer detects unsatisfiable guards over bounded InPorts.
 * @scenario Define InPort 'sensor_temp' in [-50.0, 50.0] with guard 'in.sensor_temp > 90.0f'.
 * @expected Analyzer identifies unsatisfiable guard condition and reports diagnostic finding.
 */
TEST(EfsmIntervalAnalysis, GuardOutsidePortDomain_EmitsUnsatisfiableDiagnostic) {
    FsmIr model;
    model.name = "SensorFSM";
    model.initial_state = "Active";
    model.add_state("Active");
    model.add_state("Alert");

    PortDefinition in_p("sensor_temp", "float", PortDirection::In);
    in_p.min_value = -50.0;
    in_p.max_value = 50.0;
    model.ports.push_back(in_p);

    GuardModel gm("OverheatGuard", "OverheatGuard", "in.sensor_temp > 90.0f", "in.sensor_temp > 90.0f");
    model.guards.push_back(gm);

    TransitionEdge t("t1", "Active", "Alert", AnonymousTrigger{});
    t.guard = "OverheatGuard";
    model.add_transition(t);

    DiagnosticEngine diag;
    EFSMIntervalAnalyzer analyzer(model);
    auto findings = analyzer.analyze(diag);

    bool found_unsat_guard = false;
    for (const auto& f : findings) {
        if (f.variable_name == "sensor_temp" && !f.is_error) {
            found_unsat_guard = true;
            break;
        }
    }
    EXPECT_TRUE(found_unsat_guard);
}

}  // namespace
