#include "fsm/ir/guard.hpp"

namespace fsm::ir {

std::string GuardAstNode::to_string() const {
    if (op == GuardOp::None) {
        return expression;
    }
    if (op == GuardOp::Not) {
        if (!children.empty()) {
            const std::string inner = children[0].to_string();
            const bool needs_parens = (children[0].op != GuardOp::None) || (inner.find(' ') != std::string::npos);
            return "!" + (needs_parens ? ("(" + inner + ")") : inner);
        }
        return "!" + expression;
    }
    if (op == GuardOp::And || op == GuardOp::Or) {
        const std::string op_str = (op == GuardOp::And) ? " && " : " || ";
        std::string result;
        for (std::size_t i = 0; i < children.size(); ++i) {
            if (i > 0) {
                result += op_str;
            }
            const bool needs_parens = (children[i].op == GuardOp::Or && op == GuardOp::And);
            if (needs_parens) {
                result += "(";
            }
            result += children[i].to_string();
            if (needs_parens) {
                result += ")";
            }
        }
        return result;
    }
    return expression;
}

void GuardAstNode::collect_atomic_guards(std::vector<std::string>& out) const {
    if (op == GuardOp::None) {
        if (!expression.empty() && expression != "else" && expression != "otherwise" && expression != "default") {
            out.push_back(expression);
        }
    } else {
        for (const auto& child : children) {
            child.collect_atomic_guards(out);
        }
    }
}

}  // namespace fsm::ir
