#include <algorithm>
#include <cctype>
#include <cstdint>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/ir/guard.hpp"
#include "fsm/ir/identifier_utils.hpp"

namespace fsm::ir {

namespace {

constexpr std::size_t kMaxGuardRecursionDepth = 128;

bool starts_with_sv(std::string_view s, std::string_view prefix) noexcept {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

std::string_view trim_sv(std::string_view s) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
        s.remove_suffix(1);
    }
    return s;
}

enum class TokenType : std::uint8_t { Ident, And, Or, Not, LParen, RParen, End };

struct Token {
    TokenType type;
    std::string value;
    std::string raw_expr;
};

class GuardTokenizerAndParser {
  public:
    explicit GuardTokenizerAndParser(std::string_view src) : src_(src) { tokenize(); }

    ParsedGuardResult parse_expression() {
        token_idx_ = 0;
        error_ = false;
        std::vector<std::string> atomic_guards;
        std::vector<AtomicGuardInfo> atomic_details;
        auto ast = parse_or(atomic_guards, atomic_details, 0);
        if (error_ || !ast.has_value()) {
            std::string fallback = sanitize_identifier(src_);
            if (fallback.empty()) {
                return {"", {}, {}, std::nullopt};
            }
            return {fallback, {fallback}, {{fallback, std::string(src_)}}, GuardAstNode(fallback)};
        }
        std::string canonical = ast->to_string();
        return {std::move(canonical), std::move(atomic_guards), std::move(atomic_details), std::move(ast)};
    }

  private:
    void tokenize() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (std::isspace(static_cast<unsigned char>(c)) != 0) {
                ++pos_;
                continue;
            }
            if (c == '&') {
                if (pos_ + 1 < src_.size() && src_[pos_ + 1] == '&') {
                    tokens_.push_back({TokenType::And, "&&", "&&"});
                    pos_ += 2;
                } else {
                    tokens_.push_back({TokenType::And, "&", "&"});
                    pos_ += 1;
                }
                continue;
            }
            if (c == '|') {
                if (pos_ + 1 < src_.size() && src_[pos_ + 1] == '|') {
                    tokens_.push_back({TokenType::Or, "||", "||"});
                    pos_ += 2;
                } else {
                    tokens_.push_back({TokenType::Or, "|", "|"});
                    pos_ += 1;
                }
                continue;
            }
            if (c == '!') {
                tokens_.push_back({TokenType::Not, "!", "!"});
                ++pos_;
                continue;
            }
            if (c == '(') {
                tokens_.push_back({TokenType::LParen, "(", "("});
                ++pos_;
                continue;
            }
            if (c == ')') {
                tokens_.push_back({TokenType::RParen, ")", ")"});
                ++pos_;
                continue;
            }

            // Identifier or comparison expression (consumes until logical delimiter or unnested paren)
            size_t start = pos_;
            int paren_depth = 0;
            char in_quote = '\0';
            while (pos_ < src_.size()) {
                char ch = src_[pos_];
                if (in_quote != '\0') {
                    if (ch == '\\' && pos_ + 1 < src_.size()) {
                        pos_ += 2;
                        continue;
                    }
                    if (ch == in_quote) {
                        in_quote = '\0';
                    }
                    ++pos_;
                    continue;
                }
                if (ch == '"' || ch == '\'') {
                    in_quote = ch;
                    ++pos_;
                    continue;
                }
                if (ch == '(') {
                    if (pos_ == start) {
                        break;
                    }
                    paren_depth++;
                } else if (ch == ')') {
                    if (paren_depth == 0) {
                        break;
                    }
                    paren_depth--;
                } else if (paren_depth == 0) {
                    if (ch == '&' || ch == '|') {
                        break;
                    }
                }
                ++pos_;
            }
            std::string raw_ident = std::string(trim_sv(src_.substr(start, pos_ - start)));
            std::string ident = sanitize_identifier(raw_ident);
            if (!ident.empty()) {
                tokens_.push_back({TokenType::Ident, ident, raw_ident});
            }
        }
        tokens_.push_back({TokenType::End, "", ""});
    }

    std::optional<GuardAstNode> parse_or(std::vector<std::string>& atomic, std::vector<AtomicGuardInfo>& atomic_details,
                                         std::size_t depth) {
        if (depth > kMaxGuardRecursionDepth) {
            error_ = true;
            return std::nullopt;
        }
        auto left = parse_and(atomic, atomic_details, depth + 1);
        while (left.has_value() && current().type == TokenType::Or) {
            advance();
            auto right = parse_and(atomic, atomic_details, depth + 1);
            if (!right.has_value()) {
                error_ = true;
                return std::nullopt;
            }
            left = GuardAstNode(GuardOp::Or, {std::move(*left), std::move(*right)});
        }
        return left;
    }

    std::optional<GuardAstNode> parse_and(std::vector<std::string>& atomic,
                                          std::vector<AtomicGuardInfo>& atomic_details, std::size_t depth) {
        if (depth > kMaxGuardRecursionDepth) {
            error_ = true;
            return std::nullopt;
        }
        auto left = parse_unary(atomic, atomic_details, depth + 1);
        while (left.has_value() && current().type == TokenType::And) {
            advance();
            auto right = parse_unary(atomic, atomic_details, depth + 1);
            if (!right.has_value()) {
                error_ = true;
                return std::nullopt;
            }
            left = GuardAstNode(GuardOp::And, {std::move(*left), std::move(*right)});
        }
        return left;
    }

    std::optional<GuardAstNode> parse_unary(std::vector<std::string>& atomic,
                                            std::vector<AtomicGuardInfo>& atomic_details, std::size_t depth) {
        if (depth > kMaxGuardRecursionDepth) {
            error_ = true;
            return std::nullopt;
        }
        if (current().type == TokenType::Not) {
            advance();
            auto sub = parse_unary(atomic, atomic_details, depth + 1);
            if (!sub.has_value()) {
                error_ = true;
                return std::nullopt;
            }
            return GuardAstNode(GuardOp::Not, {std::move(*sub)});
        }
        return parse_primary(atomic, atomic_details, depth + 1);
    }

    std::optional<GuardAstNode> parse_primary(std::vector<std::string>& atomic,
                                              std::vector<AtomicGuardInfo>& atomic_details, std::size_t depth) {
        if (depth > kMaxGuardRecursionDepth) {
            error_ = true;
            return std::nullopt;
        }
        if (current().type == TokenType::LParen) {
            advance();
            auto inner = parse_or(atomic, atomic_details, depth + 1);
            if (current().type == TokenType::RParen) {
                advance();
            }
            return inner;
        }
        if (current().type == TokenType::Ident) {
            std::string ident = current().value;
            std::string raw = current().raw_expr;
            advance();
            atomic.push_back(ident);
            atomic_details.push_back({ident, raw});
            return GuardAstNode(ident);
        }
        return std::nullopt;
    }

    [[nodiscard]] const Token& current() const {
        if (token_idx_ < tokens_.size()) {
            return tokens_[token_idx_];
        }
        static const Token end_tok{TokenType::End, "", ""};
        return end_tok;
    }

    void advance() {
        if (token_idx_ < tokens_.size()) {
            ++token_idx_;
        }
    }

    std::string_view src_;
    size_t pos_{0};
    std::vector<Token> tokens_;
    size_t token_idx_{0};
    bool error_{false};
};

std::string render_ast_with_guards(const GuardAstNode& node, const std::vector<GuardModel>& guards) {
    auto resolve_leaf = [&](const std::string& name) -> std::string {
        if (guards.empty()) {
            return name;
        }
        auto it = std::find_if(guards.begin(), guards.end(), [&](const GuardModel& gm) { return gm.name == name; });
        if (it != guards.end() && it->raw_expression.has_value() && !it->raw_expression->empty()) {
            return *it->raw_expression;
        }
        return name;
    };

    if (node.op == GuardOp::None) {
        return resolve_leaf(node.expression);
    }
    if (node.op == GuardOp::Not) {
        if (!node.children.empty()) {
            const std::string inner = render_ast_with_guards(node.children[0], guards);
            return (node.children[0].op != GuardOp::None) ? ("!(" + inner + ")") : ("!" + inner);
        }
        return "!" + resolve_leaf(node.expression);
    }
    if (node.op == GuardOp::And || node.op == GuardOp::Or) {
        const std::string op_str = (node.op == GuardOp::And) ? " && " : " || ";
        std::string result;
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            if (i > 0) {
                result += op_str;
            }
            const bool needs_parens = (node.children[i].op == GuardOp::Or && node.op == GuardOp::And);
            if (needs_parens) {
                result += "(";
            }
            result += render_ast_with_guards(node.children[i], guards);
            if (needs_parens) {
                result += ")";
            }
        }
        return result;
    }
    return resolve_leaf(node.expression);
}

std::string to_diagram_string_impl(std::string_view raw_expr, const std::vector<GuardModel>& guards,
                                   std::size_t depth) {
    if (depth > kMaxGuardRecursionDepth) {
        return std::string(trim_sv(raw_expr));
    }
    std::string expr = std::string(trim_sv(raw_expr));
    if (expr.empty()) {
        return "";
    }

    auto resolve_leaf = [&](const std::string& name) -> std::string {
        if (guards.empty()) {
            return name;
        }
        auto it = std::find_if(guards.begin(), guards.end(), [&](const GuardModel& gm) { return gm.name == name; });
        if (it != guards.end() && it->raw_expression.has_value() && !it->raw_expression->empty()) {
            return *it->raw_expression;
        }
        return name;
    };

    // Decode legacy C++ combinator strings if encountered (e.g. external/legacy JSON)
    if (expr.find("fsm::") != std::string::npos) {
        if (starts_with_sv(expr, "::fsm::")) {
            expr = expr.substr(2);
        }
        if (starts_with_sv(expr, "fsm::not_<") && expr.back() == '>') {
            std::string_view inner = expr;
            inner.remove_prefix(10);
            inner.remove_suffix(1);
            std::string inner_str = to_diagram_string_impl(inner, guards, depth + 1);
            if (starts_with_sv(trim_sv(inner), "fsm::and_<") || starts_with_sv(trim_sv(inner), "fsm::or_<")) {
                return "!(" + inner_str + ")";
            }
            return "!" + inner_str;
        }
        if (starts_with_sv(expr, "fsm::and_<") && expr.back() == '>') {
            std::string_view inner = expr;
            inner.remove_prefix(10);
            inner.remove_suffix(1);
            size_t angle_depth = 0;
            size_t comma_pos = std::string_view::npos;
            for (size_t i = 0; i < inner.size(); ++i) {
                if (inner[i] == '<') {
                    angle_depth++;
                } else if (inner[i] == '>' && angle_depth > 0) {
                    angle_depth--;
                } else if (inner[i] == ',' && angle_depth == 0) {
                    comma_pos = i;
                    break;
                }
            }
            if (comma_pos != std::string_view::npos) {
                std::string_view left_raw = inner.substr(0, comma_pos);
                std::string_view right_raw = inner.substr(comma_pos + 1);
                std::string left = to_diagram_string_impl(left_raw, guards, depth + 1);
                std::string right = to_diagram_string_impl(right_raw, guards, depth + 1);
                if (starts_with_sv(trim_sv(left_raw), "fsm::or_<")) {
                    left = "(" + left + ")";
                }
                if (starts_with_sv(trim_sv(right_raw), "fsm::or_<")) {
                    right = "(" + right + ")";
                }
                return left + " && " + right;
            }
        }
        if (starts_with_sv(expr, "fsm::or_<") && expr.back() == '>') {
            std::string_view inner = expr;
            inner.remove_prefix(9);
            inner.remove_suffix(1);
            size_t angle_depth = 0;
            size_t comma_pos = std::string_view::npos;
            for (size_t i = 0; i < inner.size(); ++i) {
                if (inner[i] == '<') {
                    angle_depth++;
                } else if (inner[i] == '>' && angle_depth > 0) {
                    angle_depth--;
                } else if (inner[i] == ',' && angle_depth == 0) {
                    comma_pos = i;
                    break;
                }
            }
            if (comma_pos != std::string_view::npos) {
                std::string left = to_diagram_string_impl(inner.substr(0, comma_pos), guards, depth + 1);
                std::string right = to_diagram_string_impl(inner.substr(comma_pos + 1), guards, depth + 1);
                return left + " || " + right;
            }
        }
        return resolve_leaf(expr);
    }

    // Resolve atomic guard names inside target-agnostic infix boolean expressions when guard table is provided
    if (!guards.empty() && (expr.find("&&") != std::string::npos || expr.find("||") != std::string::npos ||
                            expr.find('!') != std::string::npos)) {
        GuardTokenizerAndParser parser(expr);
        auto parsed = parser.parse_expression();
        if (parsed.ast.has_value()) {
            return render_ast_with_guards(*parsed.ast, guards);
        }
    }

    return resolve_leaf(expr);
}

}  // namespace

std::string GuardExpressionParser::to_diagram_string(std::string_view raw_expr) {
    return to_diagram_string_impl(raw_expr, {}, 0);
}

std::string GuardExpressionParser::to_diagram_string(std::string_view raw_expr,
                                                     const std::vector<ir::GuardModel>& guards) {
    return to_diagram_string_impl(raw_expr, guards, 0);
}

ParsedGuardResult GuardExpressionParser::parse(std::string_view raw_expr) {
    std::string expr = to_diagram_string(raw_expr);
    if (expr.empty()) {
        return {"", {}, {}, std::nullopt};
    }
    if (expr == "else" || expr == "otherwise" || expr == "default") {
        return {expr, {}, {}, GuardAstNode(expr)};
    }

    // Normalize textual boolean keywords
    static const std::regex not_re(R"(\bnot\b|\bNOT\b)", std::regex::optimize);
    static const std::regex and_re(R"(\band\b|\bAND\b)", std::regex::optimize);
    static const std::regex or_re(R"(\bor\b|\bOR\b)", std::regex::optimize);
    expr = std::regex_replace(expr, not_re, "!");
    expr = std::regex_replace(expr, and_re, "&&");
    expr = std::regex_replace(expr, or_re, "||");

    // Fast path: if no logical operators, it's a simple identifier
    if (expr.find('&') == std::string_view::npos && expr.find('|') == std::string_view::npos &&
        expr.find('!') == std::string_view::npos && expr.find('(') == std::string_view::npos &&
        expr.find(')') == std::string_view::npos) {
        std::string ident = sanitize_identifier(expr);
        if (ident.empty()) {
            return {"", {}, {}, std::nullopt};
        }
        return {ident, {ident}, {{ident, expr}}, GuardAstNode(ident)};
    }

    // Full boolean expression parser
    GuardTokenizerAndParser parser(expr);
    return parser.parse_expression();
}

}  // namespace fsm::ir
