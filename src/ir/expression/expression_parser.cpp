#include <cctype>
#include <cmath>
#include <limits>

#include "fsm/ir/expression.hpp"

namespace fsm::ir {

namespace {

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

    [[nodiscard]] bool match_keyword_op(std::string_view kw) {
        skip_ws();
        if (pos + kw.size() > src.size() || src.substr(pos, kw.size()) != kw) {
            return false;
        }
        if (pos + kw.size() < src.size()) {
            char next_c = src[pos + kw.size()];
            if (std::isalnum(static_cast<unsigned char>(next_c)) != 0 || next_c == '_' || next_c == '.' ||
                next_c == ':') {
                return false;
            }
        }
        pos += kw.size();
        return true;
    }

    static constexpr std::size_t kMaxExprDepth = 128;
    std::size_t depth = 0;

    struct DepthGuard {
        std::size_t& d;
        explicit DepthGuard(std::size_t& ref) noexcept : d(ref) { ++d; }
        ~DepthGuard() noexcept { --d; }
    };

    std::optional<ExpressionAstNode> parse_expression() {
        if (depth >= kMaxExprDepth) {
            return std::nullopt;
        }
        DepthGuard guard(depth);
        return parse_logical_or();
    }

    std::optional<ExpressionAstNode> parse_logical_or() {
        auto left = parse_logical_and();
        if (!left)
            return std::nullopt;
        skip_ws();
        while (pos < src.size()) {
            if (pos + 1 < src.size() && src[pos] == '|' && src[pos + 1] == '|') {
                pos += 2;
            } else if (!match_keyword_op("or") && !match_keyword_op("OR")) {
                break;
            }
            auto right = parse_logical_and();
            if (!right)
                return std::nullopt;
            left = ExpressionAstNode::make_binary(ExpressionOp::LogicalOr, std::move(*left), std::move(*right));
            skip_ws();
        }
        return left;
    }

    std::optional<ExpressionAstNode> parse_logical_and() {
        auto left = parse_bitwise_or();
        if (!left)
            return std::nullopt;
        skip_ws();
        while (pos < src.size()) {
            if (pos + 1 < src.size() && src[pos] == '&' && src[pos + 1] == '&') {
                pos += 2;
            } else if (!match_keyword_op("and") && !match_keyword_op("AND")) {
                break;
            }
            auto right = parse_bitwise_or();
            if (!right)
                return std::nullopt;
            left = ExpressionAstNode::make_binary(ExpressionOp::LogicalAnd, std::move(*left), std::move(*right));
            skip_ws();
        }
        return left;
    }

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
            left = ExpressionAstNode::make_binary(ExpressionOp::BitwiseOr, std::move(*left), std::move(*right));
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
            left = ExpressionAstNode::make_binary(ExpressionOp::BitwiseXor, std::move(*left), std::move(*right));
            skip_ws();
        }
        return left;
    }

    std::optional<ExpressionAstNode> parse_bitwise_and() {
        auto left = parse_equality();
        if (!left)
            return std::nullopt;
        skip_ws();
        while (pos < src.size() && src[pos] == '&' && (pos + 1 >= src.size() || src[pos + 1] != '&')) {
            pos++;
            auto right = parse_equality();
            if (!right)
                return std::nullopt;
            left = ExpressionAstNode::make_binary(ExpressionOp::BitwiseAnd, std::move(*left), std::move(*right));
            skip_ws();
        }
        return left;
    }

    std::optional<ExpressionAstNode> parse_equality() {
        auto left = parse_relational();
        if (!left)
            return std::nullopt;
        skip_ws();
        while (pos + 1 < src.size()) {
            ExpressionOp op = ExpressionOp::None;
            if (src[pos] == '=' && src[pos + 1] == '=') {
                op = ExpressionOp::Equal;
            } else if (src[pos] == '!' && src[pos + 1] == '=') {
                op = ExpressionOp::NotEqual;
            } else {
                break;
            }
            pos += 2;
            auto right = parse_relational();
            if (!right)
                return std::nullopt;
            left = ExpressionAstNode::make_binary(op, std::move(*left), std::move(*right));
            skip_ws();
        }
        return left;
    }

    std::optional<ExpressionAstNode> parse_relational() {
        auto left = parse_shift();
        if (!left)
            return std::nullopt;
        skip_ws();
        while (pos < src.size()) {
            ExpressionOp op = ExpressionOp::None;
            if (pos + 1 < src.size() && src[pos] == '<' && src[pos + 1] == '=') {
                op = ExpressionOp::LessEqual;
                pos += 2;
            } else if (pos + 1 < src.size() && src[pos] == '>' && src[pos + 1] == '=') {
                op = ExpressionOp::GreaterEqual;
                pos += 2;
            } else if (src[pos] == '<' && (pos + 1 >= src.size() || src[pos + 1] != '<')) {
                op = ExpressionOp::LessThan;
                pos += 1;
            } else if (src[pos] == '>' && (pos + 1 >= src.size() || src[pos + 1] != '>')) {
                op = ExpressionOp::GreaterThan;
                pos += 1;
            } else {
                break;
            }
            auto right = parse_shift();
            if (!right)
                return std::nullopt;
            left = ExpressionAstNode::make_binary(op, std::move(*left), std::move(*right));
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
                left = ExpressionAstNode::make_binary(ExpressionOp::ShiftLeft, std::move(*left), std::move(*right));
                skip_ws();
            } else if (src[pos] == '>' && src[pos + 1] == '>') {
                pos += 2;
                auto right = parse_additive();
                if (!right)
                    return std::nullopt;
                left = ExpressionAstNode::make_binary(ExpressionOp::ShiftRight, std::move(*left), std::move(*right));
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
                left = ExpressionAstNode::make_binary(c == '+' ? ExpressionOp::Add : ExpressionOp::Subtract,
                                                      std::move(*left), std::move(*right));
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
                ExpressionOp o =
                    (c == '*') ? ExpressionOp::Multiply : (c == '/' ? ExpressionOp::Divide : ExpressionOp::Modulo);
                left = ExpressionAstNode::make_binary(o, std::move(*left), std::move(*right));
                skip_ws();
            } else {
                break;
            }
        }
        return left;
    }

    std::optional<ExpressionAstNode> parse_unary() {
        if (depth >= kMaxExprDepth) {
            return std::nullopt;
        }
        DepthGuard guard(depth);
        skip_ws();
        if (pos >= src.size())
            return std::nullopt;
        char c = src[pos];
        if (c == '-') {
            pos++;
            auto child = parse_unary();
            if (!child)
                return std::nullopt;
            return ExpressionAstNode::make_unary(ExpressionOp::Negate, std::move(*child));
        }
        if (c == '~') {
            pos++;
            auto child = parse_unary();
            if (!child)
                return std::nullopt;
            return ExpressionAstNode::make_unary(ExpressionOp::BitwiseNot, std::move(*child));
        }
        if (c == '!') {
            pos++;
            auto child = parse_unary();
            if (!child)
                return std::nullopt;
            return ExpressionAstNode::make_unary(ExpressionOp::LogicalNot, std::move(*child));
        }
        if (match_keyword_op("not") || match_keyword_op("NOT")) {
            auto child = parse_unary();
            if (!child)
                return std::nullopt;
            return ExpressionAstNode::make_unary(ExpressionOp::LogicalNot, std::move(*child));
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
                    return ExpressionAstNode::make_float_literal(std::stod(num_str));
                }
                return ExpressionAstNode::make_int_literal(std::stoll(num_str));
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
                return ExpressionAstNode::make_bool_literal(true);
            }
            if (id == "false") {
                return ExpressionAstNode::make_bool_literal(false);
            }

            // Strip qualifiers like in., out., reg.
            if (id.starts_with("in.")) {
                return ExpressionAstNode::make_port_ref(std::string(id.substr(3)));
            }
            if (id.starts_with("out.")) {
                return ExpressionAstNode::make_port_ref(std::string(id.substr(4)));
            }
            if (id.starts_with("reg.")) {
                return ExpressionAstNode::make_variable_ref(std::string(id.substr(4)));
            }
            if (id.starts_with("payload.") || id.starts_with("cmd.") || id.starts_with("event.") ||
                id.starts_with("sig.") || id.starts_with("ev.")) {
                size_t dot_pos = id.find('.');
                return ExpressionAstNode::make_event_param_ref("", std::string(id.substr(dot_pos + 1)));
            }
            if (id.find("::") != std::string_view::npos) {
                return ExpressionAstNode::make_enum_literal(std::string(id));
            }
            if (id.find('.') != std::string_view::npos) {
                size_t dot_pos = id.find('.');
                return ExpressionAstNode::make_port_ref(std::string(id.substr(0, dot_pos)),
                                                        std::string(id.substr(dot_pos + 1)));
            }
            return ExpressionAstNode::make_variable_ref(std::string(id));
        }

        return std::nullopt;
    }
};

}  // namespace

ExpressionAstNode ExpressionAstNode::parse(std::string_view text) {
    std::string trimmed;
    for (char c : text) {
        if (c != '\r' && c != '\n')
            trimmed += c;
    }
    // Remove trailing semicolon if present
    while (!trimmed.empty() &&
           (std::isspace(static_cast<unsigned char>(trimmed.back())) != 0 || trimmed.back() == ';')) {
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

}  // namespace fsm::ir
