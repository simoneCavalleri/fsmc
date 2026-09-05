#pragma once

#include <cctype>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace fsm::ir {

// ============================================================================
// Algebraic Expression AST Metamodel (Algebraic EFSM)
// ============================================================================

enum class ExpressionKind : std::uint8_t {
    // Leaf nodes: Literals
    IntegerLiteral,
    FloatLiteral,
    BooleanLiteral,
    EnumLiteral,

    // Leaf nodes: References
    VariableRef,
    PortRef,
    EventParamRef,

    // Composite Operations
    UnaryOp,
    BinaryOp,

    // Fallback opaque node (unlinearized C++ code, external functions)
    RawExpression
};

enum class ExpressionOp : std::uint8_t {
    None,

    // Unary Operators
    Negate,      ///< -x
    BitwiseNot,  ///< ~x
    LogicalNot,  ///< !x

    // Binary Arithmetic Operators
    Add,       ///< +
    Subtract,  ///< -
    Multiply,  ///< *
    Divide,    ///< /
    Modulo,    ///< %

    // Binary Bitwise & Shift Operators
    ShiftLeft,   ///< <<
    ShiftRight,  ///< >>
    BitwiseAnd,  ///< &
    BitwiseOr,   ///< |
    BitwiseXor   ///< ^
};

[[nodiscard]] constexpr std::string_view expression_kind_to_string(ExpressionKind kind) noexcept {
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

[[nodiscard]] constexpr std::string_view expression_op_to_string(ExpressionOp op) noexcept {
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
    }
    return "";
}

[[nodiscard]] constexpr ExpressionOp string_to_expression_op(std::string_view str) noexcept {
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
    if (str == "~")
        return ExpressionOp::BitwiseNot;
    if (str == "!")
        return ExpressionOp::LogicalNot;
    return ExpressionOp::None;
}

/**
 * @brief AST node representing algebraic datapath expressions in actions and effect sequences.
 */
struct ExpressionAstNode {
    ExpressionKind kind{ExpressionKind::RawExpression};
    ExpressionOp op{ExpressionOp::None};
    std::string symbol;                                           ///< Identifier, enum literal, or raw expression text
    std::variant<std::monostate, int64_t, double, bool> value{};  ///< Literal value
    std::vector<ExpressionAstNode> children;                      ///< Child subexpressions (1 for unary, 2 for binary)

    ExpressionAstNode() = default;

    // ------------------------------------------------------------------------
    // Factories
    // ------------------------------------------------------------------------

    static ExpressionAstNode make_int_literal(int64_t val) {
        ExpressionAstNode node;
        node.kind = ExpressionKind::IntegerLiteral;
        node.value = val;
        node.symbol = std::to_string(val);
        return node;
    }

    static ExpressionAstNode make_float_literal(double val) {
        ExpressionAstNode node;
        node.kind = ExpressionKind::FloatLiteral;
        node.value = val;
        node.symbol = std::to_string(val);
        return node;
    }

    static ExpressionAstNode make_bool_literal(bool val) {
        ExpressionAstNode node;
        node.kind = ExpressionKind::BooleanLiteral;
        node.value = val;
        node.symbol = val ? "true" : "false";
        return node;
    }

    static ExpressionAstNode make_enum_literal(std::string enum_type_or_literal, std::string literal_name = "") {
        ExpressionAstNode node;
        node.kind = ExpressionKind::EnumLiteral;
        if (literal_name.empty()) {
            node.symbol = std::move(enum_type_or_literal);
        } else {
            node.symbol = enum_type_or_literal + "::" + literal_name;
        }
        return node;
    }

    static ExpressionAstNode make_variable_ref(std::string var_name) {
        ExpressionAstNode node;
        node.kind = ExpressionKind::VariableRef;
        node.symbol = std::move(var_name);
        return node;
    }

    static ExpressionAstNode make_port_ref(std::string port_name, std::string field_name = "") {
        ExpressionAstNode node;
        node.kind = ExpressionKind::PortRef;
        if (field_name.empty()) {
            node.symbol = std::move(port_name);
        } else {
            node.symbol = port_name + "." + field_name;
        }
        return node;
    }

    static ExpressionAstNode make_event_param_ref(std::string event_name, std::string param_name) {
        ExpressionAstNode node;
        node.kind = ExpressionKind::EventParamRef;
        if (event_name.empty()) {
            node.symbol = std::move(param_name);
        } else {
            node.symbol = event_name + "." + param_name;
        }
        return node;
    }

    static ExpressionAstNode make_unary(ExpressionOp unary_op, ExpressionAstNode child) {
        ExpressionAstNode node;
        node.kind = ExpressionKind::UnaryOp;
        node.op = unary_op;
        node.children.push_back(std::move(child));
        return node;
    }

    static ExpressionAstNode make_binary(ExpressionOp bin_op, ExpressionAstNode left, ExpressionAstNode right) {
        ExpressionAstNode node;
        node.kind = ExpressionKind::BinaryOp;
        node.op = bin_op;
        node.children.push_back(std::move(left));
        node.children.push_back(std::move(right));
        return node;
    }

    static ExpressionAstNode make_raw_expression(std::string raw) {
        ExpressionAstNode node;
        node.kind = ExpressionKind::RawExpression;
        node.symbol = std::move(raw);
        return node;
    }

    // ------------------------------------------------------------------------
    // String representation
    // ------------------------------------------------------------------------

    [[nodiscard]] std::string to_string() const {
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
                if (children[0].kind == ExpressionKind::BinaryOp) {
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
                            return 6;
                        case ExpressionOp::Add:
                        case ExpressionOp::Subtract:
                            return 5;
                        case ExpressionOp::ShiftLeft:
                        case ExpressionOp::ShiftRight:
                            return 4;
                        case ExpressionOp::BitwiseAnd:
                            return 3;
                        case ExpressionOp::BitwiseXor:
                            return 2;
                        case ExpressionOp::BitwiseOr:
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

    // ------------------------------------------------------------------------
    // JSON Serialization
    // ------------------------------------------------------------------------

    [[nodiscard]] std::string to_json() const {
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

    // ------------------------------------------------------------------------
    // Algebraic Parser with RawExpression Fallback
    // ------------------------------------------------------------------------

    static ExpressionAstNode parse(std::string_view text) {
        std::string trimmed;
        for (char c : text) {
            if (c != '\r' && c != '\n')
                trimmed += c;
        }
        // Remove trailing semicolon if present
        while (!trimmed.empty() && (std::isspace(static_cast<unsigned char>(trimmed.back())) != 0 || trimmed.back() == ';')) {
            trimmed.pop_back();
        }
        // Trim leading spaces
        size_t start = 0;
        while (start < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[start])) != 0) {
            start++;
        }
        trimmed = trimmed.substr(start);

        if (trimmed.empty()) {
            return make_raw_expression("");
        }

        Parser parser(trimmed);
        auto result = parser.parse_expression();
        if (result.has_value() && parser.is_eof()) {
            return std::move(*result);
        }
        return make_raw_expression(std::string(text));
    }

    // ------------------------------------------------------------------------
    // Comparison
    // ------------------------------------------------------------------------

    bool operator==(const ExpressionAstNode& other) const noexcept {
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

  private:
    struct Parser {
        std::string_view src;
        size_t pos{0};

        explicit Parser(std::string_view s) : src(s) {}

        void skip_ws() {
            while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos])) != 0) {
                pos++;
            }
        }

        [[nodiscard]] bool is_eof() {
            skip_ws();
            return pos >= src.size();
        }

        std::optional<ExpressionAstNode> parse_expression() { return parse_bitwise_or(); }

        std::optional<ExpressionAstNode> parse_bitwise_or() {
            auto left = parse_bitwise_xor();
            if (!left)
                return std::nullopt;
            skip_ws();
            while (pos < src.size() && src[pos] == '|' && (pos + 1 >= src.size() || src[pos + 1] != '|')) {
                pos++;
                auto right = parse_bitwise_xor();
                if (!right)
                    return std::nullopt;
                left = make_binary(ExpressionOp::BitwiseOr, std::move(*left), std::move(*right));
                skip_ws();
            }
            return left;
        }

        std::optional<ExpressionAstNode> parse_bitwise_xor() {
            auto left = parse_bitwise_and();
            if (!left)
                return std::nullopt;
            skip_ws();
            while (pos < src.size() && src[pos] == '^') {
                pos++;
                auto right = parse_bitwise_and();
                if (!right)
                    return std::nullopt;
                left = make_binary(ExpressionOp::BitwiseXor, std::move(*left), std::move(*right));
                skip_ws();
            }
            return left;
        }

        std::optional<ExpressionAstNode> parse_bitwise_and() {
            auto left = parse_shift();
            if (!left)
                return std::nullopt;
            skip_ws();
            while (pos < src.size() && src[pos] == '&' && (pos + 1 >= src.size() || src[pos + 1] != '&')) {
                pos++;
                auto right = parse_shift();
                if (!right)
                    return std::nullopt;
                left = make_binary(ExpressionOp::BitwiseAnd, std::move(*left), std::move(*right));
                skip_ws();
            }
            return left;
        }

        std::optional<ExpressionAstNode> parse_shift() {
            auto left = parse_additive();
            if (!left)
                return std::nullopt;
            skip_ws();
            while (pos + 1 < src.size()) {
                if (src[pos] == '<' && src[pos + 1] == '<') {
                    pos += 2;
                    auto right = parse_additive();
                    if (!right)
                        return std::nullopt;
                    left = make_binary(ExpressionOp::ShiftLeft, std::move(*left), std::move(*right));
                    skip_ws();
                } else if (src[pos] == '>' && src[pos + 1] == '>') {
                    pos += 2;
                    auto right = parse_additive();
                    if (!right)
                        return std::nullopt;
                    left = make_binary(ExpressionOp::ShiftRight, std::move(*left), std::move(*right));
                    skip_ws();
                } else {
                    break;
                }
            }
            return left;
        }

        std::optional<ExpressionAstNode> parse_additive() {
            auto left = parse_multiplicative();
            if (!left)
                return std::nullopt;
            skip_ws();
            while (pos < src.size()) {
                char c = src[pos];
                if (c == '+' || c == '-') {
                    pos++;
                    auto right = parse_multiplicative();
                    if (!right)
                        return std::nullopt;
                    left = make_binary(c == '+' ? ExpressionOp::Add : ExpressionOp::Subtract, std::move(*left),
                                       std::move(*right));
                    skip_ws();
                } else {
                    break;
                }
            }
            return left;
        }

        std::optional<ExpressionAstNode> parse_multiplicative() {
            auto left = parse_unary();
            if (!left)
                return std::nullopt;
            skip_ws();
            while (pos < src.size()) {
                char c = src[pos];
                if (c == '*' || c == '/' || c == '%') {
                    pos++;
                    auto right = parse_unary();
                    if (!right)
                        return std::nullopt;
                    ExpressionOp o = (c == '*') ? ExpressionOp::Multiply
                                                : (c == '/' ? ExpressionOp::Divide : ExpressionOp::Modulo);
                    left = make_binary(o, std::move(*left), std::move(*right));
                    skip_ws();
                } else {
                    break;
                }
            }
            return left;
        }

        std::optional<ExpressionAstNode> parse_unary() {
            skip_ws();
            if (pos >= src.size())
                return std::nullopt;
            char c = src[pos];
            if (c == '-') {
                pos++;
                auto child = parse_unary();
                if (!child)
                    return std::nullopt;
                return make_unary(ExpressionOp::Negate, std::move(*child));
            }
            if (c == '~') {
                pos++;
                auto child = parse_unary();
                if (!child)
                    return std::nullopt;
                return make_unary(ExpressionOp::BitwiseNot, std::move(*child));
            }
            if (c == '!') {
                pos++;
                auto child = parse_unary();
                if (!child)
                    return std::nullopt;
                return make_unary(ExpressionOp::LogicalNot, std::move(*child));
            }
            if (c == '+') {
                pos++;  // unary plus, ignore
                return parse_unary();
            }
            return parse_primary();
        }

        std::optional<ExpressionAstNode> parse_primary() {
            skip_ws();
            if (pos >= src.size())
                return std::nullopt;

            // Parentheses
            if (src[pos] == '(') {
                pos++;
                auto inside = parse_expression();
                skip_ws();
                if (!inside || pos >= src.size() || src[pos] != ')') {
                    return std::nullopt;
                }
                pos++;
                return inside;
            }

            // Numeric literals
            if (std::isdigit(static_cast<unsigned char>(src[pos])) != 0) {
                size_t num_start = pos;
                bool is_float = false;
                while (pos < src.size() && (std::isdigit(static_cast<unsigned char>(src[pos])) != 0 || src[pos] == '.')) {
                    if (src[pos] == '.')
                        is_float = true;
                    pos++;
                }
                // Optional suffix: f, F, u, U, l, L
                while (pos < src.size() && (src[pos] == 'f' || src[pos] == 'F' || src[pos] == 'u' || src[pos] == 'U' ||
                                            src[pos] == 'l' || src[pos] == 'L')) {
                    pos++;
                }
                std::string num_str(src.substr(num_start, pos - num_start));
                // Remove suffix for parsing
                while (!num_str.empty() && (num_str.back() == 'f' || num_str.back() == 'F' || num_str.back() == 'u' ||
                                            num_str.back() == 'U' || num_str.back() == 'l' || num_str.back() == 'L')) {
                    num_str.pop_back();
                }
                try {
                    if (is_float) {
                        return make_float_literal(std::stod(num_str));
                    }
                    return make_int_literal(std::stoll(num_str));
                } catch (...) {
                    return std::nullopt;
                }
            }

            // Identifiers / Keywords / References
            if (std::isalpha(static_cast<unsigned char>(src[pos])) != 0 || src[pos] == '_') {
                size_t id_start = pos;
                while (pos < src.size() && (std::isalnum(static_cast<unsigned char>(src[pos])) != 0 || src[pos] == '_' ||
                                            src[pos] == '.' || src[pos] == ':')) {
                    pos++;
                }
                std::string_view id = src.substr(id_start, pos - id_start);
                if (id == "true") {
                    return make_bool_literal(true);
                }
                if (id == "false") {
                    return make_bool_literal(false);
                }

                // Strip qualifiers like in., out., reg.
                if (id.starts_with("in.")) {
                    return make_port_ref(std::string(id.substr(3)));
                }
                if (id.starts_with("out.")) {
                    return make_port_ref(std::string(id.substr(4)));
                }
                if (id.starts_with("reg.")) {
                    return make_variable_ref(std::string(id.substr(4)));
                }
                if (id.find("::") != std::string_view::npos) {
                    return make_enum_literal(std::string(id));
                }
                if (id.find('.') != std::string_view::npos) {
                    size_t dot_pos = id.find('.');
                    return make_port_ref(std::string(id.substr(0, dot_pos)), std::string(id.substr(dot_pos + 1)));
                }
                return make_variable_ref(std::string(id));
            }

            return std::nullopt;
        }
    };
};

}  // namespace fsm::ir

namespace fsm {
using ExpressionKind = ir::ExpressionKind;
using ExpressionOp = ir::ExpressionOp;
using ExpressionAstNode = ir::ExpressionAstNode;
using ir::expression_kind_to_string;
using ir::expression_op_to_string;
using ir::string_to_expression_op;
}  // namespace fsm

