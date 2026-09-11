/**
 * @file formal_property.hpp
 * @brief Formal Verification & Temporal Logic (LTL, CTL, Invariants) Specifications in FSM IR.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/ir/deterministic_id.hpp"

namespace fsm::ir {

/**
 * @brief Temporal and propositional logic operators for formal verification.
 */
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

/**
 * @brief Converts a TemporalOp enum to its string symbol.
 */
[[nodiscard]] std::string temporal_op_to_string(TemporalOp op);

/**
 * @brief Structured Abstract Syntax Tree node representing temporal logic formulae (LTL/CTL).
 */
struct PropertyAstNode {
    TemporalOp op{TemporalOp::Atom};        ///< Temporal or propositional operator
    std::string atom;                       ///< Atomic proposition name (for TemporalOp::Atom)
    std::vector<PropertyAstNode> children;  ///< Child formula subtrees

    PropertyAstNode() = default;
    explicit PropertyAstNode(std::string prop_atom) : atom(std::move(prop_atom)) {}
    PropertyAstNode(TemporalOp operation, std::vector<PropertyAstNode> sub_nodes)
        : op(operation), children(std::move(sub_nodes)) {}

    /**
     * @brief Formats the temporal property AST into standard temporal formula text.
     */
    [[nodiscard]] std::string to_string() const;

    bool operator==(const PropertyAstNode& other) const noexcept {
        return op == other.op && atom == other.atom && children == other.children;
    }
};

/**
 * @brief Formal verification property objective classification.
 */
enum class PropertyKind : std::uint8_t {
    Safety,          ///< Invariant safety property: "something bad never happens"
    Liveness,        ///< Liveness property: "something good eventually happens"
    Invariant,       ///< Inductive state invariant
    Reachability,    ///< State or condition reachability check
    DeadlockFreedom  ///< Global freedom from deadlock states
};

[[nodiscard]] std::string property_kind_to_string(PropertyKind kind);
[[nodiscard]] PropertyKind property_kind_from_string(std::string_view str);

/**
 * @brief Formal Property specification containing raw formula, AST, and requirement traceability.
 */
struct FormalProperty {
    std::string id;                           ///< Deterministic property identifier
    std::string name;                         ///< Symbolic property name (e.g. "SafeLanding")
    std::string description;                  ///< Human-readable property description
    PropertyKind kind{PropertyKind::Safety};  ///< Property classification
    std::string raw_formula;                  ///< Source formula string
    std::optional<PropertyAstNode> ast;       ///< Parsed temporal logic AST
    std::string traceability_req;             ///< Traceability requirement tag (e.g. "REQ-SAFE-01")

    FormalProperty() = default;
    FormalProperty(std::string prop_name, PropertyKind prop_kind, std::string formula, std::string desc = "",
                   std::string req = "");

    bool operator==(const FormalProperty& other) const noexcept {
        return id == other.id && name == other.name && description == other.description && kind == other.kind &&
               raw_formula == other.raw_formula && ast == other.ast && traceability_req == other.traceability_req;
    }
};

}  // namespace fsm::ir
