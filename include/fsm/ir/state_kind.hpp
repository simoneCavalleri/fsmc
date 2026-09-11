/**
 * @file state_kind.hpp
 * @brief Structural classification of hierarchical state nodes and pseudostates in FSM IR.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace fsm::ir {

/**
 * @brief Structural state classification according to UML / SysML / SCXML formal semantics.
 */
enum class StateKind : std::uint8_t {
    Atomic,          ///< Simple leaf state with no internal child states
    Composite,       ///< Sequential composite state containing nested substates
    Parallel,        ///< Orthogonal composite state executing concurrent regions in parallel
    Initial,         ///< Initial pseudostate pointing to default substate
    Final,           ///< Terminating state indicating activity completion
    ShallowHistory,  ///< Shallow history pseudostate ([H]) restoring the immediate child state
    DeepHistory,     ///< Deep history pseudostate ([H*]) restoring all nested active configurations
    Choice,          ///< Dynamic conditional branch pseudostate (<<choice>>)
    Junction,        ///< Static merge/branch pseudostate (<<junction>>)
    Fork,            ///< Parallel split pseudostate splitting a single transition into concurrent regions (<<fork>>)
    Join,            ///< Parallel rendezvous pseudostate synchronizing concurrent regions (<<join>>)
    EntryPoint,      ///< Explicit entry connection point on a composite state boundary
    ExitPoint,       ///< Explicit exit connection point on a composite state boundary
    Terminate        ///< Fatal non-recoverable termination pseudostate ceasing entire machine lifecycle
};

/**
 * @brief Converts a StateKind enum into its canonical string representation.
 * @param kind The StateKind to convert.
 * @return String representation of the state kind (e.g. "Atomic", "Composite").
 */
[[nodiscard]] inline std::string state_kind_to_string(StateKind kind) {
    switch (kind) {
        case StateKind::Atomic:
            return "Atomic";
        case StateKind::Composite:
            return "Composite";
        case StateKind::Parallel:
            return "Parallel";
        case StateKind::Initial:
            return "Initial";
        case StateKind::Final:
            return "Final";
        case StateKind::ShallowHistory:
            return "ShallowHistory";
        case StateKind::DeepHistory:
            return "DeepHistory";
        case StateKind::Choice:
            return "Choice";
        case StateKind::Junction:
            return "Junction";
        case StateKind::Fork:
            return "Fork";
        case StateKind::Join:
            return "Join";
        case StateKind::EntryPoint:
            return "EntryPoint";
        case StateKind::ExitPoint:
            return "ExitPoint";
        case StateKind::Terminate:
            return "Terminate";
    }
    return "Atomic";
}

/**
 * @brief Parses a string label into its corresponding StateKind enum.
 * @param str The string view to parse.
 * @return Parsed StateKind, defaulting to StateKind::Atomic if unrecognized.
 */
[[nodiscard]] inline StateKind state_kind_from_string(std::string_view str) {
    if (str == "Composite")
        return StateKind::Composite;
    if (str == "Parallel")
        return StateKind::Parallel;
    if (str == "Initial")
        return StateKind::Initial;
    if (str == "Final")
        return StateKind::Final;
    if (str == "ShallowHistory")
        return StateKind::ShallowHistory;
    if (str == "DeepHistory")
        return StateKind::DeepHistory;
    if (str == "Choice")
        return StateKind::Choice;
    if (str == "Junction")
        return StateKind::Junction;
    if (str == "Fork")
        return StateKind::Fork;
    if (str == "Join")
        return StateKind::Join;
    if (str == "EntryPoint")
        return StateKind::EntryPoint;
    if (str == "ExitPoint")
        return StateKind::ExitPoint;
    if (str == "Terminate")
        return StateKind::Terminate;
    return StateKind::Atomic;
}

}  // namespace fsm::ir
