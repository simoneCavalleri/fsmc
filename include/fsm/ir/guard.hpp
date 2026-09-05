#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fsm::ir {

// ============================================================================
// Guard AST, Operations & Models
// ============================================================================

enum class GuardOp : std::uint8_t { None, Not, And, Or };

/**
 * @brief AST node representing composable boolean guard logic (AND, OR, NOT, Atomic Predicates).
 */
struct GuardAstNode {
    GuardOp op{GuardOp::None};
    std::string expression;  ///< Raw predicate expression or guard struct name
    std::vector<GuardAstNode> children;

    GuardAstNode() = default;
    explicit GuardAstNode(std::string expr) : expression(std::move(expr)) {}
    GuardAstNode(GuardOp operation, std::vector<GuardAstNode> sub_nodes)
        : op(operation), children(std::move(sub_nodes)) {}

    [[nodiscard]] std::string to_string() const {
        if (op == GuardOp::None) {
            return expression;
        }
        if (op == GuardOp::Not) {
            if (!children.empty()) {
                return "!" + (children[0].op != GuardOp::None ? "(" + children[0].to_string() + ")"
                                                              : children[0].to_string());
            }
            return "!" + expression;
        }
        if (op == GuardOp::And || op == GuardOp::Or) {
            const std::string op_str = (op == GuardOp::And) ? " && " : " || ";
            std::string result;
            for (std::size_t i = 0; i < children.size(); ++i) {
                if (i > 0)
                    result += op_str;
                const bool needs_parens = (children[i].op == GuardOp::Or && op == GuardOp::And);
                if (needs_parens)
                    result += "(";
                result += children[i].to_string();
                if (needs_parens)
                    result += ")";
            }
            return result;
        }
        return expression;
    }

    void collect_atomic_guards(std::vector<std::string>& out) const {
        if (op == GuardOp::None) {
            if (!expression.empty() && expression != "else" && expression != "otherwise" && expression != "default") {
                out.push_back(expression);
            }
        } else {
            for (const auto& child : children) {
                child.collect_atomic_guards(out);
            }
        }
    }

    bool operator==(const GuardAstNode& other) const noexcept {
        return op == other.op && expression == other.expression && children == other.children;
    }
};

struct GuardModel {
    std::string name;
    std::string description;
    std::optional<std::string> raw_expression;
    std::optional<std::string> cpp_expression;

    explicit GuardModel(std::string guard_name = "", std::string guard_desc = "",
                        std::optional<std::string> raw_expr = std::nullopt,
                        std::optional<std::string> cpp_expr = std::nullopt)
        : name(std::move(guard_name)),
          description(std::move(guard_desc)),
          raw_expression(std::move(raw_expr)),
          cpp_expression(std::move(cpp_expr)) {}

    bool operator<(const GuardModel& other) const noexcept { return name < other.name; }
};

}  // namespace fsm::ir

namespace fsm {
using GuardOp = ir::GuardOp;
using GuardAstNode = ir::GuardAstNode;
using GuardModel = ir::GuardModel;
}

