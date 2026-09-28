/**
 * @file transition_edge_kind.hpp
 * @brief Transition edge topology semantics (External, Internal, Local) in FSM IR.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace fsm::ir {

/**
 * @brief Formal semantics of a transition edge relative to source state boundaries.
 */
enum class TransitionEdgeKind : std::uint8_t {
    External,  ///< Standard transition: executes source exit actions and target entry actions
    Internal,  ///< Internal transition: executes transition action without exiting or entering the state
    Local      ///< Local transition: stays within containing composite state without re-entering it
};

/**
 * @brief Converts a TransitionEdgeKind enum into its string representation.
 * @param kind The TransitionEdgeKind to convert.
 * @return String representation ("External", "Internal", "Local").
 */
[[nodiscard]] inline std::string transition_edge_kind_to_string(TransitionEdgeKind kind) {
    switch (kind) {
        case TransitionEdgeKind::External:
            return "External";
        case TransitionEdgeKind::Internal:
            return "Internal";
        case TransitionEdgeKind::Local:
            return "Local";
    }
    return "External";
}

/**
 * @brief Parses a string label into a TransitionEdgeKind enum.
 * @param str The string view to parse.
 * @return Parsed TransitionEdgeKind, defaulting to External if unrecognized.
 */
[[nodiscard]] inline TransitionEdgeKind transition_edge_kind_from_string(std::string_view str) {
    if (str == "Internal")
        return TransitionEdgeKind::Internal;
    if (str == "Local")
        return TransitionEdgeKind::Local;
    return TransitionEdgeKind::External;
}

}  // namespace fsm::ir
