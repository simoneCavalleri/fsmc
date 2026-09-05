#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/ir/expression.hpp"

namespace fsm::ir {

// ============================================================================
// Action Signatures, Assignment Operators & Structured Models
// ============================================================================

/**
 * @brief Algebraic assignment operators for datapath state effects.
 */
enum class AssignmentOp : std::uint8_t {
    Assign,     ///< =
    AddAssign,  ///< +=
    SubAssign,  ///< -=
    MulAssign,  ///< *=
    DivAssign,  ///< /=
    ModAssign,  ///< %=
    ShlAssign,  ///< <<=
    ShrAssign,  ///< >>=
    AndAssign,  ///< &=
    OrAssign,   ///< |=
    XorAssign   ///< ^=
};

[[nodiscard]] constexpr std::string_view assignment_op_to_string(AssignmentOp op) noexcept {
    switch (op) {
        case AssignmentOp::Assign:
            return "=";
        case AssignmentOp::AddAssign:
            return "+=";
        case AssignmentOp::SubAssign:
            return "-=";
        case AssignmentOp::MulAssign:
            return "*=";
        case AssignmentOp::DivAssign:
            return "/=";
        case AssignmentOp::ModAssign:
            return "%=";
        case AssignmentOp::ShlAssign:
            return "<<=";
        case AssignmentOp::ShrAssign:
            return ">>=";
        case AssignmentOp::AndAssign:
            return "&=";
        case AssignmentOp::OrAssign:
            return "|=";
        case AssignmentOp::XorAssign:
            return "^=";
    }
    return "=";
}

[[nodiscard]] constexpr AssignmentOp string_to_assignment_op(std::string_view str) noexcept {
    if (str == "+=")
        return AssignmentOp::AddAssign;
    if (str == "-=")
        return AssignmentOp::SubAssign;
    if (str == "*=")
        return AssignmentOp::MulAssign;
    if (str == "/=")
        return AssignmentOp::DivAssign;
    if (str == "%=")
        return AssignmentOp::ModAssign;
    if (str == "<<=")
        return AssignmentOp::ShlAssign;
    if (str == ">>=")
        return AssignmentOp::ShrAssign;
    if (str == "&=")
        return AssignmentOp::AndAssign;
    if (str == "|=")
        return AssignmentOp::OrAssign;
    if (str == "^=")
        return AssignmentOp::XorAssign;
    return AssignmentOp::Assign;
}

/**
 * @brief Structured action effect assignment with algebraic AST and C++ string representation.
 */
struct ActionAssignment {
    std::string target_variable;                             ///< Target register or port
    AssignmentOp op{AssignmentOp::Assign};                   ///< Assignment operator (=, +=, -=, etc.)
    std::string expression;                                  ///< Canonical text expression
    std::optional<ExpressionAstNode> expr_ast{std::nullopt};  ///< Structured algebraic AST

    ActionAssignment() = default;

    ActionAssignment(std::string var, std::string expr, AssignmentOp assign_op = AssignmentOp::Assign)
        : target_variable(std::move(var)), op(assign_op), expression(std::move(expr)) {
        if (!expression.empty()) {
            expr_ast = ExpressionAstNode::parse(expression);
        }
    }

    ActionAssignment(std::string var, AssignmentOp assign_op, ExpressionAstNode ast)
        : target_variable(std::move(var)), op(assign_op), expression(ast.to_string()), expr_ast(std::move(ast)) {}

    ActionAssignment(std::string var, ExpressionAstNode ast)
        : ActionAssignment(std::move(var), AssignmentOp::Assign, std::move(ast)) {}

    static ActionAssignment parse(std::string_view statement) {
        // Find operator: =, +=, -=, *=, /=, %=, <<=, >>=, &=, |=, ^=
        const std::pair<std::string_view, AssignmentOp> ops[] = {
            {"<<=", AssignmentOp::ShlAssign}, {">>=", AssignmentOp::ShrAssign}, {"+=", AssignmentOp::AddAssign},
            {"-=", AssignmentOp::SubAssign},  {"*=", AssignmentOp::MulAssign},  {"/=", AssignmentOp::DivAssign},
            {"%=", AssignmentOp::ModAssign},  {"&=", AssignmentOp::AndAssign},  {"|=", AssignmentOp::OrAssign},
            {"^=", AssignmentOp::XorAssign},  {"=", AssignmentOp::Assign}};

        for (const auto& [op_str, op_kind] : ops) {
            size_t pos = statement.find(op_str);
            if (pos != std::string_view::npos) {
                std::string_view lhs = statement.substr(0, pos);
                std::string_view rhs = statement.substr(pos + op_str.size());

                // Trim lhs
                while (!lhs.empty() && std::isspace(static_cast<unsigned char>(lhs.front())) != 0)
                    lhs.remove_prefix(1);
                while (!lhs.empty() && std::isspace(static_cast<unsigned char>(lhs.back())) != 0)
                    lhs.remove_suffix(1);

                // Trim rhs
                while (!rhs.empty() && std::isspace(static_cast<unsigned char>(rhs.front())) != 0)
                    rhs.remove_prefix(1);
                while (!rhs.empty() &&
                       (std::isspace(static_cast<unsigned char>(rhs.back())) != 0 || rhs.back() == ';'))
                    rhs.remove_suffix(1);

                std::string var(lhs);
                if (var.starts_with("out."))
                    var = var.substr(4);
                else if (var.starts_with("reg."))
                    var = var.substr(4);

                return ActionAssignment(var, std::string(rhs), op_kind);
            }
        }
        return ActionAssignment(std::string(statement), "");
    }

    bool operator==(const ActionAssignment& other) const noexcept {
        return target_variable == other.target_variable && op == other.op && expression == other.expression &&
               expr_ast == other.expr_ast;
    }
};

struct ActionSignature {
    std::string name;
    std::string invocation;                     // e.g. "srv.SendAlert()"
    bool accepts_event{false};                  // Passes const Event&
    std::vector<ActionAssignment> assignments;  // State variable assignments

    ActionSignature() = default;
    explicit ActionSignature(std::string act_name, std::string act_inv = "")
        : name(std::move(act_name)), invocation(std::move(act_inv)) {}

    bool operator==(const ActionSignature& other) const noexcept {
        return name == other.name && invocation == other.invocation && accepts_event == other.accepts_event &&
               assignments == other.assignments;
    }
};

struct ActionModel {
    std::string name;
    std::string description;

    explicit ActionModel(std::string action_name = "", std::string action_desc = "")
        : name(std::move(action_name)), description(std::move(action_desc)) {}

    bool operator<(const ActionModel& other) const noexcept { return name < other.name; }
};

}  // namespace fsm::ir

namespace fsm {
using AssignmentOp = ir::AssignmentOp;
using ActionAssignment = ir::ActionAssignment;
using ActionSignature = ir::ActionSignature;
using ActionModel = ir::ActionModel;
using ir::assignment_op_to_string;
using ir::string_to_assignment_op;
}  // namespace fsm

