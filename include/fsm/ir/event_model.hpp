#pragma once

#include <string>
#include <utility>
#include <vector>

#include "fsm/ir/transition_edge.hpp"

namespace fsm::codegen {

// ============================================================================
// Event & Choice Node Models
// ============================================================================

struct EventModel {
    std::string name;
    std::string description;

    explicit EventModel(std::string event_name = "", std::string event_desc = "")
        : name(std::move(event_name)), description(std::move(event_desc)) {}

    bool operator<(const EventModel& other) const noexcept { return name < other.name; }
};

struct ChoiceNodeModel {
    std::string name;
    std::vector<TransitionEdge> outgoing_branches;

    explicit ChoiceNodeModel(std::string choice_name = "") : name(std::move(choice_name)) {}
};

}  // namespace fsm::codegen
