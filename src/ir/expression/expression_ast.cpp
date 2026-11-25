#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>

#include "fsm/ir/expression.hpp"

namespace fsm::ir {

std::string_view expression_kind_to_string(ExpressionKind kind) noexcept {
    switch (kind) {
        case ExpressionKind::IntegerLiteral:
            return "IntegerLiteral";
        case ExpressionKind::FloatLiteral:
            return "FloatLiteral";
        case ExpressionKind::BooleanLiteral:
            return "BooleanLiteral";
        case ExpressionKind::EnumLiteral:
            return "EnumLiteral";
        case ExpressionKind::VariableRef:
            return "VariableRef";
        case ExpressionKind::PortRef:
            return "PortRef";
        case ExpressionKind::EventParamRef:
            return "EventParamRef";
        case ExpressionKind::UnaryOp:
            return "UnaryOp";
        case ExpressionKind::BinaryOp:
            return "BinaryOp";
        case ExpressionKind::RawExpression:
            return "RawExpression";
    }
    return "RawExpression";
}

std::string_view expression_op_to_string(ExpressionOp op) noexcept {
    switch (op) {
        case ExpressionOp::None:
            return "";
        case ExpressionOp::Negate:
            return "-";
        case ExpressionOp::BitwiseNot:
            return "~";
        case ExpressionOp::LogicalNot:
            return "!";
        case ExpressionOp::Add:
            return "+";
        case ExpressionOp::Subtract:
            return "-";
        case ExpressionOp::Multiply:
            return "*";
        case ExpressionOp::Divide:
            return "/";
        case ExpressionOp::Modulo:
            return "%";
        case ExpressionOp::ShiftLeft:
            return "<<";
        case ExpressionOp::ShiftRight:
            return ">>";
        case ExpressionOp::BitwiseAnd:
            return "&";
        case ExpressionOp::BitwiseOr:
            return "|";
        case ExpressionOp::BitwiseXor:
            return "^";
        case ExpressionOp::Equal:
            return "==";
        case ExpressionOp::NotEqual:
            return "!=";
        case ExpressionOp::LessThan:
            return "<";
        case ExpressionOp::LessEqual:
            return "<=";
        case ExpressionOp::GreaterThan:
            return ">";
        case ExpressionOp::GreaterEqual:
            return ">=";
        case ExpressionOp::LogicalAnd:
            return "&&";
        case ExpressionOp::LogicalOr:
            return "||";
    }
    return "";
}

ExpressionOp string_to_expression_op(std::string_view str) noexcept {
    if (str == "+")
        return ExpressionOp::Add;
    if (str == "-")
        return ExpressionOp::Subtract;
    if (str == "*")
        return ExpressionOp::Multiply;
    if (str == "/")
        return ExpressionOp::Divide;
    if (str == "%")
        return ExpressionOp::Modulo;
    if (str == "<<")
        return ExpressionOp::ShiftLeft;
    if (str == ">>")
        return ExpressionOp::ShiftRight;
    if (str == "&")
        return ExpressionOp::BitwiseAnd;
    if (str == "|")
        return ExpressionOp::BitwiseOr;
    if (str == "^")
        return ExpressionOp::BitwiseXor;
    if (str == "==")
        return ExpressionOp::Equal;
    if (str == "!=")
        return ExpressionOp::NotEqual;
    if (str == "<")
        return ExpressionOp::LessThan;
    if (str == "<=")
        return ExpressionOp::LessEqual;
    if (str == ">")
        return ExpressionOp::GreaterThan;
    if (str == ">=")
        return ExpressionOp::GreaterEqual;
    if (str == "&&" || str == "and" || str == "AND")
        return ExpressionOp::LogicalAnd;
    if (str == "||" || str == "or" || str == "OR")
        return ExpressionOp::LogicalOr;
    if (str == "~")
        return ExpressionOp::BitwiseNot;
    if (str == "!" || str == "not" || str == "NOT")
        return ExpressionOp::LogicalNot;
    return ExpressionOp::None;
}

std::string ExpressionAstNode::to_string() const {
    switch (kind) {
        case ExpressionKind::IntegerLiteral:
            return std::holds_alternative<int64_t>(value) ? std::to_string(std::get<int64_t>(value)) : symbol;
        case ExpressionKind::FloatLiteral: {
            if (std::holds_alternative<double>(value)) {
                std::string s = std::to_string(std::get<double>(value));
                while (s.size() > 1 && s.back() == '0' && s[s.size() - 2] != '.') {
                    s.pop_back();
                }
                return s;
            }
            return symbol;
        }
        case ExpressionKind::BooleanLiteral:
            return (std::holds_alternative<bool>(value) && std::get<bool>(value)) ? "true" : "false";
        case ExpressionKind::EnumLiteral:
        case ExpressionKind::VariableRef:
        case ExpressionKind::PortRef:
        case ExpressionKind::EventParamRef:
        case ExpressionKind::RawExpression:
            return symbol;
        case ExpressionKind::UnaryOp: {
            if (children.empty())
                return std::string(expression_op_to_string(op)) + symbol;
            std::string child_str = children[0].to_string();
            if (children[0].kind == ExpressionKind::BinaryOp ||
                (children[0].kind == ExpressionKind::UnaryOp && op == ExpressionOp::Negate &&
                 children[0].op == ExpressionOp::Negate) ||
                (op == ExpressionOp::Negate && !child_str.empty() && child_str.front() == '-')) {
                child_str = "(" + child_str + ")";
            }
            return std::string(expression_op_to_string(op)) + child_str;
        }
        case ExpressionKind::BinaryOp: {
            if (children.size() < 2)
                return symbol;
            auto op_prec = [](ExpressionOp o) -> int {
                switch (o) {
                    case ExpressionOp::Multiply:
                    case ExpressionOp::Divide:
                    case ExpressionOp::Modulo:
                        return 10;
                    case ExpressionOp::Add:
                    case ExpressionOp::Subtract:
                        return 9;
                    case ExpressionOp::ShiftLeft:
                    case ExpressionOp::ShiftRight:
                        return 8;
                    case ExpressionOp::LessThan:
                    case ExpressionOp::LessEqual:
                    case ExpressionOp::GreaterThan:
                    case ExpressionOp::GreaterEqual:
                        return 7;
                    case ExpressionOp::Equal:
                    case ExpressionOp::NotEqual:
                        return 6;
                    case ExpressionOp::BitwiseAnd:
                        return 5;
                    case ExpressionOp::BitwiseXor:
                        return 4;
                    case ExpressionOp::BitwiseOr:
                        return 3;
                    case ExpressionOp::LogicalAnd:
                        return 2;
                    case ExpressionOp::LogicalOr:
                        return 1;
                    default:
                        return 0;
                }
            };
            int my_prec = op_prec(op);

            auto format_child = [my_prec, op_prec](const ExpressionAstNode& c, bool is_right) -> std::string {
                std::string s = c.to_string();
                if (c.kind == ExpressionKind::BinaryOp) {
                    int child_prec = op_prec(c.op);
                    if (child_prec < my_prec || (is_right && child_prec == my_prec)) {
                        return "(" + s + ")";
                    }
                }
                return s;
            };

            return format_child(children[0], false) + " " + std::string(expression_op_to_string(op)) + " " +
                   format_child(children[1], true);
        }
    }
    return symbol;
}

std::string ExpressionAstNode::to_json() const {
    std::ostringstream ss;
    ss << "{";
    ss << "\"kind\": \"" << expression_kind_to_string(kind) << "\"";
    if (op != ExpressionOp::None) {
        ss << ", \"op\": \"" << expression_op_to_string(op) << "\"";
    }
    if (!symbol.empty()) {
        std::string escaped;
        for (char c : symbol) {
            if (c == '"')
                escaped += "\\\"";
            else if (c == '\\')
                escaped += "\\\\";
            else
                escaped += c;
        }
        ss << ", \"symbol\": \"" << escaped << "\"";
    }
    if (std::holds_alternative<int64_t>(value)) {
        ss << ", \"value\": " << std::get<int64_t>(value);
    } else if (std::holds_alternative<double>(value)) {
        ss << ", \"value\": " << std::get<double>(value);
    } else if (std::holds_alternative<bool>(value)) {
        ss << ", \"value\": " << (std::get<bool>(value) ? "true" : "false");
    }
    if (!children.empty()) {
        ss << ", \"children\": [";
        for (size_t i = 0; i < children.size(); ++i) {
            if (i > 0)
                ss << ", ";
            ss << children[i].to_json();
        }
        ss << "]";
    }
    ss << "}";
    return ss.str();
}

bool ExpressionAstNode::operator==(const ExpressionAstNode& other) const noexcept {
    if (kind != other.kind || op != other.op || symbol != other.symbol || children != other.children) {
        return false;
    }
    if (value.index() != other.value.index()) {
        return false;
    }
    if (std::holds_alternative<int64_t>(value)) {
        return std::get<int64_t>(value) == std::get<int64_t>(other.value);
    }
    if (std::holds_alternative<double>(value)) {
        return std::get<double>(value) == std::get<double>(other.value);
    }
    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) == std::get<bool>(other.value);
    }
    return true;
}

}  // namespace fsm::ir
