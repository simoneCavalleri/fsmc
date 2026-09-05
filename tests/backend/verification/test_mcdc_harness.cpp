#include <gtest/gtest.h>

#include "fsm/backend/verification/mcdc_harness_generator.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::backend::verification;
using namespace fsm::backend;
using namespace fsm::ir;

namespace {

/**
 * @brief Test Intent: Verify MCDC independence pair computation on conjunction A && B.
 *
 * Scenario:
 * - Decision D = A && B.
 * - Compute MC/DC independence pairs.
 * - Verify condition A has independence pair (A=T, B=T -> T) and (A=F, B=T -> F).
 * - Verify condition B has independence pair (A=T, B=T -> T) and (A=T, B=F -> F).
 */
TEST(McdcHarnessTest, ConjunctionIndependencePairs) {
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
 * @brief Test Intent: Verify McdcHarnessGenerator produces GoogleTest harness string for transition guards.
 *
 * Scenario:
 * - Create model with transition having composite guard "SensorOk && SpeedValid".
 * - Generate GoogleTest harness.
 * - Verify emitted code contains MCDC test macros and test vectors.
 */
TEST(McdcHarnessTest, HarnessCodeGeneration) {
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

}  // namespace
