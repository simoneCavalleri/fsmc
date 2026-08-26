#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace fsm::codegen {

// ============================================================================
// TransitionEdgeKind: Edge Semantics
// ============================================================================

enum class TransitionEdgeKind : std::uint8_t { External, Internal, Local };

inline std::string transition_edge_kind_to_string(TransitionEdgeKind kind) {
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

inline TransitionEdgeKind transition_edge_kind_from_string(std::string_view str) {
    if (str == "Internal")
        return TransitionEdgeKind::Internal;
    if (str == "Local")
        return TransitionEdgeKind::Local;
    return TransitionEdgeKind::External;
}

}  // namespace fsm::codegen
