#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "parser_interface.hpp"

namespace fsm::codegen {

struct ParsedGuardResult {
    std::string cpp_type;
    std::vector<std::string> atomic_guards;
};

class GuardExpressionParser {
  public:
    static ParsedGuardResult parse(std::string_view raw_expr) {
        std::string_view expr = trim(raw_expr);
        if (expr.empty()) {
            return {"", {}};
        }

        // Fast path: if no logical operators, it's a simple identifier
        if (expr.find('&') == std::string_view::npos && expr.find('|') == std::string_view::npos &&
            expr.find('!') == std::string_view::npos && expr.find('(') == std::string_view::npos &&
            expr.find(')') == std::string_view::npos) {
            std::string ident = sanitize_identifier(expr);
            if (ident.empty()) {
                return {"", {}};
            }
            return {ident, {ident}};
        }

        // Full boolean expression parser
        GuardExpressionParser parser(expr);
        return parser.parse_expression();
    }

  private:
    enum class TokenType { Ident, And, Or, Not, LParen, RParen, End };

    struct Token {
        TokenType type;
        std::string value;
    };

    explicit GuardExpressionParser(std::string_view src) : src_(src), pos_(0) { tokenize(); }

    void tokenize() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (std::isspace(static_cast<unsigned char>(c))) {
                ++pos_;
                continue;
            }
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
            if (c == '!') {
                tokens_.push_back({TokenType::Not, "!"});
                ++pos_;
                continue;
            }
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

            // Identifier
            size_t start = pos_;
            while (pos_ < src_.size()) {
                char ch = src_[pos_];
                if (std::isspace(static_cast<unsigned char>(ch)) || ch == '&' || ch == '|' || ch == '!' || ch == '(' ||
                    ch == ')') {
                    break;
                }
                ++pos_;
            }
            std::string raw_ident(src_.substr(start, pos_ - start));
            std::string ident = sanitize_identifier(raw_ident);
            if (!ident.empty()) {
                tokens_.push_back({TokenType::Ident, ident});
            }
        }
        tokens_.push_back({TokenType::End, ""});
    }

    ParsedGuardResult parse_expression() {
        token_idx_ = 0;
        std::vector<std::string> atomic_guards;
        std::string cpp_type = parse_or(atomic_guards);
        if (cpp_type.empty()) {
            std::string fallback = sanitize_identifier(src_);
            return {fallback, {fallback}};
        }
        return {cpp_type, atomic_guards};
    }

    std::string parse_or(std::vector<std::string>& atomic) {
        std::string left = parse_and(atomic);
        while (current().type == TokenType::Or) {
            advance();
            std::string right = parse_and(atomic);
            left = "fsm::or_<" + left + ", " + right + ">";
        }
        return left;
    }

    std::string parse_and(std::vector<std::string>& atomic) {
        std::string left = parse_unary(atomic);
        while (current().type == TokenType::And) {
            advance();
            std::string right = parse_unary(atomic);
            left = "fsm::and_<" + left + ", " + right + ">";
        }
        return left;
    }

    std::string parse_unary(std::vector<std::string>& atomic) {
        if (current().type == TokenType::Not) {
            advance();
            std::string sub = parse_unary(atomic);
            return "fsm::not_<" + sub + ">";
        }
        return parse_primary(atomic);
    }

    std::string parse_primary(std::vector<std::string>& atomic) {
        if (current().type == TokenType::LParen) {
            advance();
            std::string inner = parse_or(atomic);
            if (current().type == TokenType::RParen) {
                advance();
            }
            return inner;
        }
        if (current().type == TokenType::Ident) {
            std::string ident = current().value;
            advance();
            atomic.push_back(ident);
            return ident;
        }
        return "";
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
