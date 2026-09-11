/**
 * @file action.hpp
 * @brief Formal Action Signatures, Assignment Operators, and Structured EFSM Datapath Models.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "fsm/ir/expression.hpp"

namespace fsm::ir {

/**
 * @brief Algebraic assignment operators for datapath state effects.
 */
enum class AssignmentOp : std::uint8_t {
    Assign,     ///< Simple assignment (=)
    AddAssign,  ///< Addition assignment (+=)
    SubAssign,  ///< Subtraction assignment (-=)
    MulAssign,  ///< Multiplication assignment (*=)
    DivAssign,  ///< Division assignment (/=)
    ModAssign,  ///< Modulo assignment (%=)
    ShlAssign,  ///< Bitwise shift left assignment (<<=)
    ShrAssign,  ///< Bitwise shift right assignment (>>=)
    AndAssign,  ///< Bitwise AND assignment (&=)
    OrAssign,   ///< Bitwise OR assignment (|=)
    XorAssign   ///< Bitwise XOR assignment (^=)
};

/**
 * @brief Converts an assignment operator enum to its canonical string representation.
 * @param op The assignment operator.
 * @return String view representing the operator (e.g. "+=", "=").
 */
[[nodiscard]] constexpr std::string_view assignment_op_to_string(AssignmentOp op) noexcept {
    switch (op) {
        case AssignmentOp::Assign:
            return "=";
        case AssignmentOp::AddAssign:
            return "+=";
        case AssignmentOp::SubAssign:
            return "-=";
        case AssignmentOp::MulAssign:
            return "*=";
        case AssignmentOp::DivAssign:
            return "/=";
        case AssignmentOp::ModAssign:
            return "%=";
        case AssignmentOp::ShlAssign:
            return "<<=";
        case AssignmentOp::ShrAssign:
            return ">>=";
        case AssignmentOp::AndAssign:
            return "&=";
        case AssignmentOp::OrAssign:
            return "|=";
        case AssignmentOp::XorAssign:
            return "^=";
    }
    return "=";
}

/**
 * @brief Parses an assignment operator string token into its corresponding enum value.
 * @param str The operator token string.
 * @return The parsed AssignmentOp, defaulting to Assign if unrecognized.
 */
[[nodiscard]] constexpr AssignmentOp string_to_assignment_op(std::string_view str) noexcept {
    if (str == "+=")
        return AssignmentOp::AddAssign;
    if (str == "-=")
        return AssignmentOp::SubAssign;
    if (str == "*=")
        return AssignmentOp::MulAssign;
    if (str == "/=")
        return AssignmentOp::DivAssign;
    if (str == "%=")
        return AssignmentOp::ModAssign;
    if (str == "<<=")
        return AssignmentOp::ShlAssign;
    if (str == ">>=")
        return AssignmentOp::ShrAssign;
    if (str == "&=")
        return AssignmentOp::AndAssign;
    if (str == "|=")
        return AssignmentOp::OrAssign;
    if (str == "^=")
        return AssignmentOp::XorAssign;
    return AssignmentOp::Assign;
}

/**
 * @brief Architectural scope categorization of an lvalue target.
 */
enum class LValueScope : std::uint8_t {
    Register,  ///< Internal state variable / register ("reg.")
    OutPort,   ///< Output port ("out.")
    Local      ///< Local action temporary / unqualified ("local.")
};

/**
 * @brief Converts an lvalue scope enum to its string label.
 * @param scope The target scope.
 * @return String view representing the scope.
 */
[[nodiscard]] constexpr std::string_view lvalue_scope_to_string(LValueScope scope) noexcept {
    switch (scope) {
        case LValueScope::Register:
            return "Register";
        case LValueScope::OutPort:
            return "OutPort";
        case LValueScope::Local:
            return "Local";
    }
    return "Local";
}

/**
 * @brief Structured target lvalue representing an assignment destination in EFSM datapath actions.
 */
struct LValueTarget {
    std::string name;                                           ///< Root variable or port name (e.g. "battery")
    LValueScope scope{LValueScope::Register};                   ///< Architectural scope: Register, OutPort, Local
    std::vector<std::string> member_path;                       ///< Struct member path (e.g. ["soc", "level"])
    std::optional<std::uint32_t> constant_index{std::nullopt};  ///< Optional constant array index

    LValueTarget() = default;

    /* implicit */ LValueTarget(std::string_view str) { parse_from_string(str); }
    /* implicit */ LValueTarget(const char* str) : LValueTarget(std::string_view(str ? str : "")) {}
    /* implicit */ LValueTarget(std::string str) : LValueTarget(std::string_view(str)) {}

    LValueTarget(std::string root_name, LValueScope target_scope) : name(std::move(root_name)), scope(target_scope) {}

    /**
     * @brief Computes the unqualified dot-separated and indexed member path (e.g. "battery.soc.level[0]").
     */
    [[nodiscard]] std::string full_path() const;

    /**
     * @brief Computes the qualified scoped path prefixed by domain (e.g. "reg.battery.soc", "out.status").
     */
    [[nodiscard]] std::string scoped_path() const;

    /**
     * @brief Checks whether the target identifier is empty.
     */
    [[nodiscard]] bool empty() const noexcept { return name.empty(); }

    /* implicit */ operator std::string() const { return full_path(); }

    bool operator==(const LValueTarget& other) const noexcept = default;
    bool operator==(const char* str) const noexcept;
    bool operator==(const std::string& str) const noexcept;
    bool operator==(std::string_view str) const noexcept;

    friend bool operator==(const char* lhs, const LValueTarget& rhs) noexcept { return rhs == lhs; }

    friend bool operator==(const std::string& lhs, const LValueTarget& rhs) noexcept { return rhs == lhs; }

  private:
    void parse_from_string(std::string_view str);
};

/**
 * @brief Structured action effect assignment with algebraic AST and C++ string representation.
 */
struct ActionAssignment {
    LValueTarget target;                    ///< Target register, port, struct member, or array element
    AssignmentOp op{AssignmentOp::Assign};  ///< Assignment operator (=, +=, -=, etc.)
    std::string expression;                 ///< Canonical text expression
    std::optional<ExpressionAstNode> expr_ast{std::nullopt};  ///< Structured algebraic AST

    ActionAssignment() = default;

    /**
     * @brief Constructs an assignment from target, expression string, and operator.
     */
    ActionAssignment(LValueTarget tgt, std::string expr, AssignmentOp assign_op = AssignmentOp::Assign);

    /**
     * @brief Constructs an assignment from target, operator, and parsed algebraic AST.
     */
    ActionAssignment(LValueTarget tgt, AssignmentOp assign_op, ExpressionAstNode ast);

    /**
     * @brief Constructs an assignment with default Assign (=) operator from target and AST.
     */
    ActionAssignment(LValueTarget tgt, ExpressionAstNode ast);

    /**
     * @brief Parses an assignment statement string (e.g. "x += 10;") into an ActionAssignment.
     * @param statement The raw assignment statement string.
     * @return Parsed ActionAssignment structure.
     */
    static ActionAssignment parse(std::string_view statement);

    bool operator==(const ActionAssignment& other) const noexcept {
        return target == other.target && op == other.op && expression == other.expression && expr_ast == other.expr_ast;
    }
};

/**
 * @brief Primitive instruction opcodes for target-agnostic datapath action execution.
 */
enum class ActionOpKind : std::uint8_t {
    Store,       ///< Register / extended variable write: target := expression
    PortWrite,   ///< Output port / bus write: port.write(expression)
    PortRead,    ///< Input port sampled read: reg := port.read()
    SignalEmit,  ///< Asynchronous or synchronous signal dispatch: emit Signal(args)
    ActionCall   ///< Controlled opaque invocation with explicit read/write sets
};

/**
 * @brief Register or extended state variable assignment instruction.
 */
struct StoreOp {
    LValueTarget target;
    AssignmentOp op{AssignmentOp::Assign};
    std::string expression;
    std::optional<ExpressionAstNode> expr_ast{std::nullopt};

    bool operator==(const StoreOp& other) const noexcept = default;
};

/**
 * @brief Output port write operation transmitting data over external bus or hardware port.
 */
struct PortWriteOp {
    std::string port_name;
    std::string expression;
    std::optional<ExpressionAstNode> expr_ast{std::nullopt};
    bool is_latched{false};

    bool operator==(const PortWriteOp& other) const noexcept = default;
};

/**
 * @brief Sampled read operation transferring input port value into a local or state register.
 */
struct PortReadOp {
    std::string port_name;
    LValueTarget destination;

    bool operator==(const PortReadOp& other) const noexcept = default;
};

/**
 * @brief Signal or event emission instruction dispatching an event to the local or remote queues.
 */
struct SignalEmitOp {
    std::string signal_name;
    std::vector<std::string> arguments;
    std::optional<std::string> target_port{std::nullopt};

    bool operator==(const SignalEmitOp& other) const noexcept = default;
};

/**
 * @brief Controlled opaque function invocation declaring explicit read and write memory footprints.
 */
struct ActionCallOp {
    std::string function_name;
    std::vector<std::string> arguments;
    std::vector<std::string> read_set;   ///< Data-path variables read (for race detection)
    std::vector<std::string> write_set;  ///< Data-path variables mutated (for race detection)

    bool operator==(const ActionCallOp& other) const noexcept = default;
};

using ActionAstVariant = std::variant<StoreOp, PortWriteOp, PortReadOp, SignalEmitOp, ActionCallOp>;

/**
 * @brief Target-agnostic Action AST instruction node.
 */
struct ActionAstNode {
    ActionOpKind kind{ActionOpKind::Store};
    ActionAstVariant op{StoreOp{}};
    std::string description;

    ActionAstNode() = default;
    ActionAstNode(StoreOp o, std::string desc = "")
        : kind(ActionOpKind::Store), op(std::move(o)), description(std::move(desc)) {}
    ActionAstNode(PortWriteOp o, std::string desc = "")
        : kind(ActionOpKind::PortWrite), op(std::move(o)), description(std::move(desc)) {}
    ActionAstNode(PortReadOp o, std::string desc = "")
        : kind(ActionOpKind::PortRead), op(std::move(o)), description(std::move(desc)) {}
    ActionAstNode(SignalEmitOp o, std::string desc = "")
        : kind(ActionOpKind::SignalEmit), op(std::move(o)), description(std::move(desc)) {}
    ActionAstNode(ActionCallOp o, std::string desc = "")
        : kind(ActionOpKind::ActionCall), op(std::move(o)), description(std::move(desc)) {}

    bool operator==(const ActionAstNode& other) const noexcept = default;
};

/**
 * @brief Action signature describing callback methods, parameters, and register assignments.
 */
struct ActionSignature {
    std::string name;                           ///< Action identifier / function name
    std::string invocation;                     ///< Concrete call expression (e.g. "srv.SendAlert()")
    bool accepts_event{false};                  ///< True if callback accepts triggering event reference
    std::vector<ActionAssignment> assignments;  ///< Sequence of datapath state variable assignments
    std::vector<ActionAstNode> instructions;    ///< Sequence of target-agnostic primitive instructions

    ActionSignature() = default;
    /* implicit */ ActionSignature(std::string_view act_name) : name(act_name), invocation(act_name) {}
    /* implicit */ ActionSignature(const char* act_name)
        : name(act_name ? act_name : ""), invocation(act_name ? act_name : "") {}
    /* implicit */ ActionSignature(std::string act_name) : name(std::move(act_name)), invocation(name) {}
    ActionSignature(std::string act_name, std::string act_inv)
        : name(std::move(act_name)), invocation(std::move(act_inv)) {}

    [[nodiscard]] bool empty() const noexcept {
        return name.empty() && invocation.empty() && assignments.empty() && instructions.empty();
    }

    /* implicit */ operator std::string() const { return name; }

    bool operator==(const ActionSignature& other) const noexcept {
        return name == other.name && invocation == other.invocation && accepts_event == other.accepts_event &&
               assignments == other.assignments && instructions == other.instructions;
    }

    bool operator==(std::string_view str) const noexcept { return name == str; }
};

/**
 * @brief Documentation and metadata model for actions registered in FSM IR.
 */
struct ActionModel {
    std::string name;         ///< Unique action identifier
    std::string description;  ///< Optional human-readable documentation comment

    explicit ActionModel(std::string action_name = "", std::string action_desc = "")
        : name(std::move(action_name)), description(std::move(action_desc)) {}

    bool operator<(const ActionModel& other) const noexcept { return name < other.name; }
};

}  // namespace fsm::ir
