/**
 * @file test_guard_simplification.cpp
 * @brief Unit tests for GuardSimplificationPass boolean algebra reductions.
 */

#include <gtest/gtest.h>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/passes/guard_simplification_pass.hpp"

using namespace fsm::diagnostic;
using namespace fsm::middleend::passes;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify algebraic boolean simplifications (double negation, constant folding, idempotency).
 * @scenario Combinations of double negation (!(!Ready)), identity with true/false, and idempotency (A && A).
 * @expected Normalized expressions with algebraic simplifications applied and redundant terms eliminated.
 */
TEST(GuardSimplification, AlgebraicBooleanExpressions_SimplifiedAndNormalized) {
    // 1. Double negation: !(!Ready) -> Ready
    GuardAstNode double_not(GuardOp::Not, {GuardAstNode(GuardOp::Not, {GuardAstNode("Ready")})});
    GuardAstNode res_dn = GuardSimplificationPass::simplify_node(double_not);
    EXPECT_EQ(res_dn.op, GuardOp::None);
    EXPECT_EQ(res_dn.expression, "Ready");

    // 2. AND with true: (Ready && true) -> Ready
    GuardAstNode and_true(GuardOp::And, {GuardAstNode("Ready"), GuardAstNode("true")});
    GuardAstNode res_at = GuardSimplificationPass::simplify_node(and_true);
    EXPECT_EQ(res_at.op, GuardOp::None);
    EXPECT_EQ(res_at.expression, "Ready");

    // 3. AND with false: (Ready && false) -> false
    GuardAstNode and_false(GuardOp::And, {GuardAstNode("Ready"), GuardAstNode("false")});
    GuardAstNode res_af = GuardSimplificationPass::simplify_node(and_false);
    EXPECT_EQ(res_af.op, GuardOp::None);
    EXPECT_EQ(res_af.expression, "false");

    // 4. OR with false: (Ready || false) -> Ready
    GuardAstNode or_false(GuardOp::Or, {GuardAstNode("Ready"), GuardAstNode("false")});
    GuardAstNode res_of = GuardSimplificationPass::simplify_node(or_false);
    EXPECT_EQ(res_of.op, GuardOp::None);
    EXPECT_EQ(res_of.expression, "Ready");

    // 5. OR with true: (Ready || true) -> true
    GuardAstNode or_true(GuardOp::Or, {GuardAstNode("Ready"), GuardAstNode("true")});
    GuardAstNode res_ot = GuardSimplificationPass::simplify_node(or_true);
    EXPECT_EQ(res_ot.op, GuardOp::None);
    EXPECT_EQ(res_ot.expression, "true");

    // 6. Idempotency: (Ready && Ready) -> Ready
    GuardAstNode dup_and(GuardOp::And, {GuardAstNode("Ready"), GuardAstNode("Ready")});
    GuardAstNode res_da = GuardSimplificationPass::simplify_node(dup_and);
    EXPECT_EQ(res_da.op, GuardOp::None);
    EXPECT_EQ(res_da.expression, "Ready");
}

}  // namespace
