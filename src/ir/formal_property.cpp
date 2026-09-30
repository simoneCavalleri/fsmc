#include "fsm/ir/formal_property.hpp"

namespace fsm::ir {

std::string temporal_op_to_string(TemporalOp op) {
    switch (op) {
        case TemporalOp::Atom:
            return "Atom";
        case TemporalOp::Globally:
            return "G";
        case TemporalOp::Finally:
            return "F";
        case TemporalOp::Next:
            return "X";
        case TemporalOp::Until:
            return "U";
        case TemporalOp::Release:
            return "R";
        case TemporalOp::Implies:
            return "->";
        case TemporalOp::Equivalent:
            return "<->";
        case TemporalOp::And:
            return "&&";
        case TemporalOp::Or:
            return "||";
        case TemporalOp::Not:
            return "!";
        case TemporalOp::EX:
            return "EX";
        case TemporalOp::AX:
            return "AX";
        case TemporalOp::EF:
            return "EF";
        case TemporalOp::AF:
            return "AF";
        case TemporalOp::EG:
            return "EG";
        case TemporalOp::AG:
            return "AG";
        case TemporalOp::EU:
            return "EU";
        case TemporalOp::AU:
            return "AU";
    }
    return "Atom";
}

std::string PropertyAstNode::to_string() const {
    if (op == TemporalOp::Atom) {
        return atom;
    }
    if (op == TemporalOp::Not) {
        if (!children.empty()) {
            return "!" +
                   (children[0].op != TemporalOp::Atom ? "(" + children[0].to_string() + ")" : children[0].to_string());
        }
        return "!" + atom;
    }
    if (op == TemporalOp::Globally || op == TemporalOp::Finally || op == TemporalOp::Next || op == TemporalOp::EX ||
        op == TemporalOp::AX || op == TemporalOp::EF || op == TemporalOp::AF || op == TemporalOp::EG ||
        op == TemporalOp::AG) {
        std::string op_str =
            (op == TemporalOp::Globally)
                ? "G "
                : ((op == TemporalOp::Finally) ? "F "
                                               : ((op == TemporalOp::Next) ? "X " : (temporal_op_to_string(op) + " ")));
        if (!children.empty()) {
            return op_str + "(" + children[0].to_string() + ")";
        }
        return op_str + "(" + atom + ")";
    }
    if (op == TemporalOp::EU || op == TemporalOp::AU) {
        std::string prefix = (op == TemporalOp::EU) ? "E [" : "A [";
        if (children.size() >= 2) {
            return prefix + children[0].to_string() + " U " + children[1].to_string() + "]";
        }
        return prefix + atom + "]";
    }
    if (op == TemporalOp::Until || op == TemporalOp::Release || op == TemporalOp::Implies ||
        op == TemporalOp::Equivalent || op == TemporalOp::And || op == TemporalOp::Or) {
        std::string op_str;
        switch (op) {
            case TemporalOp::Until:
                op_str = " U ";
                break;
            case TemporalOp::Release:
                op_str = " R ";
                break;
            case TemporalOp::Implies:
                op_str = " -> ";
                break;
            case TemporalOp::Equivalent:
                op_str = " <-> ";
                break;
            case TemporalOp::And:
                op_str = " && ";
                break;
            case TemporalOp::Or:
                op_str = " || ";
                break;
            default:
                break;
        }
        std::string result;
        for (std::size_t i = 0; i < children.size(); ++i) {
            if (i > 0)
                result += op_str;
            const bool needs_parens = (children[i].op != TemporalOp::Atom && children[i].children.size() > 1);
            if (needs_parens)
                result += "(";
            result += children[i].to_string();
            if (needs_parens)
                result += ")";
        }
        return result;
    }
    return atom;
}

std::string property_kind_to_string(PropertyKind kind) {
    switch (kind) {
        case PropertyKind::Safety:
            return "Safety";
        case PropertyKind::Liveness:
            return "Liveness";
        case PropertyKind::Invariant:
            return "Invariant";
        case PropertyKind::Reachability:
            return "Reachability";
        case PropertyKind::DeadlockFreedom:
            return "DeadlockFreedom";
    }
    return "Safety";
}

PropertyKind property_kind_from_string(std::string_view str) {
    if (str == "Liveness")
        return PropertyKind::Liveness;
    if (str == "Invariant")
        return PropertyKind::Invariant;
    if (str == "Reachability")
        return PropertyKind::Reachability;
    if (str == "DeadlockFreedom")
        return PropertyKind::DeadlockFreedom;
    return PropertyKind::Safety;
}

FormalProperty::FormalProperty(std::string prop_name, PropertyKind prop_kind, std::string formula, std::string desc,
                               std::string req)
    : name(std::move(prop_name)),
      description(std::move(desc)),
      kind(prop_kind),
      raw_formula(std::move(formula)),
      traceability_req(std::move(req)) {
    id = compute_deterministic_id(name + ":" + raw_formula);
    if (!raw_formula.empty()) {
        ast = PropertyAstNode(raw_formula);
    }
}

}  // namespace fsm::ir
