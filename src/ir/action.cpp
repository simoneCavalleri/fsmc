#include "fsm/ir/action.hpp"

#include <cctype>

namespace fsm::ir {

std::string LValueTarget::full_path() const {
    std::string res = name;
    for (const auto& member : member_path) {
        res += "." + member;
    }
    if (constant_index.has_value()) {
        res += "[" + std::to_string(*constant_index) + "]";
    }
    return res;
}

std::string LValueTarget::scoped_path() const {
    std::string prefix;
    if (scope == LValueScope::Register) {
        prefix = "reg.";
    } else if (scope == LValueScope::OutPort) {
        prefix = "out.";
    } else if (scope == LValueScope::Local) {
        prefix = "local.";
    }
    return prefix + full_path();
}

bool LValueTarget::operator==(const char* str) const noexcept {
    return str != nullptr && (full_path() == str || scoped_path() == str || name == str);
}

bool LValueTarget::operator==(const std::string& str) const noexcept {
    return full_path() == str || scoped_path() == str || name == str;
}

bool LValueTarget::operator==(std::string_view str) const noexcept {
    return full_path() == str || scoped_path() == str || name == str;
}

void LValueTarget::parse_from_string(std::string_view str) {
    while (!str.empty() && std::isspace(static_cast<unsigned char>(str.front())) != 0)
        str.remove_prefix(1);
    while (!str.empty() && std::isspace(static_cast<unsigned char>(str.back())) != 0)
        str.remove_suffix(1);
    if (str.empty())
        return;

    if (str.starts_with("out.")) {
        scope = LValueScope::OutPort;
        str.remove_prefix(4);
    } else if (str.starts_with("reg.")) {
        scope = LValueScope::Register;
        str.remove_prefix(4);
    } else if (str.starts_with("local.")) {
        scope = LValueScope::Local;
        str.remove_prefix(6);
    }

    auto bracket_pos = str.find('[');
    if (bracket_pos != std::string_view::npos) {
        auto close_pos = str.find(']', bracket_pos);
        if (close_pos != std::string_view::npos) {
            std::string_view idx_str = str.substr(bracket_pos + 1, close_pos - bracket_pos - 1);
            try {
                constant_index = static_cast<std::uint32_t>(std::stoul(std::string(idx_str)));
            } catch (...) {
            }
            str = str.substr(0, bracket_pos);
        }
    }

    size_t dot_pos = str.find('.');
    if (dot_pos == std::string_view::npos) {
        name = std::string(str);
    } else {
        name = std::string(str.substr(0, dot_pos));
        std::string_view rest = str.substr(dot_pos + 1);
        while (!rest.empty()) {
            size_t next_dot = rest.find('.');
            if (next_dot == std::string_view::npos) {
                member_path.emplace_back(rest);
                break;
            }
            member_path.emplace_back(rest.substr(0, next_dot));
            rest.remove_prefix(next_dot + 1);
        }
    }
}

ActionAssignment::ActionAssignment(LValueTarget tgt, std::string expr, AssignmentOp assign_op)
    : target(std::move(tgt)), op(assign_op), expression(std::move(expr)) {
    if (!expression.empty()) {
        expr_ast = ExpressionAstNode::parse(expression);
    }
}

ActionAssignment::ActionAssignment(LValueTarget tgt, AssignmentOp assign_op, ExpressionAstNode ast)
    : target(std::move(tgt)), op(assign_op), expression(ast.to_string()), expr_ast(std::move(ast)) {}

ActionAssignment::ActionAssignment(LValueTarget tgt, ExpressionAstNode ast)
    : ActionAssignment(std::move(tgt), AssignmentOp::Assign, std::move(ast)) {}

ActionAssignment ActionAssignment::parse(std::string_view statement) {
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
            while (!rhs.empty() && (std::isspace(static_cast<unsigned char>(rhs.back())) != 0 || rhs.back() == ';'))
                rhs.remove_suffix(1);

            return ActionAssignment(LValueTarget(lhs), std::string(rhs), op_kind);
        }
    }
    return ActionAssignment(LValueTarget(statement), "");
}

}  // namespace fsm::ir
