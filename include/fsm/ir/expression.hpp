/**
 * @file expression.hpp
 * @brief Algebraic Expression AST Metamodel and Builders for Extended Finite State Machines (EFSM).
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace fsm::ir {

/**
 * @brief Classification of algebraic expression nodes.
 */
enum class ExpressionKind : std::uint8_t {
    // Leaf nodes: Literals
    IntegerLiteral,  ///< Integer constant (e.g. 42, -10)
    FloatLiteral,    ///< Floating-point constant (e.g. 3.14, 0.0)
    BooleanLiteral,  ///< Boolean constant (true, false)
    EnumLiteral,     ///< Scoped enum literal (e.g. "Mode::Active")

    // Leaf nodes: References
    VariableRef,    ///< State variable reference (e.g. "counter")
    PortRef,        ///< Port variable reference (e.g. "sensor.value")
    EventParamRef,  ///< Event payload attribute reference (e.g. "event.payload")

    // Composite Operations
    UnaryOp,   ///< Unary operation (-x, !x, ~x)
    BinaryOp,  ///< Binary operation (x + y, x * y, x && y)

    // Fallback opaque node
    RawExpression  ///< Opaque/unparsed target-specific expression text
};

/**
 * @brief Algebraic and logical operator kinds.
 */
enum class ExpressionOp : std::uint8_t {
    None,

    // Unary Operators
    Negate,      ///< Arithmetic negation (-x)
    BitwiseNot,  ///< Bitwise complement (~x)
    LogicalNot,  ///< Logical negation (!x)

    // Binary Arithmetic Operators
    Add,       ///< Addition (+)
    Subtract,  ///< Subtraction (-)
    Multiply,  ///< Multiplication (*)
    Divide,    ///< Division (/)
    Modulo,    ///< Modulo remainder (%)

    // Binary Bitwise & Shift Operators
    ShiftLeft,   ///< Shift left (<<)
    ShiftRight,  ///< Shift right (>>)
    BitwiseAnd,  ///< Bitwise AND (&)
    BitwiseOr,   ///< Bitwise OR (|)
    BitwiseXor   ///< Bitwise XOR (^)
};

[[nodiscard]] std::string_view expression_kind_to_string(ExpressionKind kind) noexcept;
[[nodiscard]] std::string_view expression_op_to_string(ExpressionOp op) noexcept;
[[nodiscard]] ExpressionOp string_to_expression_op(std::string_view str) noexcept;

/**
 * @brief AST node representing algebraic datapath expressions in actions and effect sequences.
 */
struct ExpressionAstNode {
    ExpressionKind kind{ExpressionKind::RawExpression};           ///< Kind of expression node
    ExpressionOp op{ExpressionOp::None};                          ///< Operation if unary/binary
    std::string symbol;                                           ///< Identifier, enum literal, or raw expression text
    std::variant<std::monostate, int64_t, double, bool> value{};  ///< Constant literal value
    std::vector<ExpressionAstNode> children;                      ///< Child subexpressions (1 for unary, 2 for binary)

    ExpressionAstNode() = default;

    // --- Static Factories ---

    static ExpressionAstNode make_int_literal(int64_t val) {
        ExpressionAstNode node;
        node.kind = ExpressionKind::IntegerLiteral;
        node.value = val;
        node.symbol = std::to_string(val);
        return node;
    }

    static ExpressionAstNode make_float_literal(double val) {
        ExpressionAstNode node;
        node.kind = ExpressionKind::FloatLiteral;
        node.value = val;
        node.symbol = std::to_string(val);
        return node;
    }

    static ExpressionAstNode make_bool_literal(bool val) {
        ExpressionAstNode node;
        node.kind = ExpressionKind::BooleanLiteral;
        node.value = val;
        node.symbol = val ? "true" : "false";
        return node;
    }

    static ExpressionAstNode make_enum_literal(std::string enum_type_or_literal, std::string literal_name = "") {
        ExpressionAstNode node;
        node.kind = ExpressionKind::EnumLiteral;
        if (literal_name.empty()) {
            node.symbol = std::move(enum_type_or_literal);
        } else {
            node.symbol = enum_type_or_literal + "::" + literal_name;
        }
        return node;
    }

    static ExpressionAstNode make_variable_ref(std::string var_name) {
        ExpressionAstNode node;
        node.kind = ExpressionKind::VariableRef;
        node.symbol = std::move(var_name);
        return node;
    }

    static ExpressionAstNode make_port_ref(std::string port_name, std::string field_name = "") {
        ExpressionAstNode node;
        node.kind = ExpressionKind::PortRef;
        if (field_name.empty()) {
            node.symbol = std::move(port_name);
        } else {
            node.symbol = port_name + "." + field_name;
        }
        return node;
    }

    static ExpressionAstNode make_event_param_ref(std::string event_name, std::string param_name) {
        ExpressionAstNode node;
        node.kind = ExpressionKind::EventParamRef;
        if (event_name.empty()) {
            node.symbol = std::move(param_name);
        } else {
            node.symbol = event_name + "." + param_name;
        }
        return node;
    }

    static ExpressionAstNode make_unary(ExpressionOp unary_op, ExpressionAstNode child) {
        ExpressionAstNode node;
        node.kind = ExpressionKind::UnaryOp;
        node.op = unary_op;
        node.children.push_back(std::move(child));
        return node;
    }

    static ExpressionAstNode make_binary(ExpressionOp bin_op, ExpressionAstNode left, ExpressionAstNode right) {
        ExpressionAstNode node;
        node.kind = ExpressionKind::BinaryOp;
        node.op = bin_op;
        node.children.push_back(std::move(left));
        node.children.push_back(std::move(right));
        return node;
    }

    static ExpressionAstNode make_raw_expression(std::string raw) {
        ExpressionAstNode node;
        node.kind = ExpressionKind::RawExpression;
        node.symbol = std::move(raw);
        return node;
    }

    /**
     * @brief Serializes the expression AST to its infix mathematical string representation.
     */
    [[nodiscard]] std::string to_string() const;

    /**
     * @brief Serializes the expression AST to an informative JSON string.
     */
    [[nodiscard]] std::string to_json() const;

    /**
     * @brief Parses an algebraic mathematical text expression into a structured AST.
     * @param text String view containing arithmetic/bitwise expressions.
     * @return Root ExpressionAstNode of the parsed expression.
     */
    static ExpressionAstNode parse(std::string_view text);

    bool operator==(const ExpressionAstNode& other) const noexcept;
};

}  // namespace fsm::ir
