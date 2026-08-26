#pragma once

#include <string>
#include <utility>
#include <vector>

namespace fsm::codegen {

// ============================================================================
// Action Signatures, Models & Structured Assignments
// ============================================================================

struct ActionAssignment {
    std::string target_variable;
    std::string expression;

    ActionAssignment() = default;
    ActionAssignment(std::string var, std::string expr)
        : target_variable(std::move(var)), expression(std::move(expr)) {}

    bool operator==(const ActionAssignment& other) const noexcept {
        return target_variable == other.target_variable && expression == other.expression;
    }
};

struct ActionSignature {
    std::string name;
    std::string invocation;                     // e.g. "ctx.on_data(payload)"
    bool accepts_event{false};                  // Passes const Event&
    bool accepts_context{false};                // Passes Context&
    std::vector<ActionAssignment> assignments;  // State variable assignments

    ActionSignature() = default;
    explicit ActionSignature(std::string act_name, std::string act_inv = "")
        : name(std::move(act_name)), invocation(std::move(act_inv)) {}

    bool operator==(const ActionSignature& other) const noexcept {
        return name == other.name && invocation == other.invocation && accepts_event == other.accepts_event &&
               accepts_context == other.accepts_context && assignments == other.assignments;
    }
};

struct ActionModel {
    std::string name;
    std::string description;

    explicit ActionModel(std::string action_name = "", std::string action_desc = "")
        : name(std::move(action_name)), description(std::move(action_desc)) {}

    bool operator<(const ActionModel& other) const noexcept { return name < other.name; }
};

}  // namespace fsm::codegen
