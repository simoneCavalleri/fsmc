#include "fsm/frontend/formal/sysml2_block_scanner.hpp"

#include <cctype>

namespace fsm::frontend::formal {

std::string Sysml2BlockScanner::trim_str(std::string_view sv) {
    size_t s = 0;
    while (s < sv.size() && std::isspace(static_cast<unsigned char>(sv[s])) != 0)
        s++;
    if (s == sv.size())
        return "";
    size_t e = sv.size() - 1;
    while (e > s && std::isspace(static_cast<unsigned char>(sv[e])) != 0)
        e--;
    return std::string(sv.substr(s, e - s + 1));
}

bool Sysml2BlockScanner::ends_with_word(std::string_view text, std::string_view word) {
    if (text == word)
        return true;
    if (text.size() > word.size()) {
        size_t pos = text.size() - word.size();
        return text.substr(pos) == word && std::isspace(static_cast<unsigned char>(text[pos - 1])) != 0;
    }
    return false;
}

bool Sysml2BlockScanner::is_structural_statement(std::string_view stmt) {
    std::string s = trim_str(stmt);
    if (s.rfind("connect ", 0) == 0 || s.rfind("bind ", 0) == 0 || s.rfind("allocate ", 0) == 0 ||
        s.rfind("allocation ", 0) == 0) {
        return true;
    }
    if (s.rfind("part ", 0) == 0 && s.find(':') != std::string_view::npos) {
        return true;
    }
    return false;
}

bool Sysml2BlockScanner::is_structural_block_head(std::string_view head) {
    std::string h = trim_str(head);
    return (h.rfind("part def", 0) == 0 || h.rfind("part ", 0) == 0 || h.rfind("interface def", 0) == 0 ||
            h.rfind("allocation def", 0) == 0 || h.rfind("connection def", 0) == 0 || h.rfind("item def Part", 0) == 0);
}

bool Sysml2BlockScanner::is_container_block_head(std::string_view head) {
    std::string h = trim_str(head);
    if (h.rfind("package ", 0) == 0 || h.rfind("package\t", 0) == 0 || h.rfind("state def ", 0) == 0 ||
        h.rfind("state def\t", 0) == 0 || h.rfind("state ", 0) == 0 || h.rfind("state\t", 0) == 0 ||
        h.rfind("parallel state ", 0) == 0 || h.rfind("parallel state\t", 0) == 0 || h.rfind("enum def ", 0) == 0 ||
        h.rfind("enum def\t", 0) == 0 || h.rfind("struct def ", 0) == 0 || h.rfind("struct def\t", 0) == 0 ||
        h.rfind("datatype def ", 0) == 0 || h.rfind("datatype def\t", 0) == 0 || h.rfind("item def ", 0) == 0 ||
        h.rfind("item def\t", 0) == 0 || h.rfind("event def ", 0) == 0 || h.rfind("event def\t", 0) == 0 ||
        h.rfind("attribute def ", 0) == 0 || h.rfind("attribute def\t", 0) == 0 || h.rfind("port def ", 0) == 0 ||
        h.rfind("port def\t", 0) == 0) {
        return true;
    }
    return false;
}

bool Sysml2BlockScanner::is_transition_like(std::string_view stmt) {
    std::string s = trim_str(stmt);
    return (s.rfind("transition", 0) == 0 || s.find(" then ") != std::string::npos ||
            s.find(" to ") != std::string::npos || s.find(" first ") != std::string::npos);
}

std::vector<SysmlScannedToken> Sysml2BlockScanner::scan(std::string_view content) {
    std::vector<SysmlScannedToken> tokens;
    size_t idx = 0;
    const size_t len = content.size();
    size_t current_line = 1;

    std::string accumulated;
    size_t structural_block_depth = 0;
    size_t inline_brace_depth = 0;

    while (idx < len) {
        char c = content[idx];

        // Handle newlines
        if (c == '\n') {
            current_line++;
        }

        // Line comment //
        if (c == '/' && idx + 1 < len && content[idx + 1] == '/') {
            size_t line_end = content.find('\n', idx + 2);
            if (line_end == std::string_view::npos)
                line_end = len;
            std::string_view comment_text = content.substr(idx, line_end - idx);
            std::string trimmed_comment = trim_str(comment_text.substr(2));
            if (trimmed_comment.rfind("@fsm:", 0) == 0) {
                tokens.push_back({SysmlTokenKind::Directive, trimmed_comment, current_line});
            }
            idx = line_end;
            continue;
        }

        // Block comment /* ... */
        if (c == '/' && idx + 1 < len && content[idx + 1] == '*') {
            size_t comment_end = content.find("*/", idx + 2);
            if (comment_end == std::string_view::npos)
                comment_end = len;
            else
                comment_end += 2;
            idx = comment_end;
            continue;
        }

        // String literal "..."
        if (c == '"') {
            size_t str_end = idx + 1;
            while (str_end < len && content[str_end] != '"') {
                if (content[str_end] == '\\' && str_end + 1 < len)
                    str_end++;
                if (content[str_end] == '\n')
                    current_line++;
                str_end++;
            }
            if (str_end < len)
                str_end++;
            std::string str_lit(content.substr(idx, str_end - idx));
            if (structural_block_depth == 0) {
                accumulated += str_lit;
            }
            idx = str_end;
            continue;
        }

        // Structural block inner brace tracking (skip non-FSM parts/connects)
        if (structural_block_depth > 0) {
            if (c == '{') {
                structural_block_depth++;
            } else if (c == '}') {
                structural_block_depth--;
            }
            idx++;
            continue;
        }

        // Semicolon: end of statement if not inside inline braces
        if (c == ';') {
            if (inline_brace_depth > 0) {
                accumulated += ';';
            } else {
                std::string stmt = trim_str(accumulated);
                accumulated.clear();
                if (!stmt.empty() && !is_structural_statement(stmt)) {
                    tokens.push_back({SysmlTokenKind::Statement, stmt, current_line});
                }
            }
            idx++;
            continue;
        }

        // Open brace '{'
        if (c == '{') {
            if (inline_brace_depth > 0) {
                inline_brace_depth++;
                accumulated += '{';
            } else {
                std::string head = trim_str(accumulated);
                if (is_structural_block_head(head)) {
                    accumulated.clear();
                    structural_block_depth = 1;
                } else if (is_container_block_head(head)) {
                    accumulated.clear();
                    tokens.push_back({SysmlTokenKind::BlockOpen, head, current_line});
                } else {
                    // Inline brace block (port constraints, transition actions, entry/exit actions)
                    inline_brace_depth = 1;
                    accumulated += " {";
                }
            }
            idx++;
            continue;
        }

        // Close brace '}'
        if (c == '}') {
            if (inline_brace_depth > 0) {
                inline_brace_depth--;
                accumulated += "}";
                if (inline_brace_depth == 0) {
                    // If not a continuation statement like transition, emit immediately
                    if (!is_transition_like(accumulated)) {
                        std::string stmt = trim_str(accumulated);
                        accumulated.clear();
                        if (!stmt.empty() && !is_structural_statement(stmt)) {
                            tokens.push_back({SysmlTokenKind::Statement, stmt, current_line});
                        }
                    }
                }
            } else {
                std::string stmt = trim_str(accumulated);
                accumulated.clear();
                if (!stmt.empty() && !is_structural_statement(stmt)) {
                    tokens.push_back({SysmlTokenKind::Statement, stmt, current_line});
                }
                tokens.push_back({SysmlTokenKind::BlockClose, "}", current_line});
            }
            idx++;
            continue;
        }

        accumulated += c;
        idx++;
    }

    std::string final_stmt = trim_str(accumulated);
    if (!final_stmt.empty() && !is_structural_statement(final_stmt)) {
        tokens.push_back({SysmlTokenKind::Statement, final_stmt, current_line});
    }

    return tokens;
}

}  // namespace fsm::frontend::formal
