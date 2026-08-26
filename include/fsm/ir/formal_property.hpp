#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/ir/deterministic_id.hpp"

namespace fsm::codegen {

// ============================================================================
// Formal Verification & Temporal Logic (LTL / CTL / Invariants)
// ============================================================================

enum class TemporalOp : std::uint8_t {
    Atom,        ///< Atomic proposition / state predicate (e.g. "InFlight", "BatteryLow")
    Globally,    ///< G or [] (Always in the future)
    Finally,     ///< F or <> (Eventually in the future)
    Next,        ///< X or () (Next step)
    Until,       ///< U (Strong until)
    Release,     ///< R (Weak release)
    Implies,     ///< -> (Implication)
    Equivalent,  ///< <-> (Equivalence)
    And,         ///< && (Conjunction)
    Or,          ///< || (Disjunction)
    Not          ///< ! (Negation)
};

inline std::string temporal_op_to_string(TemporalOp op) {
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
    }
    return "Atom";
}

struct PropertyAstNode {
    TemporalOp op{TemporalOp::Atom};
    std::string atom;
    std::vector<PropertyAstNode> children;

    PropertyAstNode() = default;
    explicit PropertyAstNode(std::string prop_atom) : atom(std::move(prop_atom)) {}
    PropertyAstNode(TemporalOp operation, std::vector<PropertyAstNode> sub_nodes)
        : op(operation), children(std::move(sub_nodes)) {}

    [[nodiscard]] std::string to_string() const {
        if (op == TemporalOp::Atom) {
            return atom;
        }
        if (op == TemporalOp::Not) {
            if (!children.empty()) {
                return "!" + (children[0].op != TemporalOp::Atom ? "(" + children[0].to_string() + ")"
                                                                 : children[0].to_string());
            }
            return "!" + atom;
        }
        if (op == TemporalOp::Globally || op == TemporalOp::Finally || op == TemporalOp::Next) {
            std::string op_str = (op == TemporalOp::Globally) ? "G " : ((op == TemporalOp::Finally) ? "F " : "X ");
            if (!children.empty()) {
                return op_str + "(" + children[0].to_string() + ")";
            }
            return op_str + "(" + atom + ")";
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

    bool operator==(const PropertyAstNode& other) const noexcept {
        return op == other.op && atom == other.atom && children == other.children;
    }
};

enum class PropertyKind : std::uint8_t { Safety, Liveness, Invariant, Reachability, DeadlockFreedom };

inline std::string property_kind_to_string(PropertyKind kind) {
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

inline PropertyKind property_kind_from_string(std::string_view str) {
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

struct FormalProperty {
    std::string id;
    std::string name;
    std::string description;
    PropertyKind kind{PropertyKind::Safety};
    std::string raw_formula;
    std::optional<PropertyAstNode> ast;
    std::string traceability_req;

    FormalProperty() = default;
    FormalProperty(std::string prop_name, PropertyKind prop_kind, std::string formula, std::string desc = "",
                   std::string req = "")
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

    bool operator==(const FormalProperty& other) const noexcept {
        return id == other.id && name == other.name && description == other.description && kind == other.kind &&
               raw_formula == other.raw_formula && ast == other.ast && traceability_req == other.traceability_req;
    }
};

}  // namespace fsm::codegen
