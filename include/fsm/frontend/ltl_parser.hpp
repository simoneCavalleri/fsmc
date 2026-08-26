#pragma once

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/frontend/parser_interface.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

/**
 * @brief Tokenizer and Recursive-Descent Parser for Linear Temporal Logic (LTL)
 * and Computation Tree Logic (CTL) property formulas.
 *
 * Supports operators:
 * - Unary Temporal: G (Globally/[]), F (Finally/<>), X (Next/())
 * - Unary Boolean:  ! (Not)
 * - Binary Temporal: U (Until), R (Release)
 * - Binary Boolean:  && (And), || (Or), -> (Implies), <-> (Equivalent)
 * - Atomic propositions: Identifiers and State predicates (e.g. "InFlight", "BatteryLow", "state == Alarm")
 */
class LtlPropertyParser {
  public:
    static std::optional<PropertyAstNode> parse(std::string_view raw_formula) {
        std::string expr = std::string(trim(raw_formula));
        if (expr.empty()) {
            return std::nullopt;
        }

        LtlPropertyParser parser(expr);
        return parser.parse_formula();
    }

  private:
    enum class TokenType : std::uint8_t {
        Atom,
        Globally,    // G or []
        Finally,     // F or <>
        Next,        // X
        Until,       // U
        Release,     // R
        Implies,     // -> or =>
        Equivalent,  // <-> or <=>
        And,         // && or & or AND
        Or,          // || or | or OR
        Not,         // ! or NOT
        LParen,      // (
        RParen,      // )
        End
    };

    struct Token {
        TokenType type;
        std::string value;
    };

    explicit LtlPropertyParser(std::string_view src) : src_(src) { tokenize(); }

    void tokenize() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (std::isspace(static_cast<unsigned char>(c)) != 0) {
                ++pos_;
                continue;
            }

            // G (Globally) or []
            if (c == '[' && pos_ + 1 < src_.size() && src_[pos_ + 1] == ']') {
                tokens_.push_back({TokenType::Globally, "G"});
                pos_ += 2;
                continue;
            }
            if (c == 'G' && (pos_ + 1 >= src_.size() || is_delim(src_[pos_ + 1]))) {
                tokens_.push_back({TokenType::Globally, "G"});
                ++pos_;
                continue;
            }

            // F (Finally) or <>
            if (c == '<' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '>') {
                tokens_.push_back({TokenType::Finally, "F"});
                pos_ += 2;
                continue;
            }
            if (c == 'F' && (pos_ + 1 >= src_.size() || is_delim(src_[pos_ + 1]))) {
                tokens_.push_back({TokenType::Finally, "F"});
                ++pos_;
                continue;
            }

            // X (Next)
            if (c == 'X' && (pos_ + 1 >= src_.size() || is_delim(src_[pos_ + 1]))) {
                tokens_.push_back({TokenType::Next, "X"});
                ++pos_;
                continue;
            }

            // U (Until)
            if (c == 'U' && (pos_ + 1 >= src_.size() || is_delim(src_[pos_ + 1]))) {
                tokens_.push_back({TokenType::Until, "U"});
                ++pos_;
                continue;
            }

            // R (Release)
            if (c == 'R' && (pos_ + 1 >= src_.size() || is_delim(src_[pos_ + 1]))) {
                tokens_.push_back({TokenType::Release, "R"});
                ++pos_;
                continue;
            }

            // Equivalent: <-> or <=>
            if (pos_ + 2 < src_.size() && ((src_[pos_] == '<' && src_[pos_ + 1] == '-' && src_[pos_ + 2] == '>') ||
                                           (src_[pos_] == '<' && src_[pos_ + 1] == '=' && src_[pos_ + 2] == '>'))) {
                tokens_.push_back({TokenType::Equivalent, "<->"});
                pos_ += 3;
                continue;
            }

            // Implies: -> or =>
            if (pos_ + 1 < src_.size() &&
                ((src_[pos_] == '-' && src_[pos_ + 1] == '>') || (src_[pos_] == '=' && src_[pos_ + 1] == '>'))) {
                tokens_.push_back({TokenType::Implies, "->"});
                pos_ += 2;
                continue;
            }

            // And: && or &
            if (c == '&') {
                if (pos_ + 1 < src_.size() && src_[pos_ + 1] == '&') {
                    tokens_.push_back({TokenType::And, "&&"});
                    pos_ += 2;
                } else {
                    tokens_.push_back({TokenType::And, "&"});
                    pos_ += 1;
                }
                continue;
            }

            // Or: || or |
            if (c == '|') {
                if (pos_ + 1 < src_.size() && src_[pos_ + 1] == '|') {
                    tokens_.push_back({TokenType::Or, "||"});
                    pos_ += 2;
                } else {
                    tokens_.push_back({TokenType::Or, "|"});
                    pos_ += 1;
                }
                continue;
            }

            // Not: !
            if (c == '!') {
                tokens_.push_back({TokenType::Not, "!"});
                ++pos_;
                continue;
            }

            // Parentheses
            if (c == '(') {
                tokens_.push_back({TokenType::LParen, "("});
                ++pos_;
                continue;
            }
            if (c == ')') {
                tokens_.push_back({TokenType::RParen, ")"});
                ++pos_;
                continue;
            }

            // Identifier or Predicate Expression (Atom)
            size_t start = pos_;
            while (pos_ < src_.size()) {
                char ch = src_[pos_];
                if (std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == '(' || ch == ')' || ch == '!' ||
                    ch == '&' || ch == '|' || ch == '<' || ch == '>' || ch == '=' || ch == '-') {
                    // Check if it might be part of an operator or delimiter
                    if (ch == '-' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '>') {
                        break;
                    }
                    if (ch == '=' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '>') {
                        break;
                    }
                    if (ch == '<' && pos_ + 1 < src_.size() && (src_[pos_ + 1] == '-' || src_[pos_ + 1] == '=')) {
                        break;
                    }
                    if (ch == '(' || ch == ')' || ch == '!' || ch == '&' || ch == '|' ||
                        std::isspace(static_cast<unsigned char>(ch)) != 0) {
                        break;
                    }
                }
                ++pos_;
            }

            std::string raw_word(src_.substr(start, pos_ - start));
            std::string word = std::string(trim(raw_word));
            if (word == "AND" || word == "and") {
                tokens_.push_back({TokenType::And, "&&"});
            } else if (word == "OR" || word == "or") {
                tokens_.push_back({TokenType::Or, "||"});
            } else if (word == "NOT" || word == "not") {
                tokens_.push_back({TokenType::Not, "!"});
            } else if (!word.empty()) {
                tokens_.push_back({TokenType::Atom, word});
            }
        }
        tokens_.push_back({TokenType::End, ""});
    }

    static bool is_delim(char c) {
        return (std::isspace(static_cast<unsigned char>(c)) != 0) || c == '(' || c == ')' || c == '!' || c == '&' ||
               c == '|' || c == '-' || c == '=' || c == '<' || c == '>';
    }

    std::optional<PropertyAstNode> parse_formula() {
        token_idx_ = 0;
        auto node = parse_equivalence();
        if (current().type != TokenType::End) {
            // Unconsumed tokens, but return what we have if non-empty
        }
        return node;
    }

    // Equivalence: expr <-> expr
    PropertyAstNode parse_equivalence() {
        PropertyAstNode left = parse_implication();
        while (current().type == TokenType::Equivalent) {
            advance();
            PropertyAstNode right = parse_implication();
            left = PropertyAstNode(TemporalOp::Equivalent, {std::move(left), std::move(right)});
        }
        return left;
    }

    // Implication: expr -> expr (Right-associative)
    PropertyAstNode parse_implication() {
        PropertyAstNode left = parse_or();
        if (current().type == TokenType::Implies) {
            advance();
            PropertyAstNode right = parse_implication();  // right-associative
            return PropertyAstNode(TemporalOp::Implies, {std::move(left), std::move(right)});
        }
        return left;
    }

    // Logical OR: expr || expr
    PropertyAstNode parse_or() {
        PropertyAstNode left = parse_and();
        while (current().type == TokenType::Or) {
            advance();
            PropertyAstNode right = parse_and();
            left = PropertyAstNode(TemporalOp::Or, {std::move(left), std::move(right)});
        }
        return left;
    }

    // Logical AND: expr && expr
    PropertyAstNode parse_and() {
        PropertyAstNode left = parse_until_release();
        while (current().type == TokenType::And) {
            advance();
            PropertyAstNode right = parse_until_release();
            left = PropertyAstNode(TemporalOp::And, {std::move(left), std::move(right)});
        }
        return left;
    }

    // Until & Release: expr U expr, expr R expr
    PropertyAstNode parse_until_release() {
        PropertyAstNode left = parse_unary();
        while (current().type == TokenType::Until || current().type == TokenType::Release) {
            TokenType t = current().type;
            advance();
            PropertyAstNode right = parse_unary();
            TemporalOp op = (t == TokenType::Until) ? TemporalOp::Until : TemporalOp::Release;
            left = PropertyAstNode(op, {std::move(left), std::move(right)});
        }
        return left;
    }

    // Unary Operators: G expr, F expr, X expr, ! expr
    PropertyAstNode parse_unary() {
        if (current().type == TokenType::Globally) {
            advance();
            PropertyAstNode sub = parse_unary();
            return PropertyAstNode(TemporalOp::Globally, {std::move(sub)});
        }
        if (current().type == TokenType::Finally) {
            advance();
            PropertyAstNode sub = parse_unary();
            return PropertyAstNode(TemporalOp::Finally, {std::move(sub)});
        }
        if (current().type == TokenType::Next) {
            advance();
            PropertyAstNode sub = parse_unary();
            return PropertyAstNode(TemporalOp::Next, {std::move(sub)});
        }
        if (current().type == TokenType::Not) {
            advance();
            PropertyAstNode sub = parse_unary();
            return PropertyAstNode(TemporalOp::Not, {std::move(sub)});
        }
        return parse_primary();
    }

    // Primary: ( expr ) or Atom
    PropertyAstNode parse_primary() {
        if (current().type == TokenType::LParen) {
            advance();
            PropertyAstNode inner = parse_equivalence();
            if (current().type == TokenType::RParen) {
                advance();
            }
            return inner;
        }
        if (current().type == TokenType::Atom) {
            std::string atom = current().value;
            advance();
            return PropertyAstNode(atom);
        }
        return PropertyAstNode("");
    }

    [[nodiscard]] const Token& current() const {
        if (token_idx_ < tokens_.size()) {
            return tokens_[token_idx_];
        }
        static const Token end_tok{TokenType::End, ""};
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
};

}  // namespace fsm::codegen

namespace fsm {
using LtlPropertyParser = ::fsm::codegen::LtlPropertyParser;
}  // namespace fsm
