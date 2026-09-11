/**
 * @file test_mcdc_harness.cpp
 * @brief Unit test suite for MC/DC test harness generation and independence pair analysis.
 */

#include <gtest/gtest.h>

#include "fsm/backend/verification/mcdc_harness_generator.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::backend::verification;
using namespace fsm::backend;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify MCDC independence pair computation on conjunction A && B.
 * @scenario Compute MC/DC independence pairs for binary conjunction decision D = A && B.
 * @expected Conditions A and B each have valid independence pair proving singular influence on outcome.
 */
TEST(McdcHarness, ConjunctionExpression_IndependencePairsCalculated) {
    auto a = std::make_unique<VarExpr>("A");
    auto b = std::make_unique<VarExpr>("B");
    BinaryExpr expr(BinaryExpr::And, std::move(a), std::move(b));

    std::vector<std::string> conditions = {"A", "B"};
    auto pairs = McdcHarnessGenerator::compute_mcdc_pairs(expr, conditions);

    ASSERT_EQ(pairs.size(), 2u);

    // Check condition A pair
    const auto& pair_a = pairs[0];
    EXPECT_EQ(pair_a.condition_name, "A");
    EXPECT_TRUE(pair_a.vector_true.decision_outcome);
    EXPECT_FALSE(pair_a.vector_false.decision_outcome);
    EXPECT_TRUE(pair_a.vector_true.condition_values.at("A"));
    EXPECT_FALSE(pair_a.vector_false.condition_values.at("A"));
    EXPECT_EQ(pair_a.vector_true.condition_values.at("B"), pair_a.vector_false.condition_values.at("B"));

    // Check condition B pair
    const auto& pair_b = pairs[1];
    EXPECT_EQ(pair_b.condition_name, "B");
    EXPECT_TRUE(pair_b.vector_true.decision_outcome);
    EXPECT_FALSE(pair_b.vector_false.decision_outcome);
    EXPECT_TRUE(pair_b.vector_true.condition_values.at("B"));
    EXPECT_FALSE(pair_b.vector_false.condition_values.at("B"));
    EXPECT_EQ(pair_b.vector_true.condition_values.at("A"), pair_b.vector_false.condition_values.at("A"));
}

/**
 * @brief Verify McdcHarnessGenerator produces GoogleTest harness string for transition guards.
 * @scenario Create model with transition having composite guard "SensorOk && SpeedValid" and generate harness.
 * @expected Emitted code contains MC/DC test fixtures, evaluator functions, and automated test vectors.
 */
TEST(McdcHarness, TransitionGuard_GtestHarnessGenerated) {
    FsmIr model;
    model.name = "FlightNavFSM";
    model.add_state("Standby");
    model.add_state("Active");

    TransitionEdge t;
    t.source = "Standby";
    t.target = "Active";
    t.event = "EngageCmd";
    t.guard = "fsm::and_<SensorOk, SpeedValid>";
    model.add_transition(t);

    std::string harness = McdcHarnessGenerator::generate_gtest_harness(model);

    EXPECT_NE(harness.find("Automated MC/DC Safety Verification Test Harness"), std::string::npos);
    EXPECT_NE(harness.find("McdcSafetyHarness"), std::string::npos);
    EXPECT_NE(harness.find("SensorOk"), std::string::npos);
    EXPECT_NE(harness.find("SpeedValid"), std::string::npos);
    EXPECT_NE(harness.find("evaluate_decision"), std::string::npos);
}

/**
 * @brief Verify synthesis of driver harness for multi-condition avionics transition.
 * @scenario Ingest avionics transition guarded by GpsLock && CompassCalibrated.
 * @expected Synthesized harness includes condition variables and coverage evaluation macros.
 */
TEST(McdcHarness, AvionicsModel_DriverHarnessSynthesized) {
    FsmIr model;
    model.name = "AvionicsFSM";
    model.add_state("Idle");
    model.add_state("Navigating");

    TransitionEdge t;
    t.source = "Idle";
    t.target = "Navigating";
    t.event = "StartNav";
    t.guard = "GpsLock && CompassCalibrated";
    model.add_transition(t);

    std::string harness = McdcHarnessGenerator::generate_gtest_harness(model);
    EXPECT_NE(harness.find("GpsLock"), std::string::npos);
    EXPECT_NE(harness.find("CompassCalibrated"), std::string::npos);
    EXPECT_NE(harness.find("McdcCoverage"), std::string::npos);
}

}  // namespace
