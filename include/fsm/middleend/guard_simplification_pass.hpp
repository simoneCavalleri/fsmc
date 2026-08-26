#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/guard.hpp"

namespace fsm::codegen {

/**
 * @brief Target-Agnostic Middle-End Pass: Algebraic simplification and canonicalization of Guard ASTs.
 *
 * Rules applied:
 * 1. Double Negation Elimination: !(!A) -> A
 * 2. Constant Folding:
 *    - !(true) -> false, !(false) -> true
 *    - A && true -> A, A && false -> false
 *    - A || false -> A, A || true -> true
 * 3. Associative Flattening:
 *    - And(A, And(B, C)) -> And(A, B, C)
 *    - Or(A, Or(B, C)) -> Or(A, B, C)
 * 4. Idempotency & Duplicate Elimination:
 *    - A && A -> A, A || A -> A
 * 5. Trivial Node Reduction:
 *    - And(A) -> A, Or(A) -> A
 */
class GuardSimplificationPass {
  public:
    [[nodiscard]] static std::string name() { return "GuardSimplification"; }
    [[nodiscard]] static std::string description() {
        return "Performs algebraic simplification, constant folding, and canonicalization of guard ASTs";
    }

    static GuardAstNode simplify_node(const GuardAstNode& node) {
        // Base case: atomic expression
        if (node.op == GuardOp::None) {
            return node;
        }

        // Simplify all children first (bottom-up)
        std::vector<GuardAstNode> simplified_children;
        simplified_children.reserve(node.children.size());
        for (const auto& child : node.children) {
            simplified_children.push_back(simplify_node(child));
        }

        // 1. NOT operator
        if (node.op == GuardOp::Not) {
            if (simplified_children.empty()) {
                return node;
            }
            const auto& child = simplified_children[0];
            // !(!A) -> A
            if (child.op == GuardOp::Not && !child.children.empty()) {
                return child.children[0];
            }
            // !(true) -> false, !(false) -> true
            if (child.op == GuardOp::None) {
                if (child.expression == "true")
                    return GuardAstNode("false");
                if (child.expression == "false")
                    return GuardAstNode("true");
            }
            return GuardAstNode(GuardOp::Not, {child});
        }

        // 2. Associative Flattening for AND / OR
        std::vector<GuardAstNode> flattened;
        flattened.reserve(simplified_children.size());
        for (auto& child : simplified_children) {
            if (child.op == node.op) {
                for (auto& grand_child : child.children) {
                    flattened.push_back(std::move(grand_child));
                }
            } else {
                flattened.push_back(std::move(child));
            }
        }

        // 3. Constant Folding & Duplicate Elimination
        std::vector<GuardAstNode> cleaned;
        cleaned.reserve(flattened.size());

        if (node.op == GuardOp::And) {
            for (const auto& item : flattened) {
                // If any child is "false", whole AND is false
                if (item.op == GuardOp::None && item.expression == "false") {
                    return GuardAstNode("false");
                }
                // Skip "true" in AND
                if (item.op == GuardOp::None && item.expression == "true") {
                    continue;
                }
                // Duplicate check
                if (std::find(cleaned.begin(), cleaned.end(), item) == cleaned.end()) {
                    cleaned.push_back(item);
                }
            }
            if (cleaned.empty()) {
                return GuardAstNode("true");
            }
            if (cleaned.size() == 1) {
                return cleaned[0];
            }
            return GuardAstNode(GuardOp::And, std::move(cleaned));
        }

        if (node.op == GuardOp::Or) {
            for (const auto& item : flattened) {
                // If any child is "true", whole OR is true
                if (item.op == GuardOp::None && item.expression == "true") {
                    return GuardAstNode("true");
                }
                // Skip "false" in OR
                if (item.op == GuardOp::None && item.expression == "false") {
                    continue;
                }
                // Duplicate check
                if (std::find(cleaned.begin(), cleaned.end(), item) == cleaned.end()) {
                    cleaned.push_back(item);
                }
            }
            if (cleaned.empty()) {
                return GuardAstNode("false");
            }
            if (cleaned.size() == 1) {
                return cleaned[0];
            }
            return GuardAstNode(GuardOp::Or, std::move(cleaned));
        }

        return node;
    }

    bool run(FsmIr& ir, DiagnosticEngine& diag) {
        for (auto& t : ir.transitions) {
            if (t.guard_ast.has_value()) {
                GuardAstNode simplified = simplify_node(*t.guard_ast);
                t.guard_ast = simplified;
                t.guard = simplified.to_string();

                if (t.guard == "false") {
                    diag.report(Diagnostic::warning(
                        "W_GUARD_STATIC_FALSE",
                        "Transition '" + t.source + " -> " + t.target + "' has guard simplified to static 'false'."));
                }
            }
        }
        return true;
    }
};

}  // namespace fsm::codegen

namespace fsm {
using GuardSimplificationPass = ::fsm::codegen::GuardSimplificationPass;
}  // namespace fsm
