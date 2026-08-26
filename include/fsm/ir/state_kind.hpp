#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace fsm::codegen {

// ============================================================================
// StateKind: Structural State Classification
// ============================================================================

enum class StateKind : std::uint8_t {
    Atomic,
    Composite,
    Parallel,        // Orthogonal Region container
    Initial,         // Initial pseudostate
    Final,           // Final state
    ShallowHistory,  // [H]
    DeepHistory,     // [H*]
    Choice,          // Dynamic conditional branch pseudostate <<choice>>
    Junction,        // Static merge/branch pseudostate <<junction>>
    Fork,            // Parallel split pseudostate <<fork>>
    Join,            // Parallel rendezvous pseudostate <<join>>
    EntryPoint,      // Named entry point connection on composite boundary
    ExitPoint        // Named exit point connection on composite boundary
};

inline std::string state_kind_to_string(StateKind kind) {
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
    }
    return "Atomic";
}

inline StateKind state_kind_from_string(std::string_view str) {
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
    return StateKind::Atomic;
}

}  // namespace fsm::codegen
