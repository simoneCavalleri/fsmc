#include <cmath>

#include "fsm/ir/expression.hpp"

namespace fsm::ir {

void ExpressionAstNode::collect_references(std::vector<std::string>& out) const {
    if (kind == ExpressionKind::VariableRef || kind == ExpressionKind::PortRef ||
        kind == ExpressionKind::EventParamRef) {
        if (!symbol.empty()) {
            out.push_back(symbol);
        }
    }
    for (const auto& child : children) {
        child.collect_references(out);
    }
}

std::optional<double> ExpressionAstNode::evaluate_numeric(const std::unordered_map<std::string, double>& env) const {
    switch (kind) {
        case ExpressionKind::IntegerLiteral:
            if (std::holds_alternative<int64_t>(value)) {
                return static_cast<double>(std::get<int64_t>(value));
            }
            return std::nullopt;
        case ExpressionKind::FloatLiteral:
            if (std::holds_alternative<double>(value)) {
                return std::get<double>(value);
            }
            return std::nullopt;
        case ExpressionKind::BooleanLiteral:
            if (std::holds_alternative<bool>(value)) {
                return std::get<bool>(value) ? 1.0 : 0.0;
            }
            if (symbol == "true") {
                return 1.0;
            }
            if (symbol == "false") {
                return 0.0;
            }
            return std::nullopt;
        case ExpressionKind::VariableRef:
        case ExpressionKind::PortRef:
        case ExpressionKind::EventParamRef: {
            if (auto it = env.find(symbol); it != env.end()) {
                return it->second;
            }
            std::string_view sv(symbol);
            static constexpr std::string_view prefixes[] = {"in.",  "out.",   "reg.", "payload.",
                                                            "cmd.", "event.", "sig.", "ev."};
            for (std::string_view p : prefixes) {
                if (sv.starts_with(p)) {
                    std::string stripped(sv.substr(p.size()));
                    if (auto it2 = env.find(stripped); it2 != env.end()) {
                        return it2->second;
                    }
                }
            }
            return std::nullopt;
        }
        case ExpressionKind::UnaryOp: {
            if (children.empty()) {
                return std::nullopt;
            }
            auto child_val = children[0].evaluate_numeric(env);
            if (!child_val.has_value()) {
                return std::nullopt;
            }
            auto safe_to_int64 = [](double v) -> std::optional<int64_t> {
                if (!std::isfinite(v)) {
                    return std::nullopt;
                }
                // 2^63 is exactly representable in IEEE-754 double; reject values >= 2^63 or < -2^63
                constexpr double kMinI64 = -9223372036854775808.0;
                constexpr double kMaxI64Exclusive = 9223372036854775808.0;
                if (v < kMinI64 || v >= kMaxI64Exclusive) {
                    return std::nullopt;
                }
                return static_cast<int64_t>(v);
            };
            switch (op) {
                case ExpressionOp::Negate:
                    return -(*child_val);
                case ExpressionOp::BitwiseNot: {
                    auto iv = safe_to_int64(*child_val);
                    if (!iv.has_value()) {
                        return std::nullopt;
                    }
                    return static_cast<double>(~(*iv));
                }
                case ExpressionOp::LogicalNot:
                    return (*child_val == 0.0) ? 1.0 : 0.0;
                default:
                    return std::nullopt;
            }
        }
        case ExpressionKind::BinaryOp: {
            if (children.size() < 2) {
                return std::nullopt;
            }
            if (op == ExpressionOp::LogicalAnd) {
                auto lhs = children[0].evaluate_numeric(env);
                if (!lhs.has_value()) {
                    return std::nullopt;
                }
                if (*lhs == 0.0) {
                    return 0.0;
                }
                auto rhs = children[1].evaluate_numeric(env);
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                return (*rhs != 0.0) ? 1.0 : 0.0;
            }
            if (op == ExpressionOp::LogicalOr) {
                auto lhs = children[0].evaluate_numeric(env);
                if (!lhs.has_value()) {
                    return std::nullopt;
                }
                if (*lhs != 0.0) {
                    return 1.0;
                }
                auto rhs = children[1].evaluate_numeric(env);
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                return (*rhs != 0.0) ? 1.0 : 0.0;
            }
            auto lhs = children[0].evaluate_numeric(env);
            auto rhs = children[1].evaluate_numeric(env);
            if (!lhs.has_value() || !rhs.has_value()) {
                return std::nullopt;
            }
            auto safe_to_int64 = [](double v) -> std::optional<int64_t> {
                if (!std::isfinite(v)) {
                    return std::nullopt;
                }
                constexpr double kMinI64 = -9223372036854775808.0;
                constexpr double kMaxI64Exclusive = 9223372036854775808.0;
                if (v < kMinI64 || v >= kMaxI64Exclusive) {
                    return std::nullopt;
                }
                return static_cast<int64_t>(v);
            };
            switch (op) {
                case ExpressionOp::Add: {
                    double res = *lhs + *rhs;
                    return std::isfinite(res) ? std::optional<double>(res) : std::nullopt;
                }
                case ExpressionOp::Subtract: {
                    double res = *lhs - *rhs;
                    return std::isfinite(res) ? std::optional<double>(res) : std::nullopt;
                }
                case ExpressionOp::Multiply: {
                    double res = *lhs * *rhs;
                    return std::isfinite(res) ? std::optional<double>(res) : std::nullopt;
                }
                case ExpressionOp::Divide: {
                    if (*rhs == 0.0 || !std::isfinite(*lhs) || !std::isfinite(*rhs)) {
                        return std::nullopt;
                    }
                    double res = *lhs / *rhs;
                    return std::isfinite(res) ? std::optional<double>(res) : std::nullopt;
                }
                case ExpressionOp::Modulo: {
                    auto l_int = safe_to_int64(*lhs);
                    auto r_int = safe_to_int64(*rhs);
                    if (!l_int.has_value() || !r_int.has_value() || *r_int == 0) {
                        return std::nullopt;
                    }
                    if (*l_int == std::numeric_limits<int64_t>::min() && *r_int == -1) {
                        return std::nullopt;
                    }
                    return static_cast<double>(*l_int % *r_int);
                }
                case ExpressionOp::ShiftLeft: {
                    auto l_int = safe_to_int64(*lhs);
                    auto r_int = safe_to_int64(*rhs);
                    if (!l_int.has_value() || !r_int.has_value() || *r_int < 0 || *r_int >= 64) {
                        return std::nullopt;
                    }
                    const auto shifted = static_cast<uint64_t>(*l_int) << static_cast<unsigned int>(*r_int);
                    return static_cast<double>(static_cast<int64_t>(shifted));
                }
                case ExpressionOp::ShiftRight: {
                    auto l_int = safe_to_int64(*lhs);
                    auto r_int = safe_to_int64(*rhs);
                    if (!l_int.has_value() || !r_int.has_value() || *r_int < 0 || *r_int >= 64) {
                        return std::nullopt;
                    }
                    return static_cast<double>(*l_int >> static_cast<unsigned int>(*r_int));
                }
                case ExpressionOp::BitwiseAnd: {
                    auto l_int = safe_to_int64(*lhs);
                    auto r_int = safe_to_int64(*rhs);
                    if (!l_int.has_value() || !r_int.has_value()) {
                        return std::nullopt;
                    }
                    return static_cast<double>(*l_int & *r_int);
                }
                case ExpressionOp::BitwiseOr: {
                    auto l_int = safe_to_int64(*lhs);
                    auto r_int = safe_to_int64(*rhs);
                    if (!l_int.has_value() || !r_int.has_value()) {
                        return std::nullopt;
                    }
                    return static_cast<double>(*l_int | *r_int);
                }
                case ExpressionOp::BitwiseXor: {
                    auto l_int = safe_to_int64(*lhs);
                    auto r_int = safe_to_int64(*rhs);
                    if (!l_int.has_value() || !r_int.has_value()) {
                        return std::nullopt;
                    }
                    return static_cast<double>(*l_int ^ *r_int);
                }
                case ExpressionOp::Equal:
                    return (*lhs == *rhs) ? 1.0 : 0.0;
                case ExpressionOp::NotEqual:
                    return (*lhs != *rhs) ? 1.0 : 0.0;
                case ExpressionOp::LessThan:
                    return (*lhs < *rhs) ? 1.0 : 0.0;
                case ExpressionOp::LessEqual:
                    return (*lhs <= *rhs) ? 1.0 : 0.0;
                case ExpressionOp::GreaterThan:
                    return (*lhs > *rhs) ? 1.0 : 0.0;
                case ExpressionOp::GreaterEqual:
                    return (*lhs >= *rhs) ? 1.0 : 0.0;
                default:
                    return std::nullopt;
            }
        }
        case ExpressionKind::EnumLiteral:
        case ExpressionKind::RawExpression:
            return std::nullopt;
    }
    return std::nullopt;
}

std::optional<bool> ExpressionAstNode::evaluate_boolean(const std::unordered_map<std::string, double>& env) const {
    if (kind == ExpressionKind::BooleanLiteral) {
        if (std::holds_alternative<bool>(value)) {
            return std::get<bool>(value);
        }
        if (symbol == "true") {
            return true;
        }
        if (symbol == "false") {
            return false;
        }
        return std::nullopt;
    }
    if (kind == ExpressionKind::BinaryOp && children.size() >= 2) {
        if (op == ExpressionOp::LogicalAnd) {
            auto lhs = children[0].evaluate_boolean(env);
            if (!lhs.has_value()) {
                return std::nullopt;
            }
            if (!*lhs) {
                return false;
            }
            return children[1].evaluate_boolean(env);
        }
        if (op == ExpressionOp::LogicalOr) {
            auto lhs = children[0].evaluate_boolean(env);
            if (!lhs.has_value()) {
                return std::nullopt;
            }
            if (*lhs) {
                return true;
            }
            return children[1].evaluate_boolean(env);
        }
    }
    auto val = evaluate_numeric(env);
    if (!val.has_value()) {
        return std::nullopt;
    }
    return *val != 0.0;
}

}  // namespace fsm::ir
