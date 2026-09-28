/**
 * @file test_guard_satisfiability.cpp
 * @brief Unit tests for GuardSatisfiabilityPass interval analysis, dead guards, and mutual exclusivity.
 */

#include <gtest/gtest.h>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/efsm_interval_analysis.hpp"
#include "fsm/middleend/analysis/guard_satisfiability_pass.hpp"

using namespace fsm::diagnostic;
using namespace fsm::middleend;
using namespace fsm::middleend::analysis;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify that provably disjoint numeric guard intervals emit no ambiguity warnings.
 * @scenario Two transitions from 'Idle' on 'Tick' with disjoint guards 'x > 50' and 'x <= 30'.
 * @expected Pass validates mutual exclusivity and produces zero warnings.
 */
TEST(GuardSatisfiability, DisjointNumericGuardIntervals_EmitsNoWarnings) {
    FsmIr ir;
    ir.states.push_back(StateNode{"Idle"});
    ir.states.push_back(StateNode{"Active"});
    ir.states.push_back(StateNode{"Off"});

    TransitionEdge t1;
    t1.source = "Idle";
    t1.target = "Active";
    t1.event = "Tick";
    t1.guard = "x > 50";
    t1.priority = 1;

    TransitionEdge t2;
    t2.source = "Idle";
    t2.target = "Off";
    t2.event = "Tick";
    t2.guard = "x <= 30";
    t2.priority = 1;

    ir.transitions.push_back(t1);
    ir.transitions.push_back(t2);

    DiagnosticEngine diag;
    GuardSatisfiabilityPass pass;
    EXPECT_TRUE(pass.run(ir, diag));
    EXPECT_FALSE(diag.has_warnings());
}

/**
 * @brief Verify that overlapping guard intervals on the same event and priority emit warning W0301.
 * @scenario Two transitions from 'Idle' on 'Tick' with overlapping guards 'x > 10' and 'x > 20' sharing priority 1.
 * @expected Pass detects potential non-deterministic race and emits diagnostic warning W0301.
 */
TEST(GuardSatisfiability, OverlappingGuardsIdenticalPriority_EmitsAmbiguityWarning) {
    FsmIr ir;
    ir.states.push_back(StateNode{"Idle"});
    ir.states.push_back(StateNode{"Active"});
    ir.states.push_back(StateNode{"Pending"});

    TransitionEdge t1;
    t1.source = "Idle";
    t1.target = "Active";
    t1.event = "Tick";
    t1.guard = "x > 10";
    t1.priority = 1;

    TransitionEdge t2;
    t2.source = "Idle";
    t2.target = "Pending";
    t2.event = "Tick";
    t2.guard = "x > 20";
    t2.priority = 1;

    ir.transitions.push_back(t1);
    ir.transitions.push_back(t2);

    DiagnosticEngine diag;
    GuardSatisfiabilityPass pass;
    EXPECT_TRUE(pass.run(ir, diag));
    EXPECT_TRUE(diag.has_warnings());

    bool found_w0301 = false;
    for (const auto& d : diag.get_diagnostics()) {
        if (d.code == "W0301") {
            found_w0301 = true;
            break;
        }
    }
    EXPECT_TRUE(found_w0301);
}

/**
 * @brief Verify that contradictory guard conditions emit dead guard warning W0302.
 * @scenario Transition configured with guard 'x > 100 && x < 50' whose interval intersection is empty.
 * @expected Pass identifies unsatisfiable guard and emits dead guard warning W0302.
 */
TEST(GuardSatisfiability, ContradictoryIntervalGuards_EmitsDeadGuardWarning) {
    FsmIr ir;
    ir.states.push_back(StateNode{"Idle"});
    ir.states.push_back(StateNode{"Active"});

    TransitionEdge t1;
    t1.source = "Idle";
    t1.target = "Active";
    t1.event = "Tick";
    t1.guard = "x > 100 && x < 50";
    t1.priority = 1;

    ir.transitions.push_back(t1);

    DiagnosticEngine diag;
    GuardSatisfiabilityPass pass;
    EXPECT_TRUE(pass.run(ir, diag));
    EXPECT_TRUE(diag.has_warnings());

    bool found_w0302 = false;
    for (const auto& d : diag.get_diagnostics()) {
        if (d.code == "W0302") {
            found_w0302 = true;
            break;
        }
    }
    EXPECT_TRUE(found_w0302);
}

/**
 * @brief Verify that overlapping guards with differentiated transition priorities do not emit ambiguity warnings.
 * @scenario Overlapping guards 'x > 10' and 'x > 20' differentiated by priorities 1 and 2.
 * @expected Pass confirms deterministic evaluation order and produces zero warnings.
 */
TEST(GuardSatisfiability, OverlappingGuardsDifferentiatedPriority_AvoidsAmbiguityWarning) {
    FsmIr ir;
    ir.states.push_back(StateNode{"Idle"});
    ir.states.push_back(StateNode{"Active"});
    ir.states.push_back(StateNode{"Pending"});

    TransitionEdge t1;
    t1.source = "Idle";
    t1.target = "Active";
    t1.event = "Tick";
    t1.guard = "x > 10";
    t1.priority = 1;

    TransitionEdge t2;
    t2.source = "Idle";
    t2.target = "Pending";
    t2.event = "Tick";
    t2.guard = "x > 20";
    t2.priority = 2;  // Distinct priority ensures deterministic resolution

    ir.transitions.push_back(t1);
    ir.transitions.push_back(t2);

    DiagnosticEngine diag;
    GuardSatisfiabilityPass pass;
    EXPECT_TRUE(pass.run(ir, diag));
    EXPECT_FALSE(diag.has_warnings());
}

/**
 * @brief Verify that complementary boolean guards are recognized as mutually exclusive.
 * @scenario Two transitions on event 'Toggle' with boolean guards 'enabled == true' and 'enabled == false'.
 * @expected Pass recognizes complementary partitioning and produces zero ambiguity warnings.
 */
TEST(GuardSatisfiability, ComplementaryBooleanGuards_RecognizedAsMutuallyExclusive) {
    FsmIr ir;
    ir.states.push_back(StateNode{"Idle"});
    ir.states.push_back(StateNode{"Active"});
    ir.states.push_back(StateNode{"Off"});

    TransitionEdge t1;
    t1.source = "Idle";
    t1.target = "Active";
    t1.event = "Toggle";
    t1.guard = "enabled == true";
    t1.priority = 1;

    TransitionEdge t2;
    t2.source = "Idle";
    t2.target = "Off";
    t2.event = "Toggle";
    t2.guard = "enabled == false";
    t2.priority = 1;

    ir.transitions.push_back(t1);
    ir.transitions.push_back(t2);

    DiagnosticEngine diag;
    GuardSatisfiabilityPass pass;
    EXPECT_TRUE(pass.run(ir, diag));
    EXPECT_FALSE(diag.has_warnings());
}

/**
 * @brief Verify zero-allocation parse_guard_domain with qualifiers and numeric formats.
 * @scenario Parse guard domain expressions with port/register qualifiers ('in.temp >= 75.5', 'reg.count < 100').
 * @expected Interval boundaries are parsed accurately without throwing exceptions.
 */
TEST(GuardSatisfiability, QualifiedVariableDomainParsing_ExtractsExactIntervalBoundaries) {
    auto ival1 = EFSMIntervalAnalyzer::parse_guard_domain("in.temp >= 75.5", "temp");
    ASSERT_TRUE(ival1.has_value());
    EXPECT_DOUBLE_EQ(ival1->lo, 75.5);

    auto ival2 = EFSMIntervalAnalyzer::parse_guard_domain("reg.count < 100", "count");
    ASSERT_TRUE(ival2.has_value());
    EXPECT_LT(ival2->hi, 100.0);

    auto ival3 = EFSMIntervalAnalyzer::parse_guard_domain("state_val == 42", "state_val");
    ASSERT_TRUE(ival3.has_value());
    EXPECT_DOUBLE_EQ(ival3->lo, 42.0);
    EXPECT_DOUBLE_EQ(ival3->hi, 42.0);
}

}  // namespace
