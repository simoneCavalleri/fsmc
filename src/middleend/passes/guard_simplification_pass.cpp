#include "fsm/middleend/passes/guard_simplification_pass.hpp"

#include <algorithm>
#include <utility>

namespace fsm::middleend::passes {

using namespace fsm::ir;
using namespace fsm::diagnostic;

namespace {

bool are_complementary(const GuardAstNode& a, const GuardAstNode& b) {
    if (a.op == GuardOp::Not && !a.children.empty() && a.children[0] == b) {
        return true;
    }
    if (b.op == GuardOp::Not && !b.children.empty() && b.children[0] == a) {
        return true;
    }
    if (a.op == GuardOp::None && b.op == GuardOp::None) {
        if (a.expression == "!" + b.expression || a.expression == "!(" + b.expression + ")") {
            return true;
        }
        if (b.expression == "!" + a.expression || b.expression == "!(" + a.expression + ")") {
            return true;
        }
    }
    return false;
}

bool is_true_literal(const GuardAstNode& n) {
    return n.op == GuardOp::None && (n.expression == "true" || n.expression == "true_");
}

bool is_false_literal(const GuardAstNode& n) {
    return n.op == GuardOp::None && (n.expression == "false" || n.expression == "false_");
}

std::string_view trim_sv(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) {
        sv.remove_suffix(1);
    }
    return sv;
}

GuardAstNode parse_cpp_guard_ast(std::string_view raw) {
    raw = trim_sv(raw);
    auto parse_children = [](std::string_view inner) {
        std::vector<GuardAstNode> children;
        int angle_depth = 0;
        int paren_depth = 0;
        std::size_t start = 0;
        for (std::size_t i = 0; i < inner.size(); ++i) {
            const char c = inner[i];
            if (c == '<') {
                ++angle_depth;
            } else if (c == '>' && angle_depth > 0) {
                --angle_depth;
            } else if (c == '(') {
                ++paren_depth;
            } else if (c == ')' && paren_depth > 0) {
                --paren_depth;
            } else if (c == ',' && angle_depth == 0 && paren_depth == 0) {
                children.push_back(parse_cpp_guard_ast(inner.substr(start, i - start)));
                start = i + 1;
            }
        }
        if (start <= inner.size()) {
            std::string_view last = trim_sv(inner.substr(start));
            if (!last.empty()) {
                children.push_back(parse_cpp_guard_ast(last));
            }
        }
        return children;
    };

    if (raw.size() > 10 && raw.substr(0, 9) == "fsm::not<" && raw.back() == '>') {
        // Handle optional legacy form
        return GuardAstNode(GuardOp::Not, parse_children(raw.substr(9, raw.size() - 10)));
    }
    if (raw.size() > 11 && raw.substr(0, 10) == "fsm::not_<" && raw.back() == '>') {
        return GuardAstNode(GuardOp::Not, parse_children(raw.substr(10, raw.size() - 11)));
    }
    if (raw.size() > 11 && raw.substr(0, 10) == "fsm::and_<" && raw.back() == '>') {
        return GuardAstNode(GuardOp::And, parse_children(raw.substr(10, raw.size() - 11)));
    }
    if (raw.size() > 10 && raw.substr(0, 9) == "fsm::or_<" && raw.back() == '>') {
        return GuardAstNode(GuardOp::Or, parse_children(raw.substr(9, raw.size() - 10)));
    }
    return GuardAstNode(std::string(raw));
}

}  // namespace

GuardAstNode GuardSimplificationPass::simplify_node(const GuardAstNode& node) {
    // Base case: atomic expression (re-parse if it encodes a compound fsm::and_/or_/not_ type)
    if (node.op == GuardOp::None) {
        if (node.expression.rfind("fsm::", 0) == 0) {
            GuardAstNode parsed = parse_cpp_guard_ast(node.expression);
            if (parsed.op != GuardOp::None) {
                return simplify_node(parsed);
            }
        }
        if (is_true_literal(node)) {
            return GuardAstNode("true");
        }
        if (is_false_literal(node)) {
            return GuardAstNode("false");
        }
        return node;
    }

    // Simplify all children first (bottom-up)
    std::vector<GuardAstNode> simplified_children;
    simplified_children.reserve(node.children.size());
    for (const auto& child : node.children) {
        simplified_children.push_back(simplify_node(child));
    }

    // 1. NOT operator
    if (node.op == GuardOp::Not) {
        if (simplified_children.empty()) {
            return node;
        }
        const auto& child = simplified_children[0];
        // !(!A) -> A
        if (child.op == GuardOp::Not && !child.children.empty()) {
            return child.children[0];
        }
        // !(true) -> false, !(false) -> true
        if (is_true_literal(child))
            return GuardAstNode("false");
        if (is_false_literal(child))
            return GuardAstNode("true");
        return GuardAstNode(GuardOp::Not, {child});
    }

    // 2. Associative Flattening for AND / OR
    std::vector<GuardAstNode> flattened;
    flattened.reserve(simplified_children.size());
    for (auto& child : simplified_children) {
        if (child.op == node.op) {
            for (auto& grand_child : child.children) {
                flattened.push_back(std::move(grand_child));
            }
        } else {
            flattened.push_back(std::move(child));
        }
    }

    // 3. Constant Folding & Duplicate Elimination
    std::vector<GuardAstNode> cleaned;
    cleaned.reserve(flattened.size());

    if (node.op == GuardOp::And) {
        for (const auto& item : flattened) {
            // If any child is "false", whole AND is false
            if (is_false_literal(item)) {
                return GuardAstNode("false");
            }
            // Skip "true" in AND
            if (is_true_literal(item)) {
                continue;
            }
            // Complementary check (A && !A -> false)
            for (const auto& existing : cleaned) {
                if (are_complementary(existing, item)) {
                    return GuardAstNode("false");
                }
            }
            // Duplicate check
            if (std::find(cleaned.begin(), cleaned.end(), item) == cleaned.end()) {
                cleaned.push_back(item);
            }
        }
        if (cleaned.empty()) {
            return GuardAstNode("true");
        }
        if (cleaned.size() == 1) {
            return cleaned[0];
        }
        return GuardAstNode(GuardOp::And, std::move(cleaned));
    }

    if (node.op == GuardOp::Or) {
        for (const auto& item : flattened) {
            // If any child is "true", whole OR is true
            if (is_true_literal(item)) {
                return GuardAstNode("true");
            }
            // Skip "false" in OR
            if (is_false_literal(item)) {
                continue;
            }
            // Complementary check (A || !A -> true)
            for (const auto& existing : cleaned) {
                if (are_complementary(existing, item)) {
                    return GuardAstNode("true");
                }
            }
            // Duplicate check
            if (std::find(cleaned.begin(), cleaned.end(), item) == cleaned.end()) {
                cleaned.push_back(item);
            }
        }
        if (cleaned.empty()) {
            return GuardAstNode("false");
        }
        if (cleaned.size() == 1) {
            return cleaned[0];
        }
        return GuardAstNode(GuardOp::Or, std::move(cleaned));
    }

    return node;
}

bool GuardSimplificationPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    for (auto& t : ir.transitions) {
        if (t.guard_ast.has_value()) {
            GuardAstNode simplified = simplify_node(*t.guard_ast);
            t.set_guard_ast(simplified);

            if (t.guard == "false") {
                diag.report(Diagnostic::warning(
                    "W_GUARD_STATIC_FALSE",
                    "Transition '" + t.source + " -> " + t.target + "' has guard simplified to static 'false'."));
            }
        }
    }
    return true;
}

}  // namespace fsm::middleend::passes
