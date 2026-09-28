/**
 * @file guard.hpp
 * @brief Formal Guard Predicates, Composable Boolean AST, and Lowerings for the FSM Intermediate Representation.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fsm::ir {

/**
 * @brief Logical operator classification for compound guard predicates in FSM IR.
 */
enum class GuardOp : std::uint8_t {
    None,  ///< Atomic predicate / literal / identifier
    Not,   ///< Boolean unary negation (!)
    And,   ///< Boolean conjunction (&&)
    Or     ///< Boolean disjunction (||)
};

/**
 * @brief AST node representing composable boolean guard logic (AND, OR, NOT, Atomic Predicates).
 *
 * Provides backend-agnostic logical representation with language-specific lowering methods:
 * - to_cpp(): ISO C++ format ("!", " && ", " || ")
 * - to_sysml(): SysML v2 / KerML format ("not ", " and ", " or ")
 * - to_smv(): nuXmv / SMV format ("!", " & ", " | ")
 * - to_rust(): Rust format ("!", " && ", " || ")
 */
struct GuardAstNode {
    GuardOp op{GuardOp::None};           ///< Logical operator of this node
    std::string expression;              ///< Raw predicate expression or guard struct name
    std::vector<GuardAstNode> children;  ///< Child operand subtrees

    GuardAstNode() = default;

    /**
     * @brief Constructs an atomic guard predicate node.
     * @param expr The boolean expression string or symbol name.
     */
    explicit GuardAstNode(std::string expr) : expression(std::move(expr)) {}

    /**
     * @brief Constructs a compound guard operation node.
     * @param operation Logical operator (Not, And, Or).
     * @param sub_nodes Child operand AST subtrees.
     */
    GuardAstNode(GuardOp operation, std::vector<GuardAstNode> sub_nodes)
        : op(operation), children(std::move(sub_nodes)) {}

    /**
     * @brief Returns the canonical string representation (defaults to C++ syntax).
     */
    [[nodiscard]] std::string to_string() const { return to_cpp(); }

    /**
     * @brief Lowering for ISO C++17/C++20 condition evaluation syntax.
     */
    [[nodiscard]] std::string to_cpp() const;

    /**
     * @brief Lowering for OMG SysML v2 / KerML constraint syntax.
     */
    [[nodiscard]] std::string to_sysml() const;

    /**
     * @brief Lowering for nuXmv / SMV symbolic model verifier syntax.
     */
    [[nodiscard]] std::string to_smv() const;

    /**
     * @brief Lowering for Rust condition evaluation syntax.
     */
    [[nodiscard]] std::string to_rust() const { return to_cpp(); }

    /**
     * @brief Recursively collects all atomic leaf guard identifiers across the AST.
     * @param[out] out Destination vector for atomic predicate expressions.
     */
    void collect_atomic_guards(std::vector<std::string>& out) const;

    bool operator==(const GuardAstNode& other) const noexcept {
        return op == other.op && expression == other.expression && children == other.children;
    }
};

/**
 * @brief Guard predicate metamodel describing symbolic guard interfaces and expressions.
 */
struct GuardModel {
    std::string name;                                  ///< Unique guard identifier
    std::string description;                           ///< Human-readable documentation comment
    std::optional<std::string> raw_expression;         ///< Original source-level expression text
    std::optional<std::string> normalized_expression;  ///< Language-agnostic normalized expression

    explicit GuardModel(std::string guard_name = "", std::string guard_desc = "",
                        std::optional<std::string> raw_expr = std::nullopt,
                        std::optional<std::string> norm_expr = std::nullopt)
        : name(std::move(guard_name)),
          description(std::move(guard_desc)),
          raw_expression(std::move(raw_expr)),
          normalized_expression(std::move(norm_expr)) {}

    bool operator<(const GuardModel& other) const noexcept { return name < other.name; }
};

}  // namespace fsm::ir
