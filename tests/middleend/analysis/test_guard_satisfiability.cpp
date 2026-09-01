/**
 * @file test_guard_satisfiability.cpp
 * @brief Unit test suite for GuardSatisfiabilityPass (interval satisfiability, dead guards, and mutual exclusivity).
 *
 * Test Intent:
 * Verify that the GuardSatisfiabilityPass accurately detects:
 * - Provably disjoint numeric and boolean guard intervals (zero warnings).
 * - Potentially overlapping guards without priority differentiation (Warning W0301).
 * - Dead / unsatisfiable contradictory guard conditions (Warning W0302).
 * - Priority-disambiguated overlapping guards (zero warnings).
 */

#include <gtest/gtest.h>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/guard_satisfiability_pass.hpp"

using namespace fsm::codegen;

namespace {

/**
 * @brief Test Intent: Verify that provably disjoint numeric guard intervals emit no warnings.
 *
 * Scenario:
 * - Define two transitions on the same source state and event with guards 'x > 50' and 'x <= 30'.
 * - Run GuardSatisfiabilityPass and verify that diag.has_warnings() is false.
 */
TEST(GuardSatisfiabilityTest, MutuallyExclusiveNumericGuardsNoWarning) {
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
 * @brief Test Intent: Verify that overlapping guard intervals on the same event and priority emit warning W0301.
 *
 * Scenario:
 * - Define two transitions with guards 'x > 10' and 'x > 20' sharing identical priority 1.
 * - Run GuardSatisfiabilityPass and verify that diagnostic code W0301 is emitted.
 */
TEST(GuardSatisfiabilityTest, OverlappingGuardsEmitWarningW0301) {
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
 * @brief Test Intent: Verify that contradictory guard conditions (e.g. x > 100 && x < 50) emit dead guard warning
 * W0302.
 *
 * Scenario:
 * - Define a transition with guard 'x > 100 && x < 50' whose interval intersection is empty.
 * - Run GuardSatisfiabilityPass and verify that diagnostic code W0302 is emitted.
 */
TEST(GuardSatisfiabilityTest, DeadGuardEmitWarningW0302) {
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
 * @brief Test Intent: Verify that overlapping guards with differentiated transition priorities do not emit W0301.
 *
 * Scenario:
 * - Define two overlapping guards ('x > 10' and 'x > 20') with distinct priorities (priority 1 vs priority 2).
 * - Run GuardSatisfiabilityPass and verify that no ambiguity warning is emitted.
 */
TEST(GuardSatisfiabilityTest, DifferentPrioritiesAvoidW0301) {
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
 * @brief Test Intent: Verify that complementary boolean guards (enabled == true vs enabled == false) are recognized as
 * disjoint.
 *
 * Scenario:
 * - Define two transitions on event 'Toggle' with boolean guards 'enabled == true' and 'enabled == false'.
 * - Run GuardSatisfiabilityPass and verify that diag.has_warnings() is false.
 */
TEST(GuardSatisfiabilityTest, BooleanGuardsMutuallyExclusive) {
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

}  // namespace
